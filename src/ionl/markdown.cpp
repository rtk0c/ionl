#include "markdown.hpp"
#include "ionl/gap_buffer.hpp"

#include <ionl/macros.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

using namespace std::literals;

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

enum class TokenType {
    Text,
    ParagraphBreak,

    CtlSeq_BEGIN,
    CtlSeqGeneric = CtlSeq_BEGIN,
    CtlSeqInlineCode,
    CtlSeqCodeBlock,
    CtlSeqBold,
    CtlSeqItalicAsterisk,
    CtlSeqItalicUnderscore,
    CtlSeqUnderline,
    CtlSeqStrikethrough,
    CtlSeq_END,
};

static void ToggleStyleForControlSeq(Ionl::TextStyle& style, TokenType ctrlseq) {
    switch (ctrlseq) {
        using enum TokenType;
        case CtlSeqGeneric:
            break;
        case CtlSeqInlineCode:
            style.isMonospace = !style.isMonospace;
            break;
        case CtlSeqBold:
            style.isBold = !style.isBold;
            break;
        case CtlSeqItalicAsterisk:
        case CtlSeqItalicUnderscore:
            style.isItalic = !style.isItalic;
            break;
        case CtlSeqUnderline:
            style.isUnderline = !style.isUnderline;
            break;
        case CtlSeqStrikethrough:
            style.isStrikethrough = !style.isStrikethrough;
            break;

        case Text:
        case ParagraphBreak:
        case CtlSeqCodeBlock:
            // No-op
            break;

        case CtlSeq_END: UNREACHABLE;
    }
}

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

