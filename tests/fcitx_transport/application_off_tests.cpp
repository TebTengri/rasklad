// SPDX-License-Identifier: MIT
// Actual SDK, synthetic owned fixture memory; no daemon or runtime transport.
#include "../../src/fcitxlab.cpp"
#include <functional>
#include <iostream>
namespace {
#define require(x) do { if (!(x)) { std::cerr << "application Off line " << __LINE__ << '\n'; std::abort(); } } while (false)
struct ApplicationOffTest {
    static LabState &state(RaskladLab &lab, fcitx::InputContext *ic) { return *lab.state(ic); }
    static bool holderEmpty(RaskladLab &lab, fcitx::InputContext *ic) {
        auto *holder = static_cast<LabProperty *>(ic->propertyFor(&lab.factory_));
        return holder && !holder->state;
    }
    static LabState *peek(RaskladLab &lab, fcitx::InputContext *ic) { return lab.state(ic, false); }
    static bool releaseWithoutCapture(RaskladLab &lab, fcitx::InputContext *ic) {
        fcitx::KeyEvent event(ic, fcitx::Key(FcitxKey_a), true);
        lab.handleKey(event);
        return event.filtered();
    }
    static bool rawKey(RaskladLab &lab, fcitx::InputContext *ic, fcitx::KeySym sym, bool release = false) {
        fcitx::KeyEvent event(ic, fcitx::Key(sym), release);
        lab.handleKey(event);
        return event.filtered();
    }
    static bool key(RaskladLab &lab, fcitx::InputContext *ic, bool release = false) {
        fcitx::KeyEvent event(ic, fcitx::Key(FcitxKey_a), release);
        lab.handleKey(event);
        if (release) require(event.filtered());
        return event.filtered();
    }
    static bool delimiter(RaskladLab &lab, fcitx::InputContext *ic) {
        fcitx::KeyEvent event(ic, fcitx::Key(FcitxKey_space));
        lab.handleKey(event);
        return event.filtered();
    }
    static void reset(RaskladLab &lab, fcitx::InputContext *ic) {
        if (auto *s = peek(lab, ic)) RaskladLab::discard(ic, *s);
    }
    static void off(RaskladLab &lab, fcitx::InputContext *ic) { lab.requestLabOff(ic); }
    static auto context(fcitx::InputContext *ic, const LabState &s) { return RaskladLab::fixtureContext(ic, s); }
    static bool on(RaskladLab &lab) { return lab.requestLabOn(); }
    static auto permission(RaskladLab &lab, const char *id) { return lab.permissions_.snapshot(id); }
};
class Context final : public fcitx::InputContext {
public:
    Context(fcitx::InputContextManager &m, const char *program) : InputContext(m, program) {
        created(); setCapabilityFlags(fcitx::CapabilityFlag::Preedit); focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "application-off-test"; }
    std::function<void()> callback;
    unsigned writes = 0;
protected:
    void commitStringImpl(const std::string &text) override {
        require(text == "a"); ++writes;
        auto copy = callback;
        if (copy) copy();
    }
    void updatePreeditImpl() override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { require(false); }
    void deleteSurroundingTextImpl(int, unsigned) override { require(false); }
};
void scenario(int mode) {
    fcitx::InputContextManager manager;
    RaskladLab lab(manager);
    auto first = std::make_unique<Context>(manager, kLabProgram);
    auto second = std::make_unique<Context>(manager, "chromium-browser");
    auto other = std::make_unique<Context>(manager, "unregistered-fixture");
    for (auto *context : {first.get(), second.get(), other.get()}) {
        require(!ApplicationOffTest::peek(lab, context));
        require(!ApplicationOffTest::releaseWithoutCapture(lab, context));
        ApplicationOffTest::reset(lab, context);
        require(!ApplicationOffTest::peek(lab, context));
    }
    require(ApplicationOffTest::key(lab, first.get()));
    require(ApplicationOffTest::key(lab, second.get()));
    require(ApplicationOffTest::state(lab, first.get()).word() == "a");
    require(ApplicationOffTest::state(lab, second.get()).word() == "a");
    auto operation1 = ApplicationOffTest::state(lab, first.get()).disable;
    auto operation2 = ApplicationOffTest::state(lab, second.get()).disable;
    const auto oldLease = ApplicationOffTest::state(lab, first.get()).lease;
    const auto oldPermission = ApplicationOffTest::permission(lab, kLabProgram);
    unsigned callbacks = 0;
    std::unique_ptr<Context> fresh;
    auto callback = [&](Context *sender) {
        ++callbacks;
        require(!ApplicationOffTest::on(lab)); // InFlight blocks family re-enable.
        require(ApplicationOffTest::permission(lab, kLabProgram).mode == policy::Mode::Off);
        require(ApplicationOffTest::permission(lab, "chromium-browser").mode == policy::Mode::Off);
        // Both controllers must already be stopped before the first write.
        require(operation1->state() != policy::CompositionDisable::State::Active);
        require(operation2->state() != policy::CompositionDisable::State::Active);
        fresh = std::make_unique<Context>(manager, "chromium-browser");
        require(!ApplicationOffTest::key(lab, fresh.get()));
        require(!ApplicationOffTest::peek(lab, fresh.get()));
        if (mode == 1) {
            // Destroy whichever other context has not yet submitted.
            if (!first->writes) first.reset();
            else if (!second->writes) second.reset();
        } else if (mode == 2) {
            auto *target = sender == first.get() ? second.get() : first.get();
            // A lifecycle reset after staging invalidates the original lease.
            ApplicationOffTest::state(lab, target).invalidate();
        } else if (mode == 3) {
            auto *target = sender == first.get() ? second.get() : first.get();
            target->focusOut();
        } else if (mode == 4) {
            auto *target = sender == first.get() ? second.get() : first.get();
            target->setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::Preedit) | fcitx::CapabilityFlag::Sensitive);
        } else {
            auto *target = sender == first.get() ? second.get() : first.get();
            const auto before = ApplicationOffTest::state(lab, target).word();
            require(!ApplicationOffTest::key(lab, target));
            require(ApplicationOffTest::state(lab, target).word() == before);
            ApplicationOffTest::off(lab, target); // Reentrant Off cannot duplicate.
        }
    };
    first->callback = [&] { callback(first.get()); };
    second->callback = [&] { callback(second.get()); };
    ApplicationOffTest::off(lab, first.get());
    require(callbacks == (mode != 0 ? 1U : 2U));
    require(other->writes == 0);
    require(!ApplicationOffTest::peek(lab, other.get()));
    for (auto *context : {first.get(), second.get()}) {
        if (!context) continue;
        if (mode == 0) {
            // Reentrant raw presses superseded both original release records.
            require(!ApplicationOffTest::peek(lab, context));
            require(!ApplicationOffTest::releaseWithoutCapture(lab, context));
        } else {
            require(ApplicationOffTest::peek(lab, context)); // Held release pins payload.
            require(ApplicationOffTest::key(lab, context, true));
            if (context->writes) require(!ApplicationOffTest::peek(lab, context));
            else require(ApplicationOffTest::peek(lab, context)); // Deferred original retained.
        }
    }
    auto *remaining = first ? first.get() : second.get();
    ApplicationOffTest::off(lab, remaining);
    require(callbacks == (mode != 0 ? 1U : 2U));
    require(!ApplicationOffTest::key(lab, fresh.get()));
    require(!ApplicationOffTest::peek(lab, fresh.get()));
    if (mode < 2) {
        require(ApplicationOffTest::on(lab));
        const auto permission = ApplicationOffTest::permission(lab, kLabProgram);
        require(permission.mode == policy::Mode::Automatic);
        require(permission.revision > oldPermission.revision);
        require(ApplicationOffTest::permission(lab, "chromium-browser").mode == policy::Mode::Automatic);
        auto createdAfterOn = std::make_unique<Context>(manager, "chromium-browser");
        require(!ApplicationOffTest::peek(lab, fresh.get()));
        require(!ApplicationOffTest::peek(lab, createdAfterOn.get()));
        for (auto *context : {remaining, fresh.get(), createdAfterOn.get()}) {
            require(!ApplicationOffTest::key(lab, context));
            require(ApplicationOffTest::state(lab, context).empty());
            ApplicationOffTest::reset(lab, context);
            require(!ApplicationOffTest::key(lab, context));
            require(ApplicationOffTest::state(lab, context).empty());
            require(!ApplicationOffTest::delimiter(lab, context));
            require(ApplicationOffTest::key(lab, context));
            require(ApplicationOffTest::state(lab, context).word() == "a");
        }
        const auto &state = ApplicationOffTest::state(lab, remaining);
        const auto context = ApplicationOffTest::context(remaining, state);
        require(state.lease.valid && state.lease.permissionRevision == permission.revision);
        require(!policy::revalidateCorrection(kFixtureRegistry, permission, context, oldLease).allowed());
        require(state.word() == "a");
    } else {
        auto *deferred = first->writes ? second.get() : first.get();
        const auto original = ApplicationOffTest::state(lab, deferred).word();
        require(original == "a");
        require(!ApplicationOffTest::on(lab));
        require(ApplicationOffTest::state(lab, deferred).word() == original);
        require(ApplicationOffTest::permission(lab, kLabProgram).mode == policy::Mode::Off);
        require(!ApplicationOffTest::key(lab, fresh.get()));
    }
}
void reclamation() {
    fcitx::InputContextManager manager;
    RaskladLab lab(manager);
    Context empty(manager, kLabProgram), active(manager, "chromium-browser");
    // Explicitly materialize an idle payload: terminal empty Off reclaims it.
    ApplicationOffTest::state(lab, &empty);
    require(ApplicationOffTest::key(lab, &active));
    auto operation = ApplicationOffTest::state(lab, &active).disable;
    auto binding = ApplicationOffTest::state(lab, &active).callbackBinding;
    active.callback = [&] {
        require(operation->state() == policy::CompositionDisable::State::InFlight);
        ApplicationOffTest::key(lab, &active, true); // Drain inside commit callback.
        require(ApplicationOffTest::peek(lab, &active)); // InFlight blocks retirement.
        require(binding->alive);
    };
    ApplicationOffTest::off(lab, &active);
    require(!ApplicationOffTest::peek(lab, &empty));
    require(!ApplicationOffTest::peek(lab, &active));
    require(!binding->alive && operation->mayTeardown());
    require(active.writes == 1);
    ApplicationOffTest::off(lab, &active);
    require(active.writes == 1); // Reclaimed payload cannot replay its original.
    require(!ApplicationOffTest::peek(lab, &active));
    require(ApplicationOffTest::on(lab));
    require(!ApplicationOffTest::peek(lab, &active));
    require(!ApplicationOffTest::key(lab, &active));
    require(ApplicationOffTest::peek(lab, &active));
    require(ApplicationOffTest::state(lab, &active).empty());
    require(!ApplicationOffTest::delimiter(lab, &active));
    require(ApplicationOffTest::key(lab, &active));
}
void supersededRelease() {
    fcitx::InputContextManager manager;
    RaskladLab lab(manager);
    Context context(manager, kLabProgram);
    require(ApplicationOffTest::key(lab, &context));
    auto binding = ApplicationOffTest::state(lab, &context).callbackBinding;
    ApplicationOffTest::off(lab, &context);
    require(context.writes == 1 && ApplicationOffTest::peek(lab, &context));
    // A different raw key cannot cancel the retained 'a' release obligation.
    require(!ApplicationOffTest::rawKey(lab, &context, FcitxKey_b));
    require(!ApplicationOffTest::rawKey(lab, &context, FcitxKey_b, true));
    require(ApplicationOffTest::peek(lab, &context)->hasPending());
    // Simulate missing old key-up, then a new raw down/up of the same key.
    require(!ApplicationOffTest::rawKey(lab, &context, FcitxKey_a));
    require(!ApplicationOffTest::peek(lab, &context));
    require(!binding->alive);
    require(!ApplicationOffTest::rawKey(lab, &context, FcitxKey_a, true));
    require(!ApplicationOffTest::peek(lab, &context));
    ApplicationOffTest::off(lab, &context);
    require(context.writes == 1); // No replay or new payload allocation.
}
void emptyLifecycle() {
    fcitx::InputContextManager manager;
    RaskladLab lab(manager);
    std::vector<std::unique_ptr<Context>> contexts;
    for (int i = 0; i < 16; ++i) {
        contexts.push_back(std::make_unique<Context>(manager, kLabProgram));
        contexts.push_back(std::make_unique<Context>(manager, "unregistered-fixture"));
    }
    auto inspect = [&] {
        for (auto &context : contexts) {
            require(ApplicationOffTest::holderEmpty(lab, context.get()));
            require(!ApplicationOffTest::peek(lab, context.get()));
            require(!ApplicationOffTest::releaseWithoutCapture(lab, context.get()));
            ApplicationOffTest::reset(lab, context.get());
            require(ApplicationOffTest::holderEmpty(lab, context.get()));
        }
    };
    inspect(); inspect();
    ApplicationOffTest::off(lab, contexts.front().get());
    inspect();
    require(ApplicationOffTest::on(lab));
    inspect();
    require(!ApplicationOffTest::key(lab, contexts[1].get()));
    require(ApplicationOffTest::holderEmpty(lab, contexts[1].get()));
    auto *owned = contexts.front().get();
    require(!ApplicationOffTest::key(lab, owned)); // First press after On stays raw.
    require(!ApplicationOffTest::holderEmpty(lab, owned));
    require(ApplicationOffTest::peek(lab, owned));
    require(!ApplicationOffTest::delimiter(lab, owned));
    require(ApplicationOffTest::key(lab, owned));
    for (std::size_t i = 1; i < contexts.size(); ++i)
        require(ApplicationOffTest::holderEmpty(lab, contexts[i].get()));
}
}
int main() {
    std::cout << "ABI layout bytes only (not total allocation/RSS): LabProperty="
              << sizeof(LabProperty) << " LabState=" << sizeof(LabState) << '\n';
    emptyLifecycle(); reclamation(); supersededRelease(); for (int i=0; i<5; ++i) scenario(i); std::cout << "PASS: lazy allocation/reclamation and superseded release plus 5 shared family Off/On scenarios, callback rejection, resume boundaries across reset/new contexts, fresh revisions, deferred preservation, release drain\n"; }
