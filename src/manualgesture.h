// SPDX-License-Identifier: MIT
//
// SYS-225 slice 1: double-Shift gesture detector and us->ru letter table for
// the current preedit word. Pure logic: no Fcitx, I/O, clocks or text storage.
// Times are the key events' own millisecond timestamps (KeyEvent::time()).
#pragma once

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

// Converts a word of lowercase ASCII letters and the seven keys above. Anything
// else returns "" (the caller keeps the original; fail closed).
inline std::string usToRu(std::string_view word) {
    std::string out;
    out.reserve(word.size() * 2);
    for (const char c : word) {
        if (c >= 'a' && c <= 'z') out.append(kUsToRu[static_cast<std::size_t>(c - 'a')]);
        else if (const auto r = punctToRu(c); !r.empty()) out.append(r);
        else return {};
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
