#include "three_ds_recomp/native_render_control.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using ThreeDsRecomp::Renderer::NativeRenderControl;

    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    try {
        // Reset control to default state
        NativeRenderControl::ResetToDefault();
        require(NativeRenderControl::IsNativeRenderEnabled(), "Default native_render should be enabled");

        // Test explicit override (R22)
        NativeRenderControl::SetNativeRenderEnabled(false);
        require(!NativeRenderControl::IsNativeRenderEnabled(), "Explicit disable should report false");

        NativeRenderControl::SetNativeRenderEnabled(true);
        require(NativeRenderControl::IsNativeRenderEnabled(), "Explicit enable should report true");

        // Reset to default
        NativeRenderControl::ResetToDefault();
        require(NativeRenderControl::IsNativeRenderEnabled(), "Reset to default should restore initial state");

        std::cout << "NativeRenderControl tests passed (Rule R22 validated)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NativeRenderControl test failure: " << error.what() << '\n';
        return 1;
    }
}
