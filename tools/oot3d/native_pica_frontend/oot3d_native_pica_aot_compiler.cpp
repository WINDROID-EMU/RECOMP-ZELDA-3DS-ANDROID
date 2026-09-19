#include "fast/oot3d/pica_aot_shader_pack.h"
#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/oot3d/builtin_pass_shaders.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <shaderc/shaderc.hpp>

namespace {

struct Options {
    std::vector<std::filesystem::path> Inventories;
    std::filesystem::path Pack;
    std::filesystem::path Manifest;
    std::filesystem::path MergedInventory;
    std::filesystem::path RendererCache;
    uint32_t Threads = 0;
};

void PrintUsage() {
    std::cerr
        << "usage: oot3d_native_pica_aot_compiler "
           "--inventory <file> [--inventory <file> ...] "
           "--pack <file> --manifest <file> "
           "[--merged-inventory <file>] [--threads <count>]\n"
        << "or: --prepare-renderer-cache <directory> --manifest <file>\n";
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (index + 1 >= argc)
            return false;
        const std::filesystem::path value = argv[++index];
        if (argument == "--inventory")
            options.Inventories.push_back(value);
        else if (argument == "--pack")
            options.Pack = value;
        else if (argument == "--manifest")
            options.Manifest = value;
        else if (argument == "--merged-inventory")
            options.MergedInventory = value;
        else if (argument == "--prepare-renderer-cache")
            options.RendererCache = value;
        else if (argument == "--threads") {
            try {
                options.Threads = static_cast<uint32_t>(std::stoul(value.string()));
            } catch (...) {
                return false;
            }
        } else
            return false;
    }
    if (!options.RendererCache.empty())
        return options.Inventories.empty() && options.Pack.empty() &&
               options.MergedInventory.empty() && !options.Manifest.empty();
    return !options.Inventories.empty() && !options.Pack.empty() &&
           !options.Manifest.empty();
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("could not open inventory: " +
                                 path.string());
    return nlohmann::json::parse(input);
}

uint64_t HashWords(std::span<const uint32_t> words) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    for (const uint32_t word : words) {
        for (uint32_t shift = 0; shift < 32U; shift += 8U)
            hash = (hash ^ static_cast<uint8_t>(word >> shift)) * kPrime;
    }
    return hash == 0U ? 1U : hash;
}

std::vector<uint32_t> CompileShader(
    shaderc::Compiler& compiler, const std::string& source,
    Fast::Oot3d::PicaAotShaderStage stage,
    const std::string& sourceName) {
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto kind = stage == Fast::Oot3d::PicaAotShaderStage::Vertex
                          ? shaderc_vertex_shader
                          : shaderc_fragment_shader;
    const auto result = compiler.CompileGlslToSpv(
        source, kind, sourceName.c_str(), "main", options);
    if (result.GetCompilationStatus() !=
        shaderc_compilation_status_success) {
        throw std::runtime_error("shaderc failed for " + sourceName +
                                 ": " + result.GetErrorMessage());
    }
    return {result.cbegin(), result.cend()};
}

void WriteJsonAtomically(const std::filesystem::path& path,
                         const nlohmann::json& value) {
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        throw std::runtime_error("could not create manifest directory: " +
                                 error.message());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        output << value.dump(2) << '\n';
        if (!output)
            throw std::runtime_error("could not write AOT manifest");
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error)
        throw std::runtime_error("could not publish AOT manifest: " +
                                 error.message());
}

} // namespace

