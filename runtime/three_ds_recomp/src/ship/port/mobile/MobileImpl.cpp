#if defined(__ANDROID__) || defined(__IOS__)
#include "ship/port/mobile/MobileImpl.h"
#if !defined(__ANDROID__)
#include <SDL2/SDL.h>
#endif

#include <imgui_internal.h>

static bool isShowingVirtualKeyboard = true;

void Ship::Mobile::ImGuiProcessEvent(bool wantsTextInput) {
#if !defined(__ANDROID__)
    ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID());

    if (wantsTextInput) {
        if (!isShowingVirtualKeyboard) {
            state->ClearText();

            isShowingVirtualKeyboard = true;
            SDL_StartTextInput();
        }
    } else {
        if (isShowingVirtualKeyboard) {
            isShowingVirtualKeyboard = false;
            SDL_StopTextInput();
        }
    }
#else
    (void)wantsTextInput;
#endif
}
#endif
