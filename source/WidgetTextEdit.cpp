#include "WidgetTextEdit.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cassert>
#include <span>
#include <utility>

// Development/debugging helpers
// #define IONL_SHOW_DEBUG_BOUNDING_BOXES
// #define IONL_SHOW_DEBUG_INFO

using namespace std::literals;

Ionl::TextStyles Ionl::gTextStyles;

namespace {
using namespace Ionl;

int64_t MapLogicalIndexToBufferIndex(const GapBuffer& buffer, int64_t logicalIdx) {
    if (logicalIdx < buffer.frontSize) {
        return logicalIdx;
    } else {
        return logicalIdx + buffer.gapSize;
    }
}

// If the buffer index does not point to a valid logical location (i.e. it points to somewhere in the gap), -1 is returned
int64_t MapBufferIndexToLogicalIndex(const GapBuffer& buffer, int64_t bufferIdx) {
    if (bufferIdx < buffer.frontSize) {
        return bufferIdx;
    } else if (/* bufferIdx >= buffer.frontSize && */ bufferIdx < (buffer.frontSize + buffer.gapSize)) {
        return -1;
    } else {
        return bufferIdx - buffer.gapSize;
    }
}

int64_t AdjustBufferIndex(const GapBuffer& buffer, int64_t /*buffer index*/ idx, int64_t delta) {
    int64_t gapBeginIdx = buffer.frontSize;
    int64_t gapEndIdx = buffer.frontSize + buffer.gapSize;
    int64_t gapSize = buffer.gapSize;

    if (idx >= gapEndIdx) {
        return idx + delta < gapEndIdx
            ? idx + (-gapSize) + delta
            : idx + delta;
    } else {
        return idx + delta >= gapBeginIdx
            ? idx + (+gapSize) + delta
            : idx + delta;
    }
}

struct GapBufferIterator {
    GapBuffer* obj;
    // We use signed here to avoid all the Usual Arithmetic Conversion issues, where when doing `signed + unsigned`, both operands get converted to unsigned when we expected "delta"-ing behavior
    // Note that even though `signed = signed + unsigned` does work if both operands have the same width due to wraparound arithmetic, and the fact that the rhs is immediately converted to signed
    // But expressions like `(signed + unsigned) > constant` breaks our intuition because the lhs stays unsigned before entering operator>
    int64_t idx; // Buffer index

