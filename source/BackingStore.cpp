#include "BackingStore.hpp"

#include "Document.hpp"
#include "Macros.hpp"
#include "SQLiteHelper.hpp"
#include "Utils.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std::literals;
using namespace Ionl;

class SQLiteBackingStore::Private {
public:
    SQLiteDatabase database;
    SQLiteStatement beginTransaction;
    SQLiteStatement commitTransaction;
    SQLiteStatement rollbackTransaction;
    SQLiteStatement getBulletContent;
    SQLiteStatement getBulletParent;
    SQLiteStatement getBulletChildren;
    SQLiteStatement insertBullet;
    SQLiteStatement deleteBullet;
    SQLiteStatement setBulletContent;
    SQLiteStatement setBulletPosition;

public:
    void SetDatabaseUserVersion() {
        sqlite3_exec(database, "PRAGMA user_version = " STRINGIFY(CURRENT_DATABASE_VERSION), nullptr, nullptr, nullptr);
    }

    void SetDatabaseOptions() {
        sqlite3_exec(database, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr);
    }

    void InitializeTables() {
        char* errMsg = nullptr;
        // clang-format off
        int result = sqlite3_exec(database, R"""(
BEGIN TRANSACTION;
CREATE TABLE Config(
    Key TEXT PRIMARY KEY,
    Value,
    UNIQUE (Key)
);

CREATE TABLE Bullets(
    Pbid INTEGER PRIMARY KEY,
    ParentPbid INTEGER REFERENCES Bullets(Pbid),
    ParentSorting INTEGER,
    CreationTime DATETIME,
    ModifyTime DATETIME,
    -- enum BulletType
    ContentType INTEGER,
    -- If BulletType::Simple, this is TEXT
    -- If BulletType::Reference, this is INTEGER REFERENCES Bullet(Pbid)
    ContentValue
);

CREATE UNIQUE INDEX Idx_Bullets_ParentChild
ON Bullets(ParentPbid, ParentSorting);

CREATE INDEX Idx_Bullets_CreationTime
ON Bullets(CreationTime);

CREATE INDEX Idx_Bullets_ModifyTime
ON Bullets(ModifyTime);
)"""
// Root bullet
// NOTE: all of other fields are left NULL because they are irrelevant
"INSERT INTO BULLETS(Pbid)"
"VALUES (" STRINGIFY(ROOT_BULLET_PBID) ")"
";"
"COMMIT TRANSACTION;",
            nullptr,
            nullptr,
            &errMsg);
        // clang-format on
        assert(result == SQLITE_OK);
    }
};

SQLiteBackingStore::SQLiteBackingStore(const char* dbPath)
    : m{ new Private() } //
{
    int reuslt = sqlite3_open(dbPath, &m->database);
    if (reuslt != SQLITE_OK) {
        std::string msg;
        msg += "Failed to open SQLite3 database, error message:\n";
        msg += sqlite3_errmsg(m->database);
        throw std::runtime_error(msg);
    }

    // NOTE: These pragmas are not persistent, so we need to set them every time
    // As of SQLite3 3.38.5, it defaults to foreign_keys = OFF, so we need this to be on for ON DELETE CASCADE and etc. to work
    sqlite3_exec(m->database, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);

    {
        SQLiteStatement readVersionStmt;
        readVersionStmt.InitializeLazily(m->database, "PRAGMA user_version"sv);

        int result = sqlite3_step(readVersionStmt);
        assert(result == SQLITE_ROW);
        int currentDatabaseVersion = sqlite3_column_int(readVersionStmt, 0);

        result = sqlite3_step(readVersionStmt);
        assert(result == SQLITE_DONE);

        if (currentDatabaseVersion == 0) {
            // Newly created database, initialize it
            m->SetDatabaseUserVersion();
            m->SetDatabaseOptions();
            m->InitializeTables();
        } else if (currentDatabaseVersion == CURRENT_DATABASE_VERSION) {
            // Same version, no need to do anything
        } else {
            // TODO automatic migration?
            std::string msg;
            msg += "Incompatbile database versions ";
            msg += currentDatabaseVersion;
            msg += " (in file) vs ";
            msg += CURRENT_DATABASE_VERSION;
            msg += " (expected).";
            throw std::runtime_error(msg);
        }
    }

    m->beginTransaction.Initialize(m->database, "BEGIN TRANSACTION");
    m->commitTransaction.Initialize(m->database, "COMMIT TRANSACTION");
    m->rollbackTransaction.Initialize(m->database, "ROLLBACK TRANSACTION");

    m->getBulletContent.Initialize(m->database, "SELECT CreationTime, ModifyTime, ContentType, ContentValue FROM Bullets WHERE Bullets.Pbid = ?1"sv);
    m->getBulletParent.Initialize(m->database, "SELECT ParentPbid FROM Bullets WHERE Bullets.Pbid = ?1"sv);
    m->getBulletChildren.Initialize(m->database, R"""(
SELECT Bullets.Pbid FROM Bullets
WHERE Bullets.ParentPbid = ?1
ORDER BY ParentSorting
)"""sv);

    m->insertBullet.Initialize(m->database, R"""(
INSERT INTO Bullets(ParentPbid, ParentSorting, CreationTime, ModifyTime)
SELECT ?1, max(ParentSorting) + 1, datetime('now'), datetime('now')
    FROM Bullets
    WHERE ParentPbid = ?1
)"""sv);

    m->deleteBullet.Initialize(m->database, R"""(
DELETE FROM Bullets
WHERE Pbid = ?1
)"""sv);

    m->setBulletContent.Initialize(m->database, R"""(
UPDATE Bullets
SET ModifyTime = datetime('now'),
    ContentType = ?2,
    ContentValue = ?3
WHERE Pbid = ?1
)"""sv);

    m->setBulletPosition.Initialize(m->database, R"""(
UPDATE BULLETS
SET ModifyTime = datetime('now'),
    ParentPbid = ?2,
    ParentSorting = ?3
WHERE Pbid = ?1;

UPDATE BULLETS
SET ParentSorting = ParentSorting + 1
WHERE ParentPbid = ?2
  AND ParentSorting > ?3;
)"""sv);
}