auto Ionl::ParseMarkdownBuffer(const GapBuffer& src) -> std::vector<TextRun> {
    std::vector<TextRun> result;

    // TODO handle cases like ***bold and italic***, the current greedy matching method parses it as **/*text**/* which breaks the control seq pairing logic
    //      probably has to handle this by parsing *** specially, and then do some fancy pairing logic with ***/**/* tokens to break this one up

    // TODO might be an idea to adopt GFM, i.e. do paragraph break only on 2 or more consecutive \n, a single \n is simply ignored for formatting
    //      but this might not be that useful since we are not performing rendering on this

    constexpr auto kInvalidTokenIdx = std::numeric_limits<size_t>::max();
    constexpr auto kMaxHeadingLevel = 5;

    struct Token {
        int64_t begin;
        int64_t end;
        // For some reason MSVC rejects this (referring to a local variable?)
        //        size_t pairedTokenIdx = kInvalidTokenIdx;
        size_t pairedTokenIdx = std::numeric_limits<size_t>::max();
        int headingLevel = 0;
        TokenType type;
        bool dead = false; //< Flag used to mark weather this control sequence token is allowed to have a match

        bool IsText() const {
            return type == TokenType::Text;
        }

        bool IsControlSequence() const {
            auto n = (int)type;
            return n >= (int)TokenType::CtlSeq_BEGIN && n < (int)TokenType::CtlSeq_END;
        }

        bool HasPairedToken() const { return pairedTokenIdx != kInvalidTokenIdx; }
    };
    std::vector<Token> tokens;

    // The characters inside the parser's processing area (for size N, basically 1 current char + N-1 lookahead chars)
    constexpr int64_t kVisionSize = 3;
    ImWchar visionBuffer[kVisionSize] = {};

    bool isEscaping = false;
    bool isBeginningOfLine = true;
    // == 0, regular text
    // > 0, heading
    int currHeadingLevel = 0;

    int64_t reader;
    int readerAdvance = kVisionSize;
    int readerAdvanceDone = 0; // Used inside loop, keeping track of number of advancements completed across segment changes

    auto produceControlSequence = [&](TokenType tokenType) {
        if (isEscaping) {
            isEscaping = false;
            return;
        }

        auto tokenBeginIdx = AdjustBufferIndex(src, reader, -kVisionSize);
        tokens.push_back(Token{
            .begin = tokenBeginIdx,
            .end = tokenBeginIdx + readerAdvance,
            .headingLevel = currHeadingLevel,
            .type = tokenType,
        });
    };

    std::pair<int64_t, int64_t> sourceSegments[] = {
        { src.GetFrontBegin(), src.GetFrontSize() },
        { src.GetBackBegin(), src.GetBackSize() },
        // The dummy segment at the very end for `reader` to advance until the very end of source buffer
        { src.GetBackEnd(), kVisionSize - 1 },
    };
    for (auto it = std::begin(sourceSegments); it != std::end(sourceSegments); ++it) {
        const auto& sourceSegment = *it;
        bool isLastSourceSegment = it + 1 == std::end(sourceSegments);

        // The main parser body.
        // The parser works by running the parser branches repeatedly for this segment, trying to match stuff within the vision buffer.
        // It may instruct `reader` to be advanced by a certain number, depending on what it saw.

        // `reader` is index to the next char to be read into the vision buffer.
        //  Use AdjustBufferIndex(buffer, reader, -kVisionSize) to get idx of the first char in the vision buffer.

        // `readerAdvance` is the number of characters the parser will advance, handled at the top.
        // `readerAdvance` and `readerAdvanceDone` are kept across segment changes to achieve the following logic:
        // - The parser body is executed, setting a `readerAdvance` value
        // - Next iteration, we start advancing `reader`; if we hit end of a segment boundary, we skip onto the next
        //   segment by running `goto segmentDone;`
        //   and then continuing to advance `reader` until we finished the request value

        int64_t segmentBegin = sourceSegment.first;
        int64_t segmentEnd = sourceSegment.first + sourceSegment.second;

        reader = segmentBegin;
        while (true) {
            // Advance `reader`
            for (; readerAdvanceDone < readerAdvance; ++readerAdvanceDone) {
                if (reader >= segmentEnd) {
                    goto segmentDone;
                }

                std::shift_left(std::begin(visionBuffer), std::end(visionBuffer), 1);
                if (!isLastSourceSegment) {
                    visionBuffer[kVisionSize - 1] = src.buffer[reader];
                } else {
                    visionBuffer[kVisionSize - 1] = '\0';
                }
                reader += 1;
            }
            readerAdvanceDone = 0;

            // Move ahead by 1 character by default, overridden by parser branches below
            readerAdvance = 1;

            // Parse heading
            if (isBeginningOfLine && visionBuffer[0] == '#') {
                auto beginIdx = AdjustBufferIndex(src, reader, -kVisionSize);

                // We need quite a lot of lookahead here, so we use GapBufferIterator instead of having a super large vision buffer to avoid having to move around lots of data in the normal code path
                GapBuffer::const_iterator iter(src, beginIdx);
                int headingLevel = 1;
                while (*iter == '#' && iter.HasNext()) {
                    headingLevel += 1;
                    ++iter;
                }

                if (*iter == ' ') {
                    // Parsed heading sequence successfully
                    currHeadingLevel = headingLevel;
                    readerAdvance = headingLevel;

                    continue;
                } else {
                    // Bad heading sequence, skip all the scanned parts as plain text
                    readerAdvance = headingLevel + 1;
                }
            }

            if (visionBuffer[0] == '`') {
                if (visionBuffer[1] == '`' && visionBuffer[2] == '`') {
                    // ```code block```
                    readerAdvance = 3;
                    produceControlSequence(TokenType::CtlSeqCodeBlock);
                    // TODO eat until next ``` closer
                    continue;
                }
                // `inline code`
                readerAdvance = 1;
                produceControlSequence(TokenType::CtlSeqInlineCode);
                continue;
            }
            if (visionBuffer[0] == '*') {
                if (visionBuffer[1] == '*') {
                    // **bold**
                    readerAdvance = 2;
                    produceControlSequence(TokenType::CtlSeqBold);
                } else {
                    // *bold*
                    readerAdvance = 1;
                    produceControlSequence(TokenType::CtlSeqItalicAsterisk);
                }
                continue;
            }
            if (visionBuffer[0] == '_') {
                if (visionBuffer[1] == '_') {
                    // __underline__
                    readerAdvance = 2;
                    produceControlSequence(TokenType::CtlSeqUnderline);
                } else {
                    // _italics_
                    readerAdvance = 1;
                    produceControlSequence(TokenType::CtlSeqItalicUnderscore);
                }
                continue;
            }
            if (visionBuffer[0] == '~' && visionBuffer[1] == '~') {
                // ~~strikethrough~~
                readerAdvance = 2;
                produceControlSequence(TokenType::CtlSeqStrikethrough);
                continue;
            }

            // Set escaping state for the next character
            // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
            // If this is anything else, this condition will evaluate to false
            isEscaping = visionBuffer[0] == '\\' && !isEscaping;

            // Set for next iteration
            if (visionBuffer[0] == '\n') {
                auto beginIdx = AdjustBufferIndex(src, reader, -kVisionSize);
                tokens.push_back({
                    .begin = beginIdx,
                    .end = beginIdx + 1,
                    .headingLevel = currHeadingLevel,
                    .type = TokenType::ParagraphBreak,
                });

                isBeginningOfLine = true;
                isEscaping = false;
                currHeadingLevel = 0;
            } else {
                isBeginningOfLine = false;
            }
        }

    segmentDone:;
    }

    // Do token pairing
    std::vector<size_t> tokenPairingStack;
    for (size_t currIdx = 0; currIdx < tokens.size(); ++currIdx) {
        auto& curr = tokens[currIdx];
        if (curr.IsControlSequence()) {
            // Scan the stack for matching controls
            // This is just a backwards iteration loop -- not using reverse iterator because converting them to indices for std::vector::resize is even more confusing than this
            for (size_t candIdxIt = tokenPairingStack.size(); candIdxIt-- > 0;) {
                size_t candIdx = tokenPairingStack[candIdxIt];
                auto& cand = tokens[candIdx];

                assert(candIdx != currIdx);

                // Case: found
                // - Discard all controls after this one, they are unmatched, e.g. **text__** gives a bold 'text__'
                // - This leaves the pairedSymbolIndex field as undefined, which implies that it's not consumed
                if (cand.type == curr.type) {
                    cand.pairedTokenIdx = currIdx;
                    curr.pairedTokenIdx = candIdx;

                    if (cand.type == TokenType::CtlSeqInlineCode) {
                        // Disable all other formatting control sequences inside inline code
                        for (size_t i = candIdx + 1; i < currIdx; ++i) {
                            auto& token = tokens[i];
                            token.pairedTokenIdx = kInvalidTokenIdx;
                        }
                    }

                    // Remove elements in vector including and after the `candIdxIdx`-th element
                    tokenPairingStack.resize(candIdxIt);
                    goto searchPairsDone;
                }
            }

            // Case: not found
            // - Push symbol into stack
            tokenPairingStack.push_back(currIdx);

        searchPairsDone:;
        }
    }
    // At this point everything left in `tokenPairingStack` is also unpaired

    // TODO if we inserted a ParagraphBreak at the very end, it could make TextRun generation much simpler

    // Insert a single TextRun into `result`, while handling breaking across the gap
    auto outputTextRun = [&result, &src](TextRun run) {
        auto gapBegin = src.GetGapBegin();
        auto gapEnd = src.GetGapEnd();

        assert(run.begin < run.end);
        assert(run.end - run.begin > 0);
        // It CANNOT be the case that both begin and end are on the gap boundary, because that is a 0-length TextRun
        assert(!(run.begin == gapBegin && run.end == gapEnd));

        if (run.begin < gapBegin && run.end > gapEnd) {
            // TextRun spans over the gap, we need to split it
            TextRun& frontRun = run;
            TextRun backRun = run;

            /* frontRun.begin; */ // Remain unchanged
            frontRun.end = gapBegin;
            backRun.begin = gapEnd;
            /* backRun.end; */ // Remain unchanged

            result.push_back(std::move(frontRun));
            result.push_back(std::move(backRun));
        } else {
            // If either begin or end is on the gap boundary, move it to the other side.
            // This way we have a contiguous segment of text again.
            if (run.begin == gapBegin)
                run.begin = gapEnd;
            if (run.end == gapEnd)
                run.end = gapBegin;
            result.push_back(std::move(run));
        }
    };

    TextStyle currStyle{};
    int64_t currTextRunBegin = 0;

    auto outputCurrTextRun = [&](int headingLevel, int64_t end) {
        if (currTextRunBegin == end) {
            return;
        }

        currStyle.type = MakeHeadingLevel(headingLevel);
        outputTextRun({
            .begin = currTextRunBegin,
            .end = end,
            .style = currStyle,
        });
    };

    for (size_t idx = 0; idx < tokens.size(); ++idx) {
        const auto& token = tokens[idx];

        if (token.type == TokenType::ParagraphBreak) {
            outputCurrTextRun(token.headingLevel, token.begin);
            if (!result.empty()) {
                result.back().hasParagraphBreak = true;
            }

            currTextRunBegin = token.end; // We don't want the \n char to be a part of the text output
            currStyle = {};

            continue;
        }

        if (token.IsControlSequence() && token.HasPairedToken()) {
            int64_t boundary = token.pairedTokenIdx > idx
                // This is an opening control sequence
                ? token.begin
                // This is a closing control sequence
                : token.end;
            outputCurrTextRun(token.headingLevel, boundary);
            currTextRunBegin = boundary;
            ToggleStyleForControlSeq(currStyle, token.type);
            continue;
        }
    }
    // Add the last text range if there is any left
    if (auto lastTextEnd = src.GetLastTextEnd();
        currTextRunBegin != lastTextEnd)
    {
        outputTextRun({
            .begin = currTextRunBegin,
            .end = lastTextEnd,
            .style = currStyle,
        });
    }

    return result;
}
