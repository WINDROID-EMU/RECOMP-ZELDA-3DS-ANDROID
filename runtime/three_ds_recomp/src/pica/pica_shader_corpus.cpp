#include "three_ds_recomp/pica/pica_shader_corpus.h"

#include <fstream>
#include <iostream>

namespace ThreeDsRecomp::Pica {

PicaShaderCorpus::PicaShaderCorpus(size_t maxMemoryBytes)
    : m_maxMemoryBytes(maxMemoryBytes) {}

bool PicaShaderCorpus::RegisterShader(uint64_t stateKey, std::vector<uint32_t> spirvBinary) {
    if (stateKey == 0 || spirvBinary.empty()) {
        return false;
    }

    const size_t entryBytes = sizeof(CorpusShaderEntry) + spirvBinary.size() * sizeof(uint32_t);

    auto it = m_entries.find(stateKey);
    if (it != m_entries.end()) {
        m_currentMemoryBytes -= it->second.SpirvBinary.size() * sizeof(uint32_t);
        it->second.SpirvBinary = std::move(spirvBinary);
        m_currentMemoryBytes += it->second.SpirvBinary.size() * sizeof(uint32_t);
        return true;
    }

    // Evict lowest hit count entry if over budget (R14)
    while (m_currentMemoryBytes + entryBytes > m_maxMemoryBytes && !m_entries.empty()) {
        uint64_t victimKey = 0;
        uint32_t minHits = UINT32_MAX;
        for (const auto& [k, v] : m_entries) {
            if (v.HitCount < minHits) {
                minHits = v.HitCount;
                victimKey = k;
            }
        }
        auto victimIt = m_entries.find(victimKey);
        if (victimIt != m_entries.end()) {
            m_currentMemoryBytes -= (sizeof(CorpusShaderEntry) + victimIt->second.SpirvBinary.size() * sizeof(uint32_t));
            m_entries.erase(victimIt);
        } else {
            break;
        }
    }

    CorpusShaderEntry entry;
    entry.StateKey = stateKey;
    entry.SpirvBinary = std::move(spirvBinary);
    entry.HitCount = 0;

    m_currentMemoryBytes += entryBytes;
    m_entries.emplace(stateKey, std::move(entry));
    return true;
}

const CorpusShaderEntry* PicaShaderCorpus::FindShader(uint64_t stateKey) {
    auto it = m_entries.find(stateKey);
    if (it == m_entries.end()) {
        return nullptr;
    }
    it->second.HitCount++;
    return &it->second;
}

bool PicaShaderCorpus::SaveCorpusFile(const std::string& filePath) const {
    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        return false;
    }

    const uint32_t magic = kMagic;
    const uint32_t version = kVersion;
    const uint32_t count = static_cast<uint32_t>(m_entries.size());
    const uint32_t reserved = 0;

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    out.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

    for (const auto& [key, entry] : m_entries) {
        const uint64_t k = entry.StateKey;
        const uint32_t words = static_cast<uint32_t>(entry.SpirvBinary.size());
        out.write(reinterpret_cast<const char*>(&k), sizeof(k));
        out.write(reinterpret_cast<const char*>(&words), sizeof(words));
        if (words > 0) {
            out.write(reinterpret_cast<const char*>(entry.SpirvBinary.data()), words * sizeof(uint32_t));
        }
    }

    return out.good();
}

bool PicaShaderCorpus::LoadCorpusFile(const std::string& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    uint32_t reserved = 0;

    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
        !in.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
        !in.read(reinterpret_cast<char*>(&count), sizeof(count)) ||
        !in.read(reinterpret_cast<char*>(&reserved), sizeof(reserved))) {
        // Corrupted header, discard safely (R15)
        return false;
    }

    if (magic != kMagic || version != kVersion) {
        // Incompatible version or corrupted magic (R15)
        return false;
    }

    Clear();

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t key = 0;
        uint32_t words = 0;
        if (!in.read(reinterpret_cast<char*>(&key), sizeof(key)) ||
            !in.read(reinterpret_cast<char*>(&words), sizeof(words))) {
            // Truncated file, keep what was loaded or clear cleanly
            return false;
        }

        if (words > 1024 * 1024) { // 4MB per shader sanity limit
            Clear();
            return false;
        }

        std::vector<uint32_t> spirv(words);
        if (words > 0) {
            if (!in.read(reinterpret_cast<char*>(spirv.data()), words * sizeof(uint32_t))) {
                Clear();
                return false;
            }
        }

        RegisterShader(key, std::move(spirv));
    }

    return true;
}

void PicaShaderCorpus::Clear() {
    m_entries.clear();
    m_currentMemoryBytes = 0;
}

} // namespace ThreeDsRecomp::Pica
