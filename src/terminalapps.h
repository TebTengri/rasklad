// SPDX-License-Identifier: MIT
// SYS-227: window app ids of terminal emulators. Terminals carry commands and
// passwords, so the addon never treats them as listed applications and the
// settings app refuses to add them, whatever the configuration says.
// Matching is case-insensitive on the whole id.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace rasklad {

inline constexpr std::array<std::string_view, 22> kTerminalApps{
    "foot", "footclient", "kitty", "alacritty", "com.mitchellh.ghostty", "ghostty",
    "org.wezfurlong.wezterm", "wezterm", "xterm", "uxterm", "urxvt", "st", "konsole",
    "org.kde.konsole", "gnome-terminal", "gnome-terminal-server", "org.gnome.terminal",
    "org.gnome.console", "kgx", "terminator", "tilix", "com.gexperts.tilix"};

inline bool terminalApp(std::string_view id)
{
    for (const auto t : kTerminalApps) {
        if (t.size() != id.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < id.size() && same; ++i) {
            char c = id[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            same = c == t[i];
        }
        if (same) return true;
    }
    return false;
}

} // namespace rasklad
