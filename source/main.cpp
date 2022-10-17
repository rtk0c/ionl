#include "BackingStore.hpp"
#include "Document.hpp"
#include "Utils.hpp"
#include "WidgetMisc.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <glad/glad.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace Ionl;

static void GlfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

class DocumentView {
private:
    Document* mDocument;
    Bullet* mCurrentBullet;

public:
    DocumentView(Document& doc);

    Document& GetDocument() { return *mDocument; }
    const Document& GetDocument() const { return *mDocument; }
    Bullet& GetCurrentBullet() { return *mCurrentBullet; }
    const Bullet& GetCurrentBullet() const { return *mCurrentBullet; }

    void Show();
};

DocumentView::DocumentView(Document& doc)
    : mDocument{ &doc }
    , mCurrentBullet{ &doc.GetRoot() } {
}

// TODO move to config file
constexpr int kConfMaxFetchCount = 100;
constexpr int kConfMaxFetchDepth = 6;

struct ShowContext {
    Document* document;
    Bullet* rootBullet;
    int depth = 0;
    int count = 0;
};

struct BulletContext {
    // Filled in by creator
    Bullet* bullet;

    // Generated when calling Init()
    BulletType bulletType;
    ImGuiID id;

    void Init() {
        this->bulletType = bullet->content.GetType();
        this->id = ImGui::GetCurrentWindow()->GetID(bullet->pbid);
    }
};

enum class BulletAction {
    OpenCtxMenu,
};

static void ShowBulletCollapseFlag(ShowContext& gctx, BulletContext& bctx) {
    auto window = ImGui::GetCurrentWindow();

    float fontSize = ImGui::GetCurrentContext()->FontSize;
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(fontSize * 0.8f, fontSize) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bctx.bullet->pbid)) {
        return;
    }

    // Bullet has no children, no need for collapse/expand button
    if (bctx.bullet->children.empty()) {
        return;
    }

    // TODO button color
    // TODO highlight on hover

    ImVec2 center = bb.GetCenter();
    ImVec2 a, b, c;
    float r = fontSize * 0.3f;
    if (bctx.bullet->expanded) {
        // Down
        a = ImVec2(+0.000f, +0.750f) * r;
        b = ImVec2(-0.866f, -0.750f) * r;
        c = ImVec2(+0.866f, -0.750f) * r;
    } else {
        // Right
        a = ImVec2(+0.750f, +0.000f) * r;
        b = ImVec2(-0.750f, +0.866f) * r;
        c = ImVec2(-0.750f, -0.866f) * r;
    }
    window->DrawList->AddTriangleFilled(center + a, center + b, center + c, ImGui::GetColorU32(ImGuiCol_Text));
#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        bctx.bullet->expanded = !bctx.bullet->expanded;
    }
}

static void ShowBulletIcon(ShowContext& gctx, BulletContext& bctx) {
    auto window = ImGui::GetCurrentWindow();

    float fontSize = ImGui::GetCurrentContext()->FontSize;
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(fontSize * 0.8f, fontSize) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bctx.bullet->pbid)) {
        return;
    }

    auto center = bb.GetCenter();

    // TODO better colors
    if (!bctx.bullet->expanded) {
        window->DrawList->AddCircleFilled(center, fontSize * 0.35f, ImGui::GetColorU32(ImGuiCol_TabActive));
    }
    window->DrawList->AddCircleFilled(center, fontSize * 0.2f, ImGui::GetColorU32(ImGuiCol_Text));
#ifdef IONL_DRAW_DEBUG_BOUNDING_BOXES
    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // TODO zoom in
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        ImGui::OpenPopup(bctx.id);
    }

    if (ImGui::BeginPopupEx(bctx.id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
        // TODO implement key combos
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            // TODO
        }
        if (ImGui::MenuItem("Cut", "Ctrl+X")) {
            // TODO
        }
        if (ImGui::MenuItem("Delete", "Backspace")) {
            // TODO
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy internal link")) {
            // TODO
        }
        if (ImGui::MenuItem("Copy mirror link")) {
            // TODO
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Expand all")) {
            // TODO
        }
        if (ImGui::MenuItem("Collpase all")) {
            // TODO
        }
        ImGui::Separator();
        // TODO show creation time and modify time
        ImGui::Text("Created on UNIMPLEMENTED");
        ImGui::Text("Last changed on UNIMPLEMENTED");
        ImGui::EndPopup();
    }

    // TODO trigger this with left click drag
    if (ImGui::BeginDragDropSource()) {
        // Reason: intentionally using pointer as payload
        ImGui::SetDragDropPayload("Ionl::Bullet", bctx.bullet, sizeof(bctx.bullet)); // NOLINT(bugprone-sizeof-expression)
        ImGui::EndDragDropSource();
    }
}

