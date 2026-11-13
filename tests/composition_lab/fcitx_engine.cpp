// Own developer-only adapter using the installed public Fcitx API. No device
// access, surrounding-text queries, model files or user text logs. Not installed.
#include "composer.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
struct Counters {
    int handled = 0, corrections = 0, commits = 0, resets = 0, protectedRefusals = 0, missingCapability = 0;
    void report() const {
        // Bounded metadata only, captured by the closed fixture launcher.
        std::printf("RASKLAD_COUNTERS {\"handled\":%d,\"corrections\":%d,\"commits\":%d,\"resets\":%d,\"protected_refusals\":%d,\"missing_capability\":%d}\n",
            handled, corrections, commits, resets, protectedRefusals, missingCapability);
        std::fflush(stdout);
    }
} counters;
class Evidence final : public rasklad::routing::EvidenceSource {
public:
    std::optional<rasklad::routing::TokenEvidence> evidence(const rasklad::routing::EvidenceQuery &q) const override {
        rasklad::routing::TokenEvidence e;
        if (q.token == QStringLiteral("ghbdtn")) { e.full.alternative = true; e.model = rasklad::DecisionAction::Correct; return e; }
        if (q.token == QStringLiteral("hello")) { e.full.observed = true; return e; }
        return std::nullopt;
    }
} evidence;
struct State final : fcitx::InputContextProperty {
    rasklad::composition::Composer composer{rasklad::routing::InputLayout::KnownUs, &evidence};
};
class Engine final : public fcitx::InputMethodEngine {
public:
    explicit Engine(fcitx::Instance *instance) {
        instance->inputContextManager().registerProperty("rasklad-lab", &states_);
    }
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) override {
        if (event.isRelease()) return;
        auto *ic = event.inputContext();
        const auto flags = ic->capabilityFlags();
        if (flags.testAny(fcitx::CapabilityFlag::PasswordOrSensitive)) {
            ++counters.protectedRefusals; counters.report(); return;
        }
        if (!flags.test(fcitx::CapabilityFlag::Preedit) || !flags.test(fcitx::CapabilityFlag::ClientUnfocusCommit)) {
            ++counters.missingCapability; counters.report(); return;
        }
        auto *state = ic->propertyFor(&states_);
        // Preserve original preedit before a shortcut or navigation key. The
        // original event remains unfiltered so the editor performs its action.
        if (event.key().states().testAny(fcitx::KeyStates(fcitx::KeyState::Ctrl) | fcitx::KeyState::Alt | fcitx::KeyState::Super | fcitx::KeyState::Meta)) {
            publish(ic, state->composer.finishLiteral());
            return;
        }
        const auto update = event.key().sym() == FcitxKey_BackSpace ? state->composer.backspace()
            : state->composer.text(fcitx::Key::keySymToUnicode(event.key().sym()));
        if (!update.handled) {
            publish(ic, state->composer.finishLiteral());
            return;
        }
        event.filterAndAccept();
        ++counters.handled;
        publish(ic, update);
    }
    void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override {
        auto *ic = event.inputContext();
        ic->propertyFor(&states_)->composer.reset();
        ic->inputPanel().reset();
        if (!ic->capabilityFlags().testAny(fcitx::CapabilityFlag::PasswordOrSensitive)) ic->updatePreedit();
        ++counters.resets; counters.report();
    }
private:
    static void publish(fcitx::InputContext *ic, const rasklad::composition::Update &update) {
        if (!update.commit.isEmpty()) { ic->commitString(update.commit.toStdString()); ++counters.commits; }
        if (update.corrected) ++counters.corrections;
        fcitx::Text preedit(update.preedit.toStdString());
        preedit.setCursor(update.preedit.toUtf8().size());
        ic->inputPanel().setClientPreedit(preedit);
        ic->updatePreedit();
        counters.report();
    }
    fcitx::SimpleInputContextPropertyFactory<State> states_;
};
class Factory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        const char *marker = std::getenv("RASKLAD_FCITX_FIXTURE");
        const char *runtime = std::getenv("XDG_RUNTIME_DIR");
        if (!marker || std::strcmp(marker,"1") || !runtime || std::strcmp(runtime,"/run/lab")) return nullptr;
        return new Engine(manager->instance());
    }
};
}
FCITX_ADDON_FACTORY(Factory)
