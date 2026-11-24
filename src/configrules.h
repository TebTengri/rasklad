// SPDX-License-Identifier: MIT
// SYS-255: the rules for the owner's configuration, shared by the addon and the
// settings window so they can never disagree. No Qt, no Fcitx: plain strings in,
// positions out. Each side keeps its own string type and reads the entries it is
// told to keep.
#pragma once

#include "manualgesture.h"
#include "terminalapps.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rasklad::config {

inline constexpr std::size_t kMaxApps = 64;
inline constexpr std::size_t kMaxExceptions = 256;

// An application id as Fcitx reports it: [A-Za-z0-9._-], 1..128.
inline bool appIdentifier(std::string_view id) noexcept
{
    if (id.empty() || id.size() > 128) return false;
    for (const unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return false;
    return true;
}

// An id that may be on the list: valid, and never a terminal.
inline bool listableApp(std::string_view id) noexcept { return appIdentifier(id) && !terminalApp(id); }

// Positions of the list entries that count, in list order: listable, first of
// their kind, at most kMaxApps.
inline std::vector<std::size_t> keptApplications(const std::vector<std::string_view> &ids)
{
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < ids.size() && kept.size() < kMaxApps; ++i) {
        if (!listableApp(ids[i])) continue;
        const bool repeat = std::any_of(kept.begin(), kept.end(), [&](std::size_t k) { return ids[k] == ids[i]; });
        if (!repeat) kept.push_back(i);
    }
    return kept;
}

// Positions of the exception words that count, in list order: a valid word, the
// first with its keys (so "ok", "OK" and "щл" are one entry), at most
// kMaxExceptions. The cap counts distinct entries, never the raw list.
inline std::vector<std::size_t> keptExceptions(const std::vector<std::string_view> &words)
{
    std::vector<std::size_t> kept;
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < words.size() && kept.size() < kMaxExceptions; ++i) {
        std::string key = manual::exceptionKeys(words[i]);
        if (key.empty() || std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        keys.push_back(std::move(key));
        kept.push_back(i);
    }
    return kept;
}

} // namespace rasklad::config
