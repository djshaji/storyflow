#pragma once

#include "completion.hpp"

#include <sqlite3.h>

class ProfileStore {
public:
    explicit ProfileStore(const std::string& database);
    ~ProfileStore();
    ProfileStore(const ProfileStore&) = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;

    std::string selected() const;
    bool select(const std::string& profile);
    bool learn(const std::string& profile, const std::string& path,
               const WritingStatistics& statistics);
    bool load(const std::string& profile, Vocabulary& words, Transitions& phrases) const;

private:
    sqlite3* database_ = nullptr;
    bool available_ = false;
};
