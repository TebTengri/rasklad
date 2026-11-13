// Own developer-only Fcitx candidate: native letters, correction on Space.
// Only the private fixture program/frontend is admitted. Not installed.
#include "decisionrouter.h"
#include <QList>
#include <chrono>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/surroundingtext.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
class Evidence final : public rasklad::routing::EvidenceSource {
public:
    std::optional<rasklad::routing::TokenEvidence> evidence(const rasklad::routing::EvidenceQuery &q) const override {
        if (q.token != QStringLiteral("ghbdtn")) return std::nullopt;
        rasklad::routing::TokenEvidence e; e.full.alternative = true;
        e.model = rasklad::DecisionAction::Correct; return e;
    }
} evidence;
struct State final : fcitx::InputContextProperty {
    QString tail;
    bool blocked = false;
    void reset() { tail.clear(); blocked = false; }
    void interrupt() { tail.clear(); blocked = true; }
};
class Engine final : public fcitx::InputMethodEngine {
public:
    explicit Engine(fcitx::Instance *instance) {
        instance->inputContextManager().registerProperty("rasklad-commit-lab", &states_);
        watcher_ = instance->watchEvent(fcitx::EventType::InputContextCapabilityChanged, fcitx::EventWatcherPhase::PostInputMethod, [](fcitx::Event &event) {
            auto &change = static_cast<fcitx::CapabilityChangedEvent &>(event);
            std::fprintf(stderr, "RASKLAD_CAP {\"old_surrounding\":%d,\"new_surrounding\":%d}\n",
                int(change.oldFlags().test(fcitx::CapabilityFlag::SurroundingText)), int(change.newFlags().test(fcitx::CapabilityFlag::SurroundingText)));
        });
    }
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) override {
        if (event.isRelease()) return;
        auto *ic = event.inputContext(); auto *state = ic->propertyFor(&states_);
        const auto flags = ic->capabilityFlags();
        if (!allowed(ic) || flags.testAny(fcitx::CapabilityFlags(fcitx::CapabilityFlag::PasswordOrSensitive)
                | fcitx::CapabilityFlag::Disable | fcitx::CapabilityFlag::Terminal | fcitx::CapabilityFlag::NoSpellCheck)) {
            state->reset(); ++refusals_; report(); return;
        }
        if (event.key().states().testAny(fcitx::KeyStates(fcitx::KeyState::Ctrl) | fcitx::KeyState::Alt
                | fcitx::KeyState::Super | fcitx::KeyState::Meta | fcitx::KeyState::Repeat)) {
            state->interrupt(); return;
        }
        const char32_t c = fcitx::Key::keySymToUnicode(event.key().sym());
        if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) {
            if (!state->blocked && state->tail.size() < rasklad::routing::kMaxRouteKeys) state->tail += QChar(char16_t(c));
            else state->interrupt();
            return; // do not filter: letters enter the native document
        }
        if (c != U' ') { state->interrupt(); return; }
        const QString token = state->blocked ? QString() : state->tail;
        std::fprintf(stderr,"RASKLAD_COMMIT_GATE {\"tail_length\":%lld,\"surrounding_cap\":%d}\n", static_cast<long long>(token.size()), int(flags.test(fcitx::CapabilityFlag::SurroundingText)));
        state->reset();
        if (token.isEmpty() || !flags.test(fcitx::CapabilityFlag::SurroundingText)) { ++refusals_; report(); return; }
        // Metadata checks precede access to the framework's surrounding cache.
        const auto &surrounding = ic->surroundingText();
        ++queries_;
        if (!surrounding.isValid() || surrounding.text().size() > 4096
            || surrounding.cursor() != surrounding.anchor()) { ++mismatches_; report(); return; }
        const std::string original = surrounding.text();
        const auto cursor = surrounding.cursor();
        const QString text = QString::fromUtf8(original.data(), original.size());
        const auto codepoints = text.toUcs4();
        const qsizetype start = text.size() - token.size();
        if (cursor != codepoints.size() || !text.endsWith(token) || start < 0
            || (start > 0 && !text.at(start-1).isSpace())) { ++mismatches_; report(); return; }
        const rasklad::routing::RouteRequest request{rasklad::routing::FieldContext::Prose,
            rasklad::routing::InputLayout::KnownUs, QStringView(token)};
        const auto decision = rasklad::routing::route(request, rasklad::routing::RouteProfile::SafeConfirmed, &evidence);
        if (decision.action != rasklad::routing::RouteAction::Correct || decision.candidate.isEmpty()) return;
        if (!allowed(ic) || ic->capabilityFlags() != flags || !ic->surroundingText().isValid()
            || ic->surroundingText().text() != original || ic->surroundingText().cursor() != cursor
            || ic->surroundingText().anchor() != cursor) { ++mismatches_; report(); return; }
        // This external delete+commit has no atomic version check at the editor.
        // The experiment tests actual Fcitx ordering; it does not assume safety.
        ic->deleteSurroundingText(-int(token.size()), unsigned(token.size()));
        ic->commitString((decision.candidate + QLatin1Char(' ')).toStdString());
        event.filterAndAccept(); ++corrections_; report();
    }
    void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override {
        event.inputContext()->propertyFor(&states_)->reset();
    }
private:
    static bool allowed(fcitx::InputContext *ic) {
        return ic->hasFocus() && ic->program() == "native_client" && ic->frontendName() == "dbus";
    }
    void report() const {
        std::fprintf(stderr,"RASKLAD_COMMIT_FIRST {\"corrections\":%d,\"refusals\":%d,\"mismatches\":%d,\"cache_queries\":%d}\n",
            corrections_, refusals_, mismatches_, queries_);
    }
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> watcher_;
    int corrections_ = 0, refusals_ = 0, mismatches_ = 0, queries_ = 0;
    fcitx::SimpleInputContextPropertyFactory<State> states_;
};
class Factory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        const char *marker = std::getenv("RASKLAD_FCITX_FIXTURE"), *runtime = std::getenv("XDG_RUNTIME_DIR");
        if (!marker || std::strcmp(marker,"1") || !runtime || std::strcmp(runtime,"/run/lab")) return nullptr;
        return new Engine(manager->instance());
    }
};
}
FCITX_ADDON_FACTORY(Factory)
