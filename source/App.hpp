#pragma once

#include "BackingStore.hpp"
#include "Document.hpp"

#include <memory>
#include <vector>

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

class App {
private:
    Ionl::SQLiteBackingStore store;
    Ionl::Document document;

    struct View;
    std::vector<View> views;

public:
    App();
    ~App();

    void Show();
};
