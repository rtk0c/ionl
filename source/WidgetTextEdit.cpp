#include "WidgetTextEdit.hpp"

#include "imgui_internal.h"

#include <algorithm>

// Development/debugging helpers
// #define IONL_SHOW_DEBUG_BOUNDING_BOXES
#define IONL_SHOW_DEBUG_INFO

using namespace std::string_view_literals;

Ionl::TextStyles Ionl::gTextStyles;

namespace {
using namespace Ionl;

struct GapBufferIterator {
    TextBuffer* obj;
    // We use signed here to avoid all the Usual Arithmetic Conversion issues, where when doing `signed + unsigned`, both operands get converted to unsigned when we expected "delta"-ing behavior
    // Note that even though `signed = signed + unsigned` does work if both operands have the same width due to wraparound arithmetic, and the fact that the rhs is immediately converted to signed
    // But expressions like `(signed + unsigned) > constant` breaks our intuition because the lhs stays unsigned before entering operator>
    int64_t idx; // Buffer index

    explicit GapBufferIterator(TextBuffer& buffer)
        : obj{ &buffer }
        , idx{ 0 } {}

    void SetBegin() {
        idx = 0;
    }

    void SetEnd() {
        idx = obj->bufferSize;
    }

    ImWchar& operator*() const {
        return obj->buffer[idx];
    }

    GapBufferIterator& operator++() {
        idx += 1;
        if (idx == obj->frontSize) {
            idx += obj->gapSize;
        }
        return *this;
    }

    GapBufferIterator operator+(int64_t advance) const {
        // Assumes adding `advance` to `ptr` does not go outside of gap buffer bounds

        int64_t gapBeginIdx = obj->frontSize;
        int64_t gapEndIdx = obj->frontSize + obj->gapSize;
        int64_t gapSize = obj->gapSize;

        GapBufferIterator res;
        res.obj = obj;
        if (idx >= gapEndIdx) {
            res.idx = idx + advance < gapEndIdx
                ? (idx + (-gapSize) + advance)
                : advance;
        } else {
            res.idx = idx + advance >= gapBeginIdx
                ? (idx + (+gapSize) + advance)
                : advance;
        }
        return res;
    }

    GapBufferIterator& operator+=(int64_t advance) {
        *this = *this + advance;
        return *this;
    }

    GapBufferIterator& operator--() {
        if (idx == obj->frontSize + obj->gapSize) {
            idx -= obj->gapSize;
        } else {
            idx -= 1;
        }
        return *this;
    }

    GapBufferIterator operator-(int64_t advance) const {
        return *this + (-advance);
    }

    GapBufferIterator& operator-=(int64_t advance) {
        return *this += (-advance);
    }

    bool HasNext() const {
        return idx != obj->bufferSize;
    }

    bool operator<(const GapBufferIterator& that) const {
        return this->idx < that.idx;
    }

    bool operator>(const GapBufferIterator& that) const {
        return this->idx > that.idx;
    }

