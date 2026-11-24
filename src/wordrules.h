// SPDX-License-Identifier: MIT
// SYS-255: the word rules of automatic correction (SYS-226, SYS-250), out of the
// addon so they are tested on their own. No Qt, no Fcitx: keys and code points in,
// answers out. The addon keeps the key events, the field and the profile.
#pragma once

#include "manualgesture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rasklad::word {

// A typed character that ends the word: № or printable ASCII punctuation, never a
// letter, a digit or Space. (Enter and Tab end it too; the addon checks those keys.)
inline bool endsWord(std::uint32_t codePoint) noexcept
{
    if (codePoint == 0x2116) return true; // №
    return codePoint > 0x20 && codePoint < 0x7f &&
           !((codePoint >= '0' && codePoint <= '9') || (codePoint >= 'a' && codePoint <= 'z') ||
             (codePoint >= 'A' && codePoint <= 'Z'));
}

// The case automatic correction accepts on the first `keys` keys: all lowercase,
// a capital first letter, or all capitals. Bit i of `upper`: key i was a capital.
inline bool caseEligible(std::uint32_t upper, std::size_t keys) noexcept
{
    const std::uint32_t all = keys >= 32 ? ~std::uint32_t{0} : (std::uint32_t{1} << keys) - 1;
    const std::uint32_t mask = upper & all;
    return mask == 0 || mask == 1 || mask == all;
}

// The word as shown: the us keys as typed, or their Russian form, with the
// capitals of `upper`. A key with no Russian form keeps the typed text.
inline std::string form(const std::string &typed, bool asRu, std::uint32_t upper = 0)
{
    std::string out = asRu ? manual::usToRu(typed) : typed;
    if (out.empty()) out = typed;
    return upper ? manual::applyUpper(out, upper) : out;
}

// SYS-250: how many leading keys are letters before the keys that end a us-typed
// word ("ckjdj," -> 5). 0 when the word has no letter, or when it ends in a
// letter: then there is nothing to strip and the fallback does not apply.
inline std::size_t lettersBeforeTail(std::string_view keys) noexcept
{
    std::size_t cut = keys.size();
    while (cut > 0 && !(keys[cut - 1] >= 'a' && keys[cut - 1] <= 'z')) --cut;
    return cut == keys.size() ? 0 : cut;
}

} // namespace rasklad::word
