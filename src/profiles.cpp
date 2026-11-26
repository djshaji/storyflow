#include "profiles.hpp"

#include <glib.h>

#include <memory>

namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* database, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    return {statement, sqlite3_finalize};
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string column(sqlite3_stmt* statement, int index) {
    return reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
}

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
}

ProfileStore::ProfileStore(const std::string& database) {
    if (sqlite3_open(database.c_str(), &database_) != SQLITE_OK)
        return;
    sqlite3_busy_timeout(database_, 250);
    available_ = execute(database_,
        "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS terms ("
        "profile TEXT NOT NULL, document TEXT NOT NULL, context TEXT NOT NULL,"
        "word TEXT NOT NULL, count INTEGER NOT NULL CHECK(count > 0),"
        "PRIMARY KEY(profile, document, context, word));");
}

ProfileStore::~ProfileStore() {
    sqlite3_close(database_);
}

std::string ProfileStore::selected() const {
    if (!available_)
        return {};
    auto query = prepare(database_, "SELECT value FROM settings WHERE key = 'profile'");
    return query && sqlite3_step(query.get()) == SQLITE_ROW ? column(query.get(), 0) : std::string{};
}

bool ProfileStore::select(const std::string& profile) {
    if (!available_)
        return false;
    auto query = prepare(database_, "INSERT OR REPLACE INTO settings VALUES ('profile', ?)");
    return query && bind_text(query.get(), 1, profile) && sqlite3_step(query.get()) == SQLITE_DONE;
}

bool ProfileStore::learn(const std::string& profile, const std::string& path,
                         const WritingStatistics& statistics) {
    if (!available_ || !execute(database_, "BEGIN IMMEDIATE"))
        return false;
    gchar* absolute = g_canonicalize_filename(path.c_str(), nullptr);
    const std::string document(absolute);
    g_free(absolute);
    auto erase = prepare(database_, "DELETE FROM terms WHERE profile = ? AND document = ?");
    auto insert = prepare(database_, "INSERT INTO terms VALUES (?, ?, ?, ?, ?)");
    bool success = erase && insert && bind_text(erase.get(), 1, profile) &&
                   bind_text(erase.get(), 2, document) && sqlite3_step(erase.get()) == SQLITE_DONE;
    const auto add = [&](const std::string& context, const std::string& word, unsigned count) {
        if (!success)
            return;
        auto* statement = insert.get();
        sqlite3_reset(statement);
        success = bind_text(statement, 1, profile) && bind_text(statement, 2, document) &&
                  bind_text(statement, 3, context) && bind_text(statement, 4, word) &&
                  sqlite3_bind_int64(statement, 5, count) == SQLITE_OK &&
                  sqlite3_step(statement) == SQLITE_DONE;
    };
    for (const auto& [word, count] : statistics.words)
        add({}, word, count);
    for (const auto& [phrase, count] : statistics.phrases)
        add(phrase.first, phrase.second, count);
    if (success && execute(database_, "COMMIT"))
        return true;
    execute(database_, "ROLLBACK");
    return false;
}

bool ProfileStore::load(const std::string& profile, Vocabulary& words, Transitions& phrases) const {
    words.clear();
    phrases.clear();
    if (!available_)
        return false;
    auto query = prepare(database_,
        "SELECT context, word, SUM(count) AS total FROM terms WHERE profile = ? "
        "GROUP BY context, word HAVING context = '' OR total >= 2 "
        "ORDER BY context, total DESC, word");
    if (!query || !bind_text(query.get(), 1, profile))
        return false;
    int status;
    while ((status = sqlite3_step(query.get())) == SQLITE_ROW) {
        const auto context = column(query.get(), 0);
        const auto word = column(query.get(), 1);
        if (context.empty())
            words.emplace(word, static_cast<unsigned>(sqlite3_column_int64(query.get(), 2)));
        else
            phrases.emplace(context, word);
    }
    if (status == SQLITE_DONE)
        return true;
    words.clear();
    phrases.clear();
    return false;
}
