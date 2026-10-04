#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace ThreeDsRecomp::Pica {

struct SurfaceKey {
    uint32_t PhysicalAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t Format = 0;

    bool operator<(const SurfaceKey& other) const noexcept {
        if (PhysicalAddress != other.PhysicalAddress) return PhysicalAddress < other.PhysicalAddress;
        if (Width != other.Width) return Width < other.Width;
        if (Height != other.Height) return Height < other.Height;
        return Format < other.Format;
    }
};

enum class DisplayTransferRotation : uint8_t {
    None = 0,
    Rotate90 = 1,
    Rotate180 = 2,
    Rotate270 = 3,
};

struct RenderSurface {
    uint32_t PhysicalAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t Format = 0;
    bool IsDepth = false;
    std::vector<uint8_t> Pixels; // Raw tiled or linear representation
    uint64_t Version = 1;
};

class PicaSurfaceCache {
public:
    // Configurable ceiling: 64 MB for render surfaces (Rule R14)
    explicit PicaSurfaceCache(size_t maxMemoryBytes = 64U * 1024U * 1024U);

    void SetMemoryCeiling(size_t maxMemoryBytes) noexcept {
        mMaxMemoryBytes = maxMemoryBytes;
    }

    [[nodiscard]] size_t GetMemoryCeiling() const noexcept {
        return mMaxMemoryBytes;
    }

    [[nodiscard]] size_t GetCurrentMemoryUsage() const noexcept {
        return mCurrentMemoryUsage;
    }

    [[nodiscard]] size_t GetSurfaceCount() const noexcept {
        return mSurfaces.size();
    }

    void Clear();

    // Get or create surface at physical address
    std::shared_ptr<RenderSurface> GetOrCreateSurface(
        uint32_t physicalAddress, uint16_t width, uint16_t height, uint8_t format, bool isDepth);

    // Process display transfer from tiled surface to linear display buffer
    static std::vector<uint8_t> PerformDisplayTransfer(
        const RenderSurface& source,
        DisplayTransferRotation rotation = DisplayTransferRotation::None,
        bool flipVertical = false);

    // Reinterpret Depth buffer as RGBA8 texture or vice-versa
    // Returns true on successful conversion, false on fallback (never aborts, R12)
    static bool ReinterpretDepthAsColor(
        const RenderSurface& depthSource,
        std::vector<uint8_t>& outColorRgba8);

private:
    void EvictIfNecessary(size_t requiredBytes);

    size_t mMaxMemoryBytes = 0;
    size_t mCurrentMemoryUsage = 0;
    std::map<SurfaceKey, std::shared_ptr<RenderSurface>> mSurfaces;
};

} // namespace ThreeDsRecomp::Pica
