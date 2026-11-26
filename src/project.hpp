#pragma once

#include "completion.hpp"
#include <glib.h>
#include <vector>

struct StoryProject {
    std::string path;
    std::vector<std::string> chapters;
    std::string characters;
    std::string location;
    std::string summary;

    static bool read(const std::string& path, StoryProject& project, GError** error);
    bool save(GError** error) const;
    std::string chapter_path(const std::string& chapter) const;
    Vocabulary names() const;
    std::string sentence_prompt(const std::string& prose) const;
};
