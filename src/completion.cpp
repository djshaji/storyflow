#include "completion.hpp"

#include <glib.h>

#include <sstream>

namespace {
std::string lower(std::string_view word) {
    auto* converted = g_utf8_strdown(word.data(), word.size());
    std::string result(converted);
    g_free(converted);
    return result;
}

bool continues_word(gunichar character) {
    return g_unichar_isalpha(character) || g_unichar_ismark(character);
}

void count_matches(std::string_view text, const std::string& prefix,
                   std::map<std::string, unsigned>& counts) {
    if (text.empty())
        return;
    const char* position = text.data();
    const char* end = position + text.size();
    while (position < end) {
        const char* start = position;
        while (position < end && continues_word(g_utf8_get_char(position)))
            position = g_utf8_next_char(position);
        if (position - start > static_cast<std::ptrdiff_t>(prefix.size())) {
            auto word = lower({start, static_cast<std::size_t>(position - start)});
            if (word.size() > prefix.size() && word.compare(0, prefix.size(), prefix) == 0)
                ++counts[word];
        }
        if (position < end)
            position = g_utf8_next_char(position);
    }
}
}

WordCompleter::WordCompleter(std::string_view frequencies) {
    std::istringstream input{std::string(frequencies)};
    std::string word;
    unsigned count;
    while (input >> word >> count)
        frequencies_.emplace(word, count);
}

std::string WordCompleter::suffix(std::string_view typed, std::string_view before,
                                  std::string_view after) const {
    if (typed.empty() || g_utf8_strlen(typed.data(), typed.size()) < 3)
        return {};
    const auto prefix = lower(typed);
    std::map<std::string, unsigned> draft_counts;
    count_matches(before, prefix, draft_counts);
    count_matches(after, prefix, draft_counts);

    std::string best;
    unsigned best_count = 0;
    unsigned best_frequency = 0;
    for (const auto& [word, count] : draft_counts) {
        const auto found = frequencies_.find(word);
        const unsigned frequency = found == frequencies_.end() ? 0 : found->second;
        if (count > best_count || (count == best_count && frequency > best_frequency)) {
            best = word;
            best_count = count;
            best_frequency = frequency;
        }
    }
    if (best.empty()) {
        for (auto it = frequencies_.upper_bound(prefix); it != frequencies_.end() &&
             it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
            if (it->second > best_frequency) {
                best = it->first;
                best_frequency = it->second;
            }
        }
    }
    if (best.empty())
        return {};

    auto ending = best.substr(prefix.size());
    bool all_caps = true;
    for (const char* letter = typed.data(); letter < typed.data() + typed.size();
         letter = g_utf8_next_char(letter)) {
        if (g_unichar_islower(g_utf8_get_char(letter))) {
            all_caps = false;
            break;
        }
    }
    if (all_caps) {
        auto* upper = g_utf8_strup(ending.c_str(), -1);
        ending = upper;
        g_free(upper);
    }
    return ending;
}

PhraseCompleter::PhraseCompleter(std::string_view transitions) {
    std::istringstream input{std::string(transitions)};
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('\t');
        if (separator != std::string::npos)
            transitions_.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
}

std::string PhraseCompleter::continuation(std::string older, std::string recent) const {
    if (recent.empty())
        return {};
    if (!older.empty())
        older = lower(older);
    recent = lower(recent);
    std::string phrase;
    for (int length = 0; length < 3; ++length) {
        auto position = older.empty() ? transitions_.end() : transitions_.find(older + " " + recent);
        if (position == transitions_.end())
            position = transitions_.find(recent);
        if (position == transitions_.end() || position->second == recent)
            break;
        if (!phrase.empty())
            phrase += ' ';
        phrase += position->second;
        older = recent;
        recent = position->second;
    }
    return phrase;
}
