#include "WidgetTextEdit.hpp"

#include "Macros.hpp"
#include "Utils.hpp"

#include "imgui_internal.h"

using namespace std::string_view_literals;

Ionl::TextStyles Ionl::gTextStyles;

namespace {
using namespace Ionl;

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

struct GapBufferSentinel {
};

struct GapBufferIterator {
    TextBuffer* obj;
    ImWchar* ptr;

    GapBufferIterator(TextBuffer& buffer)
        : obj{ &buffer }
        , ptr{ buffer.buffer } {}

    ImWchar& operator*() {
        return *ptr;
    }

    GapBufferIterator& operator++() {
        ptr += 1;
        if (ptr == obj->buffer + obj->frontSize) {
            ptr += obj->gapSize;
        }
        return *this;
    }

    GapBufferIterator& operator+=(int n) {
        ImWchar* backBegin = obj->buffer + obj->frontSize;
        ptrdiff_t dist = backBegin - ptr;
        if (dist <= n) {
            ptr = backBegin + (n - dist);
        } else {
            ptr += n;
        }
        return *this;
    }

    GapBufferIterator& operator--() {
        if (ptr == obj->buffer + obj->frontSize + obj->gapSize) {
            ptr -= obj->gapSize;
        } else {
            ptr -= 1;
        }
        return *this;
    }

    // TODO operator-=

    bool HasNext() const {
        return ptr != obj->buffer + obj->bufferSize;
    }

    bool operator==(const GapBufferIterator& that) const {
        return this->ptr == that.ptr;
    }

    bool operator==(const GapBufferSentinel&) const {
        return !HasNext();
    }
};
} // namespace

Ionl::TextBuffer::TextBuffer()
    : buffer{ AllocateBuffer(256) }
    , bufferSize{ 256 }
    , frontSize{ 0 }
    , gapSize{ 256 } {}

