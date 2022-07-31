#pragma once

#include <date/date.h>
#include <sqlite3.h>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>

struct SQLiteDatabase {
    sqlite3* database = nullptr;

    ~SQLiteDatabase() {
        // NOTE: calling with NULL is a harmless no-op
        int result = sqlite3_close(database);
        assert(result == SQLITE_OK);
    }

    operator sqlite3*() const { return database; }
    sqlite3** operator&() { return &database; }
};

struct SQLiteStatement {
    sqlite3_stmt* stmt = nullptr;

    SQLiteStatement(const SQLiteStatement&) = delete;
    SQLiteStatement& operator=(const SQLiteStatement&) = delete;

    SQLiteStatement() = default;

    SQLiteStatement(sqlite3* database, std::string_view sql) {
        Initialize(database, sql);
    }

    ~SQLiteStatement() {
        // NOTE: calling with NULL is a harmless no-op
        // NOTE: we don't care about the error code, because they are returned if the statement has errored in the most recent execution
        //       but deleting it will succeeed anyways
        sqlite3_finalize(stmt);
    }

    operator sqlite3_stmt*() const { return stmt; }
    sqlite3_stmt** operator&() { return &stmt; }

    void Initialize(sqlite3* database, std::string_view sql) {
        int result = sqlite3_prepare_v2(database, sql.data(), sql.size(), &stmt, nullptr);
        if (result != SQLITE_OK) {
            std::string msg;
            msg += "Failed to prepare SQLite3 statement, error message:\n";
            msg += sqlite3_errmsg(database);
            throw std::runtime_error(msg);
        }
    }

    bool InitializeLazily(sqlite3* database, std::string_view sql) {
        if (!stmt) {
            Initialize(database, sql);
            return true;
        }
        return false;
    }
};

struct SQLiteRunningStatement {
    sqlite3_stmt* stmt;

    SQLiteRunningStatement(const SQLiteStatement& stmt)
        : stmt{ stmt.stmt } {
    }

    ~SQLiteRunningStatement() {
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
    }

    void BindArgument(int index, int32_t value) {
        sqlite3_bind_int(stmt, index, (int)value);
    }

    void BindArgument(int index, uint32_t value) {
        sqlite3_bind_int(stmt, index, (int)value);
    }

    void BindArgument(int index, int64_t value) {
        sqlite3_bind_int64(stmt, index, value);
    }

    void BindArgument(int index, uint64_t value) {
        sqlite3_bind_int64(stmt, index, (int64_t)value);
    }

    void BindArgument(int index, const char* value) {
        sqlite3_bind_text(stmt, index, value, -1, nullptr);
    }

    void BindArgument(int index, std::string_view value) {
        sqlite3_bind_text(stmt, index, value.data(), value.size(), nullptr);
    }

    void BindArgument(int index, std::nullptr_t) {
        // Noop
    }

    template <typename... Ts>
    void BindArguments(Ts... args) {
        // NOTE: SQLite3 argument index starts at 1
        size_t idx = 1;
        auto HandleEachArgument = [this, &idx](auto arg) {
            BindArgument(idx, arg);
            ++idx;
        };
        (HandleEachArgument(std::forward<Ts>(args)), ...);
    }

    int Step() {
        return sqlite3_step(stmt);
    }

    void StepAndCheck(int forErrCode) {
        int errCode = sqlite3_step(stmt);
        assert(errCode == forErrCode);
    }

    void StepUntilDone() {
        while (true) {
            int err = sqlite3_step(stmt);
            // SQLITE_DONE, and all others are error codes
            // SQLITE_OK is never returned for sqlite3_step() //TODO fact check this
            if (err == SQLITE_DONE) {
                break;
            }
            if (err != SQLITE_ROW) {
                std::string msg;
                msg += "Error executing SQLite3 statement, error message:\n";
                msg += sqlite3_errmsg(sqlite3_db_handle(stmt));
                throw std::runtime_error(msg);
                break;
            }
        }
    }

    using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
    using TpFromUnixTimestamp = std::pair<TimePoint, int64_t>;
    using TpFromDateTime = std::pair<TimePoint, const char*>;

    // TODO replace with overloads?
    template <typename T>
    auto ResultColumn(int column) const {
        if constexpr (std::is_enum_v<T>) {
            auto value = sqlite3_column_int64(stmt, column);
            return static_cast<T>(value);
        } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, bool>) {
            return (T)sqlite3_column_int(stmt, column);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return (T)sqlite3_column_int64(stmt, column);
        } else if constexpr (std::is_same_v<T, const char*>) {
            return (const char*)sqlite3_column_text(stmt, column);
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto cstr = (const char*)sqlite3_column_text(stmt, column);
            return std::string(cstr);
        } else if constexpr (std::is_same_v<T, TpFromUnixTimestamp>) {
            auto unixTimestamp = sqlite3_column_int64(stmt, column);
            auto chrono = std::chrono::seconds(unixTimestamp);
            return TimePoint(chrono);
        } else if constexpr (std::is_same_v<T, TpFromDateTime>) {
            auto datetime = (const char*)sqlite3_column_text(stmt, column);
            if (datetime) {
                std::stringstream ss(datetime);
                TimePoint timepoint;
                ss >> date::parse("%F %T", timepoint);
                return timepoint;
            } else {
                return TimePoint();
            }
        } else {
            static_assert(false && sizeof(T), "Unknown type");
        }
    }

    template <typename... Ts>
    auto ResultColumns() {
        // NOTE: SQLite3 column index starts at 0
        // NOTE: ((size_t)-1) + 1 == 0
        size_t idx = -1;
        // NOTE: std::make_tuple() -- variadic template function
        //       std::tuple() -- CTAD constructor
        //       Both of these cause make the comma operator unsequenced, not viable here
        return std::tuple{ (++idx, ResultColumn<Ts>(idx))... };
    }
};
