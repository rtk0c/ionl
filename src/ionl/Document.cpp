#include "Document.hpp"

#include <ionl/BackingStore.hpp>
#include <ionl/Macros.hpp>
#include <ionl/Utils.hpp>

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

void Ionl::Document::ReparentBullet(Bullet& bullet, Bullet& newParent, size_t index) {
    // Update database
    // TODO simplify this convoluted logic, maybe replace PositionAfterThing logic with PositionReplace?
    if (index == 0) {
        mStore->SetBulletPositionAtBeginning(bullet.pbid, newParent.pbid);
    } else {
        size_t relativePbid;

        if (bullet.parentPbid == newParent.pbid) {
            auto oldIndex = std::find(newParent.children.begin(), newParent.children.end(), bullet.pbid) - newParent.children.begin();
            if (index > oldIndex) {
                relativePbid = newParent.children[index];
                goto doUpdate;
            } else if (index == oldIndex) {
                // Fast path to noop
                return;
            }
        }

        // - If `newParent.children` is empty, then by contract `index` must be 0 (appending at end position), which is catched by the above case
        // - Otherwise, `index` must be non-zero in this else clause (again 0 is catched by the above case)
        //   therefore, `index - 1` is always valid
        relativePbid = newParent.children[index - 1];

    doUpdate:
        mStore->SetBulletPositionAfter(bullet.pbid, newParent.pbid, relativePbid);
    }

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
