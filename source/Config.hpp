#pragma once

#include "Markdown.hpp"

#include <filesystem>
#include <string>

namespace Ionl {

struct Config {
    float baseFontSize;
    float headingFontScales[kNumTitleLevels];
    std::string regularFont;
    std::string italicFont;
    std::string boldFont;
    std::string boldItalicFont;
    std::string monospaceRegularFont;
    std::string monospaceItalicFont;
    std::string monospaceBoldFont;
    std::string monospaceBoldItalicFont;
    std::string headingFont;
};

void LoadConfigFromFile(Config& cfg, const std::filesystem::path& file);

extern Config gConfig;

} // namespace Ionl
