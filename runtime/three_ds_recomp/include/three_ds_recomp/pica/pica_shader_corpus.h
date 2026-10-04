#pragma once

#include "pica_shader.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ThreeDsRecomp::Pica {

struct CorpusShaderEntry {
    uint64_t StateKey = 0;
    std::vector<uint32_t> SpirvBinary;
    uint32_t HitCount = 0;
};

class PicaShaderCorpus {
public:
    static constexpr uint32_t kMagic = 0x53504952; // 'SPIC' (SPIR-V PICA)
    static constexpr uint32_t kVersion = 1;

    explicit PicaShaderCorpus(size_t maxMemoryBytes = 32 * 1024 * 1024);

    /// Registers a compiled SPIR-V shader into the corpus indexed by state key.
    bool RegisterShader(uint64_t stateKey, std::vector<uint32_t> spirvBinary);

    /// Looks up a precompiled shader by state key. Returns nullptr on cache miss.
    const CorpusShaderEntry* FindShader(uint64_t stateKey);

    /// Serializes the entire offline corpus to a persistent binary file.
    bool SaveCorpusFile(const std::string& filePath) const;

    /// Loads the offline corpus from a binary file. Discards and recovers safely if corrupted (R15).
    bool LoadCorpusFile(const std::string& filePath);

    /// Returns current number of cached shaders in corpus.
    size_t GetShaderCount() const { return m_entries.size(); }

    /// Returns total memory consumed by cached SPIR-V binaries in bytes.
    size_t GetMemoryUsage() const { return m_currentMemoryBytes; }

    /// Clears all entries.
    void Clear();

private:
    size_t m_maxMemoryBytes;
    size_t m_currentMemoryBytes = 0;
    std::unordered_map<uint64_t, CorpusShaderEntry> m_entries;
};

} // namespace ThreeDsRecomp::Pica
