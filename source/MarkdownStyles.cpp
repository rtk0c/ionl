#include "MarkdownStyles.hpp"

#include <array>
#include <utility>

int Ionl::CalcHeadingLevel(TextStyleType type) {
    auto n = (int)type;
    return n - (int)TextStyleType::Title_BEGIN;
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
