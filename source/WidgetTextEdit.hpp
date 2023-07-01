// Special text editor designed specifically for writing outlines, with the following features:
// - Inline markdown rendering (e.g. **text** will be rendered bold with markins kept in place)
// - Text wrapping to a certain width
// - Unicode/language-aware cursor movement (TODO do we need to grab ICU for this?)
#pragma once

#include "GapBuffer.hpp"
#include "Markdown.hpp"
#include "imgui.h"

#include <string>
#include <string_view>
#include <vector>

namespace Ionl {

// TODO DPI handling?
// TODO figure out font caching or SDF based rendering: generating a separate atlas for each heading type is really costly on VRAM

struct GlyphRun {
    TextRun tr;

    // Position of the first glyph in this run, in text canvas space
    ImVec2 pos;
    float horizontalAdvance = 0.0f; // == <used MarkdownStylesheet>.LookupFace(this->tr.style).CalcTextSize(... contents of this GlyphRun ...)
    float height = 0.0f; // == <used MarkdownStylesheet>.LookupFace(this->tr.style).FontSize

    // Whether this TextRun is on a new line, created by soft wrapping
    bool isSoftWrapped = false;
};

struct TextBuffer {
    // Canonical data
    GapBuffer gapBuffer;

    // Cached data derived from canonical data
    // Invalidation and recomputation should be done by whoever modifies `gapBuffer`.
    std::vector<TextRun> textRuns;
    int cacheDataVersion = 0;

    TextBuffer(GapBuffer buf);
};

enum class CursorAffinity {
    Irrelevant,
    Upstream,
    Downstream,
};

/// - Spans from ImGui::GetCursorPos().x, all the way to the right at max content width
/// - Height depends on the text inside
struct TextEdit {
    TextBuffer* _tb;
    std::vector<GlyphRun> _cachedGlyphRuns;

    // TODO should we move all of these to a global shared state like ImGui::InputText()?
    //   b/c there can only be one active text edit at any given time anyways, so this could simply this widget down to Ionl::TextEdit(GapBuffer& document);
    //   Counterargument: if we add shortcut to cycle between bullets, it might be desired for each bullet/TextEdit to memorize the cursor position just like every other proper text editor in existence
    //   The other advantage is not having to worry about adjusting the cursor position when multiple TextEdit refer to the same GapBuffer

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

    // Cursor affinity in the traditional text editing sense.
    // Only meaningful when the cursor is at the beginning of a soft-wrapped GlyphRun.
    CursorAffinity _cursorAffinity = CursorAffinity::Upstream;
    size_t _cursorCurrGlyphRun = 0;

    // Offset of the glyph that the cursor is hovering, from draw origin
    ImVec2 _cursorVisualOffset;
    float _cursorVisualHeight = 0.0f;
    float _cursorAnimTimer = 0.0f;

    ImGuiID _id;
    float _cachedContentHeight = 0.0f;
    float _cachedViewportWidth = 0.0f;
    int _cachedDataVersion = 0;

    // Whether the cursor is on a wrapping point (end of a soft wrapped line).
    bool _cursorIsAtWrapPoint = false;

#if IONL_DEBUG_FEATURES
    int _debugTargetBufferIndex = 0;
    int _debugMoveGapDelta = 0;
    int _debugDesiredGapSize = 64;
    bool _debugShowBoundingBoxes = false;
    bool _debugShowGapBufferDump = false;
    bool _debugShowTextRuns = false;
    bool _debugShowGlyphRuns = false;
#endif

    TextEdit(ImGuiID id, TextBuffer& textBuffer);

    void Show();

    bool HasSelection() const;
    int64_t GetSelectionBegin() const;
    int64_t GetSelectionEnd() const;
    void SetSelection(int64_t begin, int64_t end, bool cursorAtBegin = false);
    void SetCursor(int64_t cursor);
};

} // namespace Ionl