    bool operator==(const GapBufferIterator& that) const {
        return this->idx == that.idx;
    }

private:
    GapBufferIterator()
        : obj{ nullptr }, idx{ 0 } {}
};
ImWchar* AllocateBuffer(size_t size) {
    return (ImWchar*)malloc(sizeof(ImWchar) * size);
}
void ReallocateBuffer(ImWchar*& oldBuffer, size_t newSize) {
    oldBuffer = (ImWchar*)realloc(oldBuffer, newSize);
}

void MoveGap(TextBuffer& buf, size_t newIdx) {
    size_t oldIdx = buf.frontSize;
    if (oldIdx == newIdx) return;

    // NOTE: we must use memmove() because gap size may be smaller than movement distance, in which case the src region and dst region will overlap
    auto frontEnd = buf.buffer + buf.frontSize;
    auto backBegin = buf.buffer + buf.frontSize + buf.gapSize;
    if (oldIdx < newIdx) {
        // Moving forwards

        size_t size = newIdx - oldIdx;
        memmove(frontEnd, backBegin, newIdx - oldIdx);
    } else /* oldIdx > newIdx */ {
        // Moving backwards

        size_t size = oldIdx - newIdx;
        memmove(backBegin - size, frontEnd - size, size);
    }
    buf.frontSize = newIdx;
}

void IncreaseGap(TextBuffer& buf, size_t newGapSize = 0) {
    // Some assumptions:
    // - Increasing the gap size means the user is editing this buffer, which means they'll probably edit it some more
    // - Hence, it's likely that this buffer will be reallocated multiple times in the future
    // - Hence, we round buffer size to a power of 2 to reduce malloc() overhead

    size_t frontSize = buf.frontSize;
    size_t oldBackSize = buf.bufferSize - buf.frontSize - buf.gapSize;
    size_t oldGapSize = buf.gapSize;

    size_t newBufSize = ImUpperPowerOfTwo(buf.bufferSize);
    size_t minimumBufSize = buf.bufferSize - buf.gapSize + newBufSize;
    // TODO keep a reasonable size once we get above e.g. 8KB?
    do {
        newBufSize *= 2;
    } while (newBufSize < minimumBufSize);

    ReallocateBuffer(buf.buffer, newBufSize);

    buf.bufferSize = newBufSize;
    buf.frontSize /*keep intact*/;
    buf.gapSize = newBufSize - frontSize - oldBackSize;

    memmove(
        /*New back's location*/ buf.buffer + frontSize + buf.gapSize,
        /*Old back*/ buf.buffer + frontSize + oldGapSize,
        oldBackSize);
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
int64_t CalcAdjacentWordPos(TextBuffer& buf, int64_t index, int delta) {
    GapBufferIterator it(buf);
    it += index;

    int dIdx = delta > 0 ? 1 : -1; // "delta index"
    // TODO

    return index;
}

int64_t MapLogicalIndexToBufferIndex(const TextBuffer& buf, int64_t logicalIdx) {
    if (logicalIdx < buf.frontSize) {
        return logicalIdx;
    } else {
        return logicalIdx + (int64_t)buf.gapSize;
    }
}

// If the buffer index does not point to a valid logical location (i.e. it points to somewhere in the gap), -1 is returned
int64_t MapBufferIndexToLogicalIndex(const TextBuffer& buf, int64_t bufferIdx) {
    if (bufferIdx < buf.frontSize) {
        return bufferIdx;
    } else if (/* bufferIdx >= buf.frontSize && */ bufferIdx < (buf.frontSize + buf.gapSize)) {
        return -1;
    } else {
        return bufferIdx - (int64_t)buf.gapSize;
    }
}

std::pair<int64_t, int64_t> CalcLineWrapBoundsOfIndex(const TextEdit& te, int64_t index) {
    // First element greater than `index`
    // NOTE: if `index` overlaps one of the bounds, it's always the lower bound `l`

    auto ub = std::upper_bound(te._wrapPoints.begin(), te._wrapPoints.end(), index);
    size_t l, u;
    if (ub == te._wrapPoints.begin()) {
        l = 0;
        u = *ub;
    } else if (ub == te._wrapPoints.end()) {
        l = *(ub - 1);
        u = te.buffer->GetContentSize();
    } else {
        l = *(ub - 1);
        u = *ub;
    }

    return { l, u };
}
} // namespace

Ionl::TextBuffer::TextBuffer()
    : buffer{ AllocateBuffer(256) }
    , bufferSize{ 256 }
    , frontSize{ 0 }
    , gapSize{ 256 } {}

Ionl::TextBuffer::TextBuffer(std::string_view content)
    // NOTE: these set of parameters are technically invalid, but they get immediately overridden by UpdateContent() which doesn't care
    : buffer{ nullptr }
    , bufferSize{ 0 }
    , frontSize{ 0 }
    , gapSize{ 0 } {
    UpdateContent(content);
}

std::string Ionl::TextBuffer::ExtractContent() const {
    auto frontBegin = buffer;
    auto frontEnd = buffer + frontSize;
    auto backBegin = buffer + frontSize + gapSize;
    auto backEnd = buffer + bufferSize;

    size_t utf8Count =
        ImTextCountUtf8BytesFromStr(frontBegin, frontEnd) +
        ImTextCountUtf8BytesFromStr(backBegin, backEnd);

    // Add 1 to string buffer size to account for null terminator
    // ImTextStrToUtf8() writes the \0 at the end, in addition to the provided source content
    std::string result(utf8Count, '\0');
    size_t frontUtf8Count = ImTextStrToUtf8(result.data(), result.size() + 1, frontBegin, frontEnd);
    size_t backUtf8Count = ImTextStrToUtf8(result.data() + frontUtf8Count, result.size() - frontUtf8Count + 1, backBegin, backEnd);

    return result;
}

void Ionl::TextBuffer::UpdateContent(std::string_view content) {
    auto strBegin = &*content.begin();
    auto strEnd = &*content.end();
    auto newBufferSize = ImTextCountCharsFromUtf8(strBegin, strEnd);
    if (bufferSize < newBufferSize) {
        ReallocateBuffer(buffer, newBufferSize);
    }
    bufferSize = newBufferSize;
    frontSize = bufferSize;
    gapSize = 0;
    ImTextStrFromUtf8NoNullTerminate(buffer, bufferSize, strBegin, strEnd);
}

Ionl::TextBuffer::~TextBuffer() {
    free(buffer);
}

namespace {
using namespace Ionl;

struct FaceTrait {
    size_t loc = std::numeric_limits<size_t>::max();
    ImVec2 pos;
    bool state = false;
};

struct FaceDescription {
    FaceTrait bold;
    FaceTrait italic;
    FaceTrait underline;
    FaceTrait strikethrough;
    FaceTrait monospace;
};

void LocateFace(const FaceDescription& desc, ImFont*& outFont, ImU32& outColor) {
    if (desc.monospace.state) {
        if (desc.bold.state && desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceBoldItalic];
            outColor = gTextStyles.faceColors[MF_MonospaceBoldItalic];
        } else if (desc.bold.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceBold];
            outColor = gTextStyles.faceColors[MF_MonospaceBold];
        } else if (desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceItalic];
            outColor = gTextStyles.faceColors[MF_MonospaceItalic];
        } else {
            outFont = gTextStyles.faceFonts[MF_Monospace];
            outColor = gTextStyles.faceColors[MF_Monospace];
        }
    } else {
        if (desc.bold.state && desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalBoldItalic];
            outColor = gTextStyles.faceColors[MF_ProportionalBoldItalic];
        } else if (desc.bold.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalBold];
            outColor = gTextStyles.faceColors[MF_ProportionalBold];
        } else if (desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalItalic];
            outColor = gTextStyles.faceColors[MF_ProportionalItalic];
        } else {
            outFont = gTextStyles.faceFonts[MF_Proportional];
            outColor = gTextStyles.faceColors[MF_Proportional];
        }
    }
};
} // namespace

