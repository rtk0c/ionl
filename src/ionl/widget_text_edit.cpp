#include "widget_text_edit.hpp"

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <format>
#include <iostream>
#include <span>
#include <sstream>
#include <utility>

using namespace std::literals;
using namespace Ionl;

namespace {
#if IONL_DEBUG_FEATURES
struct DebugShowContext {
    ImRect bb;
    const ImWchar* sourceBeg;
    const ImWchar* sourceEnd;
};

const char* TextStyleTypeToString(TextStyleType type) {
    if (IsHeading(type)) {
        constexpr const char* kHeadings[] = { "H1", "H2", "H3", "H4", "H5" };
        return kHeadings[CalcHeadingLevel(type) - 1];
    }
    switch (type) {
        using enum TextStyleType;
        case Regular: return "Reg";
        case Url: return "URL";

        // We can ignore headings
        default: return nullptr;
    }
}

template <typename TChar>
void ShowDebugTextRunContents(const TChar* source, const TextRun& tr) {
    // HACK: support TChar being other integral types, e.g. unsigned short for UTF-8 instead of the standard wchar_t
    if constexpr (sizeof(TChar) == sizeof(char)) {
        ImGui::TextEx(source + tr.begin, source + tr.end);
    } else if constexpr (sizeof(TChar) == sizeof(ImWchar)) {
        char buf[1000];
        ImTextStrToUtf8(buf, IM_ARRAYSIZE(buf), source + tr.begin, source + tr.end);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::TextUnformatted("Unknown char type");
    }
}

template <typename TChar>
void ShowDebugTextRun(const TChar* source, const TextRun& tr) {
    ImGui::Text("Segment: [%zu,%zu); %s %c%c%c%c%c",
        tr.begin,
        tr.end,
        TextStyleTypeToString(tr.style.type),
        tr.style.isBold ? 'b' : '-',
        tr.style.isItalic ? 'i' : '-',
        tr.style.isUnderline ? 'u' : '-',
        tr.style.isStrikethrough ? 's' : '-',
        tr.style.isMonospace ? 'm' : '-');

    ImGui::SameLine();
    ImGui::TextDisabled("(show)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ShowDebugTextRunContents(source, tr);
        ImGui::EndTooltip();
    }
}

template <typename TChar>
void ShowDebugTextRuns(const TChar* source, std::span<const TextRun> textRuns) {
    ImGui::Text("Showing %zu TextRun's:", textRuns.size());
    for (size_t i = 0; i < textRuns.size(); ++i) {
        ImGui::Text("[%zu]", i);
        ImGui::SameLine();
        ShowDebugTextRun(source, textRuns[i]);
    }
}

template <typename TStream>
void PrintDebugTextRun(TStream&& out, std::string_view source, const TextRun& tr) {
    std::string_view segment;
    if (source.empty()) {
        segment = "[OMITTED]"sv;
    } else {
        segment = source.substr(tr.begin, tr.end - tr.begin);
    }
    std::print(out,
        // {:6} - force pad to 3 characters wide for alignment in the terminal
        "[{:6},{:6}) {}{}{}{}{} \"{}\"\n",
        tr.begin,
        tr.end,
        // Text property flags
        tr.style.isBold ? 'b' : '-',
        tr.style.isItalic ? 'i' : '-',
        tr.style.isUnderline ? 'u' : '-',
        tr.style.isStrikethrough ? 's' : '-',
        tr.style.isMonospace ? 'm' : '-',
        segment);
}

template <typename TStream>
void PrintDebugTextRuns(TStream&& out, std::string_view source, std::span<const TextRun> textRuns) {
    for (size_t i = 0; i < textRuns.size(); ++i) {
        std::print(out, "[{}] ", i);
        PrintDebugTextRun(out, source, textRuns[i]);
        out << ' ';
    }
}

void ShowDebugGlyphRun(const DebugShowContext& ctx, const GlyphRun& glyphRun) {
    ImGui::Text("TL: (%f, %f)", glyphRun.pos.x, glyphRun.pos.y);

    ImGui::SameLine();
    ImGui::TextDisabled("(show)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ShowDebugTextRunContents(ctx.sourceBeg, glyphRun.tr);
        ImGui::EndTooltip();
    }
}

void ShowDebugGlyphRuns(const DebugShowContext& ctx, std::span<const GlyphRun> glyphRuns) {
    ImGui::Text("Showing %zu GlyphRun's:", glyphRuns.size());
    for (size_t i = 0; i < glyphRuns.size(); ++i) {
        ImGui::Text("[%zu]", i);
        ImGui::SameLine();
        ShowDebugGlyphRun(ctx, glyphRuns[i]);
    }
}

const char* StringifyBool(bool v) {
    return v ? "true" : "false";
}

const char* StringifyCursorAffinity(CursorAffinity v) {
    switch (v) {
        using enum CursorAffinity;
        case Irrelevant: return "Irrelevant";
        case Upstream: return "Upstream";
        case Downstream: return "Downstream";
    }
    return "";
}
#endif

ImVec2 CalcSubTextRunDim(const TextBuffer& tb, const TextRun& tr, int64_t idxBeg, int64_t idxEnd) {
    auto& face = gMarkdownStylesheet.LookupFace(tr.style);
    auto beg = &tb.gapBuffer.buffer[idxBeg];
    auto end = &tb.gapBuffer.buffer[idxEnd];
    return face.font->CalcTextSize(face.font->FontSize, std::numeric_limits<float>::max(), 0.0f, beg, end);
}

struct LayoutInput {
    // [Required] Markdown styling.
    const MarkdownStylesheet* styles;
    // [Required] Source buffer which generated the TextRun's.
    const GapBuffer* src;
    // [Required]
    std::span<const TextRun> textRuns;
    // [Optional] Width to wrap lines at; set to 0.0f to ignore line width.
    float viewportWidth = std::numeric_limits<float>::max();
};

struct LayoutOutput {
    std::vector<GlyphRun> glyphRuns;
    ImVec2 boundingBox;
};

// TODO might be an idea to stop showing any text if viewportWidth is smaller than a certain threshold
LayoutOutput LayMarkdownTextRuns(const LayoutInput& in) {
    LayoutOutput out;

    ImVec2 currPos{};
    ImVec2 currLineDim{};
    bool isBeginningOfParagraph = true;

    for (const auto& textRun : in.textRuns) {
        auto& face = in.styles->LookupFace(textRun.style);

        isBeginningOfParagraph = false;

        const ImWchar* beg = &in.src->buffer[textRun.begin];
        const ImWchar* end = &in.src->buffer[textRun.end];
        // Try to lay this [beg,end) on current line, and if we can't, retry with [remaining,end) until we are done with this TextRun
        int numGenerated = 0;
        while (true) {
            numGenerated += 1;

            const ImWchar* remaining;
            // Use ImFont::CalcTextLineSize instead of ImFont::CalcTextSize to get line breaking on word boundary,
            // instead of on any arbitrary character implemented by the former.
            // TODO don't strip whitespace at end of line, we need it to be inside a GlyphRun for cursor position code to function correctly
            auto runDim = face.font->CalcTextLineSize(face.font->FontSize, in.viewportWidth, in.viewportWidth, beg, end, &remaining);
            // `beg` acts as the `remaining` from the last iteration (and for the first iteration, if nothing is placed hence `remaining == beg`, we should bail out too)
            if (remaining == beg) {
                // The inner algorithm is deadlocked, bail out
                std::stringstream ss;
                PrintDebugTextRun(ss, std::string_view(), textRun);
                std::string_view v = ss.rdbuf()->view();
                ImGui::DebugLog("LayMarkdownTextRuns(): bailing out because text cannot be laid in the given space for TextRun %.*s", (int)v.size(), v.data());
                break;
            }

            GlyphRun glyphRun;
            glyphRun.tr = textRun;
            glyphRun.tr.begin = std::distance(in.src->PtrBegin(), beg);
            glyphRun.tr.end = std::distance(in.src->PtrBegin(), remaining);
            // If a TextRun emits multiple GlyphRun's, only the last one should have this property set -- we do it after this loop [1]
            glyphRun.tr.hasParagraphBreak = false;
            // Similarly this should be set for everyone but the last GlyphRun
            glyphRun.isSoftWrapped = numGenerated >= 2;
            glyphRun.pos = currPos;
            glyphRun.horizontalAdvance = runDim.x;
            glyphRun.height = runDim.y;
            out.glyphRuns.push_back(std::move(glyphRun));

            currPos.x += runDim.x;
            currLineDim.x += runDim.x;
            currLineDim.y = ImMax(currLineDim.y, runDim.y);

            if (remaining == end) {
                // Finished processing this TextRun
                break;
            }

            // Not finished, next iteration: [remaining,end)
            beg = remaining;

            // Wrap onto next line
            currPos.x = 0;
            currPos.y += currLineDim.y + in.styles->linePadding;
            out.boundingBox.x = ImMax(out.boundingBox.x, currLineDim.x);
            out.boundingBox.y += currLineDim.y + in.styles->linePadding;
            currLineDim = {};
        }

        // Set last GlyphRun's property, see above [1]
        if (!out.glyphRuns.empty()) {
            auto& lastOut = out.glyphRuns.back();
            lastOut.tr.hasParagraphBreak = textRun.hasParagraphBreak;
        }

        if (textRun.hasParagraphBreak) {
            currPos.x = 0;
            currPos.y += currLineDim.y + in.styles->paragraphPadding;
            out.boundingBox.x = ImMax(out.boundingBox.x, currLineDim.x);
            out.boundingBox.y += currLineDim.y + in.styles->paragraphPadding;
            currLineDim = {};
            isBeginningOfParagraph = true;
        }
    }

    if (!isBeginningOfParagraph) {
        // Add last line's height (where the wrapping code is not reached)
        out.boundingBox.y += currLineDim.y;
    }

    return out;
}

void RefreshTextEditCachedData(TextEdit& te, float viewportWidth) {
    TextBuffer& tb = *te._tb;

    if (te._cachedDataVersion == tb.cacheDataVersion &&
        te._cachedViewportWidth == viewportWidth) {
        return;
    }
    // There must be a bug if we somehow have a newer version in the TextEdit (downstream) than its corresponding TextBuffer (upstream)
    assert(te._cachedDataVersion <= tb.cacheDataVersion);

    auto res = LayMarkdownTextRuns({
        .styles = &gMarkdownStylesheet,
        .src = &tb.gapBuffer,
        .textRuns = std::span(tb.textRuns),
        .viewportWidth = viewportWidth,
    });

    te._cachedGlyphRuns = std::move(res.glyphRuns);
    te._cachedContentHeight = res.boundingBox.y;
    te._cachedDataVersion = tb.cacheDataVersion;
    te._cachedViewportWidth = viewportWidth;

    // TODO adjust cursor related information
}

bool IsCharAPartOfWord(ImWchar c) {
    return !std::isspace(c);
}

bool IsWordBreaking(ImWchar a, ImWchar b) {
    // TODO break on e.g. punctuation and letter boundaries
    return false;
}

// TODO bring in ICU for a proper word breaking iterator
// # Behavior
// If the cursor is not at a word boundary, move towards the boundary of the current word.
// If the cursor is at a word boundary, move towards the adjacent word's boundary. That is, if cursor is currently at the left boundary, it will be moved
// to the previous word's left boundary; if it's at the right boundary, it will be moved to the next word's right boundary.
//
// Exceptionally, if there is no adjacent word the cursor will be clamped to the boundaries of the document.
//
// # Notes on terminology
// "move towards" means to adjust the index forwards or backwards to the desired location, depending on `delta` being positive or negative.
int64_t CalcAdjacentWordPos(GapBuffer& buf, int64_t logicalIndex, int delta) {
    GapBufferIterator it(buf);
    it += logicalIndex;

    int dIdx = delta > 0 ? 1 : -1; // "delta index"
    // TODO

    return logicalIndex;
}

std::pair<size_t, int64_t> FindLineWrapBeforeIndex(std::span<const GlyphRun> glyphRuns, size_t startingGlyphRunIdx) {
    assert(!glyphRuns.empty());
    for (size_t i = startingGlyphRunIdx; i != -1; --i) {
        auto& glyphRun = glyphRuns[i];
        // Prioritize paragraph breaks over soft wrapping
        if (glyphRun.tr.hasParagraphBreak && i != startingGlyphRunIdx) {
            return { i + 1, glyphRuns[i + 1].tr.begin };
        }
        if (glyphRun.isSoftWrapped) {
            return { i, glyphRun.tr.begin };
        }
    }
    // We consider the very first GlyphRun wrapped, for cursor moving to it
    return { 0, glyphRuns.front().tr.begin };
}

std::pair<size_t, int64_t> FindLineWrapAfterIndex(std::span<const GlyphRun> glyphRuns, size_t startingGlyphRunIdx) {
    assert(!glyphRuns.empty());
    for (size_t i = startingGlyphRunIdx; i < glyphRuns.size(); ++i) {
        auto& glyphRun = glyphRuns[i];
        if (glyphRun.isSoftWrapped && i != startingGlyphRunIdx) {
            return { i, glyphRun.tr.begin };
        }
        if (glyphRun.tr.hasParagraphBreak) {
            return { i, glyphRun.tr.end };
        }
    }
    // Similarly, the very last GlyphRun is considered wrapped, for cursor moving to it
    return { glyphRuns.size() - 1, glyphRuns.back().tr.end };
}

size_t FindGlyphRunContainingIndex(std::span<const GlyphRun> glyphRuns, size_t startingGlyphRunIdx, int64_t bufferIndex) {
    assert(!glyphRuns.empty());
    auto& startingGlyphRun = glyphRuns[startingGlyphRunIdx];

    int delta;
    size_t bound;
    if (bufferIndex < startingGlyphRun.tr.begin) {
        // Target index is before starting point
        delta = -1;
        bound = static_cast<size_t>(-1);
    } else {
        // Target index is after starting point
        delta = 1;
        bound = glyphRuns.size();
    }

    for (size_t i = startingGlyphRunIdx; i != bound; i += delta) {
        auto& r = glyphRuns[i];

        if (bufferIndex >= r.tr.begin && bufferIndex < r.tr.end) {
            return i;
        }

        // We also consider cursor on \n to be inside the GlyphRun that the \n belongs to
        if (r.tr.hasParagraphBreak && bufferIndex == r.tr.end) {
            return i;
        }
    }

    return -1;
}

void RefreshCursorState(TextEdit& te) {
    auto cursorBufIdx = MapLogicalIndexToBufferIndex(te._tb->gapBuffer, te._cursorIdx);
    te._cursorCurrGlyphRun = FindGlyphRunContainingIndex(te._cachedGlyphRuns, te._cursorCurrGlyphRun, cursorBufIdx);
    auto cursorGr = &te._cachedGlyphRuns[te._cursorCurrGlyphRun];

    te._cursorIsAtWrapPoint = cursorGr->isSoftWrapped && cursorGr->tr.begin == cursorBufIdx;
    if (!te._cursorIsAtWrapPoint) {
        te._cursorAffinity = CursorAffinity::Irrelevant;
    }

    const GlyphRun* visualGr;
    float xOff;
    if (te._cursorIsAtWrapPoint && te._cursorAffinity == CursorAffinity::Upstream) {
        visualGr = &te._cachedGlyphRuns[te._cursorCurrGlyphRun - 1];
        xOff = visualGr->horizontalAdvance;
    } else {
        visualGr = cursorGr;
        // Calculate width of text between start of GlyphRun to cursor
        xOff = CalcSubTextRunDim(*te._tb, visualGr->tr, visualGr->tr.begin, cursorBufIdx).x;
    }
    te._cursorVisualHeight = visualGr->height;
    te._cursorVisualOffset.x = visualGr->pos.x + xOff;
    te._cursorVisualOffset.y = visualGr->pos.y;
}

// mouseX and mouseY should center on draw origin
std::pair<int64_t, CursorAffinity> CalcCursorStateFromMouse(const TextEdit& te, float mouseX, float mouseY) {
    auto it = te._cachedGlyphRuns.begin();

    // Advance to the desired line by searching vertically
    while (it != te._cachedGlyphRuns.end()) {
        if (it->pos.y + it->height >= mouseY) {
            break;
        }
        ++it;
    }

    auto lineBegin = it;
    while (it != te._cachedGlyphRuns.end()) {
        auto& face = gMarkdownStylesheet.LookupFace(it->tr.style);
        float x = it->pos.x;
        for (int64_t i = it->tr.begin; i < it->tr.end; ++i) {
            ImWchar ch = te._tb->gapBuffer.buffer[i];
            float w = face.font->GetCharAdvance(ch);
            // We consider the cursor to land between two characters 'ab' if it's between the halfway point of both glyphs
            // (if the cursor is inside the latter half of 'a', it will be caught by the iteration of 'b')
            if (mouseX < (x + w / 2)) {
                return { i, CursorAffinity::Irrelevant };
            }
            x += w;
        }

        // Reached end of line, declare cursor to be on the last char
        if (it->isSoftWrapped && it != lineBegin) {
            return { it->tr.begin, CursorAffinity::Upstream };
        }
        if (it->tr.hasParagraphBreak) {
            // On \n
            return { it->tr.end, CursorAffinity::Irrelevant };
        }

        ++it;
    }

    // Place at end of document if none of the lines contain the cursor position (outside content area)
    return { te._tb->gapBuffer.GetLastTextIndex(), CursorAffinity::Irrelevant };
}

// true => accept
// false => discard
bool FilterInputCharacter(ImWchar c) {
    if (std::iscntrl(c))
        return false;

    // 1. Tab/shift+tab should be handled by the text bullet logic outside for indent/dedent, we don't care
    // 2. If we wanted this, polling for key is better anyway (see logic in ImGui::InputTextEx)
    if (c != '\t')
        return false;

    return true;
}

// Post-conditions:
// - _cursorIdx (which is a logical index) remains unchanged
// - gapBuffer.GetGapBegin() == MapLogicalIndexToBufferIndex(_cursorIdx)
void MoveGapToCursor(TextEdit& te) {
    MoveGapToLogicalIndex(te._tb->gapBuffer, te._cursorIdx);
}

void InsertAtCursor(TextEdit& te, const ImWchar* text, size_t size) {
    if (te.HasSelection()) {
        // TODO handle replacement of selection
    } else {
        MoveGapToCursor(te);
        InsertAtGap(te._tb->gapBuffer, text, size);
        te._cursorIdx += size;
        te._anchorIdx = te._cursorIdx;
    }
}
} // namespace

