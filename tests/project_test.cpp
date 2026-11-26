#include "project.hpp"

#include <glib/gstdio.h>

int main() {
    gchar* directory = g_dir_make_tmp("storyflow-projects-XXXXXX", nullptr);
    if (!directory)
        return 1;
    const std::string folder(directory);
    g_free(directory);
    StoryProject first{folder + "/one.storyflow", {"first.html", "second.html"},
                       "Mirehaven | Mira: Knows the map\nRavenswood: Unrelated",
                       "Archives", "Mira searches for the map."};
    StoryProject second{folder + "/two.storyflow", {}, "Mirador: A captain", "Harbour", ""};
    StoryProject reopened;
    const WordCompleter names{""};
    const std::string prose = "Mira turned and saw";
    bool passed = first.save(nullptr) && second.save(nullptr) &&
                  StoryProject::read(first.path, reopened, nullptr) &&
                  reopened.chapters == first.chapters &&
                  reopened.chapter_path(reopened.chapters[1]) == folder + "/second.html" &&
                  names.suffix("Mir", "", "", reopened.names()) == "a";
    const auto prompt = reopened.sentence_prompt(prose);
    passed = passed && prompt.find("Knows the map") != std::string::npos &&
             prompt.find("Archives") != std::string::npos &&
             prompt.find("Unrelated") == std::string::npos &&
             prompt.substr(prompt.size() - prose.size()) == prose &&
             StoryProject::read(second.path, reopened, nullptr) && reopened.chapters.empty() &&
             names.suffix("Mir", "", "", reopened.names()) == "ador" &&
             reopened.sentence_prompt(prose).find("Knows the map") == std::string::npos &&
             reopened.sentence_prompt(prose).find("Harbour") != std::string::npos;

    g_remove(first.path.c_str());
    g_remove(second.path.c_str());
    g_rmdir(folder.c_str());
    return passed ? 0 : 1;
}
