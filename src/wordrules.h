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

// SYS-269: the field hints Fcitx reports (fcitx::CapabilityFlag bit values; the
// addon static_asserts they match). Nothing is buffered or corrected in a field
// that is protected, or whose purpose is not prose: an e-mail ("ns@" is not
// "ты@"), a web address, a number, a phone number, a date or a time.
namespace field {
inline constexpr std::uint64_t kPassword = 1ULL << 3;
inline constexpr std::uint64_t kEmail = 1ULL << 7;
inline constexpr std::uint64_t kDigit = 1ULL << 8;
inline constexpr std::uint64_t kUrl = 1ULL << 12;
inline constexpr std::uint64_t kDialable = 1ULL << 13;
inline constexpr std::uint64_t kNumber = 1ULL << 14;
inline constexpr std::uint64_t kNoSpellCheck = 1ULL << 17;
inline constexpr std::uint64_t kTerminal = 1ULL << 32;
inline constexpr std::uint64_t kDate = 1ULL << 33;
inline constexpr std::uint64_t kTime = 1ULL << 34;
inline constexpr std::uint64_t kSensitive = 1ULL << 36;
inline constexpr std::uint64_t kDisable = 1ULL << 40;
inline constexpr std::uint64_t kRefused = kPassword | kSensitive | kNoSpellCheck | kTerminal | kDisable |
                                          kEmail | kUrl | kDigit | kNumber | kDialable | kDate | kTime;
} // namespace field

inline bool refusedField(std::uint64_t capabilityFlags) noexcept
{
    return (capabilityFlags & field::kRefused) != 0;
}

} // namespace rasklad::word