Ionl::TextEdit::TextEdit(ImGuiID id, TextBuffer& tb)
    : _id{ id }
    , _tb{ &tb } //
{
}

void Ionl::TextEdit::Show() {
    auto& g = *ImGui::GetCurrentContext();
    auto& io = ImGui::GetIO();
    auto window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    auto contentRegionAvail = ImGui::GetContentRegionAvail();

    // Performs text layout if necessary
    // -> updates _cachedGlyphRuns
    // -> updates _cachedContentHeight
    RefreshTextEditCachedData(*this, contentRegionAvail.x);

    ImVec2 widgetSize(contentRegionAvail.x, _cachedContentHeight);
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + widgetSize };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, _id)) {
        return;
    }

    bool hovered = ImGui::ItemHoverable(bb, _id, g.LastItemData.InFlags);
    bool userClicked = hovered && io.MouseClicked[ImGuiMouseButton_Left];

    auto activeId = ImGui::GetActiveID();

    if (activeId != _id && userClicked) {
        activeId = _id;

        // TODO for debugging purposes, remove after mosue click set cursor pos is implemented
        RefreshCursorState(*this);

        // Adapted from imgui_widget.cpp Imgui::InputTextEx()

        ImGui::SetActiveID(_id, window);
        ImGui::SetFocusID(_id, window);
        ImGui::FocusWindow(window);
    }
    if (activeId == _id) {
        // Declare our inputs
        // NOTE: ImGui::InputTextEx() uses keys like backspace but doesn't declare
        //       A quick look into the code shows that ActiveIdUsingKeyInputMask is only used by the nav system, and that only cares about the keys right at the moment
        if (userClicked)
            ImGui::SetKeyOwner(ImGuiKey_MouseLeft, _id);
        g.ActiveIdUsingNavDirMask |=
            (1 << ImGuiDir_Left) |
            (1 << ImGuiDir_Right) |
            (1 << ImGuiDir_Up) |
            (1 << ImGuiDir_Down);
        // ImGui::SetKeyOwner(ImGuiKey_LeftArrow, _id);
        // ImGui::SetKeyOwner(ImGuiKey_RightArrow, _id);
        // ImGui::SetKeyOwner(ImGuiKey_UpArrow, _id);
        // ImGui::SetKeyOwner(ImGuiKey_DownArrow, _id);
        // ImGui::SetKeyOwner(ImGuiKey_Escape, _id);
        // ImGui::SetKeyOwner(ImGuiKey_NavGamepadCancel, _id);
        ImGui::SetKeyOwner(ImGuiKey_Home, _id);
        ImGui::SetKeyOwner(ImGuiKey_End, _id);
    }

    int64_t bufContentSize = _tb->gapBuffer.GetContentSize();

    // Process keyboard inputs
    // We skip all keyboard inputs if the text buffer is empty (i.e. no runs generated) because all of the operate some non-empty text sequence
    if (activeId == _id && !g.ActiveIdIsJustActivated && !_cachedGlyphRuns.empty()) {
        bool isOSX = io.ConfigMacOSXBehaviors;
        bool isMovingWord = isOSX ? io.KeyAlt : io.KeyCtrl;
        bool isShortcutKey = isOSX ? (io.KeyMods == ImGuiMod_Super) : (io.KeyMods == ImGuiMod_Ctrl);

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (_cursorIsAtWrapPoint && _cursorAffinity == CursorAffinity::Downstream) {
                _cursorAffinity = CursorAffinity::Upstream;
            } else {
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(_tb->gapBuffer, _cursorIdx, -1) : -1;
                _cursorIdx = ImClamp<int64_t>(_cursorIdx, 0, bufContentSize);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                // Cleaned up by RefreshCursorState() to CursorAffinity::Irrelevant when necessary
                _cursorAffinity = CursorAffinity::Downstream;
            }

            RefreshCursorState(*this);
            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            if (_cursorIsAtWrapPoint && _cursorAffinity == CursorAffinity::Upstream) {
                _cursorAffinity = CursorAffinity::Downstream;
            } else {
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(_tb->gapBuffer, _cursorIdx, +1) : +1;
                _cursorIdx = ImClamp<int64_t>(_cursorIdx, 0, bufContentSize);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                // Cleaned up by RefreshCursorState() to CursorAffinity::Irrelevant when necessary
                _cursorAffinity = CursorAffinity::Upstream;
            }

            RefreshCursorState(*this);
            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            if (isMovingWord) {
                // Move to beginning of document
                _cursorIdx = 0;
                _anchorIdx = 0;
            } else {
                size_t starting = _cursorIsAtWrapPoint && _cursorAffinity == CursorAffinity::Upstream
                    ? _cursorCurrGlyphRun - 1
                    : _cursorCurrGlyphRun;
                auto [prevWrapPt, idx] = FindLineWrapBeforeIndex(_cachedGlyphRuns, starting);

                _cursorIdx = MapBufferIndexToLogicalIndex(_tb->gapBuffer, idx);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                // Cleaned up by RefreshCursorState() to CursorAffinity::Irrelevant when necessary
                _cursorAffinity = CursorAffinity::Downstream;
            }

            RefreshCursorState(*this);
            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            if (isMovingWord) {
                // Move to end of document
                _cursorIdx = bufContentSize;
                _anchorIdx = bufContentSize;
            } else {
                size_t starting = _cursorIsAtWrapPoint && _cursorAffinity == CursorAffinity::Upstream
                    ? _cursorCurrGlyphRun - 1
                    : _cursorCurrGlyphRun;
                auto [prevWrapPt, idx] = FindLineWrapAfterIndex(_cachedGlyphRuns, starting);

                _cursorIdx = MapBufferIndexToLogicalIndex(_tb->gapBuffer, idx);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                // Cleaned up by RefreshCursorState() to CursorAffinity::Irrelevant when necessary
                _cursorAffinity = CursorAffinity::Upstream;
            }

            RefreshCursorState(*this);
            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            // TODO
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            // TODO
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            // TODO
        } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            // TODO
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_X)) {
            // Cut
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_C)) {
            // Copy
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_V)) {
            // Paste
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            // Undo
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            // Redo
            // TODO
        } else if (isShortcutKey && ImGui::IsKeyPressed(ImGuiKey_A)) {
            // Select all
            // TODO
        }

        float mouseX = io.MousePos.x - bb.Min.x;
        float mouseY = io.MousePos.y - bb.Min.y;

        if (io.MouseClicked[ImGuiMouseButton_Left]) {
            auto [idx, affinity] = CalcCursorStateFromMouse(*this, mouseX, mouseY);
            _cursorIdx = MapBufferIndexToLogicalIndex(_tb->gapBuffer, idx);
            if (!io.KeyShift) _anchorIdx = _cursorIdx;
            _cursorAffinity = affinity;
            RefreshCursorState(*this);
            _cursorAnimTimer = 0.0f;
        }

        // Process character inputs

        if (io.InputQueueCharacters.Size > 0) {
            // TODO imgui checks "input_requested_by_nav", is that necessary?
            bool ignoreCharInputs = (io.KeyCtrl && !io.KeyAlt) || (isOSX && io.KeySuper);
            if (!ignoreCharInputs) {
                for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                    ImWchar c = io.InputQueueCharacters[i];
                    if (FilterInputCharacter(c))
                        continue;
                    InsertAtCursor(*this, &c, 1);
                }
                io.InputQueueCharacters.resize(0);

                // NOTE: this TextEdit's cache will be refreshed next frame
                _tb->RefreshCaches();
                RefreshCursorState(*this);
            }
        }
    }

    auto drawList = window->DrawList;

    auto styleTextColor = ImGui::GetColorU32(ImGuiCol_Text);
    auto styleSelectionColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    // Draw selection if one exists
    if (activeId == _id && _cursorIdx != _anchorIdx) {
        // TODO possible optimization: start searching for selection end GlyphRun at whichever location is closer, _cursorCurrGlyphRun or end of document
        auto selBegin = MapBufferIndexToLogicalIndex(_tb->gapBuffer, this->GetSelectionBegin());
        auto selBeginGrIdx = FindGlyphRunContainingIndex(_cachedGlyphRuns, _cursorCurrGlyphRun, selBegin);
        auto selEnd = MapBufferIndexToLogicalIndex(_tb->gapBuffer, this->GetSelectionEnd());
        auto selEndGrIdx = FindGlyphRunContainingIndex(_cachedGlyphRuns, _cursorCurrGlyphRun, selEnd);

        // Defensive: in case something goes wrong, we just show an error and skip drawing
        if (selBeginGrIdx != -1 && selEndGrIdx != -1) {
            if (selBeginGrIdx == selEndGrIdx) {
                auto& gr = _cachedGlyphRuns[selBeginGrIdx];
                auto pMin = bb.Min + gr.pos;
                pMin.x += CalcSubTextRunDim(*_tb, gr.tr, gr.tr.begin, selBegin).x;
                auto pMax = bb.Min + gr.pos;
                pMax.x += gr.horizontalAdvance - CalcSubTextRunDim(*_tb, gr.tr, selEnd, gr.tr.end).x;
                pMax.y += gr.height;
                drawList->AddRectFilled(pMin, pMax, styleSelectionColor);
            } else {
                ImVec2 pMin, pMax;

                // Draw selection for first GlyphRun
                auto& selBeginGr = _cachedGlyphRuns[selBeginGrIdx];
                pMin = bb.Min + selBeginGr.pos;
                pMin.x += CalcSubTextRunDim(*_tb, selBeginGr.tr, selBeginGr.tr.begin, selBegin).x;
                pMax = bb.Min + selBeginGr.pos + ImVec2(selBeginGr.horizontalAdvance, selBeginGr.height);
                drawList->AddRectFilled(pMin, pMax, styleSelectionColor);

                // Draw in between `selBeginGrIdx` and `selEndGrIdx`
                for (int64_t grIdx = selBeginGrIdx + 1; grIdx < selEndGrIdx; ++grIdx) {
                    auto& gr = _cachedGlyphRuns[grIdx];
                    drawList->AddRectFilled(
                        bb.Min + gr.pos,
                        bb.Min + gr.pos + ImVec2(gr.horizontalAdvance, gr.height),
                        styleSelectionColor);
                }

                // Draw selection for last GlyphRun
                auto& selEndGr = _cachedGlyphRuns[selEndGrIdx];
                pMin = bb.Min + selEndGr.pos;
                pMax = bb.Min + selEndGr.pos;
                pMax.x += CalcSubTextRunDim(*_tb, selEndGr.tr, selEndGr.tr.begin, selEnd).x;
                pMax.y += selEndGr.height;
                drawList->AddRectFilled(pMin, pMax, styleSelectionColor);
            }
        } else {
            ImGui::BeginTooltip();
            ImGui::Text("Error: cannot find GlyphRun corresponding to selection begin or end. This is a bug, please report to developer immediately.");
            ImGui::Text("selection begin = %" PRId64 ", queried GlyphRun index = %zu", selBegin, selBeginGrIdx);
            ImGui::Text("selection end = %" PRId64 ", queried GlyphRun index = %zu", selEnd, selEndGrIdx);
            ImGui::EndTooltip();
        }
    }

    // Draw text
    for (auto& glyphRun : _cachedGlyphRuns) {
        auto& face = gMarkdownStylesheet.LookupFace(glyphRun.tr.style);

        auto absPos = bb.Min + glyphRun.pos;
        auto font = face.font;
        auto color = face.color == 0 ? styleTextColor : face.color;
        drawList->AddText(font, font->FontSize, absPos, color, &_tb->gapBuffer.buffer[glyphRun.tr.begin], &_tb->gapBuffer.buffer[glyphRun.tr.end]);

        if (glyphRun.tr.style.isUnderline) {
            float y = absPos.y + font->FontSize;
            drawList->AddLine(ImVec2(absPos.x, y), ImVec2(absPos.x + glyphRun.horizontalAdvance, y), color);
        }
        if (glyphRun.tr.style.isStrikethrough) {
            float y = absPos.y + font->FontSize / 2;
            drawList->AddLine(ImVec2(absPos.x, y), ImVec2(absPos.x + glyphRun.horizontalAdvance, y), color);
        }
    }

    // Draw cursor
    // TODO move drawing cursor blinking outside the ImGui loop
    if (activeId == _id) {
        _cursorAnimTimer += io.DeltaTime;

        bool cursorVisible = ImFmod(_cursorAnimTimer, 1.20f) <= 0.80f;
        ImVec2 cursorPos = bb.Min + _cursorVisualOffset;
        ImRect cursorRect{
            cursorPos.x,
            cursorPos.y + 1.5f,
            cursorPos.x + 1.0f,
            cursorPos.y + _cursorVisualHeight - 0.5f,
        };
        if (cursorVisible) {
            drawList->AddLine(cursorRect.Min, cursorRect.GetBL(), ImGui::GetColorU32(ImGuiCol_Text));
        }
    }

    // TODO handle up/down arrow at edge of the document should move to prev/next bullet point

    // Release focus when we click outside
    if (activeId == _id && io.MouseClicked[ImGuiMouseButton_Left] && !hovered) {
        ImGui::ClearActiveID();
    }

