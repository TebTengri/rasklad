// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rasklad::policy {
// No I/O, process authentication, context discovery or text operations here.
// Registry snapshots are trusted immutable adapter records, NEVER user settings.
// Context evidence must come from the adapter. Declared app-id alone cannot grant.
enum class Mode { Off, ManualDoubleSuper, Automatic };
enum class Domain { RegularApplication, OwnedFixture };
enum class Identity { Unknown, DeclaredOnly, VerifiedAdapter, OwnedFixtureContract };
enum class Field { Unknown, Prose, Password, Code, Url, Command, Mention, Protected };
enum class Reason { Allowed, Disabled, UnsupportedMode, InvalidRegistry, UnknownAdapter,
                    IdentityUnproved, ProtectedContext, ContextUnready, StaleLease };
enum class DisableAction { DisableNow, FinishOriginalThenDisable, DeferUntilIdle };
struct Adapter {
    std::string_view id;
    std::string_view applicationId;
    Domain domain = Domain::RegularApplication;
    bool automatic = false;
    bool canFinishOriginal = false;
};
// Revision must change for each immutable registry snapshot; never reuse it.
struct Registry {
    const Adapter *entries = nullptr;
    std::size_t size = 0;
    std::uint64_t revision = 0;
};
// Preference store increments revision for every permission change.
struct Permission {
    bool globalEnabled = false;
    std::string_view applicationId;
    Mode mode = Mode::Off;
    std::uint64_t revision = 0;
};
struct Context {
    std::string_view adapterId;
    std::string_view declaredApplicationId;
    std::string_view verifiedApplicationId;
    Domain domain = Domain::RegularApplication;
    Identity identity = Identity::Unknown;
    Field field = Field::Unknown;
    bool focused = false;
    std::uint64_t id = 0;
    // Adapter increments epoch on focus/lifecycle changes, including away/back.
    std::uint64_t epoch = 0;
    std::uint64_t identityBinding = 0;
};
// Numeric lease holds no text or borrowed string views. Revalidate immediately
// before mutation; it is not an enduring authority across callbacks/events.
struct Lease {
    bool valid = false;
    std::size_t adapterIndex = 0;
    std::uint64_t registryRevision = 0;
    std::uint64_t permissionRevision = 0;
    std::uint64_t contextId = 0;
    std::uint64_t contextEpoch = 0;
    std::uint64_t identityBinding = 0;
};
struct Decision {
    Reason reason = Reason::Disabled;
    Lease lease;
    bool allowed() const noexcept { return reason == Reason::Allowed && lease.valid; }
};
// Empty/missing registry or user permission denies. Manual is unavailable in
// this stage, NEVER interpreted as permission for automatic capture/correction.
Decision authorizeCapture(Registry, const Permission &, const Context &) noexcept;
Decision revalidateCorrection(Registry, const Permission &, const Context &, const Lease &) noexcept;
// Turning Off forbids new capture/correction immediately. Existing composition
// is finished as ORIGINAL only with a still-bound lease/capability, otherwise
// defer adapter teardown until its native composition completion. No write here.
DisableAction disableDisposition(Registry, const Context &, const Lease &, bool composing) noexcept;
} // namespace rasklad::policy
