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

struct TextBuffer {
    ImWchar* buffer;
    size_t bufferSize;
    size_t frontSize;
    size_t gapSize;

    TextBuffer();
    TextBuffer(std::string_view content);
    ~TextBuffer();

    std::string ExtractContent() const;
    void UpdateContent(std::string_view content);
};

/// - Spans from cursor X pos, all the way to the right at max content width
/// - Height depends on the text inside
struct TextEdit {
    /* [In] */ ImGuiID id;
    /* [In] */ float linePadding = 0.0f;
    /* [In] */ TextBuffer* buffer;

    // `_cursorIdx == buffer->frontSize` may not be true if the user just moved the cursor but didn't enter any text
    // This is an optimization for when the user is just navigating by clicking in places, where if we had adjusted
    // the buffer gap, it would be completely unnecessary work and waste battery.

    // The selection range is described by:
    //     let begin = min(_cursorIdx, _anchor)
    //     let end = max(_cursorIdx, _anchor)
    //     [begin, end)
    // As such:
    // - if _cursorIdx == _anchor, there is no selection

    // Generates during render loop, a list of character indices which line wraps (both soft and hard wraps)
    ImVector<size_t> _wrapPoints;
    size_t _cursorIdx = 0;
    size_t _anchor = 0;
    // Offset of the glyph that the cursor is hovering, from draw origin
    ImVec2 _cursorOffset;
    ImFont* _cursorAssociatedFont = nullptr;
    float _cursorAnimTimer = 0.0f;

    // false: the cursor is after the wrap (next line), or that no soft wrap is applied
    // true: the cursor is before the wrap (prev line)
    // NOTE: we don't treat \n in the buffer as an actual glyph, so this is used even on real line breaks
    bool _cursorAffinity = false;

    void Show();

    bool HasSelection() const;
    size_t GetSelectionBegin() const;
    size_t GetSelectionEnd() const;
    void SetSelection(size_t begin, size_t end, bool cursorAtBegin = false);
};

} // namespace Ionl
