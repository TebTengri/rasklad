// SPDX-License-Identifier: MIT
#include "applicationpermissions.h"
#include <limits>

namespace rasklad::policy {
namespace {
bool identifier(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
}
ApplicationPermissions::ApplicationPermissions(Registry registry) {
    if (!registry.revision || !registry.entries || !registry.size || registry.size > entries_.size()) return;
    for (std::size_t i = 0; i < registry.size; ++i) {
        const auto &adapter = registry.entries[i];
        if (!identifier(adapter.id) || !identifier(adapter.applicationId) ||
            (adapter.domain != Domain::RegularApplication && adapter.domain != Domain::OwnedFixture)) return;
        for (std::size_t j = 0; j < i; ++j)
            if (registry.entries[j].id == adapter.id) return;
        if (!find(adapter.applicationId)) entries_[size_++].id = adapter.applicationId;
    }
    ready_ = true;
}
ApplicationPermissions::Entry *ApplicationPermissions::find(std::string_view id) noexcept {
    for (std::size_t i = 0; i < size_; ++i) if (entries_[i].id == id) return &entries_[i];
    return nullptr;
}
const ApplicationPermissions::Entry *ApplicationPermissions::find(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < size_; ++i) if (entries_[i].id == id) return &entries_[i];
    return nullptr;
}
Permission ApplicationPermissions::snapshot(std::string_view id) const noexcept {
    if (!ready_) return {};
    const auto *entry = find(id);
    if (!entry) return {};
    return {true, entry->id, entry->mode, entry->revision};
}
bool ApplicationPermissions::exhaust() noexcept {
    ready_ = false;
    for (std::size_t i = 0; i < size_; ++i) entries_[i].mode = Mode::Off;
    return false;
}
bool ApplicationPermissions::enable(std::string_view id, Mode mode) noexcept {
    if (!ready_ || (mode != Mode::Automatic && mode != Mode::Manual)) return false;
    auto *entry = find(id);
    if (!entry) return false;
    if (entry->mode == mode) return true;
    if (entry->revision == std::numeric_limits<std::uint64_t>::max()) return exhaust();
    ++entry->revision;
    entry->mode = mode;
    return true;
}
bool ApplicationPermissions::revoke(std::string_view id) noexcept {
    if (!ready_) return false;
    auto *entry = find(id);
    if (!entry) return false;
    if (entry->mode == Mode::Off) return true;
    if (entry->revision == std::numeric_limits<std::uint64_t>::max()) return exhaust();
    ++entry->revision;
    entry->mode = Mode::Off;
    return true;
}
bool ApplicationPermissions::revokeAll() noexcept {
    if (!ready_) return false;
    // Preflight every change before publishing any; this method invokes no code
    // outside the store. Exhaustion permanently denies all later operations.
    for (std::size_t i = 0; i < size_; ++i)
        if (entries_[i].mode != Mode::Off &&
            entries_[i].revision == std::numeric_limits<std::uint64_t>::max()) return exhaust();
    for (std::size_t i = 0; i < size_; ++i) {
        auto &entry = entries_[i];
        if (entry.mode != Mode::Off) { ++entry.revision; entry.mode = Mode::Off; }
    }
    return true;
}
} // namespace rasklad::policy
