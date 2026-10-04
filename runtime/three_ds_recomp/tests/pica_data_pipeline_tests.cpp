#include "three_ds_recomp/pica/pica_surface_cache.h"
#include "three_ds_recomp/pica/pica_texture_cache.h"
#include "three_ds_recomp/pica/pica_vertex_translator.h"

#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    using namespace ThreeDsRecomp::Pica;

    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    try {
        // =====================================================================
        // Test 1: PicaVertexTranslator
        // =====================================================================
        {
            VertexState state;
            state.Indexed = true;
            state.IndicesAre16Bit = true;
            state.IndexPhysicalAddress = 0x20001000;
            state.VertexCount = 360;

            state.Loaders[0].ByteStride = 24;
            state.Loaders[0].PhysicalAddress = 0x20000000;

            // Position: Float vec3 at offset 0
            state.Attributes[0].Binding = 0;
            state.Attributes[0].Format = VertexFormat::Float;
            state.Attributes[0].ComponentCount = 3;
            state.Attributes[0].ByteOffset = 0;
            state.Attributes[0].Enabled = true;

            // Normal: SignedByte vec3 at offset 12
            state.Attributes[1].Binding = 0;
            state.Attributes[1].Format = VertexFormat::SignedByte;
            state.Attributes[1].ComponentCount = 3;
            state.Attributes[1].ByteOffset = 12;
            state.Attributes[1].Enabled = true;

            // Texcoord: SignedShort vec2 at offset 16
            state.Attributes[2].Binding = 0;
            state.Attributes[2].Format = VertexFormat::SignedShort;
            state.Attributes[2].ComponentCount = 2;
            state.Attributes[2].ByteOffset = 16;
            state.Attributes[2].Enabled = true;

            auto translated = PicaVertexTranslator::Translate(state);
            require(translated.Indexed == true, "Indexed mismatch");
            require(translated.IndexType == VkIndexTypeCompat::Uint16, "Index type mismatch");
            require(translated.IndexCount == 360, "Index count mismatch");
            require(!translated.Bindings.empty(), "Bindings empty");
            require(translated.Bindings[0].Stride == 24, "Binding stride mismatch");

            require(translated.Attributes[0].Format == VkFormatCompat::R32G32B32_Sfloat, "Attr 0 format mismatch");
            require(translated.Attributes[1].Format == VkFormatCompat::R8G8B8A8_Snorm, "Attr 1 format mismatch");
            require(translated.Attributes[2].Format == VkFormatCompat::R16G16_Snorm, "Attr 2 format mismatch");
        }

        // =====================================================================
        // Test 2: Morton 8x8 Deswizzle byte-for-byte correctness
        // =====================================================================
        {
            // Create a small 8x8 image where pixel (x, y) = y * 8 + x
            // Construct in Morton order and verify deswizzle restores linear order
            constexpr uint32_t W = 8;
            constexpr uint32_t H = 8;
            std::vector<uint8_t> mortonTiled(W * H, 0);

            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    // Morton calculation
                    uint32_t m = ((x & 0x1) << 0) | ((y & 0x1) << 1) |
                                 ((x & 0x2) << 1) | ((y & 0x2) << 2) |
                                 ((x & 0x4) << 2) | ((y & 0x4) << 3);
                    mortonTiled[m] = static_cast<uint8_t>(y * W + x);
                }
            }

            auto linear = PicaTextureCache::DeswizzleMorton(mortonTiled, W, H, 1U);
            require(linear.size() == W * H, "Linear size mismatch");
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    uint8_t expected = static_cast<uint8_t>(y * W + x);
                    require(linear[y * W + x] == expected, "Morton deswizzle byte mismatch");
                }
            }
        }

        // =====================================================================
        // Test 3: PicaTextureCache acquire, ETC1 passthrough and ceiling eviction
        // =====================================================================
        {
            PicaTextureCache cache(1024 * 1024); // 1 MB ceiling
            require(cache.GetMemoryCeiling() == 1024 * 1024, "Ceiling mismatch");

            TextureCacheKey key1{ 0x10000000, 8, 8, TextureFormat::Rgba8, 0x11223344 };
            std::vector<uint8_t> dummyBytes(8 * 8 * 4, 0xAB);

            auto tex1 = cache.AcquireTexture(key1, dummyBytes, false);
            require(tex1 != nullptr, "Acquire tex1 failed");
            require(tex1->Width == 8 && tex1->Height == 8, "Tex1 dims mismatch");
            require(cache.GetEntryCount() == 1, "Cache entry count mismatch");
            require(cache.GetCurrentMemoryUsage() > 0, "Memory usage should be > 0");

            // Cache hit
            auto tex1_hit = cache.AcquireTexture(key1, dummyBytes, false);
            require(tex1.get() == tex1_hit.get(), "Cache hit should return identical instance");

            // Test native ETC2 pass-through (WARN-05)
            TextureCacheKey etcKey{ 0x10008000, 8, 8, TextureFormat::Etc1, 0x55667788 };
            std::vector<uint8_t> etcBytes(32, 0xEE);
            auto etcTex = cache.AcquireTexture(etcKey, etcBytes, true);
            require(etcTex->IsNativeEtc2 == true, "ETC1 should pass-through to native ETC2");

            // Test eviction: set tiny ceiling and ensure old entry is evicted (Rule R14)
            cache.SetMemoryCeiling(512); // smaller than 2 textures
            TextureCacheKey key3{ 0x10010000, 8, 8, TextureFormat::Rgba8, 0x99AABBCC };
            cache.AcquireTexture(key3, dummyBytes, false);
            require(cache.GetCurrentMemoryUsage() <= 512, "Memory usage exceeded ceiling");
        }

        // =====================================================================
        // Test 4: PicaSurfaceCache and Depth Reinterpretation
        // =====================================================================
        {
            PicaSurfaceCache surfaces(2 * 1024 * 1024);
            auto surf = surfaces.GetOrCreateSurface(0x18000000, 16, 16, 0, false);
            require(surf != nullptr, "Failed to create surface");
            require(surf->Width == 16 && surf->Height == 16, "Surface dimensions mismatch");
            require(surfaces.GetSurfaceCount() == 1, "Surface count mismatch");

            // Re-acquire same address returns cached surface
            auto surf2 = surfaces.GetOrCreateSurface(0x18000000, 16, 16, 0, false);
            require(surf.get() == surf2.get(), "Should return existing surface");

            // Depth reinterpretation test (D24S8 -> RGBA8)
            RenderSurface depthSurf;
            depthSurf.Width = 4;
            depthSurf.Height = 4;
            depthSurf.Pixels.resize(4 * 4 * 4, 0);
            // Write depth = 0x00332211, stencil = 0x44
            depthSurf.Pixels[0] = 0x11;
            depthSurf.Pixels[1] = 0x22;
            depthSurf.Pixels[2] = 0x33;
            depthSurf.Pixels[3] = 0x44;

            std::vector<uint8_t> color;
            bool ok = PicaSurfaceCache::ReinterpretDepthAsColor(depthSurf, color);
            require(ok == true, "ReinterpretDepthAsColor should succeed");
            require(color[0] == 0x11 && color[1] == 0x22 && color[2] == 0x33 && color[3] == 0x44,
                    "Depth reinterpretation byte fidelity mismatch");

            // Safe fallback test: empty surface should not abort (Rule R12)
            RenderSurface emptyDepth;
            std::vector<uint8_t> fallbackColor;
            bool fallbackResult = PicaSurfaceCache::ReinterpretDepthAsColor(emptyDepth, fallbackColor);
            require(fallbackResult == false, "Empty depth should trigger safe fallback");
            require(!fallbackColor.empty(), "Fallback should return valid default bytes");
        }

        std::cout << "All PICA data pipeline tests passed successfully.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PicaDataPipeline test failed: " << e.what() << '\n';
        return 1;
    }
}
