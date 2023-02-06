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
    ImGui::Text("Showing %zu GlyphRuns's:", glyphRuns.size());
    for (size_t i = 0; i < glyphRuns.size(); ++i) {
        ImGui::Text("[%zu] ", i);
        ImGui::SameLine();
        ShowDebugGlyphRun(ctx, glyphRuns[i]);
    }
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
        while (true) {
            const ImWchar* remaining;
            // `wrap_width` is for automatically laying the text in multiple lines (and return the size of all lines).
            // We want to perform line wrapping ourselves, so we use `max_width` to instruct ImGui to stop after reaching the line width.
            auto runDim = face.font->CalcTextSize(face.font->FontSize, in.viewportWidth, 0.0f, beg, end, &remaining);

            GlyphRun glyphRun;
            glyphRun.tr = textRun;
            glyphRun.tr.begin = std::distance(in.src->PtrBegin(), beg);
            glyphRun.tr.end = std::distance(in.src->PtrBegin(), remaining);
            glyphRun.pos = currPos;
            glyphRun.horizontalAdvance = runDim.x;
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

void TryRefreshTextEditCachedData(TextEdit& te, float viewportWidth) {
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
int64_t CalcAdjacentWordPos(GapBuffer& buf, int64_t index, int delta) {
    GapBufferIterator it(buf);
    it += index;

    int dIdx = delta > 0 ? 1 : -1; // "delta index"
    // TODO

    return index;
}

std::pair<int64_t, int64_t> FindLineWrapBoundsForIndex(const TextEdit& te, int64_t index) {
    // First element greater than `index`
    // NOTE: if `index` overlaps one of the bounds, it's always the lower bound `l`

    auto ub = std::upper_bound(te._wrapPoints.begin(), te._wrapPoints.end(), index);
    size_t l, u;
    if (ub == te._wrapPoints.begin()) {
        l = 0;
        u = *ub;
    } else if (ub == te._wrapPoints.end()) {
        l = *(ub - 1);
        u = te._tb->gapBuffer.GetContentSize();
    } else {
        l = *(ub - 1);
        u = *ub;
    }

    return { l, u };
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
    TryRefreshTextEditCachedData(*this, contentRegionAvail.x);

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

    // TODO update _cursorAssociatedFont and _cursorVisualOffset
    // Process keyboard inputs
    if (activeId == _id && !g.ActiveIdIsJustActivated) {
        bool isOSX = io.ConfigMacOSXBehaviors;
        bool isMovingWord = isOSX ? io.KeyAlt : io.KeyCtrl;
        bool isShortcutKey = isOSX ? (io.KeyMods == ImGuiMod_Super) : (io.KeyMods == ImGuiMod_Ctrl);

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (_cursorIsAtWrapPoint && !_cursorAffinity) {
                _cursorAffinity = true;
            } else {
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(_tb->gapBuffer, _cursorIdx, -1) : -1;
                _cursorIdx = ImClamp<int64_t>(_cursorIdx, 0, bufContentSize);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                _cursorIsAtWrapPoint = std::binary_search(_wrapPoints.begin(), _wrapPoints.end(), _cursorIdx);
                // NOTE: when we move left from a soft-wrapped point, this is necessary to get out of the affinitive state
                _cursorAffinity = false;
            }

            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            if (_cursorIsAtWrapPoint && _cursorAffinity) {
                _cursorAffinity = false;
            } else {
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(_tb->gapBuffer, _cursorIdx, +1) : +1;
                _cursorIdx = ImClamp<int64_t>(_cursorIdx, 0, bufContentSize);
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                _cursorIsAtWrapPoint = std::binary_search(_wrapPoints.begin(), _wrapPoints.end(), _cursorIdx);
                if (_cursorIsAtWrapPoint) {
                    _cursorAffinity = true;
                }
            }
            _cursorAnimTimer = 0.0f;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            if (isMovingWord) {
                _cursorIdx = 0;
                _anchorIdx = 0;
                _cursorAffinity = false;
                _cursorIsAtWrapPoint = false;
            } else {
                auto [l, u] = FindLineWrapBoundsForIndex(*this, _cursorIdx);

                _cursorIdx = l;
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                _cursorIsAtWrapPoint = false;
                _cursorAffinity = false;
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            if (isMovingWord) {
                _cursorIdx = bufContentSize;
                _anchorIdx = bufContentSize;
                _cursorAffinity = false;
                _cursorIsAtWrapPoint = true;
            } else {
                auto [l, u] = FindLineWrapBoundsForIndex(*this, _cursorIdx);

                _cursorIdx = u;
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                _cursorIsAtWrapPoint = true;
                bool lineIsHardWrapped = u == bufContentSize || _tb->gapBuffer.buffer[u] == '\n';
                _cursorAffinity = !lineIsHardWrapped;
            }
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
            cursorPos.y + _cursorAssociatedFont->FontSize - 0.5f,
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
        ImGui::Text("_cursorIsAtWrapPoint = %s", _cursorIsAtWrapPoint ? "true" : "false");
        ImGui::Text("_cursorAffinity = %s", _cursorAffinity ? "true" : "false");
        ImGui::Text("Wrap points: ");
        ImGui::SameLine();
        char wrapPointText[65536];
        char* wrapPointTextWt = wrapPointText;
        char* wrapPointTextEnd = std::end(wrapPointText);
        for (int i = 0; i < _wrapPoints.size(); ++i) {
            int wp = _wrapPoints[i];
            int bufSize = wrapPointTextEnd - wrapPointTextWt;
            int res = snprintf(wrapPointTextWt, bufSize, i != _wrapPoints.size() - 1 ? "%d, " : "%d", wp);

            if (res < 0 || res >= bufSize) {
                // NOTE: snprintf() always null terminates buffer even if there is not enough space
                break;
            }

            // NOTE: snprintf() return value don't count the null termiantor
            wrapPointTextWt += res;
        }
        *wrapPointTextWt = '\0';
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(wrapPointText);
        ImGui::PopTextWrapPos();
    }
    if (ImGui::CollapsingHeader("TextEdit._tb->[TextRun]")) {
        ShowDebugTextRuns(_tb->gapBuffer.buffer, _tb->textRuns);
    }
    if (ImGui::CollapsingHeader("TextEdit.[GlyphRun]")) {
        DebugShowContext ctx{
            .bb = bb,
            .sourceBeg = _tb->gapBuffer.PtrBegin(),
            .sourceEnd = _tb->gapBuffer.PtrEnd(),
        };
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
}

void Ionl::TextEdit::SetCursor(int64_t cursor) {
    _cursorIdx = cursor;
    _anchorIdx = cursor;
}
