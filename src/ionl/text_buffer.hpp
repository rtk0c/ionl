#pragma once

#include <imgui/imgui.h>
#include <ionl/gap_buffer.hpp>
#include <ionl/markdown.hpp>

#include <string>
#include <string_view>
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

constexpr int kNumTitleLevels = (int)TextStyleType::Title_END - (int)TextStyleType::Title_BEGIN;

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

struct TextBuffer {
    // Canonical data
    GapBuffer gapBuffer;

    // Cached data derived from canonical data
    // Invalidation and recomputation should be done by whoever modifies `gapBuffer`.
    std::vector<TextRun> textRuns;
    int cacheDataVersion = 0;

    explicit TextBuffer(GapBuffer buf);

    void RefreshCaches();
};

} // namespace Ionl
