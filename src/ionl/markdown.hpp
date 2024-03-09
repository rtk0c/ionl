#pragma once

#include <imgui/imgui.h>
#include <ionl/gap_buffer.hpp>
#include <ionl/text_buffer.hpp>

#include <cstdint>
#include <vector>

namespace Ionl {

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

struct MdParseInput {
    // [Required] Source buffer to parse markdown from.
    const GapBuffer* src;
};
struct MdParseOutput {
    std::vector<TextRun> textRuns;
};
MdParseOutput ParseMarkdownBuffer(const MdParseInput& in);

} // namespace Ionl
