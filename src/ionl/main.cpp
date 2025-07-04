#include "ionl/markdown.hpp"
#include <ionl/backing_store.hpp>
#include <ionl/config.hpp>
#include <ionl/document.hpp>
#include <ionl/utils.hpp>
#include <ionl/widget_misc.hpp>
#include <ionl/widget_text_edit.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_stdlib.h>

// A note on OpenGL functions:
//
// We only use the functions present in OpenGL 1.1 in this file (like glClear and glViewport) to setup the window, thus we COULD be using system headers dragged in glfw3.h.
// But since they are concrete function definitions, we'd still need to link to the dll of each platform -- by using find_package(OpenGL) + target_link_libraries() in CMake which is quite a bit more stuff to add to the buildsystem.
// As such we just use imgui_impl_opengl3_loader.h instead of the system headers.
//
// This is _technically_ not supported per comments in imgui_impl_opengl3_loader.h, but since we're explicitly not using other loaders + vendoring imgui anyways, it's fine for our purposes.
#include <imgui/imgui_impl_opengl3_loader.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>
#include <stdexcept>
#include <string>

using namespace std::literals;
using namespace Ionl;
namespace fs = std::filesystem;

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

enum class BulletAction {
    OpenCtxMenu,
};

static void ShowBulletCollapseFlag(ShowContext& gctx, Bullet& bullet, ImGuiID id) {
    auto window = ImGui::GetCurrentWindow();

    float fontSize = ImGui::GetCurrentContext()->FontSize;
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(fontSize * 0.8f, fontSize) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bullet.pbid)) {
        return;
    }

    // Bullet has no children, no need for collapse/expand button
    if (bullet.children.empty()) {
        return;
    }

    // TODO button color
    // TODO highlight on hover

    ImVec2 center = bb.GetCenter();
    ImVec2 a, b, c;
    float r = fontSize * 0.3f;
    if (bullet.expanded) {
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
#if IONL_DEBUG_FEATURES
    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        bullet.expanded = !bullet.expanded;
    }
}

static void ShowBulletIcon(ShowContext& gctx, Bullet& bullet, ImGuiID id) {
    auto window = ImGui::GetCurrentWindow();

    float fontSize = ImGui::GetCurrentContext()->FontSize;
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(fontSize * 0.8f, fontSize) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bullet.pbid)) {
        return;
    }

    auto center = bb.GetCenter();

    // TODO better colors
    if (!bullet.expanded) {
        window->DrawList->AddCircleFilled(center, fontSize * 0.35f, ImGui::GetColorU32(ImGuiCol_TabActive));
    }
    window->DrawList->AddCircleFilled(center, fontSize * 0.2f, ImGui::GetColorU32(ImGuiCol_Text));
#if IONL_DEBUG_FEATURES
    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
#endif

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // TODO zoom in
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        ImGui::OpenPopup(id);
    }

    if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
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
        ImGui::SetDragDropPayload("Ionl::Bullet", &bullet, sizeof(bullet)); // NOLINT(bugprone-sizeof-expression)
        ImGui::EndDragDropSource();
    }
}

static void ShowBulletContent(ShowContext& gctx, Bullet& bullet, ImGuiID id) {
    // TODO replace with TextEdit
    ImGui::PushID(id);
    ::VisitVariantOverloaded(
        bullet.content.v,
        [&](BulletContentTextual& bc) {
            // if (ImGui::InputText("##BulletContent", &bc.text)) {
            //     bullet.document->UpdateBulletContent(bullet);
            // }
        },
        [&](BulletContentMirror& bc) {
            // TODO
        });
    ImGui::PopID();
}