void Ionl::TextEdit::Show() {
    auto& g = *ImGui::GetCurrentContext();
    auto& io = ImGui::GetIO();
    auto window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    auto contentRegionAvail = ImGui::GetContentRegionAvail();
    auto drawList = window->DrawList;

    bool escaping = false;
    FaceDescription faceDesc;
    ImFont* faceFont;
    ImU32 faceColor;
    LocateFace(faceDesc, faceFont, faceColor);

    ImVec2 textPos = window->DC.CursorPos;
    float textStartX = textPos.x;
    float totalHeight = 0.0f;

    // TODO use the corret font for each character
    auto wordWrapFont = gTextStyles.faceFonts[MF_Proportional];
    GapBufferIterator it(*buffer);
    GapBufferIterator end(*buffer);
    end.SetEnd();

    GapBufferIterator wrapPoint = ImCalcWordWrapPosition(wordWrapFont, 1.0f, it, end, contentRegionAvail.x);
    auto CursorPosWrapLine = [&]() {
        float dy = faceFont->FontSize + linePadding;
        textPos.x = textStartX;
        textPos.y += dy;
        totalHeight += dy;
    };

    auto bufferIndexOfCursor = MapLogicalIndexToBufferIndex(*buffer, _cursorIdx);

    // NOTE: pattern must be ASCII
    auto ChMatches = [&](std::string_view patternStr) {
        auto pattern = patternStr.data();
        auto haystack = it;
        while (true) {
            // Matched the whole pattern
            if (*pattern == '\0') return true;
            // Reached end of input before matching the whole pattern
            if (!haystack.HasNext()) return false;

            if (*pattern != *haystack) {
                return false;
            }
            ++pattern;
            ++haystack;
        }
    };
    auto DrawGlyph = [&](ImWchar c) {
        auto glyph = faceFont->FindGlyph(c);
        if (!glyph) return false;

        if (glyph->Visible) {
            ImVec2 pos0(textPos.x + glyph->X0, textPos.y + glyph->Y0);
            ImVec2 pos1(textPos.x + glyph->X1, textPos.y + glyph->Y1);

            ImVec2 uv0(glyph->U0, glyph->V0);
            ImVec2 uv1(glyph->U1, glyph->V1);

            ImU32 glyphColor = glyph->Colored ? (faceColor | ~IM_COL32_A_MASK) : faceColor;
            drawList->PrimRectUV(pos0, pos1, uv0, uv1, glyphColor);
#ifdef IONL_SHOW_DEBUG_BOUNDING_BOXES
            ImGui::GetForegroundDrawList()->AddRect(pos0, pos1, IM_COL32(0, 255, 255, 255));
#endif

            textPos.x += glyph->AdvanceX;
            return true;
        }

        textPos.x += glyph->AdvanceX;
        return false;
    };

    // Clear but don't free memory
    _wrapPoints.resize(0);

    // TODO move markdown parser loop out
    // TODO use gap buffer streaming, to reduce branching inside GapBufferIterator
    // TODO define line as "characters, followed by \n" so that we can have a valid index for the cursor at the end of buffer
    while (it.HasNext()) {
        bool isHardWrap = *it == '\n';
        bool isSoftWrap = it > wrapPoint;
        if (isHardWrap || isSoftWrap) {
            if (isSoftWrap) {
                // TODO fix position calculation: this currently assumes all glyphs are of the regular proportional variant, so for e.g. all bold text, or all code text it's broken
                wrapPoint = ImCalcWordWrapPosition(wordWrapFont, 1.0f, it, end, contentRegionAvail.x);
                _wrapPoints.push_back(MapBufferIndexToLogicalIndex(*buffer, it.idx));
            }
            if (isHardWrap) {
                if (bufferIndexOfCursor == it.idx) {
                    _cursorAssociatedFont = faceFont;
                    _cursorVisualOffset = textPos - window->DC.CursorPos;
                }

                // Skip \n
                ++it;
            }

            // Position at current end of line
            auto oldTextPos = textPos;
            // NOTE: this updates `textPos`
            CursorPosWrapLine();

            // Draw the text decoration to current pos (end of line), and then "transplant" them to the next line
            if (faceDesc.underline.state) {
                float yOffset = faceFont->FontSize;
                drawList->AddLine(
                    ImVec2(faceDesc.underline.pos.x, faceDesc.underline.pos.y + yOffset),
                    ImVec2(oldTextPos.x, oldTextPos.y + yOffset),
                    faceColor);

                faceDesc.underline.pos = textPos;
            }
            if (faceDesc.strikethrough.state) {
                float yOffset = faceFont->FontSize / 2;
                drawList->AddLine(
                    ImVec2(faceDesc.strikethrough.pos.x, faceDesc.strikethrough.pos.y + yOffset),
                    ImVec2(oldTextPos.x, oldTextPos.y + yOffset),
                    faceColor);

                faceDesc.strikethrough.pos = textPos;
            }

            if (isHardWrap) {
                continue;
            }
        }

        auto oldIt = it;

        // Heading parsing
        if (*it == '#') {
            int headingLevel = 0;
            while (*it == '#') {
                ++it;
                headingLevel += 1;
            }
            headingLevel = ImMin<int>(headingLevel, MF_META_HeadingMax);

            // Do the all the text rendering here (heading style overrides everything else)
            faceFont = gTextStyles.faceFonts[MF_Heading1 + headingLevel];
            faceColor = gTextStyles.faceColors[MF_Heading1 + headingLevel];

            // TODO is it a better idea to create title faces, and then let the main loop handle it?
            // that way we could reduce quite a few lines of duplicated logic, especially for handling line breaks
            // This also allows it to handle things like inline code inside a title

            it = oldIt;
            size_t charCount = 0;
            while (*it != '\n' && it.HasNext()) {
                charCount += 1;
                ++it;
            }
            it = oldIt;

            // Recalculate wrap position, because in the general case we pretend all text is the regular proportional face
            // TODO get rid of this once the general case can handle fonts properly
            wrapPoint = ImCalcWordWrapPosition(faceFont, 1.0f, it, end, contentRegionAvail.x);

            drawList->PrimReserve(charCount * 6, charCount * 4);
            size_t charsDrawn = 0;
            while (*it != '\n' && it.HasNext()) {
                if (it > wrapPoint) {
                    // TODO record wrap point
                    CursorPosWrapLine();
                    wrapPoint = ImCalcWordWrapPosition(faceFont, 1.0f, it, end, contentRegionAvail.x);
                }

                if (it.idx == bufferIndexOfCursor) {
                    _cursorAssociatedFont = faceFont;
                    _cursorVisualOffset = textPos - window->DC.CursorPos;
                }

                if (DrawGlyph(*it)) {
                    charsDrawn += 1;
                }
                ++it;
            }
            drawList->PrimUnreserve((charCount - charsDrawn) * 6, (charCount - charsDrawn) * 4);

            // Handle the \n character
            CursorPosWrapLine();
            ++it;

            // Recalculate wrapPoint using normal face
            // TODO get rid of this once the general case can handle fonts properly
            wrapPoint = ImCalcWordWrapPosition(wordWrapFont, 1.0f, it, end, contentRegionAvail.x);
            // Restore to regular face
            faceFont = gTextStyles.faceFonts[MF_Proportional];
            faceColor = gTextStyles.faceColors[MF_Proportional];

            continue;
        }

        // Code block parsing
        if (ChMatches("```")) {
            // TODO
            continue;
        }

        bool delayedUpdateFormat = false;

        // Used for drawing underline and strikethrough
        ImVec2 delayedLineStartPos;
        ImU32 delayedLineColor;
        float delayedLineYOffset;

        size_t segmentSize = 0;
        // Returns true if a formatting operator is consumed (may not be effective, i.e. escaped)
        // Returns false if nothing is consumed
        // NOTE: pattern must be ASCII
        auto ChConsume = [&](std::string_view pattern, FaceTrait& props) {
            if (!ChMatches(pattern)) return false;

            // Treat as normal text
            if (escaping) {
                escaping = false;

                it += pattern.size();
                return false;
            }

            if (props.state) {
                // Closing specifier
                props.state = false;
                props.loc = std::numeric_limits<size_t>::max();
                props.pos = ImVec2();
                delayedUpdateFormat = true;
            } else {
                // Opening specifier
                props.state = true;
                props.loc = it.idx;
                props.pos = textPos;
                LocateFace(faceDesc, faceFont, faceColor);
            }

            it += pattern.size();
            segmentSize = pattern.size();
            return true;
        };

        do {
            // Inline code
            // TODO implement consecutive code collapsing
            if (ChConsume("`"sv, faceDesc.monospace)) break;
            if (!faceDesc.monospace.state) {
                // Bold
                if (ChConsume("**"sv, faceDesc.bold)) break;

                // Underline
                if (auto pos = faceDesc.underline.pos;
                    ChConsume("__"sv, faceDesc.underline))
                {
                    // Closing specifier
                    if (!faceDesc.underline.state) {
                        delayedLineStartPos = pos;
                        delayedLineColor = faceColor;
                        delayedLineYOffset = faceFont->FontSize;
                    }
                    break;
                }

                // Strikethrough
                if (auto pos = faceDesc.strikethrough.pos;
                    ChConsume("~~"sv, faceDesc.strikethrough))
                {
                    // Closing specifier
                    if (!faceDesc.strikethrough.state) {
                        delayedLineStartPos = pos;
                        delayedLineColor = faceColor;
                        delayedLineYOffset = faceFont->FontSize / 2;
                    }
                    break;
                }

                // Italic
                if (ChConsume("*"sv, faceDesc.italic) ||
                    ChConsume("_"sv, faceDesc.italic))
                {
                    break;
                }
            }

            // Set escaping state for the next character
            // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
            // If this is anything else, this condition will evaluate to false
            escaping = *it == '\\' && !escaping;
            segmentSize = 1;
            ++it;
        } while (false);

        // We can't do this per-line or per-buffer, because other AddLine() AddXxx() calls happen within this loop too, which will mess up the assumptions
        // TODO this is a quite dumb overhead, optimize
        drawList->PrimReserve(segmentSize * 6, segmentSize * 4);
        // Draw the current segment of text, [oldIt, cursor)
        size_t charsDrawn = 0;
        for (auto ch = oldIt; ch != it; ++ch) {
            // Handle cursor positioning
            // NOTE: we must do this before calling DrawGlyph(), because it moves textPos to the next glyph
            // NOTE: this relies on \n being processed by the loop to place hard wrapped line ends correctly
            // TODO this places definitive cursor on the last char of the previous line, not good; we need to handle the "one after line end" case
            // TODO handle hard line breaks
            // TODO if we use cursorAffinity on the char before the line wrap, it will give much nicer logic in this place
            if ((!_cursorAffinity && bufferIndexOfCursor == ch.idx) ||
                (_cursorAffinity && bufferIndexOfCursor == ch.idx + 1))
            {
                _cursorAssociatedFont = faceFont;
                _cursorVisualOffset = textPos - window->DC.CursorPos;
                if (_cursorAffinity) {
                    auto glyph = faceFont->FindGlyph(*ch);
                    _cursorVisualOffset.x += glyph ? glyph->AdvanceX : 0;
                }
            }

            if (DrawGlyph(*ch)) {
                charsDrawn += 1;
            }
        }
        drawList->PrimUnreserve((segmentSize - charsDrawn) * 6, (segmentSize - charsDrawn) * 4);

        if (delayedUpdateFormat) {
            LocateFace(faceDesc, faceFont, faceColor);
        }
        if (delayedLineStartPos.x != 0.0f) {
            drawList->AddLine(
                ImVec2(delayedLineStartPos.x, delayedLineStartPos.y + delayedLineYOffset),
                ImVec2(textPos.x, textPos.y + delayedLineYOffset),
                delayedLineColor);
        }
    }

    // For the last line (which doesn't end in a \n)
    CursorPosWrapLine();

    ImVec2 widgetSize(contentRegionAvail.x, totalHeight);
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + widgetSize };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return;
    }

    // FIXME: or ImGui::IsItemHovered()?
    bool hovered = ImGui::ItemHoverable(bb, id);
    bool userClicked = hovered && io.MouseClicked[ImGuiMouseButton_Left];

    auto activeId = ImGui::GetActiveID();

    if (activeId != id && userClicked) {
        activeId = id;

        // Adapted from imgui_widget.cpp Imgui::InputTextEx()

        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);

        // Declare our inputs
        // TODO do we need to declare other keys like Backspace? ImGui::InputTextEx() uses them but doesn't declare
        ImGui::SetActiveIdUsingKey(ImGuiKey_LeftArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_RightArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_UpArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_DownArrow);
        ImGui::SetActiveIdUsingKey(ImGuiKey_Escape);
        ImGui::SetActiveIdUsingKey(ImGuiKey_NavGamepadCancel);
        ImGui::SetActiveIdUsingKey(ImGuiKey_Home);
        ImGui::SetActiveIdUsingKey(ImGuiKey_End);
    }

    int64_t bufContentSize = buffer->GetContentSize();

    // Process keyboard inputs
    if (activeId == id && !g.ActiveIdIsJustActivated) {
        bool isOSX = io.ConfigMacOSXBehaviors;
        bool isMovingWord = isOSX ? io.KeyAlt : io.KeyCtrl;
        bool isShortcutKey = isOSX ? (io.KeyMods == ImGuiMod_Super) : (io.KeyMods == ImGuiMod_Ctrl);

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (_cursorIsAtWrapPoint && !_cursorAffinity) {
                _cursorAffinity = true;
            } else {
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(*buffer, _cursorIdx, -1) : -1;
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
                _cursorIdx += isMovingWord ? CalcAdjacentWordPos(*buffer, _cursorIdx, +1) : +1;
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
                auto [l, u] = CalcLineWrapBoundsOfIndex(*this, _cursorIdx);

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
                auto [l, u] = CalcLineWrapBoundsOfIndex(*this, _cursorIdx);

                _cursorIdx = u;
                if (!io.KeyShift) _anchorIdx = _cursorIdx;

                _cursorIsAtWrapPoint = true;
                bool lineIsHardWrapped = u == bufContentSize || (*buffer)[u] == '\n';
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
        }
    }

    // Draw cursor and selection
    // TODO move drawing cursor blinking outside the ImGui loop
    if (activeId == id) {
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
    if (activeId == id && io.MouseClicked[ImGuiMouseButton_Left] && !hovered) {
        ImGui::ClearActiveID();
    }

#ifdef IONL_SHOW_DEBUG_BOUNDING_BOXES
    auto dl = ImGui::GetForegroundDrawList();
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
    dl->AddRect(bb.Min, bb.Min + contentRegionAvail, IM_COL32(255, 0, 255, 255));
#endif

#ifdef IONL_SHOW_DEBUG_INFO
    if (ImGui::CollapsingHeader("TextEdit debug")) {
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
#endif
}

bool TextEdit::HasSelection() const {
    return _cursorIdx != _anchorIdx;
}

int64_t TextEdit::GetSelectionBegin() const {
    return ImMin(_cursorIdx, _anchorIdx);
}

int64_t TextEdit::GetSelectionEnd() const {
    return ImMax(_cursorIdx, _anchorIdx);
}

void TextEdit::SetSelection(int64_t begin, int64_t end, bool cursorAtBegin) {
    if (cursorAtBegin) {
        _cursorIdx = begin;
        _anchorIdx = end;
    } else {
        _cursorIdx = end;
        _anchorIdx = begin;
    }
}

void TextEdit::SetCursor(int64_t cursor) {
    _cursorIdx = cursor;
    _anchorIdx = cursor;
}
