// SPDX-License-Identifier: MIT
// Actual adapter helper, synthetic memory only; no Instance/frontend/daemon.
#include "../../src/fcitxlab.cpp"
#include <cstdlib>
#include <functional>
#include <iostream>
namespace {
#define require(value) do { if (!(value)) { std::cerr << "disable contract failed at line " << __LINE__ << "\n"; std::abort(); } } while (false)
struct DisableReentryTest {
    template<class Callback>
    static void off(fcitx::InputContext *ic, LabState &s, bool eligible, Callback callback) {
        RaskladLab::disableLab(ic, s, eligible, callback);
    }
    static bool acknowledge(fcitx::InputContext *ic, LabState &s,
                            policy::CompositionDisable::Token generation, bool eligible = true) {
        return RaskladLab::acknowledgeLabOriginal(ic, s, generation, eligible);
    }
    static auto commit(fcitx::InputContext *ic, const std::string &text) {
        return RaskladLab::commitOriginalUnknown(ic, text);
    }
    static void start(fcitx::InputContext *ic, LabState &s) {
        auto context = RaskladLab::fixtureContext(ic, s);
        s.generation = s.disable->bind(kFixtureRegistry,
            RaskladLab::fixturePermission(context), context);
        require(s.generation != 0);
        s.lease = policy::authorizeCapture(kFixtureRegistry,
            RaskladLab::fixturePermission(context), context).lease;
        require(s.lease.valid);
        s.push('a'); s.push('b');
    }
    static void discard(fcitx::InputContext *ic, LabState &s) {
        RaskladLab::discard(ic, s);
    }
};
class FakeContext final : public fcitx::InputContext {
public:
    explicit FakeContext(fcitx::InputContextManager &m) : InputContext(m, kLabProgram) { created(); }
    ~FakeContext() override { destroy(); }
    const char *frontend() const override { return "disable-reentry-test"; }
    std::function<void(const std::string &)> onCommit;
    std::function<void()> onPreedit;
protected:
    void commitStringImpl(const std::string &text) override {
        auto callback = onCommit; // May destroy this context.
        if (callback) callback(text);
    }
    void deleteSurroundingTextImpl(int, unsigned) override { require(false); }
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { require(false); }
    void updatePreeditImpl() override {
        auto callback = onPreedit;
        if (callback) callback();
    }
};
void scenario(int mode) {
    fcitx::InputContextManager manager;
    auto ic = std::make_unique<FakeContext>(manager);
    auto s = std::make_unique<LabState>();
    ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    ic->focusIn();
    DisableReentryTest::start(ic.get(), *s);
    auto operation = s->disable;
    unsigned writes = 0;
    if (mode == 1) ic->focusOut();
    if (mode == 2) ++s->epoch;
    ic->onCommit = [&](const std::string &original) {
        ++writes;
        require(original == "ab");
        require(!operation->mayTeardown());
        operation->requestOff();
        require(!operation->takeOriginal(kFixtureRegistry, {}));
        require(!operation->enable());
        if (mode == 3) {
            DisableReentryTest::discard(ic.get(), *s);
            require(s->word() == "ab"); // Lifecycle reset is not delivery.
        }
        if (mode == 4) { s.reset(); ic.reset(); }
    };
    auto finish = [](fcitx::InputContext *context, const std::string &original) {
        return DisableReentryTest::commit(context, original);
    };
    DisableReentryTest::off(ic.get(), *s, mode != 5, finish);
    require(writes == ((mode == 1 || mode == 2 || mode == 5) ? 0U : 1U));
    require(!operation->mayTeardown());
    require(!operation->enable());
    if (s) {
        require(s->word() == "ab");
        DisableReentryTest::off(ic.get(), *s, true, finish);
        require(writes == ((mode == 1 || mode == 2 || mode == 5) ? 0U : 1U));
        require(s->word() == "ab");
    }
}
void receiptScenario(int mode) {
    fcitx::InputContextManager manager;
    auto ic = std::make_unique<FakeContext>(manager);
    auto s = std::make_unique<LabState>();
    ic->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    ic->focusIn();
    DisableReentryTest::start(ic.get(), *s);
    auto operation = s->disable;
    const auto generation = s->generation;
    s->setPreeditShown(true);
    fcitx::Text preedit;
    preedit.append(s->word());
    ic->inputPanel().setClientPreedit(preedit);
    ic->updatePreedit(); // Populate actual SDK preedit transition cache.
    unsigned writes = 0, notifications = 0;
    ic->onCommit = [&](const std::string &text) {
        require(text == "ab"); ++writes;
        // Receipt cannot clear state while external finish still runs.
        require(!DisableReentryTest::acknowledge(ic.get(), *s, generation));
        require(!s->empty());
    };
    DisableReentryTest::off(ic.get(), *s, true,
        [](fcitx::InputContext *context, const std::string &text) {
            return DisableReentryTest::commit(context, text);
        });
    require(writes == 1);
    if (mode == 1) ++s->epoch;
    if (mode == 2) ic->focusOut();
    ic->onPreedit = [&] {
        ++notifications;
        require(operation->mayTeardown());
        require(s->empty() && !s->lease.valid && !s->preeditShown());
        if (mode == 5) { s.reset(); ic.reset(); }
    };
    const bool expected = mode == 0 || mode == 5;
    const bool accepted = DisableReentryTest::acknowledge(ic.get(), *s,
        mode == 3 ? generation + 1 : generation, mode != 4);
    require(accepted == expected);
    require(notifications == (expected ? 1U : 0U));
    require(operation->mayTeardown() == expected);
    if (s) {
        require(s->empty() == expected);
        require(!DisableReentryTest::acknowledge(ic.get(), *s,
            mode == 3 ? generation + 1 : generation, mode != 4));
    }
}
}
int main() {
    for (int i=0; i<6; ++i) scenario(i);
    for (int i=0; i<6; ++i) receiptScenario(i);
    std::cout << "PASS: six disable and six trusted-receipt helper scenarios; no native delivery claim\n";
}
