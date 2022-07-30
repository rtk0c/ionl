#pragma once

#include <robin_hood.h>
#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Ionl {

/// Persistent bullet ID (saved to database)
/// This is currently the rowid in SQLite
using Pbid = size_t;
/// Runtime bullet ID (transient)
using Rbid = size_t;

// A `Bullet` object with pbid and rbid both as 0 is automatically created on startup, and saved to the
// database (if not exists) as the root bullet. This bullet may not be deleted.

/// NOTE: do not change these values, they are a part of the on-disk format
enum class BulletType {
    Simple = 1,
    Reference = 2,
};

struct BulletContentSimple {
    std::string text;
};

struct BulletContentReference {
    std::string text;
    Pbid referee;
};

struct BulletContent {
    std::variant<
        BulletContentSimple,
        BulletContentReference>
        v;

    BulletType GetType() const;
};

struct Bullet {
    Pbid pbid;
    Rbid rbid;
    Pbid parentPbid;
    std::chrono::time_point<std::chrono::system_clock> creationTime;
    std::chrono::time_point<std::chrono::system_clock> modifyTime;
    BulletContent content;
    std::vector<Pbid> children;
    bool expanded = true;
};

class Document {
private:
    class BackingDataStore;
    BackingDataStore* mStore;

    std::deque<std::optional<Bullet>> mBullets; // Index by bullet's rbid
    std::vector<size_t> mFreeRbids;
    robin_hood::unordered_flat_map<Pbid, Rbid> mPtoRmap;

public:
    Document();
    ~Document();

    Bullet& GetRoot();
    const Bullet& GetRoot() const;

    Bullet* FetchBulletByRbid(Rbid rbid);
    Bullet& FetchBulletByPbid(Pbid pbid);
};

} // namespace Ionl
