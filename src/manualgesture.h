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

// Converts a word of lowercase ASCII letters. Anything else returns "" (the
// caller keeps the original; fail closed).
inline std::string usToRu(std::string_view word) {
    std::string out;
    out.reserve(word.size() * 2);
    for (const char c : word) {
        if (c < 'a' || c > 'z') return {};
        out.append(kUsToRu[static_cast<std::size_t>(c - 'a')]);
    }
    return out;
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
