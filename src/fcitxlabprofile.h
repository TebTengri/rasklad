// SPDX-License-Identifier: MIT
#pragma once

// Developer-only bridge from the Fcitx LAB addon to the existing verified
// evidence core (LocalEvidenceProvider + routing::route, SafeConfirmed).
// Not a detector: no word rules, no synthetic evidence. Inert if the pinned
// profile does not load. Single-threaded: create, use and destroy on the
// Fcitx main thread. No Qt types are exposed.

#include "applicationpolicy.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rasklad::fcitxlab {

class ProfileDecision final
{
public:
    // root: directory holding the pinned profile files at their relative paths.
    // unreviewedUpdate: tools/product_eval only (ProfilePins::requireReleaseApproval).
    explicit ProfileDecision(const char *root = "/lab/profile", bool unreviewedUpdate = false) noexcept;
    ~ProfileDecision();
    ProfileDecision(const ProfileDecision &) = delete;
    ProfileDecision &operator=(const ProfileDecision &) = delete;

    [[nodiscard]] bool ready() const noexcept;
    // Candidate only for RouteAction::Correct; nullopt keeps the original.
    [[nodiscard]] std::optional<std::string> correct(std::string_view word, policy::Field field) noexcept;
    // SYS-226: typed is the exact UTF-8 text of one word as typed in the given layout
    // (us keys, or their ru-layout Cyrillic). Only the safeConfirmed Correct yields a value.
    // The caller has already refused protected fields; this asks for prose routing.
    [[nodiscard]] std::optional<std::string> correctTyped(std::string_view typed, bool typedInRu) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rasklad::fcitxlab
