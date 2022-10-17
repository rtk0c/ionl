// Special text editor designed specifically for writing outlines, with the following features:
// - Inline markdown rendering (e.g. **text** will be rendered bold with markins kept in place)
// - Text wrapping to a certain width
// - Unicode/language-aware cursor movement (TODO do we need to grab ICU for this?)
#pragma once

#include "imgui.h"

#include <string>
#include <string_view>
#include <vector>

namespace Ionl {

// TODO DPI handling?
// TODO figure out font caching or SDF based rendering: generating a separate atlas for each heading type is really costly on VRAM

// Terminology:
// - "face": a font face, i.e ImFontAtlas
// - "text style": how a string should be rendered, described using bold/italics/etc. + size + color
//                 each text style can be translated into a face

// Goals:
// - Inline markup previewing (e.g. rendering **bold** text as bold)
// - UTF-8 support just like the builtin ImGui::InputText
// Non-goals:
// - Full unicode support such as RTL

/// Represents a (potential) font variant to be used.
/// Each element except 'regular' may be missing, and should fallback to the regular style.
enum MarkdownFaceVariant {
    // NOTE: these must all have the same FontSize
    // NOTE: no underline and strikethrough fonts, we just draw our own decorations for those
    MFV_Proportional,
    MFV_ProportionalItalic,
    MFV_ProportionalBold,
    MFV_ProportionalBoldItalic,
    MFV_Monospace,
    MFV_MonospaceItalic,
    MFV_MonospaceBold,
    MFV_MonospaceBoldItalic,

    MFV_Heading1,
    MFV_Heading2,
    MFV_Heading3,
    MFV_Heading4,
    MFV_Heading5,
    MFV_META_HeadingMax = MFV_Heading5,

    MFV_COUNT,
};

struct TextStyles {
    ImFont* fonts[MFV_COUNT];
    ImU32 fontColors[MFV_COUNT];
    float regularFontSize /* = fonts[MFV_Proportional]->FontSize */;
};

extern TextStyles gTextStyles;

/// - Spans from cursor X pos, all the way to the right at max content width
/// - Height depends on the text inside
struct TextEdit {
    ImGuiID id;
    float linePadding = 6.0f;

    // Internal state, use with care
    struct Line {
        // Data
        // TODO gap buffer?
        ImVector<ImWchar> buffer;
    };

    std::vector<Line> lines;

    // Ctor/dtor does nothing special except default initializes (as in sane default values, not the C++ standard kind) fields
    TextEdit(ImGuiID id);
    TextEdit(const char* id);
    ~TextEdit();

    void Show();

    void SetContent(std::string_view text);
};

} // namespace Ionl
