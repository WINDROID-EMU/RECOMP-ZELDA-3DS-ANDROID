#pragma once

#include <cstdlib>
#include <string_view>

namespace ThreeDsRecomp::Renderer {

/**
 * NativeRenderControl - Implements Rule R22 from 'regras agente.md':
 * "Cada fase fica atrás de uma flag (native_render=on/off) que volta ao caminho anterior.
 * O caminho antigo só pode ser removido após a validação final e confirmação do usuário."
 */
class NativeRenderControl {
public:
    static bool IsNativeRenderEnabled() {
        if (sOverrideActive) {
            return sNativeRenderEnabled;
        }

        // Check environment variable OOT3D_NATIVE_RENDER or NATIVE_RENDER
        const char* env1 = std::getenv("OOT3D_NATIVE_RENDER");
        if (env1 != nullptr) {
            std::string_view val(env1);
            if (val == "1" || val == "true" || val == "on" || val == "ON") {
                return true;
            }
            if (val == "0" || val == "false" || val == "off" || val == "OFF") {
                return false;
            }
        }

        const char* env2 = std::getenv("NATIVE_RENDER");
        if (env2 != nullptr) {
            std::string_view val(env2);
            if (val == "1" || val == "true" || val == "on" || val == "ON") {
                return true;
            }
            if (val == "0" || val == "false" || val == "off" || val == "OFF") {
                return false;
            }
        }

        return sDefaultNativeRender;
    }

    static void SetNativeRenderEnabled(bool enabled) {
        sNativeRenderEnabled = enabled;
        sOverrideActive = true;
    }

    static void ResetToDefault() {
        sOverrideActive = false;
    }

private:
    static inline bool sDefaultNativeRender = true;
    static inline bool sNativeRenderEnabled = true;
    static inline bool sOverrideActive = false;
};

} // namespace ThreeDsRecomp::Renderer
