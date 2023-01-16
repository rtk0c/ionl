#include "Markdown.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

using namespace std::literals;

int Ionl::CalcHeadingLevel(TextStyleType type) {
    auto n = (int)type;
    return n - (int)TextStyleType::Title_BEGIN;
}

Ionl::TextStyleType Ionl::MakeHeadingLevel(int level) {
    return (TextStyleType)((int)TextStyleType::Title_BEGIN + level);
}

bool Ionl::IsHeading(TextStyleType type) {
    auto n = (int)type;
    return n >= (int)TextStyleType::Title_BEGIN && n < (int)TextStyleType::Title_END;
}

static size_t AmalgamateVariantFlags(bool isMonospace, bool isBold, bool isItalic) {
    size_t idx = 0;
    idx |= isMonospace << 0;
    idx |= isBold << 1;
    idx |= isItalic << 2;
    return idx;
}

void Ionl::MarkdownStylesheet::SetRegularFace(MarkdownFace face, bool isMonospace, bool isBold, bool isItalic) {
    regularFaces[AmalgamateVariantFlags(isMonospace, isBold, isItalic)] = std::move(face);
}

void Ionl::MarkdownStylesheet::SetHeadingFace(MarkdownFace face, int level) {
    // Level is 1-indexed, to match with number of # in Markdown syntax
    // e.g. # maps to level 1, ## maps to level 2
    auto levelIdx = level - 1;

    assert(levelIdx >= 0 && levelIdx < std::size(headingFaces));
    headingFaces[levelIdx] = std::move(face);
}

const Ionl::MarkdownFace& Ionl::MarkdownStylesheet::LookupFace(const TextStyle& style) const {
    if (IsHeading(style.type)) {
        return headingFaces[CalcHeadingLevel(style.type)];
    } else {
        return regularFaces[AmalgamateVariantFlags(style.isMonospace, style.isBold, style.isItalic)];
    }
}

Ionl::MarkdownStylesheet Ionl::gMarkdownStylesheet{};

// Ionl::ParseMarkdownBuffer example #1
// Input text:
//     Test **bold _and italic __text__ with_ strangling_underscores** **_nest_** finishing words
// Expected output TextRun's:
//     ----- "Test "
//     b---- "**bold "
//     bi--- "_and italic "
//     biu-- "__text__"
//     bi--- " with_"
//     b---- " strangling_underscores**"
//     ----- " "
//     b---- "**"
//     bi--- "_nest_"
//     b---- "**"
//     ----- " finishing words"

Ionl::MdParseOutput Ionl::ParseMarkdownBuffer(const Ionl::MdParseInput& in) {
    MdParseOutput out;

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
        auto gapBegin = in.src->frontSize;
        auto gapEnd = in.src->frontSize + in.src->gapSize;
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
        { in.src->GetFrontBegin(), in.src->GetFrontSize() },
        { in.src->GetBackBegin(), in.src->GetBackSize() },
        // The dummy segment at the very end for "head" to advance until the very end of source buffer
        { in.src->GetBackEnd(), kVisionSize - 1 },
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
                visionBuffer[kVisionSize - 1] = in.src->buffer[reader];
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
                        .begin = AdjustBufferIndex(*in.src, reader, -kVisionSize),
                        .pattern = pattern,
                        .patternFlag = patternFlag,
                    });
                } else {
                    // Closing control sequence

                    auto& lastSeen = seenControlSeqs[lastSeenIdx];
                    lastSeen.end = AdjustBufferIndex(*in.src, reader, -kVisionSize + pattern.size());

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
        if (last.end != in.src->bufferSize) {
            outputTextRun(TextRun{
                .begin = last.end,
                .end = in.src->bufferSize,
            });
        }
    }

    return out;
}
