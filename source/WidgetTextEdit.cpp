#include "WidgetTextEdit.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cassert>
#include <span>
#include <utility>

using namespace std::literals;

namespace {
using namespace Ionl;

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

    auto window = ImGui::GetCurrentWindowRead();
    auto ptTopLeft = window->DC.CursorPos;

    // Use default font because that has clearly recognizable glyph boundaries, easier for debugging purposes
    ImGui::PushFont(nullptr);
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
    ImGui::PopFont();

    auto ptBottomLeft = window->DC.CursorPos;
    ImGui::GetWindowDrawList()->AddRect(ptTopLeft, ImVec2(ptBottomLeft.x + ImGui::GetContentRegionAvail().x, ptBottomLeft.y), IM_COL32(255, 255, 0, 255));
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

void PrintDebugTextRun(std::string_view source, const TextRun& tr) {
    auto substr = source.substr(tr.begin, tr.end - tr.begin);
    printf("[%3zu,%3zu) %c%c%c%c%c \"%.*s\"\n",
        tr.begin,
        tr.end,
        tr.style.isBold ? 'b' : '-',
        tr.style.isItalic ? 'i' : '-',
        tr.style.isUnderline ? 'u' : '-',
        tr.style.isStrikethrough ? 's' : '-',
        tr.style.isMonospace ? 'm' : '-',
        (int)substr.size(),
        substr.begin());
}

void PrintDebugTextRuns(std::string_view source, std::span<const TextRun> textRuns) {
    for (size_t i = 0; i < textRuns.size(); ++i) {
        printf("[%zu] ", i);
        PrintDebugTextRun(source, textRuns[i]);
    }
}

void ShowDebugGlyphRun(const DebugShowContext& ctx, const GlyphRun& glyphRun) {
    ImGui::Text("Top left: (%f, %f)", glyphRun.pos.x, glyphRun.pos.y);

    ImGui::SameLine();
    ImGui::TextDisabled("(show)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ShowDebugTextRun(ctx.sourceBeg, glyphRun.tr);
        ImGui::EndTooltip();

        auto& face = gMarkdownStylesheet.LookupFace(glyphRun.tr.style);
        ImVec2 ptMin = ctx.bb.Min + glyphRun.pos;
        ImVec2 ptMax(ptMin.x + glyphRun.horizontalAdvance, ptMin.y + face.font->FontSize);
        ImGui::GetForegroundDrawList()->AddRect(ptMin, ptMax, IM_COL32(255, 0, 255, 255));
    }
}

void ShowDebugGlyphRuns(const DebugShowContext& ctx, std::span<const GlyphRun> glyphRuns) {
    ImGui::Text("Showing %zu GlyphRun's:", glyphRuns.size());
    for (size_t i = 0; i < glyphRuns.size(); ++i) {
        ImGui::Text("[%zu] ", i);
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
            auto runDim = face.font->CalcTextLineSize(face.font->FontSize, in.viewportWidth, in.viewportWidth, beg, end, &remaining);

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
        auto& lastOut = out.glyphRuns.back();
        lastOut.tr.hasParagraphBreak = textRun.hasParagraphBreak;

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

void RefreshTextBufferCachedData(TextBuffer& tb) {
    auto res = ParseMarkdownBuffer({
        .src = &tb.gapBuffer,
    });

    tb.textRuns = std::move(res.textRuns);
    tb.cacheDataVersion += 1;
}

void RefreshTextEditCachedData(TextEdit& te, float viewportWidth) {
    TextBuffer& tb = *te._tb;

    if (te._cachedDataVersion == tb.cacheDataVersion) {
        return;
    }
    // There must be a bug if we somehow have a newer version in the TextEdit (downstream) than its corresponding TextBuffer (upstream)
    assert(te._cachedDataVersion < tb.cacheDataVersion);

    auto res = LayMarkdownTextRuns({
        .styles = &gMarkdownStylesheet,
        .src = &tb.gapBuffer,
        .textRuns = std::span(tb.textRuns),
        .viewportWidth = viewportWidth,
    });

    te._cachedGlyphRuns = std::move(res.glyphRuns);
    te._cachedContentHeight = res.boundingBox.y;
    te._cachedDataVersion = tb.cacheDataVersion;

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
    te._cursorCurrGlyphRun = FindGlyphRunContainingIndex(te._cachedGlyphRuns, te._cursorCurrGlyphRun, te._cursorIdx);
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
        auto& face = gMarkdownStylesheet.LookupFace(visualGr->tr.style);

        // Calculate width of text between start of GlyphRun to cursor
        auto beg = &te._tb->gapBuffer.buffer[visualGr->tr.begin];
        auto end = &te._tb->gapBuffer.buffer[cursorBufIdx];
        auto dim = face.font->CalcTextSize(face.font->FontSize, std::numeric_limits<float>::max(), 0.0f, beg, end);
        xOff = dim.x;
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
} // namespace

Ionl::TextBuffer::TextBuffer(GapBuffer buf)
    : gapBuffer{ std::move(buf) } //
{
    RefreshTextBufferCachedData(*this);
}

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

    bool hovered = ImGui::ItemHoverable(bb, _id);
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

        // Declare our inputs
        // NOTE: ImGui::InputTextEx() uses keys like backspace but doesn't declare
        //       A quick look into the code shows that ActiveIdUsingKeyInputMask is only used by the nav system, and that only cares about the keys right at the moment
        ImGui::SetActiveIdUsingKey(ImGuiKey_LeftArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_RightArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_UpArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_DownArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_Escape);
        ImGui::SetActiveIdUsingKey(ImGuiKey_NavGamepadCancel);
        ImGui::SetActiveIdUsingKey(ImGuiKey_Home);
        ImGui::SetActiveIdUsingKey(ImGuiKey_End);
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
                    auto c = io.InputQueueCharacters[i];

                    // Skips
                    if (std::iscntrl(c)) continue;
                    // 1. Tab/shift+tab should be handled by the text bullet logic outside for indent/dedent, we don't care
                    // 2. If we wanted this, polling for key is better anyway
                    if (c == '\t') continue;
                    // TODO maybe we should reuse InputTextFilterCharacter in imgui_widgets.cpp

                    // Insert character into buffer
                    // TODO
                }
            }

            io.InputQueueCharacters.resize(0);

            // NOTE: this TextEdit's cache will be refreshed next frame
            RefreshTextBufferCachedData(*_tb);
        }
    }

    auto drawList = window->DrawList;

    auto styleTextColor = ImGui::GetColorU32(ImGuiCol_Text);

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

    // Draw cursor and selection
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

    ImGui::PushID(_id);
    if (ImGui::CollapsingHeader("TextEdit general debug")) {
        ImGui::Checkbox("Show bounding boxes", &_debugShowBoundingBoxes);

        ImGui::Text("_cursorIdx = %zu", _cursorIdx);
        ImGui::Text("_anchorIdx = %zu", _cursorIdx);
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
    }
    if (ImGui::CollapsingHeader("TextEdit._tb->[TextRun]")) {
        ShowDebugTextRuns(_tb->gapBuffer.buffer, _tb->textRuns);
    }
    if (ImGui::CollapsingHeader("TextEdit.[GlyphRun]")) {
        ShowDebugGlyphRuns(ctx, _cachedGlyphRuns);
    }
    ImGui::PopID();
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
