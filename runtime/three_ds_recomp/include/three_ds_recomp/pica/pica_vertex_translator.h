#pragma once

#include "three_ds_recomp/pica/pica_state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Pica {

// Vulkan vertex input compatibility definitions
enum class VkFormatCompat : uint32_t {
    Undefined = 0,
    R8_Snorm = 9,
    R8G8_Snorm = 17,
    R8G8B8A8_Snorm = 43,
    R8_Unorm = 13,
    R8G8_Unorm = 21,
    R8G8B8A8_Unorm = 47,
    R16_Snorm = 75,
    R16G16_Snorm = 82,
    R16G16B16A16_Snorm = 96,
    R16_Sfloat = 76,
    R16G16_Sfloat = 83,
    R16G16B16A16_Sfloat = 97,
    R32_Sfloat = 100,
    R32G32_Sfloat = 103,
    R32G32B32_Sfloat = 106,
    R32G32B32A32_Sfloat = 109,
};

enum class VkIndexTypeCompat : uint32_t {
    Uint16 = 0,
    Uint32 = 1,
    Uint8 = 1000265000, // VK_INDEX_TYPE_UINT8_EXT
};

struct TranslatedVertexBinding {
    uint32_t Binding = 0;
    uint32_t Stride = 0;
    bool InputRateInstance = false;
};

struct TranslatedVertexAttribute {
    uint32_t Location = 0;
    uint32_t Binding = 0;
    VkFormatCompat Format = VkFormatCompat::Undefined;
    uint32_t Offset = 0;
};

struct TranslatedVertexInput {
    std::vector<TranslatedVertexBinding> Bindings;
    std::vector<TranslatedVertexAttribute> Attributes;

    bool Indexed = false;
    VkIndexTypeCompat IndexType = VkIndexTypeCompat::Uint16;
    uint32_t IndexBufferAddress = 0;
    uint32_t IndexCount = 0;
    uint32_t VertexCount = 0;
};

/**
 * Translates PICA200 vertex loaders and attribute states into Vulkan vertex input structures.
 */
class PicaVertexTranslator {
public:
    static TranslatedVertexInput Translate(const VertexState& state);

    static VkFormatCompat MapAttributeFormat(VertexFormat format, uint8_t componentCount);
};

} // namespace ThreeDsRecomp::Pica
