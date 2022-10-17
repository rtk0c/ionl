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
    : id{ ImGui::GetID(id) } {}

Ionl::TextEdit::~TextEdit() = default;

namespace {
struct VariantProperties {
    const ImWchar* loc = nullptr;
    ImVec2 pos;
    bool state = false;
};

struct FontProperties {
    VariantProperties bold;
    VariantProperties italic;
    VariantProperties underline;
    VariantProperties strikethrough;
    VariantProperties monospace;
};

void LocateFont(const FontProperties& props, ImFont*& outFont, ImU32& outColor) {
    using namespace Ionl;

    if (props.monospace.state) {
        if (props.bold.state && props.italic.state) {
            outFont = gTextStyles.fonts[MFV_MonospaceBoldItalic];
            outColor = gTextStyles.fontColors[MFV_MonospaceBoldItalic];
        } else if (props.bold.state) {
            outFont = gTextStyles.fonts[MFV_MonospaceBold];
            outColor = gTextStyles.fontColors[MFV_MonospaceBold];
        } else if (props.italic.state) {
            outFont = gTextStyles.fonts[MFV_MonospaceItalic];
            outColor = gTextStyles.fontColors[MFV_MonospaceItalic];
        } else {
            outFont = gTextStyles.fonts[MFV_Monospace];
            outColor = gTextStyles.fontColors[MFV_Monospace];
        }
    } else {
        if (props.bold.state && props.italic.state) {
            outFont = gTextStyles.fonts[MFV_ProportionalBoldItalic];
            outColor = gTextStyles.fontColors[MFV_ProportionalBoldItalic];
        } else if (props.bold.state) {
            outFont = gTextStyles.fonts[MFV_ProportionalBold];
            outColor = gTextStyles.fontColors[MFV_ProportionalBold];
        } else if (props.italic.state) {
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

        // Code block parsing
        if (chMatches("```")) {
            // TODO
            continue;
        }

        while (cursor < line.buffer.end()) {
            const ImWchar* oldCursor = cursor;

            bool delayedUpdateFormat = false;

            // Used for drawing underline and strikethrough
            ImVec2 delayedLineStartPos;
            ImU32 delayedLineColor;
            float delayedLineYOffset;

            // Returns true if a formatting operator is consumed (may not be effective, i.e. escaped)
            // Returns false if nothing is consumed
            auto chConsume = [&](std::string_view pattern, VariantProperties& props) {
                if (!chMatches(pattern)) return false;

                // Treat as normal text
                if (escaping) {
                    escaping = false;

                    cursor += pattern.size();
                    return false;
                }

                if (props.state) {
                    // Closing specifier
                    props.state = false;
                    props.loc = nullptr;
                    props.pos = ImVec2();
                    delayedUpdateFormat = true;
                } else {
                    // Opening specifier
                    props.state = true;
                    props.loc = cursor;
                    props.pos = textPos;
                    LocateFont(format, formatFont, formatColor);
                }

                cursor += pattern.size();
                return true;
            };

            do {
                // Inline code
                // TOOD implement consecutive code collapsing
                if (chConsume("`"sv, format.monospace)) break;
                if (!format.monospace.state) {
                    // Bold
                    if (chConsume("**"sv, format.bold)) break;

                    // Underline
                    if (auto pos = format.underline.pos;
                        chConsume("__"sv, format.underline))
                    {
                        // Closing specifier
                        if (!format.underline.state) {
                            delayedLineStartPos = pos;
                            delayedLineColor = formatColor;
                            delayedLineYOffset = formatFont->FontSize;
                        }
                        break;
                    }

                    // Strikethrough
                    if (auto pos = format.strikethrough.pos;
                        chConsume("~~"sv, format.strikethrough))
                    {
                        // Closing specifier
                        if (!format.strikethrough.state) {
                            delayedLineStartPos = pos;
                            delayedLineColor = formatColor;
                            delayedLineYOffset = formatFont->FontSize / 2;
                        }
                        break;
                    }

                    // Iatlic
                    if (chConsume("*"sv, format.italic) ||
                        chConsume("_"sv, format.italic))
                    {
                        break;
                    }
                }

                // Set escaping state for the next character
                // If this is a '\', and it's being escaped, treat this just as plain text; otherwise escape the next character
                // If this is anything else, this condition will evaluate to false
                escaping = *cursor == '\\' && !escaping;
                cursor += 1;
            } while (false);

            // We can't do this per-line or per-buffer, because other AddLine() AddXxx() calls happen within this loop too, which will mess up the assumptions
            // TODO this is a quite dumb overhead, optimize
            int vtxCount = (cursor - oldCursor) * 4;
            int idxCount = (cursor - oldCursor) * 6;
            drawList->PrimReserve(idxCount, vtxCount);

            // Draw the current segment of text, [oldCursor, cursor)
            for (auto ch = oldCursor; ch != cursor; ++ch) {
                auto glyph = formatFont->FindGlyph(*ch);
                if (!glyph) continue;
                if (!glyph->Visible) continue;

                ImVec2 pos0(textPos.x + glyph->X0, textPos.y + glyph->Y0);
                ImVec2 pos1(textPos.x + glyph->X1, textPos.y + glyph->Y1);

                ImVec2 uv0(glyph->U0, glyph->V0);
                ImVec2 uv1(glyph->U1, glyph->V1);

                ImU32 glyphColor = glyph->Colored ? (formatColor | ~IM_COL32_A_MASK) : formatColor;
                drawList->PrimRectUV(pos0, pos1, uv0, uv1, glyphColor);
#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
                drawList->AddRect(pos0, pos1, IM_COL32(0, 255, 255, 255));
#endif

                textPos.x += glyph->AdvanceX;
            }

            if (delayedUpdateFormat) {
                LocateFont(format, formatFont, formatColor);
            }
            if (delayedLineStartPos.x != 0.0f) {
                drawList->AddLine(
                    ImVec2(delayedLineStartPos.x, delayedLineStartPos.y + delayedLineYOffset),
                    ImVec2(textPos.x, textPos.y + delayedLineYOffset),
                    delayedLineColor);
            }
        }

        float dy = gTextStyles.regularFontSize + linePadding;
        textPos.x += dy;
        totalHeight += dy;
    }

    ImVec2 widgetSize(ImGui::GetContentRegionAvail().x, totalHeight);
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + widgetSize };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return;
    }

#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
    drawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif
}

void Ionl::TextEdit::SetContent(std::string_view text) {
    // TODO
}
