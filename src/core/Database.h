#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CallRecord.h"

struct sqlite3;
struct sqlite3_stmt;

namespace biem::core {

// Thin, dependency-free (besides libsqlite3) wrapper around the call-log
// database. One physical SQLite file backs both the searchable call log and
// the small radio-ID / talkgroup alias tables used by TitleBuilder.
//
// Not thread-safe: callers (CallRecorder, UI, CLI) must serialize access,
// e.g. by only touching a given Database instance from one thread, or by
// guarding it with their own mutex. SQLite itself would allow more, but
// this wrapper doesn't attempt to expose that safely.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Creates the schema if it doesn't exist yet. Safe to call every startup.
    void migrate();

    // Inserts a new call row. Fills in `rec.id` from the DB and returns it.
    // Also computes/overwrites `rec.title` via TitleBuilder if it is empty.
    int64_t insertCall(CallRecord& rec);

    std::optional<CallRecord> getById(int64_t id);

    struct SearchFilter {
        std::optional<int64_t> fromUnixTimeMs;
        std::optional<int64_t> toUnixTimeMs;
        std::optional<std::string> titleContains;  // case-sensitive substring (SQL LIKE)
        std::optional<uint32_t> talkgroupId;
        std::optional<uint32_t> radioId;
        std::optional<int> slot;
        std::optional<CallSource> source;
        int limit = 200;
        int offset = 0;
    };

    // Newest-first.
    std::vector<CallRecord> search(const SearchFilter& filter);

    // Radio ID / talkgroup alias management (see core/TitleBuilder.h).
    void setRadioLabel(uint32_t radioId, const std::string& label);
    void setTalkgroupLabel(uint32_t talkgroupId, const std::string& label);
    std::optional<std::string> resolveRadioLabel(uint32_t radioId);
    std::optional<std::string> resolveTalkgroupLabel(uint32_t talkgroupId);

private:
    sqlite3* db_ = nullptr;

    void execOrThrow(const std::string& sql);
    CallRecord readRow(sqlite3_stmt* stmt) const;
};

} // namespace biem::core
