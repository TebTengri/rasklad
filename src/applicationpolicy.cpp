// SPDX-License-Identifier: MIT
#include "applicationpolicy.h"
#include "configrules.h"

namespace rasklad::policy {
namespace {
constexpr std::size_t kMaxAdapters = config::kMaxApps;
bool identifier(std::string_view value) noexcept { return config::appIdentifier(value); }
bool validDomain(Domain domain) noexcept
{
    return domain == Domain::RegularApplication || domain == Domain::OwnedFixture;
}
struct Match { Reason reason; std::size_t index = 0; };
bool registryValid(Registry registry) noexcept
{
    if (!registry.revision || registry.size > kMaxAdapters ||
        (registry.size && !registry.entries)) return false;
    // SYS-255: a snapshot never changes under its revision (the Registry contract),
    // so its structure is checked once, not on every key: with 64 entries the
    // repeat check alone is ~2000 comparisons. Keyed by all three fields.
    struct Seen { const Adapter *entries = nullptr; std::size_t size = 0; std::uint64_t revision = 0; };
    thread_local Seen valid;
    if (valid.revision == registry.revision && valid.entries == registry.entries && valid.size == registry.size)
        return true;
    for (std::size_t i = 0; i < registry.size; ++i) {
        const auto &entry = registry.entries[i];
        if (!identifier(entry.id) || !identifier(entry.applicationId) || !validDomain(entry.domain))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (entry.id == registry.entries[j].id) return false;
    }
    valid = {registry.entries, registry.size, registry.revision};
    return true;
}
Match inspect(Registry registry, const Context &context, Mode mode) noexcept
{
    if (!registryValid(registry)) return {Reason::InvalidRegistry};
    std::size_t index = 0;
    while (index < registry.size && registry.entries[index].id != context.adapterId) ++index;
    if (index == registry.size) return {Reason::UnknownAdapter};
    const auto &entry = registry.entries[index];
    // An owned-fixture contract is never an authenticated ordinary application.
    const bool identity =
        (entry.domain == Domain::OwnedFixture && context.identity == Identity::OwnedFixtureContract) ||
        (entry.domain == Domain::RegularApplication && context.identity == Identity::VerifiedAdapter) ||
        // A compositor app-id claim: the owner's own list, for the per-word gesture and,
        // where the adapter opts in, automatic correction (owner decision, SYS-226).
        (entry.domain == Domain::RegularApplication && context.identity == Identity::CompositorAppId &&
         ((mode == Mode::Manual && entry.manual) || (mode == Mode::Automatic && entry.automatic)));
    if (!identity || !validDomain(context.domain) || context.domain != entry.domain ||
        context.declaredApplicationId != entry.applicationId ||
        context.verifiedApplicationId != entry.applicationId || !context.identityBinding)
        return {Reason::IdentityUnproved};
    // Absence of a password flag is insufficient; only explicit prose qualifies.
    // Fcitx cannot tell prose from other text in a regular application: with a
    // compositor app-id claim, Unknown is accepted; a field known to be protected,
    // code, URL, command or mention never is. Verified adapters still need Prose.
    const bool field = context.field == Field::Prose ||
        (context.identity == Identity::CompositorAppId && context.field == Field::Unknown);
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
