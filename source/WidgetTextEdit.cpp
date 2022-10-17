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
struct FaceTrait {
    const ImWchar* loc = nullptr;
    ImVec2 pos;
    bool state = false;
};

struct FaceDescription {
    FaceTrait bold;
    FaceTrait italic;
    FaceTrait underline;
    FaceTrait strikethrough;
    FaceTrait monospace;
};

void LocateFace(const FaceDescription& desc, ImFont*& outFont, ImU32& outColor) {
    using namespace Ionl;

    if (desc.monospace.state) {
        if (desc.bold.state && desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceBoldItalic];
            outColor = gTextStyles.faceColors[MF_MonospaceBoldItalic];
        } else if (desc.bold.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceBold];
            outColor = gTextStyles.faceColors[MF_MonospaceBold];
        } else if (desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_MonospaceItalic];
            outColor = gTextStyles.faceColors[MF_MonospaceItalic];
        } else {
            outFont = gTextStyles.faceFonts[MF_Monospace];
            outColor = gTextStyles.faceColors[MF_Monospace];
        }
    } else {
        if (desc.bold.state && desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalBoldItalic];
            outColor = gTextStyles.faceColors[MF_ProportionalBoldItalic];
        } else if (desc.bold.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalBold];
            outColor = gTextStyles.faceColors[MF_ProportionalBold];
        } else if (desc.italic.state) {
            outFont = gTextStyles.faceFonts[MF_ProportionalItalic];
            outColor = gTextStyles.faceColors[MF_ProportionalItalic];
        } else {
            outFont = gTextStyles.faceFonts[MF_Proportional];
            outColor = gTextStyles.faceColors[MF_Proportional];
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
    FaceDescription faceDesc;
    ImFont* faceFont;
    ImU32 faceColor;
    LocateFace(faceDesc, faceFont, faceColor);

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
            headingLevel = ImMin<int>(headingLevel, MF_META_HeadingMax);

            // Do the all of the text rendering here (heading style overrides everything else)
            auto font = gTextStyles.faceFonts[MF_Heading1 + headingLevel];
            auto color = gTextStyles.faceColors[MF_Heading1 + headingLevel];
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
            auto chConsume = [&](std::string_view pattern, FaceTrait& props) {
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
                    LocateFace(faceDesc, faceFont, faceColor);
                }

                cursor += pattern.size();
                return true;
            };

            do {
                // Inline code
                // TOOD implement consecutive code collapsing
                if (chConsume("`"sv, faceDesc.monospace)) break;
                if (!faceDesc.monospace.state) {
                    // Bold
                    if (chConsume("**"sv, faceDesc.bold)) break;

                    // Underline
                    if (auto pos = faceDesc.underline.pos;
                        chConsume("__"sv, faceDesc.underline))
                    {
                        // Closing specifier
                        if (!faceDesc.underline.state) {
                            delayedLineStartPos = pos;
                            delayedLineColor = faceColor;
                            delayedLineYOffset = faceFont->FontSize;
                        }
                        break;
                    }

                    // Strikethrough
                    if (auto pos = faceDesc.strikethrough.pos;
                        chConsume("~~"sv, faceDesc.strikethrough))
                    {
                        // Closing specifier
                        if (!faceDesc.strikethrough.state) {
                            delayedLineStartPos = pos;
                            delayedLineColor = faceColor;
                            delayedLineYOffset = faceFont->FontSize / 2;
                        }
                        break;
                    }

                    // Iatlic
                    if (chConsume("*"sv, faceDesc.italic) ||
                        chConsume("_"sv, faceDesc.italic))
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
                auto glyph = faceFont->FindGlyph(*ch);
                if (!glyph) continue;
                if (!glyph->Visible) continue;

                ImVec2 pos0(textPos.x + glyph->X0, textPos.y + glyph->Y0);
                ImVec2 pos1(textPos.x + glyph->X1, textPos.y + glyph->Y1);

                ImVec2 uv0(glyph->U0, glyph->V0);
                ImVec2 uv1(glyph->U1, glyph->V1);

                ImU32 glyphColor = glyph->Colored ? (faceColor | ~IM_COL32_A_MASK) : faceColor;
                drawList->PrimRectUV(pos0, pos1, uv0, uv1, glyphColor);
#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
                drawList->AddRect(pos0, pos1, IM_COL32(0, 255, 255, 255));
#endif

                textPos.x += glyph->AdvanceX;
            }

            if (delayedUpdateFormat) {
                LocateFace(faceDesc, faceFont, faceColor);
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