#if IONL_DEBUG_FEATURES
    if (_debugShowBoundingBoxes) {
        auto dl = ImGui::GetForegroundDrawList();
        dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
        for (auto& glyphRun : _cachedGlyphRuns) {
            auto& face = gMarkdownStylesheet.LookupFace(glyphRun.tr.style);
            auto absPos = bb.Min + glyphRun.pos;
            dl->AddRect(absPos, ImVec2(absPos.x + glyphRun.horizontalAdvance, absPos.y + face.font->FontSize), IM_COL32(255, 0, 255, 255));
        }
    }

    DebugShowContext ctx{
        .bb = bb,
        .sourceBeg = _tb->gapBuffer.PtrBegin(),
        .sourceEnd = _tb->gapBuffer.PtrEnd(),
    };

    // TODO allow all TextEdit instances to share a single window
    ImGui::Begin("dbg: TextEdit");
    {
        ImGui::Checkbox("Show bounding boxes", &_debugShowBoundingBoxes);

        ImGui::Text("_cursorIdx = %zu", _cursorIdx);
        ImGui::Text("_anchorIdx = %zu", _anchorIdx);
        if (HasSelection()) {
            ImGui::Text("Selection range: [%zu,%zu)", GetSelectionBegin(), GetSelectionEnd());
        } else {
            ImGui::Text("Selection range: none");
        }
        ImGui::Text("_cursorIsAtWrapPoint = %s", StringifyBool(_cursorIsAtWrapPoint));
        ImGui::Text("_cursorAffinity = %s", StringifyCursorAffinity(_cursorAffinity));
        ImGui::Text("_cursorVisualOffset = (%f, %f)", _cursorVisualOffset.x, _cursorVisualOffset.y);
        ImGui::Text("_cursorVisualHeight = %f", _cursorVisualHeight);
        ImGui::Text("_cursorCurrGlyphRun = [%zu]", _cursorCurrGlyphRun);
        ImGui::Indent();
        ShowDebugGlyphRun(ctx, _cachedGlyphRuns[_cursorCurrGlyphRun]);
        ImGui::Unindent();
        ImGui::Text("_cachedContentHeight = %f", _cachedContentHeight);
        ImGui::Text("_cachedViewportWidth = %f", _cachedViewportWidth);
        ImGui::Text("_cachedDataVersion = %d", _cachedDataVersion);

        ImGui::InputInt("##MoveTargetIndex", &_debugTargetBufferIndex);
        ImGui::SameLine();
        if (ImGui::Button("Move gap to (buffer) index")) {
            MoveGapToBufferIndex(_tb->gapBuffer, _debugTargetBufferIndex);
        }

        ImGui::InputInt("##MoveDelta", &_debugMoveGapDelta);
        ImGui::SameLine();
        if (ImGui::Button("Move gap by this amount")) {
            int64_t newIdx = _tb->gapBuffer.GetGapBegin() + _debugMoveGapDelta;
            MoveGapToBufferIndex(_tb->gapBuffer, newIdx);
        }

        ImGui::InputInt("##GapSize", &_debugDesiredGapSize);
        ImGui::SameLine();
        if (ImGui::Button("Widen gap")) {
            WidenGap(_tb->gapBuffer, _debugDesiredGapSize);
        }

        ImGui::Checkbox("Show GapBuffer contents", &_debugShowGapBufferDump);
        if (_debugShowGapBufferDump) {
            ImGui::Begin("dbg: TextEdit._tb->gapBuffer");
            ShowGapBuffer(_tb->gapBuffer);
            ImGui::End();
        }

        ImGui::Checkbox("Show TextEdit._tb->[TextRun]", &_debugShowTextRuns);
        if (_debugShowTextRuns) {
            ImGui::Begin("dbg: TextEdit._tb->[TextRun]");
            ShowDebugTextRuns(_tb->gapBuffer.buffer, _tb->textRuns);
            ImGui::End();
        }

        ImGui::Checkbox("Show TextEdit.[GlyphRun]", &_debugShowGlyphRuns);
        if (_debugShowGlyphRuns) {
            ImGui::Begin("dbg: TextEdit.[GlyphRun]");
            ShowDebugGlyphRuns(ctx, _cachedGlyphRuns);
            ImGui::End();
        }

        if (ImGui::Button("Dump GapBuffer contents to stdout")) {
            DumpGapBuffer(_tb->gapBuffer, std::cout);
        }

        if (ImGui::Button("Refresh TextBuffer caches")) {
            _tb->RefreshCaches();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("This will increase TextBuffer::cacheDataVersion by 1, which will cause this TextEdit's cached data to be refreshed next frame.");
            ImGui::EndTooltip();
        }

        if (ImGui::Button("Refresh TextEdit caches only")) {
            _cachedDataVersion = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Set _cachedDataVersion to 0 to force a cache refresh next frame.");
            ImGui::EndTooltip();
        }

        if (ImGui::Button("Refresh cursor state")) {
            RefreshCursorState(*this);
        }
    }
    ImGui::End();
#endif
}

bool Ionl::TextEdit::HasSelection() const {
    return _cursorIdx != _anchorIdx;
}

int64_t Ionl::TextEdit::GetSelectionBegin() const {
    return ImMin(_cursorIdx, _anchorIdx);
}

int64_t Ionl::TextEdit::GetSelectionEnd() const {
    return ImMax(_cursorIdx, _anchorIdx);
}

void Ionl::TextEdit::SetSelection(int64_t begin, int64_t end, bool cursorAtBegin) {
    if (cursorAtBegin) {
        _cursorIdx = begin;
        _anchorIdx = end;
    } else {
        _cursorIdx = end;
        _anchorIdx = begin;
    }
    RefreshCursorState(*this);
}

void Ionl::TextEdit::SetCursor(int64_t cursor) {
    _cursorIdx = cursor;
    _anchorIdx = cursor;
    RefreshCursorState(*this);
}
