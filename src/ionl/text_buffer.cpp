#include "text_buffer.hpp"

int Ionl::CalcHeadingLevel(TextStyleType type) {
    auto n = static_cast<int>(type);
    return n - static_cast<int>(TextStyleType::Title_BEGIN) + 1;
}

Ionl::TextStyleType Ionl::MakeHeadingLevel(int level) {
    if (level == 0) {
        return TextStyleType::Regular;
    } else {
        return static_cast<TextStyleType>(static_cast<int>(TextStyleType::Title_BEGIN) + level - 1);
    }
}

bool Ionl::IsHeading(TextStyleType type) {
    auto n = static_cast<int>(type);
    return n >= static_cast<int>(TextStyleType::Title_BEGIN) &&
        n < static_cast<int>(TextStyleType::Title_END);
}

Ionl::TextBuffer::TextBuffer(GapBuffer buf)
    : gapBuffer{ std::move(buf) } //
{
    RefreshCaches();
}

void Ionl::TextBuffer::RefreshCaches() {
    auto res = ParseMarkdownBuffer({
        .src = &gapBuffer,
    });

    textRuns = std::move(res.textRuns);
    cacheDataVersion += 1;
}

