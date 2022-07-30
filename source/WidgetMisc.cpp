#include "WidgetMisc.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

struct InputTextCallbackUserData {
    std::string* str;
    ImGuiInputTextCallback chainCallback;
    void* chainCallbackUserData;
};

static int InputTextCallback(ImGuiInputTextCallbackData* data) {
    auto user_data = (InputTextCallbackUserData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
        auto str = user_data->str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = (char*)str->c_str();
    } else if (user_data->chainCallback) {
        // Forward to user callback, if any
        data->UserData = user_data->chainCallbackUserData;
        return user_data->chainCallback(data);
    }
    return 0;
}

bool ImGui::InputText(const char* label, std::string* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackUserData cbUserData;
    cbUserData.str = str;
    cbUserData.chainCallback = callback;
    cbUserData.chainCallbackUserData = user_data;
    return InputText(label, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback, &cbUserData);
}

bool ImGui::InputTextMultiline(const char* label, std::string* str, const ImVec2& size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackUserData cbUserData;
    cbUserData.str = str;
    cbUserData.chainCallback = callback;
    cbUserData.chainCallbackUserData = userData;
    return InputTextMultiline(label, (char*)str->c_str(), str->capacity() + 1, size, flags, InputTextCallback, &cbUserData);
}

bool ImGui::InputTextWithHint(const char* label, const char* hint, std::string* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackUserData cbUserData;
    cbUserData.str = str;
    cbUserData.chainCallback = callback;
    cbUserData.chainCallbackUserData = userData;
    return InputTextWithHint(label, hint, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback, &cbUserData);
}
