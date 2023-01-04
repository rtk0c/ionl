#include "MarkdownStyles.hpp"

int Ionl::CalcHeadingLevel(TextStyleType type) {
    auto n = (int)type;
    return n - (int)TextStyleType::Title_BEGIN;
}

bool Ionl::IsHeading(TextStyleType type) {
    auto n = (int)type;
    return n >= (int)TextStyleType::Title_BEGIN && n < (int)TextStyleType::Title_END;
}

const Ionl::MarkdownFace& Ionl::MarkdownStylesheet::LookupFace(const TextStyle& style) const {
    if (IsHeading(style.type)) {
        return headingFaces[CalcHeadingLevel(style.type)];
    } else {
        size_t idx = 0;
        idx |= style.isMonospace << 0;
        idx |= style.isBold << 1;
        idx |= style.isItalic << 2;
        // These are decoration only
        /* textRun.isUnderline */
        /* textRun.isStrikethrough */

        return regularFaces[idx];
    }
}
