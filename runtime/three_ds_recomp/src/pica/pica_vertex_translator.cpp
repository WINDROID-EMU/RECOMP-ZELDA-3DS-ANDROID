#include "three_ds_recomp/pica/pica_vertex_translator.h"

namespace ThreeDsRecomp::Pica {

VkFormatCompat PicaVertexTranslator::MapAttributeFormat(VertexFormat format, uint8_t componentCount) {
    switch (format) {
        case VertexFormat::SignedByte:
            if (componentCount == 1) return VkFormatCompat::R8_Snorm;
            if (componentCount == 2) return VkFormatCompat::R8G8_Snorm;
            return VkFormatCompat::R8G8B8A8_Snorm;

        case VertexFormat::UnsignedByte:
            if (componentCount == 1) return VkFormatCompat::R8_Unorm;
            if (componentCount == 2) return VkFormatCompat::R8G8_Unorm;
            return VkFormatCompat::R8G8B8A8_Unorm;

        case VertexFormat::SignedShort:
            if (componentCount == 1) return VkFormatCompat::R16_Snorm;
            if (componentCount == 2) return VkFormatCompat::R16G16_Snorm;
            return VkFormatCompat::R16G16B16A16_Snorm;

        case VertexFormat::Float:
        default:
            if (componentCount == 1) return VkFormatCompat::R32_Sfloat;
            if (componentCount == 2) return VkFormatCompat::R32G32_Sfloat;
            if (componentCount == 3) return VkFormatCompat::R32G32B32_Sfloat;
            return VkFormatCompat::R32G32B32A32_Sfloat;
    }
}

TranslatedVertexInput PicaVertexTranslator::Translate(const VertexState& state) {
    TranslatedVertexInput result;
    result.Indexed = state.Indexed;
    result.IndexType = state.IndicesAre16Bit ? VkIndexTypeCompat::Uint16 : VkIndexTypeCompat::Uint8;
    result.IndexBufferAddress = state.IndexPhysicalAddress;
    result.IndexCount = state.VertexCount;
    result.VertexCount = state.VertexCount;

    // Create bindings from loaders
    for (uint32_t b = 0; b < 12; ++b) {
        const auto& loader = state.Loaders[b];
        if (loader.ByteStride > 0) {
            TranslatedVertexBinding binding;
            binding.Binding = b;
            binding.Stride = loader.ByteStride;
            binding.InputRateInstance = false;
            result.Bindings.push_back(binding);
        }
    }

    // Default binding if no loaders are defined but attributes exist
    if (result.Bindings.empty()) {
        TranslatedVertexBinding defaultBinding;
        defaultBinding.Binding = 0;
        defaultBinding.Stride = 32; // Default fallback stride
        defaultBinding.InputRateInstance = false;
        result.Bindings.push_back(defaultBinding);
    }

    // Create attribute descriptions
    for (uint32_t loc = 0; loc < 16; ++loc) {
        const auto& attr = state.Attributes[loc];
        if (attr.Enabled || loc < 4) { // PICA typically always passes at least pos, normal, uv, color
            TranslatedVertexAttribute translatedAttr;
            translatedAttr.Location = loc;
            translatedAttr.Binding = attr.Binding;
            translatedAttr.Format = MapAttributeFormat(attr.Format, attr.ComponentCount);
            translatedAttr.Offset = attr.ByteOffset;
            result.Attributes.push_back(translatedAttr);
        }
    }

    return result;
}

} // namespace ThreeDsRecomp::Pica
