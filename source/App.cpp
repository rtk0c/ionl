#include "App.hpp"

#include "Utils.hpp"

#include <imgui.h>
#include <cstdlib>

Ionl::DocumentView::DocumentView(Ionl::Document& doc)
    : mDocument{ &doc }
    , mCurrentBullet{ &doc.GetRoot() } {
}

void Ionl::DocumentView::Show() {
}

struct App::View {
    Ionl::DocumentView view;
    bool windowOpen = true;
};

App::App() {
    views.push_back(View{
        .view = Ionl::DocumentView(document),
        .windowOpen = true,
    });
}

App::~App() = default;

void App::Show() {
    for (size_t i = 0; i < views.size(); ++i) {
        auto& dv = views[i];
        auto& currBullet = dv.view.GetCurrentBullet();

        // TODO handle unicode truncation gracefully
        char windowName[256];
        const std::string* text;
        std::visit(
            Overloaded{
                [&](const Ionl::BulletContentSimple& bc) {
                    text = &bc.text;
                },
                [&](const Ionl::BulletContentReference& bc) {
                    text = &bc.text;
                },
            },
            currBullet.content.v);
        if (text->empty()) {
            snprintf(windowName, sizeof(windowName), "(Empty)###DocView%zu", i);
        } else if (text->size() > 10) {
            snprintf(windowName, sizeof(windowName), "%*.s...###DocView%zu", 10, text->c_str(), i);
        } else {
            snprintf(windowName, sizeof(windowName), "%s###DocView%zu", text->c_str(), i);
        }

        ImGui::Begin(windowName, &dv.windowOpen);
        dv.view.Show();
        ImGui::End();
    }
}
