// SPDX-License-Identifier: MIT
#include "compositiondisable.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
namespace { unsigned assertions = 0; }
#undef assert
#define assert(condition) do { ++assertions; if (!(condition)) std::abort(); } while (false)
using namespace rasklad::policy;
struct Fixture {
    Adapter adapter{"adapter", "app", Domain::RegularApplication, true, true};
    Registry registry{&adapter, 1, 1};
    Permission permission{true, "app", Mode::Automatic, 1};
    Context context{"adapter", "app", "app", Domain::RegularApplication,
                    Identity::VerifiedAdapter, Field::Prose, true, 1, 1, 1};
    CompositionDisable controller;
    CompositionDisable::Token start() {
        assert(controller.enable());
        auto decision = controller.capture(registry, permission, context);
        assert(decision.allowed());
        auto generation = controller.bind(registry, permission, context);
        assert(generation);
        return generation;
    }
};
int main()
{
    {
        Fixture f;
        assert(f.controller.mayTeardown());
        assert(!f.controller.capture(f.registry, f.permission, f.context).allowed());
        assert(!f.controller.bind(f.registry, f.permission, f.context));
        f.controller.requestOff();
        assert(!f.controller.takeOriginal(f.registry, f.context));
        assert(f.controller.enable());
        assert(!f.controller.bind(f.registry, Permission{}, f.context));
        f.controller.requestOff();
        assert(f.controller.mayTeardown());
    }
    for (int change = 0; change < 8; ++change) {
        Fixture f;
        auto generation = f.start();
        assert(!f.controller.bind(f.registry, f.permission, f.context));
        f.controller.requestOff();
        f.controller.requestOff();
        assert(!f.controller.enable());
        assert(!f.controller.capture(f.registry, f.permission, f.context).allowed());
        assert(!f.controller.correction(f.registry, f.permission, f.context).allowed());
        switch (change) {
        case 0: ++f.context.epoch; break;
        case 1: ++f.context.id; break;
        case 2: f.context.identityBinding = 0; break;
        case 3: f.context.field = Field::Password; break;
        case 4: f.context.focused = false; break;
        case 5: ++f.registry.revision; break;
        case 6: f.adapter.canFinishOriginal = false; break;
        case 7: f.context.identity = Identity::DeclaredOnly; break;
        }
        assert(!f.controller.takeOriginal(f.registry, f.context));
        assert(f.controller.state() == CompositionDisable::State::Deferred);
        assert(!f.controller.mayTeardown());
        assert(!f.controller.takeOriginal(f.registry, f.context));
        assert(!f.controller.nativeIdle(generation + 1));
        assert(f.controller.nativeIdle(generation));
        assert(f.controller.mayTeardown());
    }
    for (auto result : {CompositionDisable::Result::Completed,
                        CompositionDisable::Result::NotWritten,
                        CompositionDisable::Result::Unknown,
                        CompositionDisable::Result::Submitted}) {
        Fixture f;
        auto generation = f.start();
        assert(f.controller.correction(f.registry, f.permission, f.context).allowed());
        f.controller.requestOff();
        auto ticket = f.controller.takeOriginal(f.registry, f.context);
        assert(ticket && ticket != generation);
        // Simulate callback reentry before it reports any outcome.
        f.controller.requestOff();
        assert(!f.controller.enable());
        assert(!f.controller.takeOriginal(f.registry, f.context));
        assert(!f.controller.complete(ticket + 1, result));
        assert(!f.controller.mayTeardown());
        assert(f.controller.complete(ticket, result));
        assert(!f.controller.complete(ticket, CompositionDisable::Result::Completed));
        if (result != CompositionDisable::Result::Completed &&
            result != CompositionDisable::Result::Submitted) {
            assert(!f.controller.mayTeardown());
            assert(!f.controller.takeOriginal(f.registry, f.context));
            assert(f.controller.nativeIdle(generation));
        }
        assert(f.controller.mayTeardown());
        auto newer = f.start();
        assert(newer != generation);
        assert(!f.controller.nativeIdle(generation));
        assert(!f.controller.complete(ticket, CompositionDisable::Result::Completed));
        assert(f.controller.nativeIdle(newer));
        assert(f.controller.state() == CompositionDisable::State::Idle);
    }
    {
        Fixture f;
        auto generation = f.start();
        ++f.permission.revision;
        assert(!f.controller.capture(f.registry, f.permission, f.context).allowed());
        f.controller.requestOff();
        // Permission revocation does not replace the original composition lease.
        auto ticket = f.controller.takeOriginal(f.registry, f.context);
        assert(ticket);
        assert(f.controller.nativeIdle(generation));
        assert(!f.controller.mayTeardown());
        assert(!f.controller.enable());
        assert(f.controller.complete(ticket, CompositionDisable::Result::Unknown));
        assert(f.controller.mayTeardown());
    }
    {
        Fixture f;
        assert(f.controller.enable());
        const auto old = f.controller.capture(f.registry, f.permission, f.context);
        assert(old.allowed());
        f.controller.requestOff();
        ++f.permission.revision;
        f.permission.mode = Mode::Off;
        assert(f.controller.enable());
        // bind has no cached-Decision overload: current permission is checked.
        assert(!f.controller.bind(f.registry, f.permission, f.context));
        assert(!f.controller.correction(f.registry, f.permission, f.context).allowed());
    }
    std::printf("{\"assertions\":%u,\"failures\":0}\n", assertions);
}
