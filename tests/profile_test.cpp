#include "profiles.hpp"

#include <glib/gstdio.h>

int main() {
    gchar* directory = g_dir_make_tmp("storyflow-profiles-XXXXXX", nullptr);
    if (!directory)
        return 1;
    const std::string path = std::string(directory) + "/test.sqlite";
    Vocabulary words;
    Transitions phrases;
    const auto story = writing_statistics("Mirehaven waits. Mirehaven waits. Élodie returns.");
    bool passed;
    {
        ProfileStore first(path);
        passed = first.selected().empty() && first.select("Fiction") &&
                 first.learn("Fiction", "draft.html", story) &&
                 first.learn("Fiction", "draft.html", story) &&
                 first.learn("Academic", "draft.html", writing_statistics("Philology."));
    }
    {
        ProfileStore reopened(path);
        WordCompleter word{"mirage 20"};
        PhraseCompleter phrase{""};
        passed = passed && reopened.selected() == "Fiction" &&
                 reopened.load("Fiction", words, phrases) && words.count("mirehaven") == 1 && words.at("mirehaven") == 2 &&
                 word.suffix("Mir", "", "", words) == "ehaven" &&
                 word.suffix("Élo", "", "", words) == "die" &&
                 phrase.continuation("", "Mirehaven", phrases) == "waits" &&
                 reopened.load("Academic", words, phrases) && words.count("mirehaven") == 0 &&
                 words.count("philology") == 1 && phrases.empty() &&
                 reopened.learn("Fiction", "draft.html", writing_statistics("Ravens.")) &&
                 reopened.load("Fiction", words, phrases) &&
                 words.count("mirehaven") == 0 && words.count("ravens") == 1 && phrases.empty();
    }
    g_remove(path.c_str());
    g_rmdir(directory);
    g_free(directory);
    return passed ? 0 : 1;
}