static void ShowBulletContent(ShowContext& gctx, BulletContext& bctx) {
    // TODO replace with TextEdit
    auto& bullet = *bctx.bullet;
    ImGui::PushID(bctx.id);
    ::VisitVariantOverloaded(
        bullet.content.v,
        [&](BulletContentTextual& bc) {
            if (ImGui::InputText("##BulletContent", &bc.text)) {
                bullet.document->UpdateBulletContent(bullet);
            }
        },
        [&](BulletContentMirror& bc) {
            // TODO
        });
    ImGui::PopID();
}

static void ShowBullet(ShowContext& gctx, BulletContext& bctx) {
    bool withinCountLimit = gctx.count < kConfMaxFetchCount;
    if (!withinCountLimit) {
        // TODO recycler view instead of just limiting the number of bullets to render
        return;
    }

    if (gctx.rootBullet == bctx.bullet) {
        // TODO show "title"
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        ShowBulletCollapseFlag(gctx, bctx);
        ImGui::SameLine();
        ShowBulletIcon(gctx, bctx);

        ImGui::PopStyleVar();

        ImGui::SameLine();
        ShowBulletContent(gctx, bctx);
    }

    gctx.count += 1;

    if (!bctx.bullet->expanded) {
        return;
    }
    bool withinDepthLimit = gctx.depth < kConfMaxFetchDepth;
    if (withinDepthLimit) {
        ImGui::Indent();
        gctx.depth += 1;
        for (Pbid childPbid : bctx.bullet->children) {
            BulletContext childBctx;
            childBctx.bullet = &gctx.document->FetchBulletByPbid(childPbid);
            childBctx.Init();

            ShowBullet(gctx, childBctx);
        }
        gctx.depth -= 1;
        ImGui::Unindent();
    } else {
        ImGui::Indent();
        // TODO show ellipses
        ImGui::Unindent();
    }
}

void DocumentView::Show() {
    ShowContext gctx;
    gctx.document = mDocument;
    gctx.rootBullet = mCurrentBullet;

    BulletContext bctx;
    bctx.bullet = mCurrentBullet;
    bctx.Init();

    ShowBullet(gctx, bctx);

    auto dragDropPayland = ImGui::GetDragDropPayload();
    if (dragDropPayland &&
        std::strcmp(dragDropPayland->DataType, "Ionl::Bullet"))
    {
        // TODO
    }
}

struct AppView {
    DocumentView view;
    bool windowOpen = true;
};

struct AppState {
    Ionl::SQLiteBackingStore storeActual;
    Ionl::WriteDelayedBackingStore storeFacade;
    Ionl::Document document;
    std::vector<AppView> views;

    AppState()
        : storeActual("./notebook.sqlite3")
        , storeFacade(storeActual)
        , document(storeActual) //
    {
        views.push_back(AppView{
            .view = DocumentView(document),
            .windowOpen = true,
        });
    }
};

static const std::string& ResolveContentToText(Ionl::Document& document, const Ionl::BulletContent& content) {
    if (auto bc = std::get_if<Ionl::BulletContentTextual>(&content.v)) {
        return bc->text;
    } else if (auto bc = std::get_if<Ionl::BulletContentMirror>(&content.v)) {
        auto& that = document.FetchBulletByPbid(bc->referee);
        return ResolveContentToText(document, that.content);
    } else {
        assert(false);
    }
}

#include "WidgetTextEdit.hpp"