int main(int argc, char** argv) {
    using namespace Fast::Oot3d;
    try {
        Options options;
        if (!ParseOptions(argc, argv, options)) {
            PrintUsage();
            return 2;
        }
        if (!options.RendererCache.empty()) {
            Fast::Renderer::CachedPassShaderCompiler compiler;
            compiler.Configure(options.RendererCache);
            if (!compiler.Enabled())
                throw std::runtime_error("renderer shader cache has no compiler identity");
            const auto shaders = BuildBuiltinPassShaders();
            for (const auto& shader : shaders)
                compiler.Resolve(shader.Source, shader.Stage, shader.Name, shader.Defines);
            const auto stats = compiler.Stats();
            WriteJsonAtomically(options.Manifest, {
                {"format", "triaevum_renderer_shader_preparation_v1"},
                {"modules", shaders.size()}, {"hits", stats.Hits}, {"compiled", stats.Compilations},
                {"compile_failed", stats.CompilationFailures}, {"writes", stats.Writes},
                {"write_failed", stats.WriteFailures}, {"compile_ms", stats.CompileNanoseconds / 1e6},
                {"cache_directory", std::filesystem::absolute(options.RendererCache).string()},
                {"game_booted", false}});
            if (stats.WriteFailures)
                throw std::runtime_error("renderer shader cache could not persist every module");
            return 0;
        }
        uint32_t schema = 0U;
        nlohmann::json inputs = nlohmann::json::array();
        std::map<std::pair<std::string, std::string>, std::string>
            uniqueSources;
        for (const auto& inventoryPath : options.Inventories) {
            const auto inventory = ReadJson(inventoryPath);
            if (inventory.value("format", std::string{}) !=
                    "oot3d_pica_effective_shader_inventory_v1" ||
                !inventory.contains("shaders") ||
                !inventory.at("shaders").is_array()) {
                throw std::runtime_error(
                    "effective shader inventory format is invalid: " +
                    inventoryPath.string());
            }
            const uint32_t inventorySchema =
                inventory.value("descriptor_schema_version", 0U);
            if (inventorySchema == 0U) {
                throw std::runtime_error(
                    "effective shader inventory has no descriptor schema: " +
                    inventoryPath.string());
            }
            if (schema == 0U)
                schema = inventorySchema;
            else if (schema != inventorySchema)
                throw std::runtime_error(
                    "effective shader inventories use different descriptor schemas");

            for (const auto& input : inventory.at("shaders")) {
                const std::string stage = input.at("stage");
                const std::string sourceId = input.at("source_id");
                const std::string source = input.at("source");
                const auto key = std::pair{stage, sourceId};
                const auto [found, inserted] =
                    uniqueSources.emplace(key, source);
                if (!inserted && found->second != source) {
                    throw std::runtime_error(
                        "shader source identity collision while merging inventories: " +
                        stage + ":" + sourceId);
                }
                if (inserted)
                    inputs.push_back(input);
            }
        }
        if (inputs.empty() ||
            inputs.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error(
                "effective shader inventory merge is empty or too large");

        // Renderer-owned sources accompany every seed, without needing a game
        // capture. Explicit mode zero keeps developer diagnostics out of Forge.
        const size_t capturedSources = inputs.size();
        const auto appendRendererSource = [&](PicaAotShaderStage stage,
                                              const std::string& source) {
            const auto identity = IdentifyPicaAotShaderSource(source);
            const std::string stageName(PicaAotShaderStageName(stage));
            const auto sourceId = FormatPicaAotShaderId(identity.Id);
            const auto [found, inserted] = uniqueSources.emplace(
                std::pair{stageName, sourceId}, source);
            if (!inserted && found->second != source)
                throw std::runtime_error("renderer shader source identity collision");
            if (inserted)
                inputs.push_back({{"stage", stageName}, {"source_id", sourceId},
                    {"secondary_hash", FormatPicaAotShaderId(identity.SecondaryHash)},
                    {"source_size", identity.Size}, {"source", source}});
        };
        appendRendererSource(PicaAotShaderStage::Vertex, BuildPicaScanoutVertexShader());
        appendRendererSource(PicaAotShaderStage::Fragment, BuildPicaScanoutFragmentShader(false, 0));
        const size_t rendererSourcesAdded = inputs.size() - capturedSources;

        std::sort(inputs.begin(), inputs.end(), [](const auto& left,
                                                   const auto& right) {
            return std::tie(left.at("stage"), left.at("source_id")) <
                   std::tie(right.at("stage"), right.at("source_id"));
        });
        if (!options.MergedInventory.empty()) {
            const nlohmann::json merged = {
                {"format", "oot3d_pica_effective_shader_inventory_v1"},
                {"descriptor_schema_version", schema},
                {"shader_count", inputs.size()},
                {"shaders", inputs},
            };
            WriteJsonAtomically(options.MergedInventory, merged);
        }

        PicaAotShaderPack existingPack;
        bool hasCache = false;
        std::string existingError;
        if (std::filesystem::exists(options.Pack)) {
            if (existingPack.Load(options.Pack, &existingError) &&
                existingPack.DescriptorSchemaVersion() == schema) {
                hasCache = true;
            }
        }

        struct ShaderTask {
            PicaAotShaderStage Stage = PicaAotShaderStage::Vertex;
            std::string StageName;
            std::string Source;
            std::string SourceName;
            PicaAotShaderSourceIdentity Identity;
            std::vector<uint32_t> Spirv;
            bool FromCache = false;
        };

        std::vector<ShaderTask> tasks(inputs.size());
        size_t cachedCount = 0;
        std::vector<size_t> compilationIndices;
        compilationIndices.reserve(inputs.size());

        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            auto& task = tasks[i];
            task.StageName = input.at("stage");
            if (!ParsePicaAotShaderStage(task.StageName, task.Stage))
                throw std::runtime_error("unknown shader stage: " +
                                         task.StageName);
            task.Source = input.at("source");
            task.Identity = IdentifyPicaAotShaderSource(task.Source);
            if (input.value("source_id", std::string{}) !=
                    FormatPicaAotShaderId(task.Identity.Id) ||
                input.value("secondary_hash", std::string{}) !=
                    FormatPicaAotShaderId(task.Identity.SecondaryHash) ||
                input.value("source_size", uint64_t{0}) != task.Identity.Size) {
                throw std::runtime_error(
                    "shader source identity does not match inventory");
            }
            task.SourceName =
                "pica_" + task.StageName + "_" +
                FormatPicaAotShaderId(task.Identity.Id) +
                (task.Stage == PicaAotShaderStage::Vertex ? ".vert" :
                                                           ".frag");

            if (hasCache) {
                const auto cachedSpirv = existingPack.Find(task.Stage, task.Identity);
                if (!cachedSpirv.empty()) {
                    task.Spirv.assign(cachedSpirv.begin(), cachedSpirv.end());
                    task.FromCache = true;
                    ++cachedCount;
                    continue;
                }
            }
            compilationIndices.push_back(i);
        }

        const auto compileStart = std::chrono::steady_clock::now();
        const uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        const uint32_t threadCount = options.Threads > 0
            ? options.Threads
            : std::min(hardwareThreads,
                       static_cast<uint32_t>(compilationIndices.empty() ? 1u : compilationIndices.size()));

        if (!compilationIndices.empty()) {
            std::atomic<size_t> nextJob{0};
            std::atomic<bool> compilationFailed{false};
            std::string firstError;
            std::mutex errorMutex;

            auto worker = [&]() {
                shaderc::Compiler compiler;
                while (true) {
                    if (compilationFailed.load(std::memory_order_relaxed))
                        break;
                    const size_t job = nextJob.fetch_add(1, std::memory_order_relaxed);
                    if (job >= compilationIndices.size())
                        break;
                    const size_t taskIndex = compilationIndices[job];
                    auto& task = tasks[taskIndex];
                    try {
                        task.Spirv = CompileShader(compiler, task.Source, task.Stage, task.SourceName);
                    } catch (const std::exception& exception) {
                        std::lock_guard<std::mutex> lock(errorMutex);
                        if (!compilationFailed.load(std::memory_order_relaxed)) {
                            compilationFailed.store(true, std::memory_order_relaxed);
                            firstError = exception.what();
                        }
                        break;
                    }
                }
            };

            if (threadCount <= 1 || compilationIndices.size() <= 1) {
                worker();
            } else {
                std::vector<std::thread> workers;
                workers.reserve(threadCount);
                for (uint32_t t = 0; t < threadCount; ++t)
                    workers.emplace_back(worker);
                for (auto& workerThread : workers) {
                    if (workerThread.joinable())
                        workerThread.join();
                }
            }

            if (compilationFailed.load(std::memory_order_relaxed))
                throw std::runtime_error(firstError);
        }
        const auto compileEnd = std::chrono::steady_clock::now();
        const auto compileMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            compileEnd - compileStart).count();

        std::vector<PicaAotShaderBinary> binaries;
        binaries.reserve(tasks.size());
        nlohmann::json manifestEntries = nlohmann::json::array();
        for (auto& task : tasks) {
            const uint64_t spirvHash = HashWords(task.Spirv);
            manifestEntries.push_back({
                {"stage", task.StageName},
                {"source_id", FormatPicaAotShaderId(task.Identity.Id)},
                {"secondary_hash",
                 FormatPicaAotShaderId(task.Identity.SecondaryHash)},
                {"source_size", task.Identity.Size},
                {"spirv_words", task.Spirv.size()},
                {"spirv_hash", FormatPicaAotShaderId(spirvHash)},
            });
            binaries.push_back({task.Stage, task.Identity, std::move(task.Spirv)});
        }

        std::string error;
        if (!WritePicaAotShaderPack(options.Pack, schema, binaries,
                                    &error)) {
            throw std::runtime_error(error);
        }
        PicaAotShaderPack verification;
        if (!verification.Load(options.Pack, &error) ||
            verification.EntryCount() != binaries.size() ||
            verification.DescriptorSchemaVersion() != schema) {
            throw std::runtime_error(
                "published AOT shader pack failed verification: " + error);
        }
        for (const auto& binary : binaries) {
            const auto found = verification.Find(binary.Stage,
                                                 binary.Source);
            if (found.size() != binary.Spirv.size() ||
                !std::equal(found.begin(), found.end(),
                            binary.Spirv.begin())) {
                throw std::runtime_error(
                    "published AOT shader binary failed round-trip");
            }
        }

        nlohmann::json inventoryPaths = nlohmann::json::array();
        for (const auto& inventoryPath : options.Inventories)
            inventoryPaths.push_back(inventoryPath.string());
        const nlohmann::json manifest = {
            {"format", "oot3d_pica_aot_shader_pack_manifest_v1"},
            {"pack_format_version", kPicaAotShaderPackVersion},
            {"descriptor_schema_version", schema},
            {"inventories", std::move(inventoryPaths)},
            {"merged_inventory",
             options.MergedInventory.empty()
                 ? nlohmann::json(nullptr)
                 : nlohmann::json(options.MergedInventory.string())},
            {"pack", options.Pack.string()},
            {"shader_count", binaries.size()},
            {"renderer_sources_added", rendererSourcesAdded},
            {"shaders", std::move(manifestEntries)},
        };
        WriteJsonAtomically(options.Manifest, manifest);
        std::cout << "PICA AOT shader pack: " << binaries.size()
                  << " modules (" << cachedCount << " cached, "
                  << compilationIndices.size() << " compiled in "
                  << compileMs << "ms across " << threadCount << " threads), schema "
                  << schema << ", " << options.Pack.string() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_aot_compiler: "
                  << exception.what() << '\n';
        return 1;
    }
}
