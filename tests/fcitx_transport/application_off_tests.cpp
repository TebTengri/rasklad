// SPDX-License-Identifier: MIT
// Actual SDK, synthetic owned fixture memory; no daemon or runtime transport.
#include "../../src/fcitxlab.cpp"
#include <functional>
#include <iostream>
namespace {
#define require(x) do { if (!(x)) { std::cerr << "application Off line " << __LINE__ << '\n'; std::abort(); } } while (false)
struct ApplicationOffTest {
    static LabState &state(RaskladLab &lab, fcitx::InputContext *ic) { return *lab.state(ic); }
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
        RaskladLab::discard(ic, state(lab, ic));
    }
    static void off(RaskladLab &lab, fcitx::InputContext *ic) { lab.requestLabOff(ic, state(lab, ic)); }
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
    auto callback = [&] {
        ++callbacks;
        require(!ApplicationOffTest::on(lab)); // InFlight blocks family re-enable.
        require(ApplicationOffTest::permission(lab, kLabProgram).mode == policy::Mode::Off);
        require(ApplicationOffTest::permission(lab, "chromium-browser").mode == policy::Mode::Off);
        // Both controllers must already be stopped before the first write.
        require(operation1->state() != policy::CompositionDisable::State::Active);
        require(operation2->state() != policy::CompositionDisable::State::Active);
        fresh = std::make_unique<Context>(manager, "chromium-browser");
        require(!ApplicationOffTest::key(lab, fresh.get()));
        require(ApplicationOffTest::state(lab, fresh.get()).empty());
        if (mode == 1) {
            // Destroy whichever other context has not yet submitted.
            if (!first->writes) first.reset();
            else if (!second->writes) second.reset();
        } else if (mode == 2) {
            auto *target = first->writes ? second.get() : first.get();
            // A lifecycle reset after staging invalidates the original lease.
            ApplicationOffTest::state(lab, target).invalidate();
        } else if (mode == 3) {
            auto *target = first->writes ? second.get() : first.get();
            target->focusOut();
        } else if (mode == 4) {
            auto *target = first->writes ? second.get() : first.get();
            target->setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::Preedit) | fcitx::CapabilityFlag::Sensitive);
        } else {
            auto *target = first->writes ? second.get() : first.get();
            const auto before = ApplicationOffTest::state(lab, target).word();
            require(!ApplicationOffTest::key(lab, target));
            require(ApplicationOffTest::state(lab, target).word() == before);
            ApplicationOffTest::off(lab, target); // Reentrant Off cannot duplicate.
        }
    };
    first->callback = callback; second->callback = callback;
    ApplicationOffTest::off(lab, first.get());
    require(callbacks == (mode != 0 ? 1U : 2U));
    require(other->writes == 0);
    require(!ApplicationOffTest::state(lab, other.get()).stopping());
    if (first) ApplicationOffTest::key(lab, first.get(), true);
    if (second) ApplicationOffTest::key(lab, second.get(), true);
    auto *remaining = first ? first.get() : second.get();
    ApplicationOffTest::off(lab, remaining);
    require(callbacks == (mode != 0 ? 1U : 2U));
    require(!ApplicationOffTest::key(lab, fresh.get()));
    require(ApplicationOffTest::state(lab, fresh.get()).empty());
    if (mode < 2) {
        require(ApplicationOffTest::on(lab));
        const auto permission = ApplicationOffTest::permission(lab, kLabProgram);
        require(permission.mode == policy::Mode::Automatic);
        require(permission.revision > oldPermission.revision);
        require(ApplicationOffTest::permission(lab, "chromium-browser").mode == policy::Mode::Automatic);
        auto createdAfterOn = std::make_unique<Context>(manager, "chromium-browser");
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
}
int main() { for (int i=0; i<5; ++i) scenario(i); std::cout << "PASS: 5 shared family Off/On scenarios, callback rejection, resume boundaries across reset/new contexts, fresh revisions, deferred preservation, release drain\n"; }