SQLiteBackingStore::~SQLiteBackingStore() {
    delete m;
}

void SQLiteBackingStore::BeginTransaction() {
    int result = sqlite3_step(m->beginTransaction);
    assert(result == SQLITE_DONE);
    sqlite3_reset(m->beginTransaction);
}

void SQLiteBackingStore::CommitTransaction() {
    int result = sqlite3_step(m->commitTransaction);
    assert(result == SQLITE_DONE);
    sqlite3_reset(m->commitTransaction);
}

void SQLiteBackingStore::RollbackTransaction() {
    int result = sqlite3_step(m->rollbackTransaction);
    assert(result == SQLITE_DONE);
    sqlite3_reset(m->rollbackTransaction);
}

Bullet SQLiteBackingStore::FetchBullet(Pbid pbid) {
    Bullet result;
    result.pbid = pbid;
    result.rbid = (size_t)-1;
    {
        SQLiteRunningStatement rt(m->getBulletContent);
        rt.BindArguments(pbid);

        rt.StepAndCheck(SQLITE_ROW);

        using Time = SQLiteRunningStatement::TpFromDateTime;
        auto [creationTime, modifyTime, contentType] = rt.ResultColumns<Time, Time, BulletType>();
        result.creationTime = creationTime;
        result.modifyTime = modifyTime;
        switch (contentType) {
            case BulletType::Textual:
            default: {
                auto content = rt.ResultColumn<const char*>(/*4th*/ 3);
                result.content.v = BulletContentTextual{
                    .text = content ? content : "",
                };
            } break;

            case BulletType::Mirror: {
                auto refereePbid = (Pbid)rt.ResultColumn<int64_t>(/*4th*/ 3);
                result.content.v = BulletContentMirror{
                    .referee = refereePbid,
                };
            } break;
        }
    }
    result.parentPbid = FetchParentOfBullet(pbid);
    result.children = FetchChildrenOfBullet(pbid);
    return result;
}

Pbid SQLiteBackingStore::FetchParentOfBullet(Pbid bullet) {
    SQLiteRunningStatement rt(m->getBulletParent);
    rt.BindArguments(bullet);

    rt.StepAndCheck(SQLITE_ROW);

    auto [parentPbid] = rt.ResultColumns<int64_t>();
    return parentPbid;
}

std::vector<Pbid> SQLiteBackingStore::FetchChildrenOfBullet(Pbid bullet) {
    std::vector<Pbid> result;

    SQLiteRunningStatement rt(m->getBulletChildren);
    rt.BindArguments(bullet);
    while (true) {
        int err = rt.Step();
        if (err != SQLITE_ROW) {
            break;
        }

        auto [childPbid] = rt.ResultColumns<int64_t>();
        result.push_back(childPbid);
    }

    return result;
}

