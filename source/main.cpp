#include "BackingStore.hpp"
#include "Document.hpp"
#include "Utils.hpp"
#include "WidgetMisc.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <../res/bindings/imgui_impl_glfw.h>
#include <../res/bindings/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <imgui.h>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

static void GlfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

namespace Ionl {
class DocumentView {
private:
    Ionl::Document* mDocument;
    Ionl::Bullet* mCurrentBullet;

public:
    DocumentView(Ionl::Document& doc);

    Ionl::Document& GetDocument() { return *mDocument; }
    const Ionl::Document& GetDocument() const { return *mDocument; }
    Ionl::Bullet& GetCurrentBullet() { return *mCurrentBullet; }
    const Ionl::Bullet& GetCurrentBullet() const { return *mCurrentBullet; }

    void Show();

private:
    struct ShowContext;
    void ShowBullet(ShowContext& ctx, Ionl::Bullet& bullet);
};
} // namespace Ionl

Ionl::DocumentView::DocumentView(Ionl::Document& doc)
    : mDocument{ &doc }
    , mCurrentBullet{ &doc.GetRoot() } {
}

// TODO move to config file
constexpr int kConfMaxFetchCount = 100;
constexpr int kConfMaxFetchDepth = 6;

struct Ionl::DocumentView::ShowContext {
    size_t i = 0;
};

void Ionl::DocumentView::Show() {
    ShowContext ctx;

    for (Pbid childPbid : mCurrentBullet->children) {
        auto& child = mDocument->FetchBulletByPbid(childPbid);
        ShowBullet(ctx, child);
    }
    if (ImGui::Button("+")) {
        auto& child = mDocument->CreateBullet();
        mDocument->ReparentBullet(child, *mCurrentBullet, mCurrentBullet->children.size());
    }
}

void Ionl::DocumentView::ShowBullet(ShowContext& ctx, Bullet& bullet) {
    ImGui::PushID(ctx.i);
    ImGui::Bullet();
    ImGui::SameLine();
    std::visit(
        Overloaded{
            [&](BulletContentTextual& bc) {
                if (ImGui::InputText("BulletContent", &bc.text)) {
                    bullet.document->UpdateBulletContent(bullet);
                }
                // TODO this can cause segfault if reparent happens to cause std::vector to reallocate (parent is still iterating)
                if (ImGui::Button("First")) {
                    auto& parent = mDocument->FetchBulletByPbid(bullet.parentPbid);
                    mDocument->ReparentBullet(bullet, parent, 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Back")) {
                    auto& parent = mDocument->FetchBulletByPbid(bullet.parentPbid);
                    mDocument->ReparentBullet(bullet, parent, parent.children.size() - 1);
                }
                ImGui::SameLine();
                if (ImGui::Button("+1")) {
                    auto& parent = mDocument->FetchBulletByPbid(bullet.parentPbid);
                    auto idx = std::find(parent.children.begin(), parent.children.end(), bullet.pbid) - parent.children.begin();
                    if (idx != parent.children.size() - 1) {
                        mDocument->ReparentBullet(bullet, parent, idx + 1);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("-1")) {
                    auto& parent = mDocument->FetchBulletByPbid(bullet.parentPbid);
                    auto idx = std::find(parent.children.begin(), parent.children.end(), bullet.pbid) - parent.children.begin();
                    if (idx != 0) {
                        mDocument->ReparentBullet(bullet, parent, idx - 1);
                    }
                }
            },
            [&](BulletContentMirror& bc) {
                // TODO
            },
        },
        bullet.content.v);

    ImGui::Indent();
    for (Pbid childPbid : bullet.children) {
        auto& child = mDocument->FetchBulletByPbid(childPbid);
        ShowBullet(ctx, child);
    }
    if (ImGui::Button("+")) {
        auto& child = mDocument->CreateBullet();
        mDocument->ReparentBullet(child, bullet, bullet.children.size());
    }
    ImGui::Unindent();

    ImGui::PopID();
    ++ctx.i;
}

struct AppView {
    Ionl::DocumentView view;
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
            .view = Ionl::DocumentView(document),
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
