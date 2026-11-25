// SPDX-License-Identifier: MIT
//
// SYS-225 slice 1: double-Shift gesture detector and us->ru letter table for
// the current preedit word. Pure logic: no Fcitx, I/O, clocks or text storage.
// Times are the key events' own millisecond timestamps (KeyEvent::time()).
#pragma once

#include "wipe.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace rasklad::manual {

// XKB us (basic) a..z -> ru (default) on the same physical key, UTF-8.
// Must equal rasklad::convertLayout(UsToRu) for these letters (tested).
inline constexpr std::array<std::string_view, 26> kUsToRu{
    "ф", "и", "с", "в", "у", "а", "п", "р", "ш", "о", "л", "д", "ь",
    "т", "щ", "з", "й", "к", "ы", "е", "г", "м", "ц", "ч", "н", "я"};

// The seven unshifted punctuation keys that carry Russian letters (same physical key).
inline std::string_view punctToRu(char c) {
    switch (c) {
    case ',': return "б"; case '.': return "ю"; case ';': return "ж"; case '\'': return "э";
    case '[': return "х"; case ']': return "ъ"; case '`': return "ё";
    default: return {};
    }
}
inline bool wordChar(char c) { return (c >= 'a' && c <= 'z') || !punctToRu(c).empty(); }

// SYS-226: the us key (a..z or one of the seven punctuation keys) that carries this
// lowercase Russian letter in the default ru layout; 0 for anything else.
inline char ruToUsKey(char32_t c) {
    for (std::size_t i = 0; i < kUsToRu.size(); ++i) {
        const auto r = kUsToRu[i];
        // Every entry is one two-byte UTF-8 code point in U+0430..U+044F.
        const char32_t cp = (static_cast<char32_t>(static_cast<unsigned char>(r[0]) & 0x1f) << 6) |
                            (static_cast<unsigned char>(r[1]) & 0x3f);
        if (cp == c) return static_cast<char>('a' + i);
    }
    for (const char k : {',', '.', ';', '\'', '[', ']', '`'}) {
        const auto r = punctToRu(k);
        const char32_t cp = (static_cast<char32_t>(static_cast<unsigned char>(r[0]) & 0x1f) << 6) |
                            (static_cast<unsigned char>(r[1]) & 0x3f);
        if (cp == c) return k;
    }
    return 0;
}

// SYS-227: an exception word ("never correct this") as the us keys that type it, so
// "ok" and "щл" are the same entry and match a buffer word in either layout. One
// word of Latin a..z or Cyrillic а..я/ё (either case), not both, 1..32 letters.
// Anything else returns "" (not a valid exception).
inline std::string exceptionKeys(std::string_view word) {
    std::string out;
    bool latin = false, cyrillic = false;
    for (std::size_t i = 0; i < word.size();) {
        const unsigned char c = static_cast<unsigned char>(word[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c + 32));
            else if (c >= 'a' && c <= 'z') out.push_back(static_cast<char>(c));
            else return {};
            latin = true;
            ++i;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < word.size() &&
                   (static_cast<unsigned char>(word[i + 1]) & 0xc0) == 0x80) {
            char32_t cp = (static_cast<char32_t>(c & 0x1f) << 6) |
                          (static_cast<unsigned char>(word[i + 1]) & 0x3f);
            if (cp >= U'А' && cp <= U'Я') cp += 0x20;
            else if (cp == U'Ё') cp = U'ё';
            const char key = ruToUsKey(cp);
            if (!key) return {};
            out.push_back(key);
            cyrillic = true;
            i += 2;
        } else {
            return {};
        }
        if (out.size() > 32) return {};
    }
    if (latin && cyrillic) return {};
    return out;
}

// Converts a word of lowercase ASCII letters and the seven keys above. Anything
// else returns "" (the caller keeps the original; fail closed).
inline std::string usToRu(std::string_view word) {
    std::string out;
    out.reserve(word.size() * 2);
    for (const char c : word) {
        if (c >= 'a' && c <= 'z') out.append(kUsToRu[static_cast<std::size_t>(c - 'a')]);
        else if (const auto r = punctToRu(c); !r.empty()) out.append(r);
        else { wipe(out); return out; } // SYS-272: no partial copy; one returned variable
    }
    return out;
}

// SYS-226: uppercases code point i of UTF-8 text when bit i of mask is set; only
// a..z and Cyrillic а..я/ё change. Anything malformed returns the text unchanged.
inline std::string applyUpper(const std::string &text, std::uint32_t mask) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    for (unsigned idx = 0; i < text.size(); ++idx) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool up = idx < 32 && (mask >> idx) & 1u;
        if (c < 0x80) {
            out.push_back(up && c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : static_cast<char>(c));
            ++i;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
            char32_t cp = (static_cast<char32_t>(c & 0x1f) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3f);
            if (up) {
                if (cp >= U'\u0430' && cp <= U'\u044f') cp -= 0x20;
                else if (cp == U'\u0451') cp = U'\u0401';
            }
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            i += 2;
        } else {
            wipe(out); // SYS-272: no partial copy left behind
            out = text; // the same variable is returned on every path (NRVO)
            return out;
        }
    }
    return out;
}

// Number of Unicode code points in valid UTF-8, or -1 if malformed.
inline long codePointCount(std::string_view text) {
    long n = 0;
    for (std::size_t i = 0; i < text.size(); ++n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!len || i + len > text.size()) return -1;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80) return -1;
        i += len;
    }
    return n;
}

// True if the text immediately before the cursor (a code-point index into valid
// UTF-8) is exactly `expected`. Malformed text or an out-of-range cursor: false.
inline bool endsBeforeCursor(std::string_view text, unsigned cursor, std::string_view expected) {
    std::size_t byte = 0;
    for (unsigned cp = 0; cp < cursor; ++cp) {
        if (byte >= text.size()) return false;
        const unsigned char c = static_cast<unsigned char>(text[byte]);
        std::size_t len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!len || byte + len > text.size()) return false;
        byte += len;
    }
    if (codePointCount(text) < 0) return false;
    const std::string_view before = text.substr(0, byte);
    return before.size() >= expected.size() &&
           before.substr(before.size() - expected.size()) == expected;
}

// Event kinds the addon feeds in; everything that is not a bare Shift
// press/release is Other (it breaks a pending gesture).
enum class Input { ShiftPress, ShiftRelease, Other };

class DoubleShift {
public:
    static constexpr std::int64_t kWindowMs = 400;

    // Returns true exactly when this event completes a double tap.
    bool feed(Input in, std::int64_t timeMs) {
        switch (in) {
        case Input::Other:
            reset();
            return false;
        case Input::ShiftPress:
            if (down_) { reset(); return false; } // Second Shift held: chord, not a tap.
            if (taps_ == 1 && (timeMs < lastRelease_ || timeMs - lastRelease_ > kWindowMs)) taps_ = 0;
            down_ = true;
            return false;
        case Input::ShiftRelease:
            if (!down_) { reset(); return false; }
            down_ = false;
            if (++taps_ == 2) { reset(); return true; }
            lastRelease_ = timeMs;
            return false;
        }
        reset();
        return false;
    }
    void reset() { taps_ = 0; down_ = false; lastRelease_ = 0; }
    bool shiftDown() const { return down_; }

private:
    int taps_ = 0;
    bool down_ = false;
    std::int64_t lastRelease_ = 0;
};

} // namespace rasklad::manual
