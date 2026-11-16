// SPDX-License-Identifier: MIT
#include "applicationpolicy.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>
using namespace rasklad::policy;
namespace {
unsigned checks = 0;
void require(bool condition, const char *caseName) {
    ++checks;
    if (!condition) { std::cerr << "policy contract failed: " << caseName << '\n'; std::exit(1); }
}
struct Fixture {
    std::array<Adapter, 2> adapters{{
        {"fixture.editor", "owned.fixture", Domain::OwnedFixture, true, true},
        {"test.adapter", "test.application", Domain::RegularApplication, false, false}
    }};
    Registry registry() const { return {adapters.data(),adapters.size(),7}; }
    Permission permission{true,"owned.fixture",Mode::Automatic,9};
    Context context{"fixture.editor","owned.fixture","owned.fixture",Domain::OwnedFixture,
                    Identity::OwnedFixtureContract,Field::Prose,true,11,13,17};
};
}
int main() {
    static_assert(std::is_trivially_copyable<Lease>::value, "lease numeric/no owning data");
    Fixture f;
    const auto initial = authorizeCapture(f.registry(), f.permission, f.context);
    require(initial.allowed(),"explicit owned-fixture opt-in");
    require(revalidateCorrection(f.registry(),f.permission,f.context,initial.lease).allowed(),"same context");
    require(!authorizeCapture(f.registry(),Permission{},f.context).allowed(),"default off");
    auto p=f.permission;p.globalEnabled=false;
    require(authorizeCapture({nullptr,65,0},p,f.context).reason==Reason::Disabled,"off skips invalid registry");
    for (Mode mode : {Mode::Off,Mode::ManualDoubleSuper,static_cast<Mode>(99)}) {
        p=f.permission;p.mode=mode;
        require(!authorizeCapture(f.registry(),p,f.context).allowed(),"no auto fallback from manual/invalid/off");
    }
    p=f.permission;p.revision=0;
    require(!authorizeCapture(f.registry(),p,f.context).allowed(),"missing permission revision");
    p=f.permission;p.applicationId="test.application";
    require(!authorizeCapture(f.registry(),p,f.context).allowed(),"other application permission");
    for (Field field : {Field::Unknown,Field::Password,Field::Code,Field::Url,Field::Command,
                        Field::Mention,Field::Protected,static_cast<Field>(99)}) {
        auto c=f.context;c.field=field;
        require(!authorizeCapture(f.registry(),f.permission,c).allowed(),"non-prose no capture");
        require(!revalidateCorrection(f.registry(),f.permission,c,initial.lease).allowed(),"non-prose no correction");
        require(disableDisposition(f.registry(),c,initial.lease,true)==DisableAction::DeferUntilIdle,"no protected original write");
    }
    for (Identity identity : {Identity::Unknown,Identity::DeclaredOnly,Identity::VerifiedAdapter,static_cast<Identity>(99)}) {
        auto c=f.context;c.identity=identity;
        require(!authorizeCapture(f.registry(),f.permission,c).allowed(),"declared/spoofed/incorrect scope identity");
    }
    for (int mutation=0;mutation<7;++mutation) {
        auto c=f.context;
        switch(mutation) {
        case 0:c.verifiedApplicationId="spoofed.app";break;
        case 1:c.declaredApplicationId="spoofed.app";break;
        case 2:c.domain=Domain::RegularApplication;break;
        case 3:c.focused=false;break;
        case 4:c.id=0;break;
        case 5:c.epoch=0;break;
        case 6:c.identityBinding=0;break;
        }
        require(!authorizeCapture(f.registry(),f.permission,c).allowed(),"unbound context");
    }
    for (int mutation=0;mutation<7;++mutation) {
        auto l=initial.lease;
        switch(mutation) {
        case 0:l.valid=false;break;
        case 1:++l.adapterIndex;break;
        case 2:++l.registryRevision;break;
        case 3:++l.permissionRevision;break;
        case 4:++l.contextId;break;
        case 5:++l.contextEpoch;break;
        case 6:++l.identityBinding;break;
        }
        require(!revalidateCorrection(f.registry(),f.permission,f.context,l).allowed(),"stale lease");
    }
    auto changed=f.context;++changed.epoch;
    require(!revalidateCorrection(f.registry(),f.permission,changed,initial.lease).allowed(),"changed after capture");
    p=f.permission;++p.revision;
    require(!revalidateCorrection(f.registry(),p,f.context,initial.lease).allowed(),"policy changed before edit");
    auto r=f.registry();++r.revision;
    require(!revalidateCorrection(r,f.permission,f.context,initial.lease).allowed(),"registry changed before edit");
    require(disableDisposition(f.registry(),f.context,initial.lease,false)==DisableAction::DisableNow,"idle teardown");
    require(disableDisposition(f.registry(),f.context,initial.lease,true)==DisableAction::FinishOriginalThenDisable,"original completion only");
    require(disableDisposition(f.registry(),changed,initial.lease,true)==DisableAction::DeferUntilIdle,"changed field cannot complete old word");
    f.adapters[0].canFinishOriginal=false;
    require(disableDisposition(f.registry(),f.context,initial.lease,true)==DisableAction::DeferUntilIdle,"unsupported completion deferred");
    f.adapters[0].automatic=false;
    require(!authorizeCapture(f.registry(),f.permission,f.context).allowed(),"unsupported automatic mode");
    f.adapters[0].automatic=true;
    auto malformed=f.adapters;
    malformed[1].id=malformed[0].id;
    require(!authorizeCapture({malformed.data(),2,1},f.permission,f.context).allowed(),"duplicate record");
    malformed=f.adapters;malformed[1].applicationId="invalid app";
    require(!authorizeCapture({malformed.data(),2,1},f.permission,f.context).allowed(),"malformed unused record");
    require(!authorizeCapture({f.adapters.data(),65,1},f.permission,f.context).allowed(),"bounded registry");
    require(!authorizeCapture({nullptr,1,1},f.permission,f.context).allowed(),"missing records");
    require(!authorizeCapture({nullptr,0,1},f.permission,f.context).allowed(),"empty registry grants no app");
    auto regular=f.context;
    regular.adapterId="test.adapter";regular.declaredApplicationId=regular.verifiedApplicationId="test.application";
    regular.domain=Domain::RegularApplication;
    p=f.permission;p.applicationId="test.application";f.adapters[1].automatic=true;
    require(!authorizeCapture(f.registry(),p,regular).allowed(),"fixture contract cannot authorize regular app");
    regular.identity=Identity::DeclaredOnly;
    require(!authorizeCapture(f.registry(),p,regular).allowed(),"app-id alone never enough");
    std::cout << "{\"passed\":true,\"assertions\":" << checks
              << ",\"realApplicationEnabled\":false,\"textOperations\":false}\n";
}
