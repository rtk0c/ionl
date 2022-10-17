#include "WidgetTextEdit.hpp"

#include "Macros.hpp"
#include "Utils.hpp"

#include "imgui_internal.h"

#include <string_view>

using namespace std::string_view_literals;

Ionl::TextStyles Ionl::gTextStyles;

Ionl::TextEdit::TextEdit(ImGuiID id)
    : id{ id } {}

// TODO would this be correct behavior? if the user constructs the TextEdit before-hand but uses it in a different location in thte ID stack
Ionl::TextEdit::TextEdit(const char* id)
    : id{ ImGui::GetID(id) } {
    lines.push_back({
        .buffer = {},
    });
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('T');
    lines.back().buffer.push_back('e');
    lines.back().buffer.push_back('s');
    lines.back().buffer.push_back('t');
    lines.push_back({
        .buffer = {},
    });
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('T');
    lines.back().buffer.push_back('e');
    lines.back().buffer.push_back('s');
    lines.back().buffer.push_back('t');
    lines.push_back({
        .buffer = {},
    });
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('T');
    lines.back().buffer.push_back('e');
    lines.back().buffer.push_back('s');
    lines.back().buffer.push_back('t');
    lines.push_back({
        .buffer = {},
    });
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('#');
    lines.back().buffer.push_back('T');
    lines.back().buffer.push_back('e');
    lines.back().buffer.push_back('s');
    lines.back().buffer.push_back('t');
    lines.push_back({
        .buffer = {},
    });
    lines.back().buffer.push_back('T');
    lines.back().buffer.push_back('*');
    lines.back().buffer.push_back('*');
    lines.back().buffer.push_back('e');
    lines.back().buffer.push_back('*');
    lines.back().buffer.push_back('*');
    lines.back().buffer.push_back('s');
    lines.back().buffer.push_back('_');
    lines.back().buffer.push_back('l');
    lines.back().buffer.push_back('a');
    lines.back().buffer.push_back('l');
    lines.back().buffer.push_back('a');
    lines.back().buffer.push_back('_');
}

Ionl::TextEdit::~TextEdit() = default;

namespace {
struct FontProperties {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool monospace = false;
};

void LocateFont(const FontProperties& props, ImFont*& outFont, ImU32& outColor) {
    using namespace Ionl;

    if (props.monospace) {
        if (props.bold && props.italic) {
            outFont = gTextStyles.fonts[MFV_MonospaceBoldItalic];
            outColor = gTextStyles.fontColors[MFV_MonospaceBoldItalic];
        } else if (props.bold) {
            outFont = gTextStyles.fonts[MFV_MonospaceBold];
            outColor = gTextStyles.fontColors[MFV_MonospaceBold];
        } else if (props.italic) {
            outFont = gTextStyles.fonts[MFV_MonospaceItalic];
            outColor = gTextStyles.fontColors[MFV_MonospaceItalic];
        } else {
            outFont = gTextStyles.fonts[MFV_Monospace];
            outColor = gTextStyles.fontColors[MFV_Monospace];
        }
    } else {
        if (props.bold && props.italic) {
            outFont = gTextStyles.fonts[MFV_ProportionalBoldItalic];
            outColor = gTextStyles.fontColors[MFV_ProportionalBoldItalic];
        } else if (props.bold) {
            outFont = gTextStyles.fonts[MFV_ProportionalBold];
            outColor = gTextStyles.fontColors[MFV_ProportionalBold];
        } else if (props.italic) {
            outFont = gTextStyles.fonts[MFV_ProportionalItalic];
            outColor = gTextStyles.fontColors[MFV_ProportionalItalic];
        } else {
            outFont = gTextStyles.fonts[MFV_Proportional];
            outColor = gTextStyles.fontColors[MFV_Proportional];
        }
    }
};
} // namespace