    explicit GapBufferIterator(GapBuffer& buffer)
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
        res.idx = AdjustBufferIndex(*obj, idx, advance);
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
    oldBuffer = (ImWchar*)realloc(oldBuffer, sizeof(ImWchar) * newSize);
}

void MoveGap(GapBuffer& buf, size_t newIdx) {
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

void IncreaseGap(GapBuffer& buf, size_t newGapSize = 0) {
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

struct TextRun {
    int64_t begin;
    int64_t end;
    TextStyle style;
};

struct ParseInput {
    // [Required] Source buffer to parse markdown from.
    const GapBuffer* tb;
};

struct ParseOutput {
    std::vector<TextRun> textRuns;
};

ParseOutput ParseMarkdownBuffer(const ParseInput& in) {
    ParseOutput out;

    // TODO this honestly is a lot of edge case handling, maybe we really should just use irccloud-format-helper-2's code
    // TODO handle cases like ***bold and italic***, the current greedy matching method parses it as **/*text**/* which breaks the control seq pairing logic
    //      note this is also broken in irccloud-format-helper, so that won't help

    // TODO break parsing state on \n
    // TODO handle headings
    // TODO handle code blocks

    using FormatFlagPtr = bool TextStyle::*;
    struct ControlSequence {
        int64_t begin = 0;
        int64_t end = 0;
        std::string_view pattern = {};
        FormatFlagPtr patternFlag = nullptr;

        bool HasClosingControlSeq() const { return end != 0; }
    };
    std::vector<ControlSequence> seenControlSeqs;
    bool isEscaping = false;

    // The characters inside the parser's processing area (for size N, basically 1 current char + N-1 lookbehind chars)
    constexpr size_t kVisionSize = 3;
    ImWchar visionBuffer[kVisionSize] = {};

    auto visionBufferMatches = [&](std::string_view pattern) {
        auto needle = pattern.begin();
        auto haystack = std::begin(visionBuffer);
        while (true) {
            // Matched the whole pattern
            if (needle == pattern.end()) return true;
            // Reached end of input before matching the whole pattern
            if (haystack == std::end(visionBuffer)) return false;

            if (*needle != *haystack) {
                return false;
            }
            ++needle;
            ++haystack;
        }
    };

    // Insert a single TextRun into `out.textRuns`
    auto outputTextRun = [&](TextRun run) {
        auto gapBegin = in.tb->frontSize;
        auto gapEnd = in.tb->frontSize + in.tb->gapSize;
        if (run.begin < gapBegin && run.end > gapEnd) {
            // TextRun spans over the gap, we need to split it
            TextRun& frontRun = run;
            TextRun backRun = run;

            /* frontRun.begin; */ // Remain unchanged
            frontRun.end = gapBegin;
            backRun.begin = gapEnd;
            /* backRun.end; */ // Remain unchanged

            out.textRuns.push_back(std::move(frontRun));
            out.textRuns.push_back(std::move(backRun));
        } else {
            out.textRuns.push_back(std::move(run));
        }
    };

    // Insert all TextRun's represented inside `seenControlSeqs` into `out.textRuns`
    auto outputAllMatchedTextRuns = [&]() {
        // We assume that all elements appear with monotonically increasing `ControlSequence::begin`,
        // which should be maintained by the scanning logic below

        // TODO this could probably be much more optimized, not allocating memory or sharing a preallocated chunk

        struct ToggleOp {
            FormatFlagPtr flag;
            int64_t textLocation; // Logical index into text buffer; this is used just as a sorting number
        };
        std::vector<ToggleOp> ops;

        for (const auto& cs : seenControlSeqs) {
            if (cs.HasClosingControlSeq()) {
                ops.push_back({ cs.patternFlag, cs.begin });
                ops.push_back({ cs.patternFlag, cs.end });
            }
        }

        std::sort(ops.begin(), ops.end(), [](const ToggleOp& a, const ToggleOp& b) { return a.textLocation < b.textLocation; });

        TextRun tr{};
        // Continue from the previous text run (covering unformatted text between this and last "stack" of formatted text), if there is one
        if (!out.textRuns.empty()) {
            tr.begin = out.textRuns.back().end;
        }
        for (const auto& op : ops) {
            /* tr.begin; */ // Set by previous iteration or starting value
            tr.end = op.textLocation;

            outputTextRun(tr);

            bool& flag = tr.style.*(op.flag);
            flag = !flag;

            if (tr.begin == tr.end) {
                // This op overlaps with the previous op, we only need to set the formatting flag without outputting a TextRun
                continue;
            }

            tr.begin = tr.end;
        }
    };

    // The parser operates with two indices: the "head" index points to the current character being parsed, and the "reader" index ponts to the character coming into the lookahead buffer.
    // The buffer streaming mechanism operates on "reader", advancing to the next segment when the end of the current one is reached.
    // "head" is not stored, but calculated on the fly when it's needed.
    // When the parser initiates, "reader" is advanced (thus filling the lookahead buffer) until "head" reaches 0 (points to the first valid character), or until "reader" reaches end of the input buffer.

    std::pair<int64_t, int64_t> sourceSegments[] = {
        { in.tb->GetFrontBegin(), in.tb->GetFrontSize() },
        { in.tb->GetBackBegin(), in.tb->GetBackSize() },
        // The dummy segment at the very end for "head" to advance until the very end of source buffer
        { in.tb->GetBackEnd(), kVisionSize - 1 },
    };
    int skipCount = 0;
    for (auto it = std::begin(sourceSegments); it != std::end(sourceSegments); ++it) {
        const auto& sourceSegment = *it;
        bool isLastSourceSegment = it + 1 == std::end(sourceSegments);

        int64_t segmentBegin = sourceSegment.first;
        int64_t segmentEnd = sourceSegment.first + sourceSegment.second;

        int64_t reader = segmentBegin;
        while (reader < segmentEnd) {
            std::shift_left(std::begin(visionBuffer), std::end(visionBuffer), 1);
            if (!isLastSourceSegment) {
                visionBuffer[kVisionSize - 1] = in.tb->buffer[reader];
            } else {
                visionBuffer[kVisionSize - 1] = '\0';
            }
            reader += 1;

            if (skipCount > 0) {
                skipCount -= 1;
                continue;
            }

            // Performs control sequence matching
            // Returns whether caller should try match the next candidate.
            auto doMatching = [&](std::string_view pattern, FormatFlagPtr patternFlag) -> bool {
                assert(!pattern.empty());

                // Case: no match (try next candidate)
                if (!visionBufferMatches(pattern)) {
                    return false;
                }

                // Case: matches but escaped; treat as normal text
                if (isEscaping) {
                    isEscaping = false;

                    skipCount = pattern.size() - 1;
                    return true;
                }

                // Case: matches and valid
                constexpr auto kInvalidIdx = std::numeric_limits<size_t>::max();
                size_t lastSeenIdx = kInvalidIdx;
                for (auto it = seenControlSeqs.rbegin(); it != seenControlSeqs.rend(); ++it) {
                    if (it->pattern == pattern && !it->HasClosingControlSeq()) {
                        // https://stackoverflow.com/a/24998000
                        lastSeenIdx = std::distance(seenControlSeqs.begin(), it.base()) - 1;
                        break;
                    }
                }

                if (lastSeenIdx == kInvalidIdx) {
                    // Opening control sequence
                    seenControlSeqs.push_back(ControlSequence{
                        .begin = AdjustBufferIndex(*in.tb, reader, -kVisionSize),
                        .pattern = pattern,
                        .patternFlag = patternFlag,
                    });
                } else {
                    // Closing control sequence

                    auto& lastSeen = seenControlSeqs[lastSeenIdx];
                    lastSeen.end = AdjustBufferIndex(*in.tb, reader, -kVisionSize + pattern.size());

                    // Remove the record of this control seq (and everything after because they can't possibly be matched, since we disallow intermingled syntax like *foo_bar*_)
                    size_t lastUnclosedControlSeq = seenControlSeqs.size();
                    while (lastUnclosedControlSeq-- > 0) {
                        if (seenControlSeqs[lastUnclosedControlSeq].HasClosingControlSeq()) {
                            lastUnclosedControlSeq++;
                            break;
                        }
                    }
                    seenControlSeqs.resize(lastUnclosedControlSeq);

                    // If we matched the very first control sequence, i.e. going back to unformatted text now
                    if (lastSeenIdx == 0) {
                        outputAllMatchedTextRuns();
                        seenControlSeqs.clear();
                    }
                }
                skipCount = pattern.size() - 1;
                return true;
            };
            auto doMatchings = [&](std::initializer_list<std::string_view> patterns, FormatFlagPtr patternFlag) {
                for (const auto& pattern : patterns) {
                    if (doMatching(pattern, patternFlag)) {
                        return true;
                    }
                }
                return false;
            };

            if (doMatching("**"sv, &TextStyle::isBold) ||
                doMatching("__"sv, &TextStyle::isUnderline) ||
                doMatching("~~"sv, &TextStyle::isStrikethrough) ||
                doMatchings({ "*"sv, "_"sv }, &TextStyle::isItalic))
            {
                // No-op, handled inside the helper
            } else {
                // Set escaping state for the next character
                // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
                // If this is anything else, this condition will evaluate to false
                isEscaping = visionBuffer[0] == '\\' && !isEscaping;
            }
        }
    }

    // Output the last TextRun from end of the last formatted stack to end of buffer, if there is any
    if (!out.textRuns.empty()) {
        auto& last = out.textRuns.back();
        if (last.end != in.tb->bufferSize) {
            outputTextRun(TextRun{
                .begin = last.end,
                .end = in.tb->bufferSize,
            });
        }
    }

    return out;
}

#ifdef IONL_SHOW_DEBUG_INFO
void ShowDebugTextRun(std::string_view source, const TextRun& tr) {
    auto substr = source.substr(tr.begin, tr.end - tr.begin);
    ImGui::Text("Segment: [%zu,%zu); %c%c%c%c%c",
        tr.begin,
        tr.end,
        tr.style.isBold ? 'b' : '-',
        tr.style.isItalic ? 'i' : '-',
        tr.style.isUnderline ? 'u' : '-',
        tr.style.isStrikethrough ? 's' : '-',
        tr.style.isMonospace ? 'm' : '-');
    ImGui::Indent();
    auto window = ImGui::GetCurrentWindowRead();
    auto ptTopLeft = window->DC.CursorPos;

    // Use default font because that has clearly recognizable glyph boundaries, easier for debugging purposes
    ImGui::PushFont(nullptr);
    ImGui::Text("%.*s", (int)substr.size(), substr.begin());
    ImGui::PopFont();

    auto tpBottomLeft = window->DC.CursorPos;
    ImGui::Unindent();
    ImGui::GetWindowDrawList()->AddRect(ptTopLeft, ImVec2(ImGui::GetContentRegionAvail().x, tpBottomLeft.y), IM_COL32(255, 255, 0, 255));
}

void ShowDebugTextRuns(std::string_view source, std::span<const TextRun> textRuns) {
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
#endif

struct GlyphRun {
    TextRun tr;

    // Position of the first glyph in this run, in text canvas space
    ImVec2 pos;
    float horizontalAdvance = 0.0f;
};

struct LayoutInput {
    // [Required] Markdown styling.
    const MarkdownStylesheet* styles;
    // [Required] Source buffer which generated the TextRun's.
    const GapBuffer* tb;
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

    for (const auto& textRun : in.textRuns) {
        auto& face = in.styles->LookupFace(textRun.style);

        const ImWchar* beg = &in.tb->buffer[textRun.begin];
        const ImWchar* end = &in.tb->buffer[textRun.end];
        // Try to lay this [beg,end) on current line, and if we can't, retry with [remaining,end) until we are done with this TextRun
        while (true) {
            const ImWchar* remaining;
            // `wrap_width` is for automatically laying the text in multiple lines (and return the size of all lines).
            // We want to perform line wrapping ourselves, so we use `max_width` to instruct ImGui to stop after reaching the line width.
            auto runDim = face.font->CalcTextSize(face.font->FontSize, in.viewportWidth, 0.0f, beg, end, &remaining);
            currPos.x += runDim.x;
            currLineDim.x += runDim.x;
            currLineDim.y = ImMax(currLineDim.y, runDim.y);

            GlyphRun glyphRun;
            glyphRun.tr = textRun;
            glyphRun.tr.begin = std::distance(in.tb->begin(), beg);
            glyphRun.tr.end = std::distance(in.tb->begin(), remaining);
            glyphRun.pos = currPos;
            glyphRun.horizontalAdvance = runDim.x;
            out.glyphRuns.push_back(std::move(glyphRun));

            if (remaining == in.tb->end()) {
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
    }

    // Add last line's height (where the wrapping code is not reached)
    out.boundingBox.y += currLineDim.y;

    return out;
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
        u = te.buffer->GetContentSize();
    } else {
        l = *(ub - 1);
        u = *ub;
    }

    return { l, u };
}
} // namespace

Ionl::GapBuffer::GapBuffer()
    : buffer{ AllocateBuffer(256) }
    , bufferSize{ 256 }
    , frontSize{ 0 }
    , gapSize{ 256 } {}

Ionl::GapBuffer::GapBuffer(std::string_view content)
    // NOTE: these set of parameters are technically invalid, but they get immediately overridden by UpdateContent() which doesn't care
    : buffer{ nullptr }
    , bufferSize{ 0 }
    , frontSize{ 0 }
    , gapSize{ 0 } {
    UpdateContent(content);
}

std::string Ionl::GapBuffer::ExtractContent() const {
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

void Ionl::GapBuffer::UpdateContent(std::string_view content) {
    auto strBegin = &*content.begin();
    auto strEnd = &*content.end();
    auto minBufferSize = ImTextCountCharsFromUtf8(strBegin, strEnd);
    if (bufferSize < minBufferSize) {
        bufferSize = minBufferSize;
        frontSize = minBufferSize;
        gapSize = 0;
        ReallocateBuffer(buffer, minBufferSize);
    } else {
        // If new string size is smaller than our current buffer, we keep the buffer and simply put new data into it
        frontSize = minBufferSize;
        gapSize = bufferSize - minBufferSize;
    }
    ImTextStrFromUtf8NoNullTerminate(buffer, bufferSize, strBegin, strEnd);
}

Ionl::GapBuffer::~GapBuffer() {
    free(buffer);
}

void Ionl::TextEdit::Show() {
    auto& g = *ImGui::GetCurrentContext();
    auto& io = ImGui::GetIO();
    auto window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    auto contentRegionAvail = ImGui::GetContentRegionAvail();
    auto drawList = window->DrawList;

    float totalHeight = 10.0f; // TODO

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
