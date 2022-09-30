// Special text editor designed specifically for writing outlines, with the following features:
// - Inline markdown rendering (e.g. **text** will be rendered bold with markins kept in place)
// - Text wrapping to a certain width
// - Unicode/language-aware cursor movement (TODO do we need to grab ICU for this?)
#pragma once

#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace Ionl {

// TODO DPI handling?

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
    MFV_Regular,
    MFV_Italics,
    MFV_Bold,
    // NOTE: no underline and strikethrough, we just draw that on our own
    MFV_Heading1,
    MFV_Heading2,
    MFV_Heading3,
    MFV_Heading4,
    MFV_Heading5,
};

struct MarkdownTextStyle {
    // 0 = normal text
    // 1 = "# Heading"
    // 2 = "## Heading"
    // ...
    int headingNumber = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
};

/// - Spans from cursor X pos, all the way to the right at max content width
/// - Height depends on the text inside
struct TextEdit {
    // To be set by the user
    /// TODO ~~If 0, a random ID will be automatically chosen on the first call to Show()~~
    ImGuiID id = 0;
    float linePadding = 6.0f;

    // Internal state, use with care
    struct Line {
        // Data
        // TODO gap buffer?
        std::string text;

        // Cache derived from data
        float lineHeight = 0.0f;
    };

    std::vector<Line> lines;

    // Ctor/dtor does nothing special except default initializes (as in sane default values, not the C++ standard kind) fields
    TextEdit();
    ~TextEdit();

    void Show();

    void SetContent(std::string_view text);
};

} // namespace Ionl