void Ionl::TextEdit::Show() {
    auto window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    ImDrawList* drawList = window->DrawList;

    // A string segment of `op` located at [location, op.size())
    struct FormatOp {
        std::string_view op;
        const ImWchar* location;
    };
    std::vector<FormatOp> formatOpStack;
    bool escaping = false;

    FontProperties format;
    ImFont* formatFont;
    ImU32 formatColor;
    LocateFont(format, formatFont, formatColor);

    ImVec2 textPos = window->DC.CursorPos;
    float totalHeight = 0.0f;

    for (const auto& line : lines) {
        if (line.buffer.empty()) {
            continue;
        }

        const ImWchar* cursor = line.buffer.begin();
        auto chMatches = [&](std::string_view patternStr) {
            auto pattern = patternStr.data();
            auto haystack = cursor;
            while (true) {
                // Matched the whole pattern
                if (*pattern == '\0') return true;
                // Reached end of input before matching the whole pattern
                if (haystack == line.buffer.end()) return false;

                if (*pattern != *haystack) {
                    return false;
                }
                ++pattern;
                ++haystack;
            }
        };
        auto chConsume = [&](std::string_view pattern) {
            if (chMatches(pattern)) {
                cursor += pattern.size();

                if (!escaping) {
                    formatOpStack.push_back({
                        .op = pattern,
                        .location = cursor,
                    });
                    return true;
                }

                escaping = false;
            }
            return false;
        };

        // Heading parsing
        if (*cursor == '#') {
            int headingLevel = 0;
            while (*cursor == '#') {
                cursor += 1;
                headingLevel += 1;
            }
            headingLevel = ImMin<int>(headingLevel, MFV_META_HeadingMax);

            // Do the all of the text rendering here (heading style overrides everything else)
            auto font = gTextStyles.fonts[MFV_Heading1 + headingLevel];
            auto color = gTextStyles.fontColors[MFV_Heading1 + headingLevel];
            drawList->AddText(font, font->FontSize, textPos, color, line.buffer.begin(), line.buffer.end());

            float dy = font->FontSize + linePadding;
            textPos.y += dy;
            totalHeight += dy;

            continue;
        }

        int vtxCount = line.buffer.size() * 4;
        int idxCount = line.buffer.size() * 6;
        drawList->PrimReserve(idxCount, vtxCount);

        while (cursor < line.buffer.end()) {
            const ImWchar* oldCursor = cursor;

            if (*cursor == '\\') {
                escaping = true;
            }

            bool delayedUpdateFormat = false;
            if (chConsume("**"sv)) {
                // Bold
                if (format.bold) {
                    format.bold = false;
                    delayedUpdateFormat = true;
                } else {
                    format.bold = true;
                    LocateFont(format, formatFont, formatColor);
                }
            } else if (chConsume("__"sv)) {
                // Underline
                if (format.underline) {
                    format.underline = false;
                    delayedUpdateFormat = true;
                } else {
                    format.underline = true;
                    LocateFont(format, formatFont, formatColor);
                }
            } else if (chConsume("~~"sv)) {
                // Strikethrough
                if (format.strikethrough) {
                    format.strikethrough = false;
                    delayedUpdateFormat = true;
                } else {
                    format.strikethrough = true;
                    LocateFont(format, formatFont, formatColor);
                }
            } else if (chConsume("*"sv) || chConsume("_"sv)) {
                // Iatlic
                if (format.italic) {
                    format.italic = false;
                    delayedUpdateFormat = true;
                } else {
                    format.italic = true;
                    LocateFont(format, formatFont, formatColor);
                }
            } else if (chConsume("`"sv)) {
                // Inline code
                // TOOD implement consecutive code collapsing
                if (format.monospace) {
                    format.monospace = false;
                    delayedUpdateFormat = true;
                } else {
                    format.monospace = true;
                    LocateFont(format, formatFont, formatColor);
                }
            } else /* Regular character */ {
                // After all checks, if the previous '\' turns out to be escaping nothing, simply ignore it
                escaping = false;

                cursor += 1;
            }

            // Draw the current segment of text, [oldCursor, cursor)
            for (auto ch = oldCursor; ch != cursor; ++ch) {
                auto glyph = formatFont->FindGlyph(*ch);
                if (!glyph) continue;
                if (!glyph->Visible) continue;

                // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
                float x1 = textPos.x + glyph->X0;
                float x2 = textPos.x + glyph->X1;
                float y1 = textPos.y + glyph->Y0;
                float y2 = textPos.y + glyph->Y1;

                float u1 = glyph->U0;
                float v1 = glyph->V0;
                float u2 = glyph->U1;
                float v2 = glyph->V1;

                ImU32 glyphColor = glyph->Colored ? (formatColor | ~IM_COL32_A_MASK) : formatColor;
                drawList->PrimRectUV(ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(u1, v1), ImVec2(u2, v2), glyphColor);

                textPos.x += glyph->AdvanceX;
            }

            if (delayedUpdateFormat) {
                LocateFont(format, formatFont, formatColor);
            }
        }

        // TOOD correct calculation, we might've used different fonts on this line
        float dy = drawList->_Data->FontSize + linePadding;
        textPos.x += dy;
        totalHeight += dy;
    }

    ImVec2 widgetSize(ImGui::GetContentRegionAvail().x, totalHeight);
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + widgetSize };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return;
    }
}

void Ionl::TextEdit::SetContent(std::string_view text) {
    // TODO
}
