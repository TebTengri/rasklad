// SPDX-License-Identifier: MIT
#pragma once

// Developer-only bridge from the Fcitx LAB addon to the existing verified
// evidence core (LocalEvidenceProvider + routing::route, SafeConfirmed).
// Not a detector: no word rules, no synthetic evidence. Inert if the pinned
// profile does not load. Single-threaded: create, use and destroy on the
// Fcitx main thread. No Qt types are exposed.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rasklad::fcitxlab {

class ProfileDecision final
{
public:
    ProfileDecision() noexcept;
    ~ProfileDecision();
    ProfileDecision(const ProfileDecision &) = delete;
    ProfileDecision &operator=(const ProfileDecision &) = delete;

    [[nodiscard]] bool ready() const noexcept;
    // Candidate only for RouteAction::Correct; nullopt keeps the original.
    [[nodiscard]] std::optional<std::string> correct(std::string_view word) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rasklad::fcitxlab