Pbid SQLiteBackingStore::InsertEmptyBullet() {
    SQLiteRunningStatement rt(m->insertBullet);
    rt.BindArguments(kRootBulletPbid, nullptr);
    rt.StepUntilDoneOrError();

    return sqlite3_last_insert_rowid(m->database);
}

void SQLiteBackingStore::DeleteBullet(Pbid bullet) {
    SQLiteRunningStatement rt(m->deleteBullet);
    rt.BindArguments(bullet);
    rt.StepUntilDoneOrError();
}

void SQLiteBackingStore::SetBulletContent(Pbid bullet, const BulletContent& bulletContent) {
    SQLiteRunningStatement rt(m->setBulletContent);
    rt.BindArgument(1, bullet);
    ::VisitVariantOverloaded(
        bulletContent.v,
        [&](const BulletContentTextual& bc) {
            rt.BindArgument(2, (int)BulletType::Textual);
            rt.BindArgument(3, bc.text);
        },
        [&](const BulletContentMirror& bc) {
            rt.BindArgument(2, (int)BulletType::Mirror);
            rt.BindArgument(3, (int64_t)bc.referee);
        });
    rt.StepUntilDoneOrError();
}

void SQLiteBackingStore::SetBulletPosition(Pbid bullet, Pbid newParent, int newIndex) {
    SQLiteRunningStatement rt(m->setBulletPosition);
    rt.BindArguments(bullet, newParent, newIndex);
    rt.StepUntilDoneOrError();
}

struct DbopDeleteBullet {
    Pbid bullet;
};
struct DbopSetBulletContent {
    Pbid bullet;
    const BulletContent* bulletContent;
};
struct DbopSetBulletPosition {
    Pbid bullet;
    Pbid newParent;
    int newIndex;
};

struct WriteDelayedBackingStore::QueuedOperation {
    std::variant<
        DbopDeleteBullet,
        DbopSetBulletContent,
        DbopSetBulletPosition>
        v;
};

WriteDelayedBackingStore::WriteDelayedBackingStore(SQLiteBackingStore& receiver)
    : mReceiver{ &receiver } //
{
}

WriteDelayedBackingStore::~WriteDelayedBackingStore() = default;

Bullet WriteDelayedBackingStore::FetchBullet(Pbid pbid) {
    return mReceiver->FetchBullet(pbid);
}

Pbid WriteDelayedBackingStore::FetchParentOfBullet(Pbid bullet) {
    return mReceiver->FetchParentOfBullet(bullet);
}

std::vector<Pbid> WriteDelayedBackingStore::FetchChildrenOfBullet(Pbid bullet) {
    return mReceiver->FetchChildrenOfBullet(bullet);
}

Pbid WriteDelayedBackingStore::InsertEmptyBullet() {
    // TODO delay this by returning a bullet with "unallocated" pbid
    return mReceiver->InsertEmptyBullet();
}

void WriteDelayedBackingStore::DeleteBullet(Pbid bullet) {
    mQueuedOps.push_back(QueuedOperation{
        .v = DbopDeleteBullet{ bullet },
    });
}

void WriteDelayedBackingStore::SetBulletContent(Pbid bullet, const BulletContent& bulletContent) {
    mQueuedOps.push_back(QueuedOperation{
        .v = DbopSetBulletContent{ bullet, &bulletContent },
    });
}

void WriteDelayedBackingStore::SetBulletPosition(Pbid bullet, Pbid newParent, int newIndex) {
    mQueuedOps.push_back(QueuedOperation{
        .v = DbopSetBulletPosition{ bullet, newParent, newIndex },
    });
}

bool WriteDelayedBackingStore::HasUnflushedOps() const {
    return !mQueuedOps.empty();
}

void WriteDelayedBackingStore::ClearOps() {
    mQueuedOps.clear();
}

void WriteDelayedBackingStore::FlushOps() {
    mReceiver->BeginTransaction();
    for (auto& op : mQueuedOps) {
        ::VisitVariantOverloaded(
            op.v,
            [&](const DbopDeleteBullet& op) {
                mReceiver->DeleteBullet(op.bullet);
            },
            [&](const DbopSetBulletContent& op) {
                mReceiver->SetBulletContent(op.bullet, *op.bulletContent);
            },
            [&](const DbopSetBulletPosition& op) {
                mReceiver->SetBulletPosition(op.bullet, op.newParent, op.newIndex);
            });
    }
    mQueuedOps.clear();
    mReceiver->CommitTransaction();
}
