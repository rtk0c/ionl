#include "Markdown.hpp"

#include "Macros.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

using namespace std::literals;

int Ionl::CalcHeadingLevel(TextStyleType type) {
    auto n = (int)type;
    return n - (int)TextStyleType::Title_BEGIN + 1;
}

Ionl::TextStyleType Ionl::MakeHeadingLevel(int level) {
    return (TextStyleType)((int)TextStyleType::Title_BEGIN + level - 1);
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

    // TODO handle cases like ***bold and italic***, the current greedy matching method parses it as **/*text**/* which breaks the control seq pairing logic
    //      note this is also broken in irccloud-format-helper, so that won't help

    // TODO break parsing state on paragraph break
    // TODO handle code blocks

    // TODO might be an idea to adopt GFM, i.e. do paragraph break only on 2 or more consecutive \n, a single \n is simply ignored for formatting
    //      but this might not be that useful since we are not performing rendering on this

    constexpr auto kInvalidTokenIdx = std::numeric_limits<size_t>::max();
    constexpr auto kMaxHeadingLevel = 5;
    enum class TokenType {
        Text,
        ParagraphBreak,

        Heading_BEGIN,
        Heading_END = (int)Heading_BEGIN + kMaxHeadingLevel,

        CtlSeq_BEGIN,
        CtlSeqGeneric = CtlSeq_BEGIN,
        CtlSeqBold,
        CtlSeqItalicAsterisk,
        CtlSeqItalicUnderscore,
        CtlSeqUnderline,
        CtlSeqStrikethrough,
        CtlSeq_END,
    };

    struct Token {
        int64_t begin;
        int64_t end;
        size_t pairedTokenIdx = kInvalidTokenIdx;
        TokenType type;
        bool dead = false; //< Flag used to mark weather this control sequence token is allowed to have a match

        bool IsText() const {
            return type == TokenType::Text;
        }

        static TokenType MakeHeadingTtype(int level) {
            return (TokenType)((int)TokenType::Heading_BEGIN + level - 1);
        }

        static int CalcHeadingLevel(TokenType ttype) {
            auto n = (int)ttype;
            return n - (int)TokenType::Heading_BEGIN + 1;
        }

        bool IsHeading() const {
            auto n = (int)type;
            return n >= (int)TokenType::Heading_BEGIN && n < (int)TokenType::Heading_END;
        }

        bool IsControlSequence() const {
            auto n = (int)type;
            return n >= (int)TokenType::CtlSeq_BEGIN && n < (int)TokenType::CtlSeq_END;
        }

        bool HasPairedToken() const { return pairedTokenIdx != kInvalidTokenIdx; }
    };
    std::vector<Token> tokens;

    // The characters inside the parser's processing area (for size N, basically 1 current char + N-1 lookahead chars)
    constexpr size_t kVisionSize = 3;
    ImWchar visionBuffer[kVisionSize] = {};

    bool isEscaping = false;
    bool isBeginningOfLine = true;
    // == 0, regular text
    // > 0, heading
    bool currHeadingLevel = 0;

    int64_t reader;
    int readerAdvance = kVisionSize;

    // TODO move all the stateful variable reads like `reader` `readerAdvance` into explicit parameters
    auto produceControlSequence = [&](TokenType tokenType) {
        if (isEscaping) {
            isEscaping = false;
            return;
        }

        auto tokenBeginIdx = AdjustBufferIndex(*in.src, reader, -kVisionSize);
        tokens.push_back(Token{
            .begin = tokenBeginIdx,
            .end = tokenBeginIdx + readerAdvance,
            .type = tokenType,
        });
    };

    std::pair<int64_t, int64_t> sourceSegments[] = {
        { in.src->GetFrontBegin(), in.src->GetFrontSize() },
        { in.src->GetBackBegin(), in.src->GetBackSize() },
        // The dummy segment at the very end for `reader` to advance until the very end of source buffer
        { in.src->GetBackEnd(), kVisionSize - 1 },
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

        int64_t segmentBegin = sourceSegment.first;
        int64_t segmentEnd = sourceSegment.first + sourceSegment.second;

        reader = segmentBegin;
        // Keep `readerAdvance`'s value potentially from the last loop, if e.g. a control sequence spans across the gap
        while (true) {
            // Advance `reader`
            for (int i = 0; i < readerAdvance; ++i) {
                if (reader >= segmentEnd) {
                    goto segmentDone;
                }

                std::shift_left(std::begin(visionBuffer), std::end(visionBuffer), 1);
                if (!isLastSourceSegment) {
                    visionBuffer[kVisionSize - 1] = in.src->buffer[reader];
                } else {
                    visionBuffer[kVisionSize - 1] = '\0';
                }
                reader += 1;
            }

            // Move ahead by 1 character by default, overridden by parser branches below
            readerAdvance = 1;

            // Parse heading
            if (isBeginningOfLine && visionBuffer[0] == '#') {
                auto beginIdx = AdjustBufferIndex(*in.src, reader, -kVisionSize);

                // We need quite a lot of lookahead here, so we use GapBufferIterator instead of having a super large vision buffer to avoid having to move around lots of data in the normal code path
                GapBuffer::const_iterator iter(*in.src, beginIdx);
                int headingLevel = 1;
                while (*iter == '#' && iter.HasNext()) {
                    headingLevel += 1;
                    ++iter;
                }

                auto endIdx = iter.idx;

                if (*iter == ' ') {
                    // Parsed heading sequence successfully
                    currHeadingLevel = headingLevel;
                    readerAdvance = headingLevel;
                    tokens.push_back({ .begin = beginIdx, .end = endIdx, .type = Token::MakeHeadingTtype(headingLevel) });

                    continue;
                } else {
                    // Bad heading sequence, skip all the scanned parts as plain text
                    readerAdvance = headingLevel + 1;
                }
            }

            if (visionBuffer[0] == '*') {
                if (visionBuffer[1] == '*') {
                    // *text*
                    readerAdvance = 2;
                    produceControlSequence(TokenType::CtlSeqBold);
                } else {
                    // *text*
                    readerAdvance = 1;
                    produceControlSequence(TokenType::CtlSeqItalicAsterisk);
                }
                continue;
            }
            if (visionBuffer[0] == '_') {
                if (visionBuffer[1] == '_') {
                    // __text__
                    readerAdvance = 2;
                    produceControlSequence(TokenType::CtlSeqUnderline);
                } else {
                    // _text_
                    readerAdvance = 1;
                    produceControlSequence(TokenType::CtlSeqItalicUnderscore);
                }
                continue;
            }
            if (visionBuffer[0] == '~' && visionBuffer[1] == '~') {
                {
                    // ~~text~~
                    readerAdvance = 2;
                    produceControlSequence(TokenType::CtlSeqStrikethrough);
                }
                continue;
            }

            // Set escaping state for the next character
            // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
            // If this is anything else, this condition will evaluate to false
            isEscaping = visionBuffer[0] == '\\' && !isEscaping;

            // Set for next iteration
            if (visionBuffer[0] == '\n') {
                isBeginningOfLine = true;
                isEscaping = false;
                currHeadingLevel = 0;

                auto beginIdx = AdjustBufferIndex(*in.src, reader, -kVisionSize);
                tokens.push_back({ .begin = beginIdx, .end = beginIdx - 1, .type = TokenType::ParagraphBreak });
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
            for (auto candIdxIt = tokenPairingStack.rbegin(); candIdxIt != tokenPairingStack.rend(); ++candIdxIt) {
                auto& cand = tokens[*candIdxIt];

                // Case: found
                // - Discard all controls after this one, they are unmatched, e.g. **text__** gives a bold 'text__'
                // - This leaves the pairedSymbolIndex field as undefined, which implies that it's not consumed
                if (cand.type == curr.type) {
                    cand.pairedTokenIdx = currIdx;
                    curr.pairedTokenIdx = *candIdxIt;

                    // Remove elements in vector including and after the `candIdxIdx`-th element
                    tokenPairingStack.resize(*candIdxIt);
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

    // Insert a single TextRun into `out.textRuns`, while handling breaking across the gap
    auto outputTextRun = [&in, &out](TextRun run) {
        auto gapBegin = in.src->GetGapBegin();
        auto gapEnd = in.src->GetGapEnd();
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

    TextStyle currStyle{};
    int64_t currTextRunBegin = 0;
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        const auto& token = *it;

        if (token.type == TokenType::ParagraphBreak) {
            currStyle = {};
            continue;
        }

        if (token.IsHeading()) {
            int64_t textRangeBegin = token.begin;
            int64_t textRangeEnd;

            // Seek to paragraph break, or end of text to compute `textRangeEnd`
            ++it;
            for (; it != tokens.end(); ++it) {
                if (it->type == TokenType::ParagraphBreak) {
                    textRangeEnd = it->begin;
                    goto found;
                }
            }
            textRangeEnd = in.src->GetLastTextIndex();
        found:

            // TODO handle formatting inside a heading
            //      we don't render those currently so there is no use, let's not add complexity right now
            outputTextRun({
                .begin = textRangeBegin,
                .end = textRangeEnd,
                .style = {
                    // NOTE: every other field is initialized to 0, i.e. false in this case
                    .type = MakeHeadingLevel(Token::CalcHeadingLevel(token.type)),
                },
            });

            currTextRunBegin = textRangeEnd;

            continue;
        }

        if (token.IsControlSequence() && token.HasPairedToken()) {
            currStyle.type = TextStyleType::Regular;
            outputTextRun({
                .begin = currTextRunBegin,
                .end = token.begin,
                .style = currStyle,
            });

            currTextRunBegin = token.begin;

            switch (token.type) {
                using enum TokenType;
                case CtlSeqGeneric:
                    break;
                case CtlSeqBold:
                    currStyle.isBold = !currStyle.isBold;
                    break;
                case CtlSeqItalicAsterisk:
                case CtlSeqItalicUnderscore:
                    currStyle.isItalic = !currStyle.isItalic;
                    break;
                case CtlSeqUnderline:
                    currStyle.isUnderline = !currStyle.isUnderline;
                    break;
                case CtlSeqStrikethrough:
                    currStyle.isStrikethrough = !currStyle.isStrikethrough;
                    break;
                default: UNREACHABLE;
            }
            continue;
        }
    }
    // Add the last text range if there is any left
    if (auto lastTextIdx = in.src->GetLastTextIndex();
        currTextRunBegin != lastTextIdx)
    {
        outputTextRun({
            .begin = currTextRunBegin,
            .end = lastTextIdx,
            .style = currStyle,
        });
    }

    return out;
}
