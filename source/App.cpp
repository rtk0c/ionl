#include "App.hpp"

#include "Utils.hpp"
#include "WidgetMisc.hpp"

#include <imgui.h>
#include <cassert>
#include <cstdlib>

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

void App::Show() {
    for (size_t i = 0; i < views.size(); ++i) {
        auto& dv = views[i];
        auto& currBullet = dv.view.GetCurrentBullet();

        // TODO handle unicode truncation gracefully
        char windowName[256];
        if (currBullet.IsRootBullet()) {
            snprintf(windowName, sizeof(windowName), "Infinite Outliner###DocView%zu", i);
        } else {
            auto& text = ResolveContentToText(document, currBullet.content);
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
