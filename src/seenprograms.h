// SPDX-License-Identifier: MIT
// SYS-253: program names (InputContext::program(), exactly as Fcitx reports
// them) of applications where a text field got focus since the addon started.
// The settings app offers these, so a listed name is the one the addon matches.
// Names only, in memory only: never saved, never logged, no text of any kind.
#pragma once

#include "configrules.h"
#include "terminalapps.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rasklad {

inline constexpr std::size_t kMaxSeenPrograms = config::kMaxApps; // same cap as the application list

// Keeps the most recently focused names, oldest first. Empty, invalid and
// terminal names and the settings app itself are refused. A name seen again
// moves to the end; when full, the least recently focused name goes, so a
// long session (or a client claiming many names) never freezes the list.
// True if the list changed.
inline bool rememberProgram(std::vector<std::string> &seen, std::string_view program)
{
    if (!config::listableApp(program) || program == "rasklad-settings") return false;
    const auto it = std::find(seen.begin(), seen.end(), program);
    if (it != seen.end()) {
        if (it + 1 == seen.end()) return false;
        std::rotate(it, it + 1, seen.end()); // most recent last
        return true;
    }
    if (seen.size() >= kMaxSeenPrograms) seen.erase(seen.begin());
    seen.emplace_back(program);
    return true;
}

} // namespace rasklad
