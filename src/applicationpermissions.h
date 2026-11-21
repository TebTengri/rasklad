// SPDX-License-Identifier: MIT
#pragma once
#include "applicationpolicy.h"
#include <array>
#include <string>

namespace rasklad::policy {
// Event-thread-only permission store. Registry is trusted adapter configuration,
// not user input. Application ids are copied at construction and never changed.
// Caller performs explicit alias canonicalization before lookup; no inference.
// Returned Permission string views remain valid until this store is destroyed.
// Permission modes/revisions are VALUE snapshots: revocation cannot invalidate
// previously copied values. Fetch freshly at every authorization/revalidation
// boundary and after callbacks. Never treat string lifetime as permission freshness.
// Publish/revoke returns before any caller adapter callbacks may run.
class ApplicationPermissions final {
public:
    explicit ApplicationPermissions(Registry registry);
    ApplicationPermissions(const ApplicationPermissions &) = delete;
    ApplicationPermissions &operator=(const ApplicationPermissions &) = delete;
    bool ready() const noexcept { return ready_; }
    Permission snapshot(std::string_view applicationId) const noexcept;
    // Mode::Automatic or Mode::Manual; anything else is refused.
    bool enable(std::string_view applicationId, Mode mode = Mode::Automatic) noexcept;
    bool revoke(std::string_view applicationId) noexcept;
    bool revokeAll() noexcept;
private:
    struct Entry {
        std::string id;
        Mode mode = Mode::Off;
        std::uint64_t revision = 1;
    };
    Entry *find(std::string_view id) noexcept;
    const Entry *find(std::string_view id) const noexcept;
    bool exhaust() noexcept;
    std::array<Entry, 64> entries_;
    std::size_t size_ = 0;
    bool ready_ = false;
};
} // namespace rasklad::policy
