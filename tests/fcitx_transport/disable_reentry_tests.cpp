// SPDX-License-Identifier: MIT
// Actual adapter helper, synthetic memory only; no Instance/frontend/daemon.
#include "../../src/fcitxlab.cpp"
#include <cstdlib>
#include <functional>
#include <iostream>
namespace {
void require(bool value) { if (!value) std::abort(); }
struct DisableReentryTest {
    template<class Callback>
    static void off(fcitx::InputContext *ic, LabState &s, bool eligible, Callback callback) {
        RaskladLab::disableLab(ic, s, eligible, callback);
    }
    static auto commit(fcitx::InputContext *ic, const std::string &text) {
        return RaskladLab::commitOriginalUnknown(ic, text);
    }
    static void start(fcitx::InputContext *ic, LabState &s) {
        auto context = RaskladLab::fixtureContext(ic, s);
        s.generation = s.disable->bind(kFixtureRegistry,
            RaskladLab::fixturePermission(context), context);
        require(s.generation != 0);
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
protected:
    void commitStringImpl(const std::string &text) override {
        auto callback = onCommit; // May destroy this context.
        if (callback) callback(text);
    }
    void deleteSurroundingTextImpl(int, unsigned) override { require(false); }
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { require(false); }
    void updatePreeditImpl() override { require(false); }
};
void scenario(int mode) {
    fcitx::InputContextManager manager;
    auto ic = std::make_unique<FakeContext>(manager);
    auto s = std::make_unique<LabState>();
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
}
int main() {
    for (int i=0; i<6; ++i) scenario(i);
    std::cout << "PASS: six actual disable helper scenarios; no native delivery claim\n";
}
