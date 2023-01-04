#pragma once

#include "imgui.h"

namespace Ionl {

enum TextStyleType {
    Regular,
    Url,
    Title_BEGIN,
    Title1 = Title_BEGIN,
    Title2,
    Title3,
    Title4,
    Title5,
    Title_END,
};

int CalcHeadingLevel(TextStyleType type);
bool IsHeading(TextStyleType type);

struct TextStyle {
    TextStyleType type;

    // Face variants
    bool isMonospace;
    bool isBold;
    bool isItalic;
    // Decorations
    bool isUnderline;
    bool isStrikethrough;
};

struct MarkdownFace {
    ImFont* font = nullptr;
};

struct MarkdownStylesheet {
    MarkdownFace regularFaces[1 << 3 /*different formatting flags*/] = {};
    MarkdownFace headingFaces[5 /*5 different levels of heading*/] = {};

    float linePadding = 0.0f;
    float paragraphPadding = 4.0f;

    const MarkdownFace& LookupFace(const TextStyle& style) const;
};

} // namespace Ionl
