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

        int vtxCount = line.buffer.size() * 4;
        int idxCount = line.buffer.size() * 6;
        drawList->PrimReserve(idxCount, vtxCount);

        while (cursor < line.buffer.end()) {
            const ImWchar* oldCursor = cursor;

            if (*cursor == '\\') {
                escaping = true;
            }

            bool delayedUpdateFormat = false;
            // Returns true if a formatting operator is consumed (may not be effective, i.e. escaped)
            // Returns false if nothing is consumed
            auto chConsume = [&](std::string_view pattern, bool& state) {
                if (!chMatches(pattern)) return false;

                cursor += pattern.size();

                // Treat as normal text
                if (escaping) {
                    escaping = false;
                    return false;
                }

                if (state) {
                    IM_ASSERT(!formatOpStack.empty());
                    auto& lastOp = formatOpStack.back();
                    if (lastOp.op == pattern) {
                        // There is a matching opening specifier in the stack, correct syntax
                        formatOpStack.pop_back();
                        state = false;
                        delayedUpdateFormat = true;
                        return true;
                    } else {
                        // Treat as normal text
                        // Remove the now dangling opening specifier
                        return false;
                    }
                } else {
                    formatOpStack.push_back({
                        .op = pattern,
                        .location = cursor,
                    });
                    state = true;
                    LocateFont(format, formatFont, formatColor);
                    return true;
                }
            };

            do {
                // Inline code
                // TOOD implement consecutive code collapsing
                if (chConsume("`"sv, format.monospace)) break;
                if (!format.monospace) {
                    // Bold
                    if (chConsume("**"sv, format.bold)) break;
                    // Underline
                    if (chConsume("__"sv, format.underline)) break;
                    // Strikethrough
                    if (chConsume("~~"sv, format.strikethrough)) break;
                    // Iatlic
                    if (chConsume("*"sv, format.italic)) break;
                    if (chConsume("_"sv, format.italic)) break;
                }

                // Regular text
                // After all checks, if the previous '\' turns out to be escaping nothing, simply ignore it
                escaping = false;

                cursor += 1;
            } while (false);

            // Draw the current segment of text, [oldCursor, cursor)
            for (auto ch = oldCursor; ch != cursor; ++ch) {
                auto glyph = formatFont->FindGlyph(*ch);
                if (!glyph) continue;
                if (!glyph->Visible) continue;

                // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
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

#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
    drawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif
}

void Ionl::TextEdit::SetContent(std::string_view text) {
    // TODO
}
