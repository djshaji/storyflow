#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>

using Vocabulary = std::map<std::string, unsigned>;
using Transitions = std::map<std::string, std::string>;
using PhraseCounts = std::map<std::pair<std::string, std::string>, unsigned>;

struct WritingStatistics {
    Vocabulary words;
    PhraseCounts phrases;
};

WritingStatistics writing_statistics(std::string_view text);

class WordCompleter {
public:
    explicit WordCompleter(std::string_view frequencies);
    std::string suffix(std::string_view prefix, std::string_view before,
                       std::string_view after, const Vocabulary& personal = {}) const;

private:
    std::map<std::string, unsigned> frequencies_;
};

class PhraseCompleter {
public:
    explicit PhraseCompleter(std::string_view transitions);
    std::string continuation(std::string older, std::string recent,
                             const Transitions& personal = {}) const;

private:
    std::map<std::string, std::string> transitions_;
};
