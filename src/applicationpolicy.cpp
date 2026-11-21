// SPDX-License-Identifier: MIT
#include "applicationpolicy.h"

namespace rasklad::policy {
namespace {
constexpr std::size_t kMaxAdapters = 64;
bool identifier(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128) return false;
    for (const unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}
bool validDomain(Domain domain) noexcept
{
    return domain == Domain::RegularApplication || domain == Domain::OwnedFixture;
}
struct Match { Reason reason; std::size_t index = 0; };
Match inspect(Registry registry, const Context &context, Mode mode) noexcept
{
    if (!registry.revision || registry.size > kMaxAdapters ||
        (registry.size && !registry.entries)) return {Reason::InvalidRegistry};
    for (std::size_t i = 0; i < registry.size; ++i) {
        const auto &entry = registry.entries[i];
        if (!identifier(entry.id) || !identifier(entry.applicationId) || !validDomain(entry.domain))
            return {Reason::InvalidRegistry};
        for (std::size_t j = 0; j < i; ++j)
            if (entry.id == registry.entries[j].id) return {Reason::InvalidRegistry};
    }
    std::size_t index = 0;
    while (index < registry.size && registry.entries[index].id != context.adapterId) ++index;
    if (index == registry.size) return {Reason::UnknownAdapter};
    const auto &entry = registry.entries[index];
    // An owned-fixture contract is never an authenticated ordinary application.
    const bool identity =
        (entry.domain == Domain::OwnedFixture && context.identity == Identity::OwnedFixtureContract) ||
        (entry.domain == Domain::RegularApplication && context.identity == Identity::VerifiedAdapter) ||
        // A compositor app-id claim is enough only for the user's own per-word gesture.
        (entry.domain == Domain::RegularApplication && mode == Mode::Manual && entry.manual &&
         context.identity == Identity::CompositorAppId);
    if (!identity || !validDomain(context.domain) || context.domain != entry.domain ||
        context.declaredApplicationId != entry.applicationId ||
        context.verifiedApplicationId != entry.applicationId || !context.identityBinding)
        return {Reason::IdentityUnproved};
    // Absence of a password flag is insufficient; only explicit prose qualifies.
    // Fcitx cannot tell prose from other text in a regular application; the manual
    // gesture accepts Unknown, never a field known to be protected/code/etc.
    const bool field = context.field == Field::Prose ||
        (mode == Mode::Manual && context.field == Field::Unknown);
    if (!field) return {Reason::ProtectedContext};
    if (!context.focused || !context.id || !context.epoch) return {Reason::ContextUnready};
    return {Reason::Allowed, index};
}
bool sameContext(Registry registry, const Context &context, const Lease &lease,
                 std::size_t index) noexcept
{
    return lease.valid && lease.permissionRevision && lease.adapterIndex == index &&
           lease.registryRevision == registry.revision && lease.contextId == context.id &&
           lease.contextEpoch == context.epoch && lease.identityBinding == context.identityBinding;
}
} // namespace

Decision authorizeCapture(Registry registry, const Permission &permission,
                          const Context &context) noexcept
{
    // Off has no need to inspect adapter records or invoke any adapter operation.
    if (!permission.globalEnabled || permission.mode == Mode::Off || !permission.revision)
        return {Reason::Disabled, {}};
    if (permission.mode != Mode::Automatic && permission.mode != Mode::Manual)
        return {Reason::UnsupportedMode, {}};
    const auto match = inspect(registry, context, permission.mode);
    if (match.reason != Reason::Allowed) return {match.reason, {}};
    const auto &entry = registry.entries[match.index];
    if (permission.applicationId != entry.applicationId) return {Reason::IdentityUnproved, {}};
    if (permission.mode == Mode::Automatic ? !entry.automatic : !entry.manual)
        return {Reason::UnsupportedMode, {}};
    Lease lease{true, match.index, registry.revision, permission.revision,
                context.id, context.epoch, context.identityBinding};
    return {Reason::Allowed, lease};
}

Decision revalidateCorrection(Registry registry, const Permission &permission,
                             const Context &context, const Lease &lease) noexcept
{
    auto decision = authorizeCapture(registry, permission, context);
    if (!decision.allowed()) return decision;
    if (!sameContext(registry, context, lease, decision.lease.adapterIndex) ||
        lease.permissionRevision != permission.revision) return {Reason::StaleLease, {}};
    return decision;
}

DisableAction disableDisposition(Registry registry, const Context &context,
                                 const Lease &lease, bool composing, Mode mode) noexcept
{
    if (!composing) return DisableAction::DisableNow;
    if (mode != Mode::Automatic && mode != Mode::Manual) return DisableAction::DeferUntilIdle;
    const auto match = inspect(registry, context, mode);
    if (match.reason != Reason::Allowed ||
        !sameContext(registry, context, lease, match.index) ||
        !registry.entries[match.index].canFinishOriginal) return DisableAction::DeferUntilIdle;
    return DisableAction::FinishOriginalThenDisable;
}
} // namespace rasklad::policy
