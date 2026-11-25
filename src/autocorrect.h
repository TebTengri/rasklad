// SPDX-License-Identifier: MIT
// SYS-272: the decision of automatic correction for one finished word (SYS-226,
// SYS-227, SYS-250), shared by the addon and tools/product_eval, so the
// dictionary update gate measures exactly what the addon would do.
//
// w: the word's lowercase us keys; typedRu: typed in the ru layout; upper: bit i
// set when key i was a capital. correct(typed, typedRu) asks the profile
// (ProfileDecision::correctTyped); excepted(keys) is the owner's exception list.
// Only the profile's Correct equal to the word's other-layout form is used, or a
// frequent word of the other language. Whole: the entire word is converted (out =
// its other-layout form). Prefix: a us-typed word ending in keys that carry
// Russian letters ("ckjdj,") is corrected without them, and they stay as typed
// (out = "слово,"). No Qt, no Fcitx; every intermediate string is wiped.
#pragma once
#include "frequentwords.h"
#include "wipe.h"
#include "wordrules.h"

#include <cstdint>
#include <string>

namespace rasklad::autocorrect {

enum class Result { None, Whole, Prefix };

template <class Correct, class Excepted>
Result decide(const std::string &w, bool typedRu, std::uint32_t upper, Correct &&correct, Excepted &&excepted,
              std::string &out)
{
    std::size_t cut = w.size();
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (typedRu) break;
            cut = word::lettersBeforeTail(w);
            if (cut == 0) break; // no letters, or nothing after them to strip
            // An exception that itself ends in a letter key the fallback strips
            // ("пою" is typed gj.) is never corrected to a shorter word.
            bool kept = false;
            for (std::size_t k = cut + 1; k < w.size() && !kept; ++k) {
                std::string longer = w.substr(0, k);
                kept = excepted(longer);
                wipe(longer);
            }
            if (kept) break;
        }
        std::string keys = w.substr(0, cut);
        // The exception first: an excepted word is never corrected in a shorter
        // form just because its own case was not eligible.
        if (excepted(keys)) { wipe(keys); break; } // SYS-227: never this word
        // Case: all lowercase, a capital first letter, or all capitals, judged on
        // the keys that are converted (SYS-250: "NS," is all capitals plus a comma).
        if (!word::caseEligible(upper, cut)) { wipe(keys); continue; }
        std::string typed = word::form(keys, typedRu);
        std::string other = word::form(keys, !typedRu);
        auto fix = correct(typed, typedRu);
        const bool ok = (fix && *fix == other && other != typed) ||
                        (other != typed && frequent::isFrequent(other, typedRu));
        Result result = Result::None;
        if (ok && pass == 0) {
            std::string shown = word::form(w, !typedRu, upper);
            out.assign(shown); // copied, then the local is wiped (SYS-272)
            wipe(shown);
            result = Result::Whole;
        } else if (ok) {
            std::string tail = w.substr(cut);
            std::string both = joined(other, tail);
            std::string shown = word::form(both, false, upper);
            out.assign(shown);
            wipe(tail); wipe(both); wipe(shown);
            result = Result::Prefix;
        }
        if (fix) wipe(*fix);
        wipe(keys); wipe(typed); wipe(other);
        if (result != Result::None) return result;
    }
    return Result::None;
}

} // namespace rasklad::autocorrect
