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

/// Represents a (potential) font variant to be used.
/// Each element except 'regular' may be missing, and should fallback to the regular style.
enum MarkdownFace {
    // NOTE: these must all have the same FontSize
    // NOTE: no underline and strikethrough fonts, we just draw our own decorations for those
    MF_Proportional,
    MF_ProportionalItalic,
    MF_ProportionalBold,
    MF_ProportionalBoldItalic,
    MF_Monospace,
    MF_MonospaceItalic,
    MF_MonospaceBold,
    MF_MonospaceBoldItalic,

    MF_Heading1,
    MF_Heading2,
    MF_Heading3,
    MF_Heading4,
    MF_Heading5,
    MF_META_HeadingMax = MF_Heading5,

    MF_COUNT,
};

struct TextStyles {
    ImFont* faceFonts[MF_COUNT];
    ImU32 faceColors[MF_COUNT];
    // Must be set by the user to...
    float regularFontSize /* = fonts[MF_Proportional]->FontSize */;
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
