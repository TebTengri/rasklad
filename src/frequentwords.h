// SPDX-License-Identifier: MIT
// SYS-226: fixed table of frequent short words for automatic correction where the
// reviewed router abstains by design (fewer than 4 keys, or a frequent word the model
// does not confirm, e.g. куда). Exact match only, lowercase. Excluded (2026-11-21, by
// the packaged Hunspell dictionaries): Russian words whose us-key form is an English
// word (мы vs, где ult, еще tot, за pf, ну ye, ли kb) and English words whose ru-key
// form is a Russian word (the еру, in шт, if ша, but иге, her рук, here руку).
// Pure data and lookup: no I/O, no storage.
#pragma once

#include <array>
#include <string_view>

namespace rasklad::frequent {

inline constexpr std::array<std::string_view, 101> kRussian{
    "как", "что", "чем", "ты", "то", "да", "не", "он", "вы", "они", "она", "оно", "кто", "так",
    "уже", "ещё", "все", "всё", "его", "её", "ее", "их", "мне", "нет", "или", "для", "без", "при",
    "над", "под", "про", "вот", "тут", "там", "раз", "два", "три", "сам", "мой", "наш", "ваш", "на",
    "из", "от", "до", "по", "со", "ко", "об", "же", "бы", "во", "это", "эта", "эти", "тот", "той",
    "тем", "тоже", "куда", "когда", "зачем", "меня", "тебя", "тебе", "себя", "если", "чтобы",
    "только", "можно", "надо", "очень", "был", "была", "было", "были", "есть", "будет", "сейчас",
    "потом", "может", "нас", "вас", "нам", "вам", "им", "ей", "ему", "них", "чего", "кого", "кому",
    "сюда", "туда", "здесь", "хорошо", "привет", "спасибо", "пока", "ладно", "давай"};

inline constexpr std::array<std::string_view, 87> kEnglish{
    "and", "for", "you", "is", "it", "to", "of", "on", "at", "be", "we", "he", "me", "my", "do",
    "go", "no", "so", "up", "or", "an", "are", "not", "can", "all", "was", "has", "get", "out",
    "new", "how", "who", "why", "yes", "ok", "hi", "one", "two", "now", "did", "had", "his", "him",
    "she", "our", "its", "let", "see", "too", "way", "use", "any", "may", "say", "got", "just",
    "what", "that", "this", "with", "from", "have", "will", "your", "they", "them", "then", "than",
    "when", "there", "been", "were", "would", "could", "should", "about", "some", "time", "like",
    "know", "good", "well", "also", "only", "very", "much", "more"};

template <std::size_t N>
constexpr bool contains(const std::array<std::string_view, N> &list, std::string_view word)
{
    for (const auto w : list)
        if (w == word) return true;
    return false;
}

// other: the word's lowercase other-layout form. True if it is a listed frequent
// word of that language (Russian when typed in us, English when typed in ru).
inline bool isFrequent(std::string_view other, bool typedInRu)
{
    return typedInRu ? contains(kEnglish, other) : contains(kRussian, other);
}

} // namespace rasklad::frequent
