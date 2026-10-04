#include "three_ds_recomp/pica/pica_command_processor.h"
#include "three_ds_recomp/pica/pica_state_decoder.h"

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
        // ---------------------------------------------------------------------
        // Test 1: PicaCommandHeader encode / decode
        // ---------------------------------------------------------------------
        {
            PicaCommandHeader h;
            h.RegisterId = 0x0107;
            h.ByteMask = 0x05; // bits 0 and 2
            h.ParameterCount = 14;
            h.Consecutive = true;

            const uint32_t raw = h.Encode();
            const PicaCommandHeader decoded = PicaCommandHeader::Decode(raw);

            require(decoded.RegisterId == 0x0107, "RegisterId mismatch");
            require(decoded.ByteMask == 0x05, "ByteMask mismatch");
            require(decoded.ParameterCount == 14, "ParameterCount mismatch");
            require(decoded.Consecutive == true, "Consecutive flag mismatch");
        }

        // ---------------------------------------------------------------------
        // Test 2: PicaCommandProcessor byte mask & command list execution
        // ---------------------------------------------------------------------
        {
            PicaCommandProcessor proc;
            proc.Reset();

            // Direct write with byte mask
            proc.WriteRegister(0x100, 0xF, 0x11223344);
            require(proc.ReadRegister(0x100) == 0x11223344, "Write full register failed");

            // Partial mask: update only byte 1 (bits 8..15)
            proc.WriteRegister(0x100, 0x2, 0x0000AA00);
            require(proc.ReadRegister(0x100) == 0x1122AA44, "Partial byte mask failed");

            // Out-of-bounds write (Rule R12: safe fallback, no crash)
            proc.WriteRegister(0x400, 0xF, 0xDEADBEEF);
            require(proc.ReadRegister(0x400) == 0, "Out of bounds should return 0 safely");

            // Process command list
            // Packet 1: write 2 consecutive registers starting at 0x040
            PicaCommandHeader h1;
            h1.RegisterId = 0x040;
            h1.ByteMask = 0xF;
            h1.ParameterCount = 2;
            h1.Consecutive = true;

            std::vector<uint32_t> cmdList = {
                h1.Encode(),
                0x00000002, // 0x040: culling = 2 (KeepCounterClockwise)
                0x00100020, // 0x041: corner
            };

            const size_t consumed = proc.ProcessCommandList(cmdList);
            require(consumed == cmdList.size(), "Did not consume all words");
            require(proc.ReadRegister(0x040) == 2, "Reg 0x040 write failed");
            require(proc.ReadRegister(0x041) == 0x00100020, "Reg 0x041 consecutive write failed");
        }

        // ---------------------------------------------------------------------
        // Test 3: DecodePicaDrawState full decode
        // ---------------------------------------------------------------------
        {
            PicaCommandProcessor proc;
            proc.Reset();

            // Setup representative draw registers
            // Rasterizer / Topology / Culling
            proc.WriteRegister(0x040, 0xF, 2U);              // Culling = 2 (KeepCounterClockwise)
            proc.WriteRegister(0x041, 0xF, 0x0005000AU);      // Corner X=10, Y=5
            proc.WriteRegister(0x043, 0xF, 0x007800A0U);      // Size HalfW=160, HalfH=120
            proc.WriteRegister(0x242, 0xF, 0U);               // Topology = TriangleList

            // Textures
            proc.WriteRegister(0x080, 0xF, 1U);               // Unit 0 enabled
            proc.WriteRegister(0x081, 0xF, 0x00400100U);      // Width=256, Height=64
            proc.WriteRegister(0x082, 0xF, 0x20000000U);      // Address
            proc.WriteRegister(0x085, 0xF, 0x0000110DU);      // Format=13 (etc1a4), WrapS=1, WrapT=1

            // TEV Stage 0
            proc.WriteRegister(0x0C0, 0xF, 0x00F00E03U);      // Sources
            proc.WriteRegister(0x0C1, 0xF, 0x00000000U);      // Operands
            proc.WriteRegister(0x0C2, 0xF, 0x00010001U);      // OpRGB=1 (Modulate), OpA=1 (Modulate)
            proc.WriteRegister(0x0C3, 0xF, 0xAABBCCDD);       // Constant color
            proc.WriteRegister(0x0C4, 0xF, 0x00010001U);      // Scale=1 (2x)

            // Combiner Buffer
            proc.WriteRegister(0x0FD, 0xF, 0x00000201U);      // InMask=1, OutMask=2

            // Fragment Lighting & Fog
            proc.WriteRegister(0x08F, 0xF, 1U);               // Lighting enabled
            proc.WriteRegister(0x1C2, 0xF, 2U);               // 3 active lights (2+1)
            proc.WriteRegister(0x1C0, 0xF, 0x00000124U);      // Config=4, Fresnel=2, BumpUnit=1
            proc.WriteRegister(0x0E0, 0xF, 5U);               // Fog mode=5 (enabled)
            proc.WriteRegister(0x0E1, 0xF, 0xFF884422U);      // Fog color

            // Output Merger / Framebuffer / Depth / Stencil / Blend
            proc.WriteRegister(0x100, 0xF, 1U << 8U);         // Blend enabled
            proc.WriteRegister(0x101, 0xF, 0x12340000U);      // Blend equations and factors
            proc.WriteRegister(0x104, 0xF, 0x00001F11U);      // Alpha test enabled, ref=0x1F, func=1
            proc.WriteRegister(0x105, 0xF, 0x00000001U);      // Stencil test enabled
            proc.WriteRegister(0x107, 0xF, 0x00001001U);      // Depth test enabled, depth write enabled
            proc.WriteRegister(0x110, 0xF, 0x18000000U);      // Color address
            proc.WriteRegister(0x112, 0xF, 0x00F00140U);      // FB 320x240

            PicaDrawState state;
            std::string err;
            const bool ok = DecodePicaDrawState(proc.GetRegisters(), state, &err);
            require(ok, err.c_str());

            // Validate decoded state
            require(state.Culling == CullMode::KeepCounterClockwise, "Culling decode mismatch");
            require(state.Topology == PrimitiveTopology::TriangleList, "Topology decode mismatch");
            require(state.Viewport.CornerX == 10 && state.Viewport.CornerY == 5, "Viewport corner mismatch");
            require(state.Viewport.HalfWidth == 160.0f && state.Viewport.HalfHeight == 120.0f, "Viewport size mismatch");

            require(state.Textures[0].Enabled == true, "Texture 0 enabled mismatch");
            require(state.Textures[0].Width == 256 && state.Textures[0].Height == 64, "Texture 0 dim mismatch");
            require(state.Textures[0].Format == TextureFormat::Etc1a4, "Texture 0 format mismatch");

            require(state.Tev.Stages[0].OpRgb == TevOp::Modulate, "TEV 0 OpRGB mismatch");
            require(state.Tev.Stages[0].OpAlpha == TevOp::Modulate, "TEV 0 OpAlpha mismatch");
            require(state.Tev.Stages[0].SourceRgb[0] == TevSource::Texture0, "TEV 0 source 0 mismatch");
            require(state.Tev.CombinerBufferInputMask == 1, "TEV combiner input mask mismatch");
            require(state.Tev.CombinerBufferOutputMask == 2, "TEV combiner output mask mismatch");

            require(state.Lighting.Enabled == true, "Lighting enable mismatch");
            require(state.Lighting.ActiveLightCount == 3, "Active light count mismatch");
            require(state.Lighting.ConfigIndex == 4, "Lighting config index mismatch");
            require(state.Lighting.BumpTextureUnit == 1, "Lighting bump unit mismatch");

            require(state.Fog.Enabled == true && state.Fog.Mode == 5, "Fog decode mismatch");
            require(state.Fog.Color == 0xFF884422U, "Fog color mismatch");

            require(state.Blend.Enabled == true, "Blend enable mismatch");
            require(state.DepthStencil.DepthTest == true, "Depth test mismatch");
            require(state.DepthStencil.DepthWrite == true, "Depth write mismatch");
            require(state.DepthStencil.AlphaTest == true, "Alpha test mismatch");
            require(state.DepthStencil.AlphaRef == 0x1F, "Alpha ref mismatch");

            require(state.StateHash != 0, "StateHash must not be 0");

            // -----------------------------------------------------------------
            // Test 4: Hash repeatability and sensitivity
            // -----------------------------------------------------------------
            const uint64_t hash1 = state.StateHash;
            const uint64_t hashRepeat = ComputePicaStateHash(state);
            require(hash1 == hashRepeat, "Hash must be strictly deterministic");

            // Sensitivity test: toggle DepthWrite
            state.DepthStencil.DepthWrite = false;
            const uint64_t hashChanged = ComputePicaStateHash(state);
            require(hash1 != hashChanged, "Hash must change when DepthWrite changes");

            // -----------------------------------------------------------------
            // Test 5: DumpReadable format
            // -----------------------------------------------------------------
            const std::string dump = state.DumpReadable();
            require(!dump.empty(), "Readable dump must not be empty");
            require(dump.find("PICA DRAW STATE") != std::string::npos, "Dump header missing");
            require(dump.find("TEV[0]") != std::string::npos, "Dump TEV missing");
        }

        std::cout << "All PICA state decoder and command processor tests passed successfully.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PicaStateDecoder test failed: " << e.what() << '\n';
        return 1;
    }
}
