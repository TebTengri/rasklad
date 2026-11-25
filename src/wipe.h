#pragma once

#include <cstddef>
#include <string>

namespace rasklad {

// SYS-272: overwrites the whole buffer of a string that held typed text, not
// just its current length (a string shortened in place keeps older bytes past
// size()), then empties it. No reallocation.
inline void wipe(std::string &s) noexcept
{
    s.resize(s.capacity());
    volatile char *p = s.empty() ? nullptr : &s[0];
    for (std::size_t i = 0; p && i < s.size(); ++i) {
        p[i] = '\0';
    }
    s.clear();
}

} // namespace rasklad
