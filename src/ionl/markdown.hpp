#pragma once

#include <imgui/imgui.h>
#include <ionl/gap_buffer.hpp>

#include <cstdint>
#include <vector>

namespace Ionl {

enum class TextStyleType : char {
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

constexpr int kNumTitleLevels =
    static_cast<char>(TextStyleType::Title_END) - static_cast<char>(TextStyleType::Title_BEGIN);

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

struct TextRun {
    int64_t begin = 0; // Buffer index
    int64_t end = 0; // Buffer index
    TextStyle style = {};
    bool hasParagraphBreak = false; // Whether to break paragraph at end of this TextRun
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
    MarkdownFace headingFaces[kNumTitleLevels] = {};

    float linePadding = 0.0f;
    float paragraphPadding = 0.0f;

    void SetRegularFace(MarkdownFace face, bool isMonospace, bool isBold, bool isItalic);
    void SetHeadingFace(MarkdownFace face, int level);

    const MarkdownFace& LookupFace(const TextStyle& style) const;
};

// Global, shared, and default instance of Markdown styling
extern MarkdownStylesheet gMarkdownStylesheet;

// Each TextRun is gaurenteed to only span a contiguous segment of the buffer.
// If logically a single text run spans across the buffer, it is broken up and then outputed.
// Note, this also includes the two cases (1) run ends on the gap => run.end == gapBegin; (2) run begins on the gap => run.begin == gapEnd.
// Case (1) will never have run.end == gapEnd and case (2) will never have run.begin == gapBegin, even though this is the same thing in logical index.
std::vector<TextRun> ParseMarkdownBuffer(const GapBuffer& src);

} // namespace Ionl
