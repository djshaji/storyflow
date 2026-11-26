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
                                  std::string_view after, const Vocabulary& personal) const {
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
    const auto rank_prefix = [&](const Vocabulary& vocabulary) {
        for (auto it = vocabulary.upper_bound(prefix); it != vocabulary.end() &&
             it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
            if (it->second > best_frequency) {
                best = it->first;
                best_frequency = it->second;
            }
        }
    };
    if (best.empty())
        rank_prefix(personal);
    if (best.empty())
        rank_prefix(frequencies_);
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

std::string PhraseCompleter::continuation(std::string older, std::string recent,
                                           const Transitions& personal) const {
    if (recent.empty())
        return {};
    if (!older.empty())
        older = lower(older);
    recent = lower(recent);
    std::string phrase;
    for (int length = 0; length < 3; ++length) {
        const auto next_for = [&](const std::string& context) -> std::string {
            if (const auto found = personal.find(context); found != personal.end())
                return found->second;
            const auto found = transitions_.find(context);
            return found == transitions_.end() ? std::string{} : found->second;
        };
        auto next = older.empty() ? std::string{} : next_for(older + " " + recent);
        if (next.empty())
            next = next_for(recent);
        if (next.empty() || next == recent)
            break;
        if (!phrase.empty())
            phrase += ' ';
        phrase += next;
        older = recent;
        recent = std::move(next);
    }
    return phrase;
}


WritingStatistics writing_statistics(std::string_view text) {
    WritingStatistics result;
    if (text.empty())
        return result;
    std::string older, recent;
    const char* position = text.data();
    const char* end = position + text.size();
    while (position < end) {
        const auto character = g_utf8_get_char(position);
        if (!continues_word(character)) {
            if (character != ' ') {
                older.clear();
                recent.clear();
            }
            position = g_utf8_next_char(position);
            continue;
        }
        const char* start = position;
        do {
            position = g_utf8_next_char(position);
        } while (position < end && continues_word(g_utf8_get_char(position)));
        auto word = lower({start, static_cast<std::size_t>(position - start)});
        if (g_utf8_strlen(word.c_str(), -1) >= 4)
            ++result.words[word];
        if (!recent.empty())
            ++result.phrases[{recent, word}];
        if (!older.empty())
            ++result.phrases[{older + " " + recent, word}];
        older = std::move(recent);
        recent = std::move(word);
    }
    return result;
}
