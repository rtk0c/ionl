#include "Document.hpp"

#include "BackingStore.hpp"
#include "Macros.hpp"
#include "Utils.hpp"

#include <cassert>
#include <string_view>

using namespace std::literals;

Ionl::BulletType Ionl::BulletContent::GetType() const {
    return ::VisitVariantOverloaded(
        v,
        [](const BulletContentTextual&) { return BulletType::Textual; },
        [](const BulletContentMirror&) { return BulletType::Mirror; });
}

bool Ionl::Bullet::IsRootBullet() const {
    return pbid == kRootBulletPbid;
}

Ionl::Document::Document(IBackingStore& store)
    : mStore{ &store } //
{
    // Always load the root bullet
    auto& root = FetchBulletByPbid(kRootBulletPbid);
    assert(root.pbid == kRootBulletPbid);
    assert(root.rbid == kRootBulletRbid);
}

const Ionl::Bullet& Ionl::Document::GetRoot() const {
    return mBullets[0].value();
}

Ionl::Bullet& Ionl::Document::GetRoot() {
    return const_cast<Bullet&>(const_cast<const Document*>(this)->GetRoot());
}

Ionl::Bullet* Ionl::Document::GetBulletByRbid(Rbid rbid) {
    if (rbid >= mBullets.size()) {
        return nullptr;
    }

    auto& ob = mBullets[rbid];
    if (!ob.has_value()) {
        return nullptr;
    }

    return &ob.value();
}

Ionl::Bullet* Ionl::Document::GetBulletByPbid(Pbid pbid) {
    auto iter = mPtoRmap.find(pbid);
    if (iter != mPtoRmap.end()) {
        return GetBulletByRbid(iter->second);
    }

    return nullptr;
}

Ionl::Bullet& Ionl::Document::FetchBulletByPbid(Pbid pbid) {
    auto iter = mPtoRmap.find(pbid);
    if (iter != mPtoRmap.end()) {
        return *GetBulletByRbid(iter->second);
    }

    return *Store(mStore->FetchBullet(pbid));
}

Ionl::Bullet& Ionl::Document::CreateBullet() {
    auto pbid = mStore->InsertEmptyBullet();
    auto& bullet = *Store(mStore->FetchBullet(pbid));
    return bullet;
}

void Ionl::Document::DeleteBullet(Bullet& bullet) {
    mStore->DeleteBullet(bullet.pbid);
    mPtoRmap.erase(bullet.pbid);
    mFreeRbids.push_back(bullet.rbid);
    // Do this last, this invalidates `bullet`
    mBullets[bullet.rbid].reset();
}

void Ionl::Document::UpdateBulletContent(Bullet& bullet) {
    mStore->SetBulletContent(bullet.pbid, bullet.content);
}

void Ionl::Document::ReparentBullet(Bullet& bullet, Bullet& newParent, int index) {
    // Update database
    mStore->SetBulletPosition(bullet.pbid, newParent.pbid, index);

    // Update in-memory objects
    if (auto oldParent = GetBulletByPbid(bullet.parentPbid)) {
        auto pos = std::find(oldParent->children.begin(), oldParent->children.end(), bullet.pbid);
        if (pos != oldParent->children.end()) {
            oldParent->children.erase(pos);
        }
    }
    bullet.parentPbid = newParent.pbid;
    {
        auto pos = newParent.children.begin() + index;
        newParent.children.insert(pos, bullet.pbid);
    }
}

Ionl::Bullet* Ionl::Document::Store(Bullet bullet) {
    Bullet* result;

    // Put into storage
    if (mFreeRbids.empty()) {
        mBullets.push_back(std::move(bullet));

        result = &mBullets.back().value();
        result->rbid = mBullets.size() - 1;
    } else {
        size_t rbid = bullet.rbid = mFreeRbids.back();
        mFreeRbids.pop_back();
        mBullets[rbid] = std::move(bullet);

        result = &mBullets[rbid].value();
    }

    // Update pbid->rbid mapping
    mPtoRmap.try_emplace(result->pbid, result->rbid);

    result->document = this;
    return result;
}
