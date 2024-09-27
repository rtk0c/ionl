#pragma once

#include <imgui/imgui.h>
#include <ionl/gap_buffer.hpp>
#include <ionl/markdown.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Ionl {

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