static void ShowBullet(ShowContext& gctx, Bullet& bullet, ImGuiID id) {
    bool withinCountLimit = gctx.count < kConfMaxFetchCount;
    if (!withinCountLimit) {
        // TODO recycler view instead of just limiting the number of bullets to render
        return;
    }

    if (gctx.rootBullet == &bullet) {
        // TODO show "title"
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        ShowBulletCollapseFlag(gctx, bullet, id);
        ImGui::SameLine();
        ShowBulletIcon(gctx, bullet, id);

        ImGui::PopStyleVar();

        ImGui::SameLine();
        ShowBulletContent(gctx, bullet, id);
    }

    gctx.count += 1;

    if (!bullet.expanded) {
        return;
    }
    bool withinDepthLimit = gctx.depth < kConfMaxFetchDepth;
    if (withinDepthLimit) {
        ImGui::Indent();
        gctx.depth += 1;
        for (Pbid childPbid : bullet.children) {
            Bullet& bullet = gctx.document->FetchBulletByPbid(childPbid);
            ImGuiID id = ImGui::GetCurrentWindow()->GetID(bullet.pbid);
            ShowBullet(gctx, bullet, id);
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

    // TODO better ID
    ShowBullet(gctx, *mCurrentBullet, ImGui::GetID("Ionl Document"));

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
        // TODO
    } else if (auto bc = std::get_if<Ionl::BulletContentMirror>(&content.v)) {
        auto& that = document.FetchBulletByPbid(bc->referee);
        return ResolveContentToText(document, that.content);
    }
    throw std::runtime_error("");
}

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

#if IONL_DEBUG_FEATURES
    ImGui::Begin("TextEdit debug example");
    {
        constexpr std::string_view kExampleText = R"""(
# Heading 1
## Heading 2 -- and this is a long heading, likely to wrap
__This is an extremely long text line with underline. A long long time ago, when the people at the var end of the world still spoke and began their stories with 'in the near future', there was...__
Test **bold** _italics_ __underline__ ~~strikethrough~~
`monospace`
`monospace containing *potential* formatting should be ignored`
**`formatted`_`monospace`_**
`hello`__`more`~~`more`~~__
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Fusce nulla nibh, dictum id enim at, laoreet mattis lacus. Nullam porta justo lorem. Quisque commodo massa at lacus bibendum, sed porttitor quam vestibulum. Integer ultricies diam lectus, in aliquam est rutrum eu. Sed eu purus leo. Maecenas non massa ultricies, volutpat mi vitae, aliquam ante. Fusce tristique, massa nec consectetur sagittis, neque ipsum pulvinar ligula, vitae condimentum nulla mi nec leo. Nullam sit amet rutrum justo, vel porttitor ipsum. Vestibulum _id viverra mauris. Quisque eu porta orci, eget rhoncus nibh. Cras laoreet, odio vestibulum lobortis mattis, lectus nunc accumsan lorem, quis sollicitudin nisi augue ut tortor. Mauris feugiat vehicula augue ac condimentum. Proin tincidunt condimentum nunc eu aliquam. Duis in sapien sem. Pellentesque pellentesque risus ac luctus auctor.
```cpp
// code block
#include <iostream>
int main() {
    std::cout << "Hello, world\n";
    return 0;
}
```
)"""sv.substr(1); // Remove initial \n

        static auto textBuffer = TextBuffer(GapBuffer(kExampleText));
        static auto textEdit = TextEdit(ImGui::GetID("TextEdit"), textBuffer);

        textEdit.Show();
    }
    ImGui::End();
#endif
}

int main() {
    LoadConfigFromFile(gConfig, fs::path("./config.toml"));

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

    IMGUI_CHECKVERSION();
    auto& ctx = *ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Enable the default font for debugging overlays
    // Keep this always, as a backup in case the user supplied font is broken
    io.Fonts->AddFontDefault();

    gMarkdownStylesheet.linePadding = 0.0f;
    gMarkdownStylesheet.paragraphPadding = 4.0f;

    auto setupMdFont = [&](const std::string& fontPath, ImColor fontColor, bool isMonospace, bool isBold, bool isItalic) {
        ImFont* imFont = fontPath.empty()
            ? io.Fonts->Fonts[0]
            : io.Fonts->AddFontFromFileTTF(fontPath.c_str(), gConfig.baseFontSize);
        MarkdownFace face{ imFont, fontColor };
        gMarkdownStylesheet.SetRegularFace(face, isMonospace, isBold, isItalic);
    };
    setupMdFont(gConfig.regularFont, 0, false, false, false);
    setupMdFont(gConfig.italicFont, 0, false, false, true);
    setupMdFont(gConfig.boldFont, 0, false, true, false);
    setupMdFont(gConfig.boldItalicFont, 0, false, true, true);
    setupMdFont(gConfig.monospaceRegularFont, IM_COL32(176, 215, 221, 255), true, false, false);
    setupMdFont(gConfig.monospaceItalicFont, IM_COL32(176, 215, 221, 255), true, false, true);
    setupMdFont(gConfig.monospaceBoldFont, IM_COL32(176, 215, 221, 255), true, true, false);
    setupMdFont(gConfig.monospaceBoldItalicFont, IM_COL32(176, 215, 221, 255), true, true, true);

    for (int i = 0; i < kNumTitleLevels; ++i) {
        int headingLevel = i + 1;
        float scale = gConfig.headingFontScales[i];
        ImFont* font = gConfig.headingFont.empty()
            ? io.Fonts->Fonts[0]
            : io.Fonts->AddFontFromFileTTF(gConfig.headingFont.c_str(), gConfig.baseFontSize * scale);
        gMarkdownStylesheet.SetHeadingFace(MarkdownFace{ font, 0 }, headingLevel);
    }

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
