#pragma once

#include "Document.hpp"

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
};

} // namespace Ionl

class App {
private:
    Ionl::Document document;

    struct View;
    std::vector<View> views;

public:
    App();
    ~App();

    void Show();
};