static void ShowAppViews(AppState& as) {
    for (size_t i = 0; i < as.views.size(); ++i) {
        auto& dv = as.views[i];
        auto& currBullet = dv.view.GetCurrentBullet();

        // TODO handle unicode truncation gracefully
        char windowName[256];
        if (currBullet.IsRootBullet()) {
            snprintf(windowName, sizeof(windowName), "Infinite Outliner###DocView%zu", i);
        } else {
            auto& text = ResolveContentToText(as.document, currBullet.content);
            if (text.empty()) {
                snprintf(windowName, sizeof(windowName), "(Empty)###DocView%zu", i);
            } else if (text.size() > 10) {
                snprintf(windowName, sizeof(windowName), "%*.s...###DocView%zu", 10, text.c_str(), i);
            } else {
                snprintf(windowName, sizeof(windowName), "%s###DocView%zu", text.c_str(), i);
            }
        }

        ImGui::Begin(windowName, &dv.windowOpen);
        dv.view.Show();
        ImGui::End();
    }

    ImGui::Begin("TextEdit test");
    using namespace std::string_view_literals;
    static TextBuffer buffer("# Heading 1\n## Heading 2\nTest **bold** _italics_ __underline__ ~~strikethrough~~\n`monospace`\n**`formatted`_`monospace`_**\n`hello`__`more`~~`more`~~__\n"sv);
    static TextEdit textEdit = []() {
        TextEdit res;
        res.id = ImGui::GetID("test");
        res.buffer = &buffer;
        return res;
    }();
    textEdit.Show();
    ImGui::End();
}

int main() {
    if (!glfwInit()) {
        return -1;
    }

    glfwSetErrorCallback(&GlfwErrorCallback);

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Infinite Outliner", nullptr, nullptr);
    if (window == nullptr) {
        return -2;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return -3;
    }

    IMGUI_CHECKVERSION();
    auto& ctx = *ImGui::CreateContext();
    auto& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // TODO proper discovery code
    float fontSize = 18.0f;
    gTextStyles.faceFonts[MF_Proportional] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_ProportionalItalic] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-Italic.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_ProportionalBold] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-Bold.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_ProportionalBoldItalic] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-BoldItalic.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_Monospace] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationMono-Regular.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_MonospaceItalic] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationMono-Italic.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_MonospaceBold] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationMono-Bold.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.faceFonts[MF_MonospaceBoldItalic] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationMono-BoldItalic.ttf", fontSize, nullptr, io.Fonts->GetGlyphRangesDefault());
    gTextStyles.regularFontSize = fontSize;

    gTextStyles.faceColors[MF_Proportional] = IM_COL32(255, 255, 255, 255);
    gTextStyles.faceColors[MF_ProportionalItalic] = IM_COL32(255, 255, 255, 255);
    gTextStyles.faceColors[MF_ProportionalBold] = IM_COL32(255, 255, 255, 255);
    gTextStyles.faceColors[MF_ProportionalBoldItalic] = IM_COL32(255, 255, 255, 255);
    gTextStyles.faceColors[MF_Monospace] = IM_COL32(176, 215, 221, 255);
    gTextStyles.faceColors[MF_MonospaceItalic] = IM_COL32(176, 215, 221, 255);
    gTextStyles.faceColors[MF_MonospaceBold] = IM_COL32(176, 215, 221, 255);
    gTextStyles.faceColors[MF_MonospaceBoldItalic] = IM_COL32(176, 215, 221, 255);

    auto InitHeadingFont = [&](MarkdownFace variant, float scale) {
        gTextStyles.faceFonts[variant] = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-Bold.ttf", fontSize * scale, nullptr, io.Fonts->GetGlyphRangesDefault());
        gTextStyles.faceColors[variant] = gTextStyles.faceColors[MF_Proportional];
    };
    InitHeadingFont(MF_Heading1, 2.5f);
    InitHeadingFont(MF_Heading2, 2.0f);
    InitHeadingFont(MF_Heading3, 1.5f);
    InitHeadingFont(MF_Heading4, 1.2f);
    InitHeadingFont(MF_Heading5, 1.0f);

    AppState as;
    double lastWriteTime = 0.0;
    double lastIdleTime = 0.0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        double currTime = glfwGetTime();
        // "ufops" stands for UnFlushed OPerationS
        auto ufopsCntBeforeFrame = as.storeFacade.GetUnflushedOpsCount();

        ShowAppViews(as);
        ImGui::ShowDemoWindow();
        auto ufopsCntAfterFrame = as.storeFacade.GetUnflushedOpsCount();

        ImGui::Render();
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
        ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClearColor(clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        if (ufopsCntBeforeFrame != ufopsCntAfterFrame) {
            lastIdleTime = currTime;
        }

        if (ufopsCntAfterFrame > 0) {
            // Save strategy:
            if ((currTime - lastIdleTime) > /*seconds*/ 1.0 || // ... after 1 second of idle
                (currTime - lastWriteTime) > /*seconds*/ 10.0) // ... or every 10 seconds
            {
                lastWriteTime = currTime;
                as.storeFacade.FlushOps();
            }
        }
    }

    if (as.storeFacade.GetUnflushedOpsCount() > 0) {
        as.storeFacade.FlushOps();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
