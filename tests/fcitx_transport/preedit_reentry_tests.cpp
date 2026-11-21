// SPDX-License-Identifier: MIT
// In-memory callback ordering only; no Instance, daemon or frontend connection.
#include "../../src/fcitxlab.cpp"
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
void require(bool condition) { if (!condition) { std::cerr << "preedit reentry contract failed\n"; std::abort(); } }
struct PreeditReentryTest {
    static void outsideFixtureIsInert() {
        const char *previous = std::getenv("RASKLAD_PRIVATE_LAB");
        const bool hadPrevious = previous != nullptr;
        const std::string saved = previous ? previous : "";
        setenv("RASKLAD_PRIVATE_LAB", "0", 1);
        {
            RaskladLab addon(nullptr); // Touching Instance would be a regression.
            require(!addon.registered_ && addon.watchers_.empty());
        }
        if (hadPrevious) setenv("RASKLAD_PRIVATE_LAB", saved.c_str(), 1);
        else unsetenv("RASKLAD_PRIVATE_LAB");
    }
    static void letter(fcitx::InputContext *ic, LabState &s, fcitx::KeyEvent &event) {
        RaskladLab::acceptLetter(ic, s, event, 'a');
    }
    static void backspace(fcitx::InputContext *ic, LabState &s, fcitx::KeyEvent &event) {
        RaskladLab::acceptBackspace(ic, s, event);
    }
    static void discard(fcitx::InputContext *ic, LabState &s) {
        RaskladLab::discard(ic, s);
    }
};
class FakeInputContext final : public fcitx::InputContext {
public:
    explicit FakeInputContext(fcitx::InputContextManager &manager) : InputContext(manager, kLabProgram) { created(); }
    ~FakeInputContext() override { destroy(); }
    const char *frontend() const override { return "preedit-reentry-test"; }
    std::function<void()> callback;
protected:
    void commitStringImpl(const std::string &) override { require(false); }
    void deleteSurroundingTextImpl(int, unsigned int) override { require(false); }
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { require(false); }
    void updatePreeditImpl() override {
        auto local = callback; // It is legal for the callback to destroy this.
        if (local) local();
    }
};
void scenario(int operation, bool destroy) {
    fcitx::InputContextManager manager;
    auto context = std::make_unique<FakeInputContext>(manager);
    auto state = std::make_unique<LabState>();
    state->lease = {true, 0, 7, 9, state->id, state->epoch, state->id};
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    if (operation) {
        state->push('b');
        if (operation == 2) state->push('c');
        state->setPreeditShown(true);
        fcitx::Text initial;
        initial.append(state->word());
        context->inputPanel().setClientPreedit(initial);
        context->updatePreedit();
    }
    fcitx::KeyEvent event(context.get(), fcitx::Key(operation ? FcitxKey_BackSpace : FcitxKey_a));
    unsigned calls = 0;
    context->callback = [&] {
        ++calls;
        require(event.filtered() && event.accepted());
        require(state->takePending(event.key()));
        require(state->lease.valid == (operation != 1));
        require(state->word() == (operation == 0 ? "a" : operation == 1 ? "" : "b"));
        if (destroy) {
            state.reset();
            context.reset();
        } else {
            // Model the actual lifecycle handler's discard after focus/reset.
            context->callback = {};
            context->reset();
            context->focusOut();
            const auto epoch = state->epoch;
            PreeditReentryTest::discard(context.get(), *state);
            require(state->epoch != epoch && state->empty() && !state->lease.valid);
        }
    };
    if (operation) PreeditReentryTest::backspace(context.get(), *state, event);
    else PreeditReentryTest::letter(context.get(), *state, event);
    if (calls != 1) std::cerr << "missing callback operation=" << operation << " destroy=" << destroy << " calls=" << calls << "\n";
    require(calls == 1);
    require(event.filtered() && event.accepted());
}
} // namespace
int main() {
    PreeditReentryTest::outsideFixtureIsInert();
    for (int operation = 0; operation < 3; ++operation)
        for (bool destroy : {false, true}) scenario(operation, destroy);
    std::cout << "PASS: six actual preedit helper reentry cases and default-off guard\n";
}
