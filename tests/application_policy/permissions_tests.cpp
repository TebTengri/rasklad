// SPDX-License-Identifier: MIT
#include "../../src/applicationpermissions.h"
#include <cstdlib>
namespace { unsigned checks = 0; }
#define assert(value) do { ++checks; if (!(value)) std::abort(); } while (false)
#include <iostream>
#include <string>

using namespace rasklad::policy;
int main() {
    const Adapter adapters[] = {
        {"a", "app.a", Domain::OwnedFixture, true, true},
        {"a.second", "app.a", Domain::OwnedFixture, true, true},
        {"b", "app.b", Domain::OwnedFixture, true, true}
    };
    const Registry registry{adapters, 3, 1};
    ApplicationPermissions permissions(registry);
    assert(permissions.ready());
    const Context a{"a", "app.a", "app.a", Domain::OwnedFixture,
        Identity::OwnedFixtureContract, Field::Prose, true, 1, 1, 1};
    const Context b{"b", "app.b", "app.b", Domain::OwnedFixture,
        Identity::OwnedFixtureContract, Field::Prose, true, 2, 1, 2};
    const auto initial = permissions.snapshot("app.a");
    assert(initial.mode == Mode::Off && initial.revision);
    assert(!authorizeCapture(registry, initial, a).allowed());
    assert(!permissions.enable("app.unknown"));
    assert(!permissions.revoke("app.unknown"));
    assert(!permissions.snapshot("app.unknown").revision);
    assert(!permissions.enable("APP.A")); // Aliases are explicit caller policy.
    assert(permissions.revoke("app.a"));
    assert(permissions.snapshot("app.a").revision == initial.revision);
    assert(permissions.enable("app.a"));
    assert(permissions.enable("app.b"));
    const auto enabledA = permissions.snapshot("app.a");
    const auto enabledB = permissions.snapshot("app.b");
    const auto leaseA = authorizeCapture(registry, enabledA, a).lease;
    const auto leaseB = authorizeCapture(registry, enabledB, b).lease;
    assert(leaseA.valid && leaseB.valid);
    assert(permissions.enable("app.a"));
    assert(permissions.snapshot("app.a").revision == enabledA.revision);
    // Manual is a separate mode: switching changes the revision; other modes refused.
    assert(!permissions.enable("app.a", Mode::Off));
    assert(!permissions.enable("app.a", static_cast<Mode>(99)));
    assert(permissions.snapshot("app.a").mode == Mode::Automatic);
    assert(permissions.enable("app.a", Mode::Manual));
    assert(permissions.snapshot("app.a").mode == Mode::Manual);
    assert(permissions.snapshot("app.a").revision == enabledA.revision + 1);
    assert(!revalidateCorrection(registry, permissions.snapshot("app.a"), a, leaseA).allowed());
    assert(permissions.enable("app.a"));
    assert(permissions.snapshot("app.a").mode == Mode::Automatic);
    assert(permissions.revoke("app.a"));
    const auto offA = permissions.snapshot("app.a");
    assert(offA.revision > enabledA.revision);
    // Copies are not live capabilities: old snapshot stays enabled. Adapter
    // must fetch freshly before each operation, including after callbacks.
    assert(enabledA.mode == Mode::Automatic);
    assert(revalidateCorrection(registry, enabledA, a, leaseA).allowed());
    assert(!revalidateCorrection(registry, offA, a, leaseA).allowed());
    assert(revalidateCorrection(registry, permissions.snapshot("app.b"), b, leaseB).allowed());
    assert(permissions.revoke("app.a"));
    assert(permissions.snapshot("app.a").revision == offA.revision);
    assert(permissions.enable("app.a"));
    assert(permissions.snapshot("app.a").revision > offA.revision);
    assert(!revalidateCorrection(registry, permissions.snapshot("app.a"), a, leaseA).allowed());
    const auto newLeaseA = authorizeCapture(registry, permissions.snapshot("app.a"), a).lease;
    assert(newLeaseA.valid);
    assert(permissions.revokeAll());
    assert(!revalidateCorrection(registry, permissions.snapshot("app.a"), a, newLeaseA).allowed());
    assert(!revalidateCorrection(registry, permissions.snapshot("app.b"), b, leaseB).allowed());
    const auto globalA = permissions.snapshot("app.a").revision;
    const auto globalB = permissions.snapshot("app.b").revision;
    assert(permissions.revokeAll());
    assert(permissions.snapshot("app.a").revision == globalA);
    assert(permissions.snapshot("app.b").revision == globalB);
    assert(permissions.enable("app.a"));
    assert(permissions.snapshot("app.b").mode == Mode::Off);
    assert(!revalidateCorrection(registry, permissions.snapshot("app.a"), a, newLeaseA).allowed());

    // Immutable ids are owned, including when registry backing strings change.
    std::string source = "owned.application";
    Adapter temporary{"owned", source, Domain::OwnedFixture, true, true};
    ApplicationPermissions owned({&temporary, 1, 1});
    const auto borrowed = owned.snapshot(source);
    source.assign("changed");
    assert(borrowed.applicationId == "owned.application");
    assert(owned.enable("owned.application"));
    assert(borrowed.applicationId == "owned.application");
    assert(!owned.enable("changed"));
    ApplicationPermissions missing({});
    assert(!missing.ready() && !missing.enable("app.a") && !missing.revokeAll());
    const Adapter duplicate[] = {adapters[0], adapters[0]};
    ApplicationPermissions invalid({duplicate, 2, 1});
    assert(!invalid.ready() && !invalid.snapshot("app.a").revision);
    for (const Registry malformed : {Registry{nullptr,1,1}, Registry{adapters,65,1}, Registry{adapters,3,0}}) {
        ApplicationPermissions rejected(malformed);
        assert(!rejected.ready());
        assert(!rejected.snapshot("app.a").revision);
    }
    Adapter malformed = adapters[0]; malformed.applicationId = "invalid id";
    ApplicationPermissions rejected({&malformed,1,1});
    assert(!rejected.ready());
    std::cout << "{\"permissionAssertions\":" << checks << ",\"failures\":0}\n";
}
