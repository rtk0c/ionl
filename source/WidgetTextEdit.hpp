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

    size_t GetContentSize() const { return bufferSize - gapSize; }
    size_t GetFrontSize() const { return frontSize; }
    size_t GetBackSize() const { return bufferSize - frontSize - gapSize; }
    size_t GetGapSize() const { return gapSize; }

    const ImWchar& operator[](size_t i) const { return i > frontSize ? buffer[i + gapSize] : buffer[i]; }
    ImWchar& operator[](size_t i) { return const_cast<ImWchar&>(const_cast<const TextBuffer&>(*this)[i]); }

    std::string ExtractContent() const;
    void UpdateContent(std::string_view content);
};

/// - Spans from ImGui::GetCursorPos().x, all the way to the right at max content width
/// - Height depends on the text inside
struct TextEdit {
    /* [In] */ ImGuiID id;
    /* [In] */ float linePadding = 0.0f;
    /* [In] */ TextBuffer* buffer;

    // Generates during render loop, a list of character indices which line wraps (both soft and hard wraps)
    // This vector should always be sorted.
    // - hard wrap: point to the \n
    // - soft wrap: point to the character on the next line
    // TODO maybe we can save text properties (current face, etc.) so we can resume parsing at any wrap point, and as a result avoid reparsing/rendering the whole document every time we move the cursor
    ImVector<int64_t> _wrapPoints;

    // TODO should we move all of these to a global shared state like ImGui::InputText()?
    //   b/c there can only be one active text edit at any given time anyways, so this could simply this widget down to Ionl::TextEdit(GapBuffer& document);
    //   Counterargument: if we add shortcut to cycle between bullets, it might be desired for each bullet/TextEdit to memorize the cursor position just like every other proper text editor in existence
    //   The other advantage is not having to worry about adjusting the cursor position when multiple TextEdit refer to the same TextBuffer

    // The cursor is represented by a logical index into the buffer, that either points to an existing character or end of the logical area.
    // When the cursor is moved, the gap position is not adjusted immediately -- this means:
    //     `_cursorBufferIdx == buffer->frontSize` may not be true if the user just moved the cursor but didn't enter any text
    //     This is an optimization for when the user is just navigating by clicking in places, where if we had adjusted
    //     the buffer gap, it would be completely unnecessary work and waste battery.
    // - Implies gap size can never be 0
    // - Implies whenever we do an insert, we should always check for the size of the gap buffer afterwards and expand if necessary
    // - Example: a completely empty buffer, _cursorIdx == 0, we can write here
    // - Example: at the end of a document, _cursorIdx == buffer->GetContentSize()

    // The selection range is described by:
    //     let begin = min(_cursorIdx, _anchorIdx)
    //     let end = max(_cursorIdx, _anchorIdx)
    //     [begin, end)
    // As such:
    // - if _cursorIdx == _anchorIdx, there is no selection

    int64_t _cursorIdx = 0;
    int64_t _anchorIdx = 0;

    // Offset of the glyph that the cursor is hovering, from draw origin
    ImVec2 _cursorVisualOffset;
    // The font that the cursor is currently hovering above, determines things like cursor dimensions
    ImFont* _cursorAssociatedFont = nullptr;
    float _cursorAnimTimer = 0.0f;

    // Whether the cursor is on a wrapping point (end of a soft wrapped line).
    bool _cursorIsAtWrapPoint = false;

    // false: the cursor is after the wrap (next line), or that no soft wrap is applied
    // true: the cursor is before the wrap (prev line)
    bool _cursorAffinity = false;

    void Show();

    bool HasSelection() const;
    int64_t GetSelectionBegin() const;
    int64_t GetSelectionEnd() const;
    void SetSelection(int64_t begin, int64_t end, bool cursorAtBegin = false);
    void SetCursor(int64_t cursor);
};

} // namespace Ionl
