#include "BackingStore.hpp"
#include "Document.hpp"
#include "Utils.hpp"
#include "WidgetMisc.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <imgui.h>

#include <../res/bindings/imgui_impl_glfw.h>
#include <../res/bindings/imgui_impl_opengl3.h>
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

    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(20, 20) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bctx.bullet->pbid)) {
        return;
    }

    // Bullet has no children, no need for collapse/expand button
    if (bctx.bullet->children.empty()) {
        return;
    }

    // TODO button color
    ImGui::RenderArrow(window->DrawList, bb.GetCenter(), 0x000000, bctx.bullet->expanded ? ImGuiDir_Down : ImGuiDir_Right);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        bctx.bullet->expanded = !bctx.bullet->expanded;
    }
}

static void ShowBulletIcon(ShowContext& gctx, BulletContext& bctx) {
    auto window = ImGui::GetCurrentWindow();

    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + ImVec2(20, 20) };
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, bctx.bullet->pbid)) {
        return;
    }

    auto center = bb.GetCenter();

    // TODO better colors
    if (!bctx.bullet->expanded) {
        window->DrawList->AddCircle(center, 8.0f, ImGui::GetColorU32(ImGuiCol_WindowBg));
    }
    window->DrawList->AddCircle(center, 6.0f, ImGui::GetColorU32(ImGuiCol_Text));

    // TODO switch to ImGui::PushID()?
    char popupId[256];
    snprintf(popupId, sizeof(popupId), "BulletCtxMenu%zu", bctx.bullet->pbid);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // TODO zoom in
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        ImGui::OpenPopup(popupId);
    }

    if (ImGui::BeginPopup(popupId)) {
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
            if (ImGui::InputText("BulletContent", &bc.text)) {
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
        ShowBulletCollapseFlag(gctx, bctx);
        ImGui::SameLine();
        ShowBulletIcon(gctx, bctx);
        ImGui::SameLine();
        ShowBulletContent(gctx, bctx);
    }

    gctx.count += 1;

    if (bctx.bullet->expanded) {
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
    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

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
