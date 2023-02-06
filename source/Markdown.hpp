#pragma once

#include "GapBuffer.hpp"
#include "imgui.h"

#include <cstdint>
#include <vector>

namespace Ionl {

enum class TextStyleType {
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

// Heading level: number of #'s used in writing this heading
// e.g. # Heading -> 1
//      ## Heading -> 2
int CalcHeadingLevel(TextStyleType type);
TextStyleType MakeHeadingLevel(int level);
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
    // [Required]
    ImFont* font = nullptr;
    // [Optional]
    // Case: == 0, indicating ImGuiCol_Text should be used
    // Case: any other value, indicating should be used directly as the color
    ImU32 color = 0;
};

struct MarkdownStylesheet {
    MarkdownFace regularFaces[1 << 3 /*different formatting flags*/] = {};
    MarkdownFace headingFaces[5 /*5 different levels of heading*/] = {};

    float linePadding = 0.0f;
    float paragraphPadding = 0.0f;

    void SetRegularFace(MarkdownFace face, bool isMonospace, bool isBold, bool isItalic);
    void SetHeadingFace(MarkdownFace face, int level);

    const MarkdownFace& LookupFace(const TextStyle& style) const;
};

// Global, shared, and default instance of Markdown styling
extern MarkdownStylesheet gMarkdownStylesheet;

struct TextRun {
    int64_t begin; // Buffer index
    int64_t end; // Buffer index
    TextStyle style;
    bool hasParagraphBreak = false; // Whether to break paragraph at end of this TextRun
};

struct MdParseInput {
    // [Required] Source buffer to parse markdown from.
    const GapBuffer* src;
};
struct MdParseOutput {
    std::vector<TextRun> textRuns;
};
MdParseOutput ParseMarkdownBuffer(const MdParseInput& in);

} // namespace Ionl