Ionl::TextBuffer::TextBuffer(std::string_view content) {
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
    bufferSize = ImTextCountCharsFromUtf8(strBegin, strEnd);
    buffer = AllocateBuffer(bufferSize);
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
    const ImWchar* loc = nullptr;
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
    auto window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    ImDrawList* drawList = window->DrawList;

    bool escaping = false;
    FaceDescription faceDesc;
    ImFont* faceFont;
    ImU32 faceColor;
    LocateFace(faceDesc, faceFont, faceColor);

    ImVec2 textPos = window->DC.CursorPos;
    float totalHeight = 0.0f;

    GapBufferIterator cursor(*buffer);

    // NOTE: pattern must be ASCII
    auto chMatches = [&](std::string_view patternStr) {
        auto pattern = patternStr.data();
        auto haystack = cursor;
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
    auto drawGlyph = [&](ImWchar c) {
        auto glyph = faceFont->FindGlyph(c);
        if (!glyph) return false;

        if (glyph->Visible) {
            ImVec2 pos0(textPos.x + glyph->X0, textPos.y + glyph->Y0);
            ImVec2 pos1(textPos.x + glyph->X1, textPos.y + glyph->Y1);

            ImVec2 uv0(glyph->U0, glyph->V0);
            ImVec2 uv1(glyph->U1, glyph->V1);

            ImU32 glyphColor = glyph->Colored ? (faceColor | ~IM_COL32_A_MASK) : faceColor;
            drawList->PrimRectUV(pos0, pos1, uv0, uv1, glyphColor);
#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
            ImGui::GetForegroundDrawList()->AddRect(pos0, pos1, IM_COL32(0, 255, 255, 255));
#endif

            textPos.x += glyph->AdvanceX;
            return true;
        }

        textPos.x += glyph->AdvanceX;
        return false;
    };

    while (cursor.HasNext()) {
        if (*cursor == '\n') {
            ++cursor;
            float dy = faceFont->FontSize + linePadding;
            textPos.x = window->DC.CursorPos.x;
            textPos.y += dy;
            totalHeight += dy;
            continue;
        }

        auto oldCursor = cursor;

        // Heading parsing
        if (*cursor == '#') {
            int headingLevel = 0;
            while (*cursor == '#') {
                ++cursor;
                headingLevel += 1;
            }
            headingLevel = ImMin<int>(headingLevel, MF_META_HeadingMax);

            // Do the all of the text rendering here (heading style overrides everything else)
            faceFont = gTextStyles.faceFonts[MF_Heading1 + headingLevel];
            faceColor = gTextStyles.faceColors[MF_Heading1 + headingLevel];

            cursor = oldCursor;
            size_t charCount = 0;
            while (*cursor != '\n' && cursor.HasNext()) {
                charCount += 1;
                ++cursor;
            }
            cursor = oldCursor;
            drawList->PrimReserve(charCount * 6, charCount * 4);
            size_t charsDrawn = 0;
            while (*cursor != '\n' && cursor.HasNext()) {
                if (drawGlyph(*cursor)) {
                    charsDrawn += 1;
                }
                ++cursor;
            }
            drawList->PrimUnreserve((charCount - charsDrawn) * 6, (charCount - charsDrawn) * 4);

            float dy = faceFont->FontSize + linePadding;
            textPos.x = window->DC.CursorPos.x;
            textPos.y += dy;
            totalHeight += dy;

            // Skip the \n character
            ++cursor;

            // Restore to regular face
            faceFont = gTextStyles.faceFonts[MF_Proportional];
            faceColor = gTextStyles.faceColors[MF_Proportional];

            continue;
        }

        // Code block parsing
        if (chMatches("```")) {
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
        auto chConsume = [&](std::string_view pattern, FaceTrait& props) {
            if (!chMatches(pattern)) return false;

            // Treat as normal text
            if (escaping) {
                escaping = false;

                cursor += pattern.size();
                return false;
            }

            if (props.state) {
                // Closing specifier
                props.state = false;
                props.loc = nullptr;
                props.pos = ImVec2();
                delayedUpdateFormat = true;
            } else {
                // Opening specifier
                props.state = true;
                props.loc = cursor.ptr;
                props.pos = textPos;
                LocateFace(faceDesc, faceFont, faceColor);
            }

            cursor += pattern.size();
            segmentSize = pattern.size();
            return true;
        };

        do {
            // Inline code
            // TOOD implement consecutive code collapsing
            if (chConsume("`"sv, faceDesc.monospace)) break;
            if (!faceDesc.monospace.state) {
                // Bold
                if (chConsume("**"sv, faceDesc.bold)) break;

                // Underline
                if (auto pos = faceDesc.underline.pos;
                    chConsume("__"sv, faceDesc.underline))
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
                    chConsume("~~"sv, faceDesc.strikethrough))
                {
                    // Closing specifier
                    if (!faceDesc.strikethrough.state) {
                        delayedLineStartPos = pos;
                        delayedLineColor = faceColor;
                        delayedLineYOffset = faceFont->FontSize / 2;
                    }
                    break;
                }

                // Iatlic
                if (chConsume("*"sv, faceDesc.italic) ||
                    chConsume("_"sv, faceDesc.italic))
                {
                    break;
                }
            }

            // Set escaping state for the next character
            // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
            // If this is anything else, this condition will evaluate to false
            escaping = *cursor == '\\' && !escaping;
            segmentSize = 1;
            ++cursor;
        } while (false);

        // We can't do this per-line or per-buffer, because other AddLine() AddXxx() calls happen within this loop too, which will mess up the assumptions
        // TODO this is a quite dumb overhead, optimize
        drawList->PrimReserve(segmentSize * 6, segmentSize * 4);
        // Draw the current segment of text, [oldCursor, cursor)
        size_t charsDrawn = 0;
        for (auto ch = oldCursor; ch != cursor; ++ch) {
            if (drawGlyph(*ch)) {
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
    float dy = faceFont->FontSize + linePadding;
    textPos.x = window->DC.CursorPos.x;
    textPos.y += dy;
    totalHeight += dy;

    ImVec2 widgetSize(ImGui::GetContentRegionAvail().x, totalHeight);
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + widgetSize };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return;
    }

#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
    ImGui::GetForegroundDrawList()->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif
}
