#include "project.hpp"

#include <gio/gio.h>
#include <memory>
#include <sstream>

namespace {
using KeyFile = std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)>;

std::string field(GKeyFile* file, const char* name) {
    gchar* value = g_key_file_get_string(file, "Story", name, nullptr);
    const std::string result = value ? value : "";
    g_free(value);
    return result;
}

std::string fold(const std::string& text) {
    gchar* lower = g_utf8_casefold(text.c_str(), -1);
    const std::string result(lower);
    g_free(lower);
    return result;
}

std::string bounded(const std::string& value, glong limit) {
    if (g_utf8_strlen(value.c_str(), -1) <= limit)
        return value;
    return value.substr(0, g_utf8_offset_to_pointer(value.c_str(), limit) - value.c_str());
}
}

bool StoryProject::read(const std::string& path, StoryProject& project, GError** error) {
    KeyFile file(g_key_file_new(), g_key_file_unref);
    if (!g_key_file_load_from_file(file.get(), path.c_str(), G_KEY_FILE_NONE, error))
        return false;
    if (field(file.get(), "version") != "1") {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "This is not a supported StoryFlow project.");
        return false;
    }

    StoryProject loaded;
    loaded.path = path;
    gsize count = 0;
    auto** chapters = g_key_file_get_string_list(file.get(), "Story", "chapters", &count, error);
    if (!chapters && (!g_key_file_has_key(file.get(), "Story", "chapters", nullptr) ||
                      (error && *error)))
        return false;
    for (gsize i = 0; i < count; ++i)
        loaded.chapters.emplace_back(chapters[i]);
    g_strfreev(chapters);
    loaded.characters = field(file.get(), "characters");
    loaded.location = field(file.get(), "location");
    loaded.summary = field(file.get(), "summary");
    project = std::move(loaded);
    return true;
}

bool StoryProject::save(GError** error) const {
    KeyFile file(g_key_file_new(), g_key_file_unref);
    std::vector<const char*> entries;
    for (const auto& chapter : chapters)
        entries.push_back(chapter.c_str());
    const char* empty = nullptr;
    g_key_file_set_string(file.get(), "Story", "version", "1");
    g_key_file_set_string_list(file.get(), "Story", "chapters",
                               entries.empty() ? &empty : entries.data(), entries.size());
    g_key_file_set_string(file.get(), "Story", "characters", characters.c_str());
    g_key_file_set_string(file.get(), "Story", "location", location.c_str());
    g_key_file_set_string(file.get(), "Story", "summary", summary.c_str());
    gsize length = 0;
    gchar* data = g_key_file_to_data(file.get(), &length, nullptr);
    auto* destination = g_file_new_for_path(path.c_str());
    const bool saved = g_file_replace_contents(destination, data, length, nullptr, FALSE,
                                               G_FILE_CREATE_NONE, nullptr, nullptr, error);
    g_object_unref(destination);
    g_free(data);
    return saved;
}

std::string StoryProject::chapter_path(const std::string& chapter) const {
    gchar* absolute = g_canonicalize_filename(path.c_str(), nullptr);
    gchar* parent = g_path_get_dirname(absolute);
    g_free(absolute);
    gchar* resolved = g_canonicalize_filename(chapter.c_str(), parent);
    std::string result(resolved);
    g_free(resolved);
    g_free(parent);
    return result;
}

Vocabulary StoryProject::names() const {
    Vocabulary names;
    std::istringstream lines(characters);
    std::string line;
    while (std::getline(lines, line)) {
        const auto names_text = line.substr(0, line.find(':'));
        for (const auto& [word, count] : writing_statistics(names_text).words)
            names[word] += count;
    }
    return names;
}

std::string StoryProject::sentence_prompt(const std::string& prose) const {
    std::string notes;
    if (!location.empty())
        notes += "Location: " + bounded(location, 200) + "\n";
    if (!summary.empty())
        notes += "Scene: " + bounded(summary, 600) + "\n";
    const auto context = fold(prose + "\n" + summary);
    std::istringstream lines(characters);
    std::string line;
    int included = 0;
    while (included < 3 && std::getline(lines, line)) {
        std::istringstream aliases(line.substr(0, line.find(':')));
        std::string alias;
        while (std::getline(aliases, alias, '|')) {
            g_strstrip(alias.data());
            alias.resize(std::char_traits<char>::length(alias.c_str()));
            if (!alias.empty() && context.find(fold(alias)) != std::string::npos) {
                notes += "Character: " + bounded(line, 240) + "\n";
                ++included;
                break;
            }
        }
    }
    return notes.empty() ? prose : "Story notes\n" + notes + "\nDraft\n" + prose;
}
