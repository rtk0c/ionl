#include "Config.hpp"

#include <toml++/toml.h>

using namespace std::literals;
namespace fs = std::filesystem;

void Ionl::LoadConfigFromFile(Config& cfg, const fs::path& file) {
    auto o = toml::parse_file(file.string());
    cfg.baseFontSize = o["Style"]["BaseFontSize"].value_or(18.0f);
    cfg.headingFontScales;
    {
        auto confValue = o["Style"]["HeadingFontSizeScales"].as_array();
        if (confValue == nullptr) {
            cfg.headingFontScales[0] = 2.5f;
            cfg.headingFontScales[1] = 2.0f;
            cfg.headingFontScales[2] = 1.5f;
            cfg.headingFontScales[3] = 1.2f;
            cfg.headingFontScales[4] = 1.0f;
        } else {
            for (size_t i = 0, max = std::max(confValue->size(), std::size(cfg.headingFontScales)); i < max; ++i) {
                cfg.headingFontScales[i] = (*confValue)[i].value_or(1.0f);
            }
        }
    }
    cfg.regularFont = o["Style"]["RegularFont"].value_or(""sv);
    cfg.italicFont = o["Style"]["ItalicFont"].value_or(""sv);
    cfg.boldFont = o["Style"]["BoldFont"].value_or(""sv);
    cfg.boldItalicFont = o["Style"]["BoldItalicFont"].value_or(""sv);
    cfg.monospaceRegularFont = o["Style"]["MonospaceRegularFont"].value_or(""sv);
    cfg.monospaceItalicFont = o["Style"]["MonospaceItalicFont"].value_or(""sv);
    cfg.monospaceBoldFont = o["Style"]["MonospaceBoldFont"].value_or(""sv);
    cfg.monospaceBoldItalicFont = o["Style"]["MonospaceBoldItalicFont"].value_or(""sv);
    cfg.headingFont = o["Style"]["HeadingFont"].value_or(""sv);
}

Ionl::Config Ionl::gConfig{};
