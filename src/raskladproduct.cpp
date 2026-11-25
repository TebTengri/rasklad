// SPDX-License-Identifier: MIT
//
// rasklad Fcitx5 addon, the product (SYS-246, docs/SYS-246-PRODUCT-ADDON.md):
// the manual double-Shift gesture and (SYS-226) automatic correction on Space by
// the reviewed local profile, in applications listed in the addon configuration.
// No lab environment, receipts, supervisor pipe, D-Bus or Off/On keys. Security
// boundaries: SECURITY.md.
//
// SYS-255: split out of src/fcitxlab.cpp, which keeps the lab variants and is no
// longer built into the product. The split was mechanical: for the product flags
// the preprocessed code is identical to the old file's. Some names (LabState,
// RaskladLab, fixture*) come from that history; they are product code here.
// Shared rules live in src/configrules.h and src/wordrules.h.

#if !defined(RASKLAD_PRODUCT) || !defined(RASKLAD_MANUAL_LAB) || !defined(RASKLAD_WITH_HUNSPELL)
#error "src/raskladproduct.cpp is the product: build it with tools/fcitx_product.pro"
#endif
// As before the split: the product is never built with a lab variant's flags.
#if defined(RASKLAD_SLACK_SESSION_LAB) || defined(RASKLAD_CHROMIUM_LAB) || defined(RASKLAD_PROFILE_LAB) || \
    defined(RASKLAD_WITNESS_LAB) || defined(RASKLAD_DISABLE_TEST_LAB) || defined(RASKLAD_APPLICATION_OFF_TEST) || \
    defined(RASKLAD_PREEDIT_REENTRY_TEST) || defined(RASKLAD_DISABLE_REENTRY_TEST) || \
    defined(RASKLAD_SLACK_GATE_TEST_PROGRAM) || defined(RASKLAD_CORE_ENTRY_WITNESS_LAB) || \
    defined(RASKLAD_GRAB_ENTRY_WITNESS_LAB)
#error "RASKLAD_PRODUCT is built without lab variants"
#endif

#include "applicationpolicy.h"
#include "applicationpermissions.h"
#include "compositiondisable.h"


// SYS-225 slice 1 (docs/SYS-225-MANUAL-GESTURE.md): no automatic correction;
// a double Shift toggles the CURRENT preedit word us<->ru view, Space commits
// what is shown. Never touches text the application already accepted.
#include "manualgesture.h"
#include "wordrules.h"

// SYS-246 product build (docs/SYS-246-PRODUCT-ADDON.md): the manual double-Shift
// gesture and (SYS-226) automatic correction on Space by the reviewed local profile,
// in applications listed in the addon configuration. No lab environment, receipts,
// supervisor pipe, D-Bus or Off/On keys.
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include "fcitxlabprofile.h"
#include "frequentwords.h"
#include "layoutcue.h"
#include "configrules.h"
#include "seenprograms.h"
#include "terminalapps.h"
#include <fcitx-utils/standardpaths.h>


#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

// SYS-269: rasklad::word::field mirrors these Fcitx bits.
#define RASKLAD_FIELD_BIT(name, flag) \
    static_assert(rasklad::word::field::name == static_cast<std::uint64_t>(fcitx::CapabilityFlag::flag))
RASKLAD_FIELD_BIT(kPassword, Password);
RASKLAD_FIELD_BIT(kEmail, Email);
RASKLAD_FIELD_BIT(kDigit, Digit);
RASKLAD_FIELD_BIT(kUrl, Url);
RASKLAD_FIELD_BIT(kDialable, Dialable);
RASKLAD_FIELD_BIT(kNumber, Number);
RASKLAD_FIELD_BIT(kNoSpellCheck, NoSpellCheck);
RASKLAD_FIELD_BIT(kTerminal, Terminal);
RASKLAD_FIELD_BIT(kDate, Date);
RASKLAD_FIELD_BIT(kTime, Time);
RASKLAD_FIELD_BIT(kSensitive, Sensitive);
RASKLAD_FIELD_BIT(kDisable, Disable);
#undef RASKLAD_FIELD_BIT

constexpr std::size_t kMaxWord = 32;
// SYS-272: a longer surrounding text from the client is treated as unknown.
constexpr std::size_t kMaxSurroundingBytes = 64 * 1024;
constexpr std::size_t kMaxPendingReleases = 8;
// Separate compile-time lab variant; never an environment-controlled app list.
constexpr const char kLabProgram[] = "rasklad-ime-lab";
namespace policy = rasklad::policy;
// Product: no environment labels are read at all.
bool fixtureScope() { return true; }
constexpr policy::Adapter kFixtureAdapters[] = {
    {kLabProgram, kLabProgram, policy::Domain::OwnedFixture, true, true},
};
constexpr policy::Registry kFixtureRegistry{kFixtureAdapters,
    sizeof(kFixtureAdapters) / sizeof(kFixtureAdapters[0]), 1};
// Owner decision (2026-11-21): Slack and Chromium. Edited in the Fcitx settings.
FCITX_CONFIGURATION(RaskladConfig,
    // Owner decision 2026-11-25: off until the owner turns it on (settings app).
    // Off, the addon is loaded but does nothing: no word, gesture, cue or status names.
    fcitx::Option<bool> enabled{this, "Enabled", "Rasklad is on", false};
    fcitx::Option<std::vector<std::string>> applications{this, "Applications",
        "Applications (program names as Fcitx reports them)", {"slack", "chromium", "chromium-browser"}};
    fcitx::Option<bool> sound{this, "Sound", "Retro cue on us/ru layout switch", true};
    // SYS-227: off leaves only double Shift (and double Alt, which Hyprland handles).
    fcitx::Option<bool> autoCorrect{this, "AutoCorrect", "Correct words automatically", true};
    fcitx::Option<std::vector<std::string>> exceptions{this, "Exceptions",
        "Words never corrected automatically", {}};);
// SYS-248: read-only status at fcitx://config/addon/rasklad/status, for the bar
// widget and the settings app. Never saved; writes to it are ignored. Booleans and,
// since SYS-253, program names (no text) of apps with a focused field, kept in memory.
FCITX_CONFIGURATION(RaskladStatus,
    fcitx::Option<bool> profileReady{this, "ProfileReady", "Profile verified", false};
    fcitx::Option<bool> enabled{this, "Enabled", "Rasklad is on", false};
    fcitx::Option<bool> autoCorrect{this, "AutoCorrect", "Correct words automatically", true};
    fcitx::Option<std::vector<std::string>> seenApplications{this, "SeenApplications",
        "Applications with a focused text field since start", {}};
    // SYS-272: "<random per addon start>-<times turned on since>", so the settings
    // window forgets the names it kept itself after an off/on or a Fcitx restart it
    // did not see. Random, not derived from anything about the user.
    fcitx::Option<std::string> onEpoch{this, "OnEpoch", "Changes when turned on or restarted", ""};);
constexpr const char kConfigPath[] = "conf/rasklad.conf";
// Applications from the addon configuration (compositor app ids as Fcitx reports
// them in InputContext::program()). Claims, not authentication: policy allows
// them for the manual gesture only. Rebuilt only after every state is discarded.
struct ProductApps {
    std::vector<std::string> ids;
    std::vector<policy::Adapter> adapters;
    std::uint64_t revision = 1;
};
ProductApps &productApps() {
    static ProductApps apps;
    return apps;
}
const std::vector<policy::Adapter> &fixtureAdapters() { return productApps().adapters; }
policy::Registry fixtureRegistry() {
    const auto &apps = productApps();
    return {apps.adapters.data(), apps.adapters.size(), apps.revision};
}
// Automatic grant (owner decision 2026-11-21, SYS-226); the gesture is checked
// separately through Adapter::manual.
constexpr policy::Mode kGrantMode = policy::Mode::Automatic;
// Profile files at their pinned relative paths under <PkgData>/rasklad/profile
// (package: /usr/share/fcitx5/rasklad/profile). Every file is verified against the
// built-in pins, so a copy found first in the user data directory must be identical.
std::string profileRoot() {
    try {
        const auto marker = fcitx::StandardPaths::global().locate(
            fcitx::StandardPathsType::PkgData, "rasklad/profile/data/sources.json");
        if (marker.empty()) return {};
        return marker.parent_path().parent_path().string();
    } catch (...) {
        return {};
    }
}
constexpr const char kLabEntry[] = "keyboard-us";
constexpr const char kRuEntry[] = "keyboard-ru"; // SYS-226: switched to after a us->ru correction
constexpr const char kPropertyName[] = "raskladlabState";

// Synthetic test rule only. No dictionaries, model or other strings.
constexpr const char kRuleFrom[] = "ghbdtn";
constexpr const char kRuleTo[] = "привет";

// Wipes the whole buffer, not just the current length: a string shortened in
// place (the double-Shift swap) keeps older bytes past size() (SYS-272).
void wipeString(std::string &s) { rasklad::wipe(s); }

// Per-context state. Owned by Fcitx through the property factory; destroyed
// with its InputContext. Contains only the current lab word and bounded
// release bookkeeping, never history.
class LabState final : public fcitx::InputContextProperty {
public:
    struct CallbackBinding { bool alive = true; std::uint64_t epoch = 1; };
    std::shared_ptr<CallbackBinding> callbackBinding = std::make_shared<CallbackBinding>();
    LabState() : id(nextId_++) { disable->enable(); }
    ~LabState() override { callbackBinding->alive = false; invalidate(); clearBuffer(); }
    void invalidate() {
        ++epoch; callbackBinding->epoch = epoch; lease = {};
        gesture.reset();
        forgetLast();
    }
    rasklad::manual::DoubleShift gesture;
    bool converted = false; // the preedit shows the other layout's form
    // SYS-226: the word was typed in the ru layout. The buffer always holds the us
    // keys; the Cyrillic text is their usToRu form. One word never mixes layouts.
    bool typedRu = false;
    bool chosen = false; // the owner used the gesture on this word: never auto-correct it
    // Text to show/commit; empty conversion (never for a..z) keeps the original.
    // Bit i: the i-th key was typed as a capital letter (SYS-226). Keys are lowercase.
    std::uint32_t upper = 0;
    std::string shown() const {
        std::string w = word();
        std::string out = form(w, typedRu != converted, upper);
        rasklad::wipe(w); // SYS-272: the copy of the word is wiped
        return out;
    }
    static std::string form(const std::string &typed, bool asRu, std::uint32_t mask = 0) {
        return rasklad::word::form(typed, asRu, mask);
    }
    // The last word this addon committed with Space or a punctuation key (SYS-250),
    // kept only until any other key, lifecycle event or failed check. Only it may
    // be replaced. lastNow: the form now in the field; lastOther: the form a
    // double Shift puts there instead; lastSuffix: what follows the word (the
    // Space, or the punctuation character the client inserts itself).
    // Pending: committed, not yet confirmed by a FRESH surrounding-text update from
    // the client. Confirmed: such an update showed exactly that word + space right
    // before a collapsed cursor at lastCursor; any later update that differs
    // (text or cursor, e.g. a mouse click) forgets it. Only Confirmed may replace.
    // Both forms stay in their own strings; a swap flips lastSwapped only. Moving
    // strings would leave a short form's bytes in an object's inline storage,
    // where no wipe reaches them (SYS-272).
    std::string lastFormA;
    std::string lastFormB;
    bool lastSwapped = false;
    const std::string &lastNow() const { return lastSwapped ? lastFormB : lastFormA; }
    const std::string &lastOther() const { return lastSwapped ? lastFormA : lastFormB; }
    std::string lastSuffix;
    bool lastOtherRu = false; // lastOther is the Cyrillic form
    bool lastValid = false;
    bool lastConfirmed = false;
    unsigned lastCursor = 0;
    // How a Pending word may confirm. AfterCommit: the update's cursor must be the
    // pre-commit cursor advanced by exactly the committed length (a stale pre-commit
    // repeat, or a click onto the same text elsewhere, never confirms). AfterReplace:
    // the old-form text cannot match the new form, so no cursor rule. The cursor is
    // not compared here on purpose: Chromium reports it inside a window trimmed by
    // bytes, so the same caret gets a different offset once Cyrillic replaces Latin.
    // Never: the pre-commit cursor was unknown.
    enum class Confirm { Never, AfterCommit, AfterReplace };
    Confirm lastRule = Confirm::Never;
    unsigned lastPreCursor = 0;
    std::string lastText() const { return rasklad::joined(lastNow(), lastSuffix); }
    // SYS-271: bumped whenever the kept word is forgotten or replaced by another,
    // so a deferred undo acts only on the very word it was asked for.
    std::uint64_t lastGeneration = 0;
    // SYS-272: when the word was kept. Past kLastLifetimeUs it can no longer be
    // swapped, and a timer wipes it even if no key ever comes.
    static constexpr std::uint64_t kLastLifetimeUs = 15'000'000;
    std::uint64_t lastAtUs = 0;
    bool lastExpired() const { return !lastValid || fcitx::now(CLOCK_MONOTONIC) - lastAtUs > kLastLifetimeUs; }
    void forgetLast() {
        ++lastGeneration;
        for (std::string *text : {&lastFormA, &lastFormB, &lastSuffix}) wipeString(*text);
        lastSwapped = false; lastOtherRu = false; lastValid = false; lastConfirmed = false; lastCursor = 0;
        lastRule = Confirm::Never; lastPreCursor = 0;
    }
    // Keep a just-committed word for the double-Shift swap, only when it can ever
    // confirm: with no known pre-commit cursor it could never be replaced.
    void keepLast(const fcitx::SurroundingText &pre, std::string now, std::string other,
                  bool otherRu, std::string suffix) {
        forgetLast();
        // Copied, not moved (a short string's bytes would stay behind), then every
        // parameter is wiped whatever happens.
        const bool keep = pre.isValid() && pre.cursor() == pre.anchor() && now != other;
        if (keep) {
            lastAtUs = fcitx::now(CLOCK_MONOTONIC);
            lastFormA = now; lastFormB = other; lastSuffix = suffix;
            lastOtherRu = otherRu;
            lastValid = true; // Pending
            lastRule = Confirm::AfterCommit;
            lastPreCursor = pre.cursor();
        }
        for (std::string *text : {&now, &other, &suffix}) {
            volatile char *p = text->data();
            for (std::size_t i = 0; i < text->size(); ++i) p[i] = '\0';
        }
    }
    const std::uint64_t id;
    std::uint64_t epoch = 1;
    // Survives lifecycle invalidation: an On transition must never capture
    // a suffix of raw input typed while Off, including in a later new context.
    std::uint64_t resumeGeneration = 0;
    std::uint64_t enableGeneration = 0;
    bool needsBoundary = false;
    // Since Rasklad was turned on, this field has not shown a token start yet
    // (enableGuard); the last press under it was a Space (enableGuardSpace).
    bool enableGuard = false;
    bool enableGuardSpace = false;
    policy::Lease lease;
    std::shared_ptr<policy::CompositionDisable> disable =
        std::make_shared<policy::CompositionDisable>();
    policy::CompositionDisable::Token generation = 0;


    bool stopping() const {
        const auto phase = disable->state();
        return phase != policy::CompositionDisable::State::Idle &&
               phase != policy::CompositionDisable::State::Active;
    }
    bool empty() const { return len_ == 0; }
    bool full() const { return len_ >= kMaxWord; }
    bool preeditShown() const { return preeditShown_; }
    bool bypass() const { return bypass_; }
    void setBypass(bool value) { bypass_ = value; }
    void setPreeditShown(bool v) { preeditShown_ = v; }

    void push(char c) {
        if (len_ < kMaxWord) {
            buf_[len_++] = c;
        }
    }
    void pop() {
        if (len_ > 0) {
            buf_[--len_] = '\0';
            upper &= ~(std::uint32_t{1} << len_);
        }
    }
    std::size_t size() const { return len_; }
    std::string word() const { return std::string(buf_.data(), len_); }

    void clearBuffer() {
        volatile char *p = buf_.data();
        for (std::size_t i = 0; i < buf_.size(); ++i) {
            p[i] = '\0';
        }
        len_ = 0;
        lease = {};
        converted = false;
        typedRu = false;
        chosen = false;
        upper = 0;
        gesture.reset();
        // Normal local-buffer completion only. NEVER acknowledge a disabled
        // composition from buffer reset/destruction: that is not native delivery.
        if (disable->state() == policy::CompositionDisable::State::Active) {
            disable->nativeIdle(generation);
            generation = 0;
        }
    }

    // SYS-272: wipe, not just forget: the codes and keysyms of consumed keys.
    void clearPending() {
        for (auto &e : pending_) e = Pending{};
        pendingCount_ = 0;
    }
    bool hasPending() const { return pendingCount_ != 0; }

    // Remember a consumed press so its release can be swallowed.
    void addPending(const fcitx::Key &key) {
        if (pendingCount_ == kMaxPendingReleases) {
            // Bounded: drop the oldest; its release leaks harmlessly.
            for (std::size_t i = 1; i < pendingCount_; ++i) {
                pending_[i - 1] = pending_[i];
            }
            --pendingCount_;
        }
        pending_[pendingCount_++] = Pending{key.code(), key.sym()};
    }

    // Removes a matching entry; returns true if one existed.
    bool takePending(const fcitx::Key &key) {
        for (std::size_t i = 0; i < pendingCount_; ++i) {
            const bool match = (pending_[i].code != 0 && key.code() != 0)
                                   ? pending_[i].code == key.code()
                                   : pending_[i].sym == key.sym();
            if (match) {
                for (std::size_t j = i + 1; j < pendingCount_; ++j) {
                    pending_[j - 1] = pending_[j];
                }
                pending_[--pendingCount_] = Pending{}; // released: nothing kept
                return true;
            }
        }
        return false;
    }

    bool active() const { return len_ > 0 || preeditShown_ || pendingCount_ > 0 || bypass_; }

private:
    inline static std::uint64_t nextId_ = 1;
    struct Pending {
        int code = 0;
        fcitx::KeySym sym = FcitxKey_None;
    };

    std::array<char, kMaxWord> buf_{};
    std::size_t len_ = 0;
    bool preeditShown_ = false;
    bool bypass_ = false;
    std::array<Pending, kMaxPendingReleases> pending_{};
    std::size_t pendingCount_ = 0;
};

// Fcitx eagerly creates one property per context. Keep that holder cheap;
// allocate bounded composition/controller resources only for an owned fixture
// when an eligible press actually reaches the adapter.
class LabProperty final : public fcitx::InputContextProperty {
public:
    std::unique_ptr<LabState> state;
};

class RaskladLab final : public fcitx::AddonInstance {
public:
    explicit RaskladLab(fcitx::Instance *instance) : instance_(instance),
        manager_(instance ? &instance->inputContextManager() : nullptr) {
        if (!instance_ || !fixtureScope()) return; // Default off outside the owned fixture contract.
        fcitx::readAsIni(config_, kConfigPath);
        wasEnabled_ = *config_.enabled;
        rebuildExceptions();
        rebuildApps();
        // Missing/changed profile: no automatic correction; the gesture still works.
        profile_ = std::make_unique<rasklad::fcitxlab::ProfileDecision>(profileRoot().c_str());
        enableFixturePermissions();
        // If the name is taken we stay inert: no property, no watchers.
        registered_ = instance_->inputContextManager().registerProperty(
            kPropertyName, &factory_);
        if (!registered_) {
            return;
        }

        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextKeyEvent,
            fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event &event) {
                handleKey(static_cast<fcitx::KeyEvent &>(event));
            }));

        // Lifecycle events: discard WITHOUT any commit. Destroy is deliberately
        // absent: the property dies with the context and we must not call into
        // a context that is being destroyed.
        for (const auto type : {fcitx::EventType::InputContextFocusOut,
                                fcitx::EventType::InputContextFocusIn,
                                fcitx::EventType::InputContextReset,
                                fcitx::EventType::InputContextSwitchInputMethod,
                                fcitx::EventType::InputContextCapabilityAboutToChange,
                                fcitx::EventType::InputContextCapabilityChanged}) {
            watchers_.emplace_back(instance_->watchEvent(
                type, fcitx::EventWatcherPhase::PreInputMethod,
                [this](fcitx::Event &event) {
                    auto *ic =
                        static_cast<fcitx::InputContextEvent &>(event).inputContext();
                    if (auto *s = state(ic, false)) {
                        // Our own deferred layout switch happens only with an empty
                        // word; the last committed word stays replaceable.
                        // Any event emitted synchronously by that switch is ours too.
                        if (selfSwitch_ && s->empty())
                            return;
                        // Owner decision 2026-11-23: the layout is switched from outside
                        // (double Alt in Hyprland runs rasklad-layout-toggle, or Fcitx's
                        // own keys). A word in progress is committed as shown, so
                        // nothing typed is lost, then the switch goes ahead.
                        if (event.type() == fcitx::EventType::InputContextSwitchInputMethod &&
                            !s->empty() && !s->stopping() && ic->hasFocus() && validate(ic, *s)) {
                            auto ref = ic->watch();
                            commitAndClear(ic, *s, s->shown());
                            auto *again = ref.get(); // the client may drop the context while committing
                            if (!again) return;
                            if (auto *after = state(again, false)) discard(again, *after);
                            return;
                        }
                        discard(ic, *s); // Invalidates even an idle context first.
                    }
                }));
        }
        // SYS-253: remember which program got a focused field, so the settings app
        // offers the name this addon matches. Its own watcher: no early return in the
        // lifecycle handler can skip it. The name only; no text is read here.
        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextFocusIn, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &event) {
                auto *ic = static_cast<fcitx::InputContextEvent &>(event).inputContext();
                if (ic && *config_.enabled) rasklad::rememberProgram(seenPrograms_, ic->program());
            }));
        // SYS-226: the SOUND-001 cue when the layout changes between keyboard-us and
        // keyboard-ru, whoever switched it (double Alt in Hyprland, a correction or an undo).
        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextSwitchInputMethod, fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &event) {
                if (!*config_.enabled || !*config_.sound) return;
                auto &e = static_cast<fcitx::InputContextSwitchInputMethodEvent &>(event);
                auto *ic = e.inputContext();
                const auto *entry = ic ? instance_->inputMethodEntry(ic) : nullptr;
                if (!entry || !ic->hasFocus()) return;
                const std::string &now = entry->uniqueName();
                const std::string &old = e.oldInputMethod();
                const auto known = [](const std::string &n) { return n == kLabEntry || n == kRuEntry; };
                if (known(now) && known(old) && now != old) cue_.play();
            }));
        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextSurroundingTextUpdated, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &event) {
                auto *ic = static_cast<fcitx::InputContextEvent &>(event).inputContext();
                if (auto *s = state(ic, false)) observeSurrounding(ic, *s);
            }));

    }

    // Members are destroyed in reverse order: watchers first (no more
    // callbacks), then the factory (unregisters and deletes all properties).
    ~RaskladLab() override = default;

    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &raw) override {
        // SYS-272: refuse an oversized request before anything is parsed or saved.
        if (!boundedConfig(raw)) return;
        fcitx::RawConfig before;
        config_.save(before);
        config_.load(raw, true);
        // Not saved: keep the previous settings, so the settings app's read-back
        // reports the failure instead of a change that is lost on the next start.
        // Nothing changed then, so a word being typed is not discarded either.
        if (!fcitx::safeSaveAsIni(config_, kConfigPath)) {
            config_.load(before);
            return;
        }
        rebuildExceptions();
        applyApps();
    }
    const fcitx::Configuration *getSubConfig(const std::string &path) const override {
        if (path != "status") return nullptr;
        status_.profileReady.setValue(profile_ && profile_->ready());
        status_.enabled.setValue(*config_.enabled);
        status_.autoCorrect.setValue(*config_.autoCorrect);
        status_.onEpoch.setValue(startId_ + "-" + std::to_string(enableGeneration_));
        status_.seenApplications.setValue(seenPrograms_);
        return &status_;
    }
    void setSubConfig(const std::string &, const fcitx::RawConfig &) override {}
    void reloadConfig() override {
        fcitx::readAsIni(config_, kConfigPath);
        rebuildExceptions();
        applyApps();
    }
private:
    // SYS-272: the settings app sends at most 64 applications and 256 words; any
    // request far beyond that (node count or total bytes) is refused whole.
    static constexpr std::size_t kMaxConfigNodes = 1024;
    static constexpr std::size_t kMaxConfigBytes = 64 * 1024;
    static bool boundedConfig(const fcitx::RawConfig &raw) {
        std::size_t nodes = 0, bytes = raw.value().size();
        if (bytes > kMaxConfigBytes) return false;
        return raw.visitSubItems(
            [&](const fcitx::RawConfig &item, const std::string &) {
                bytes += item.name().size() + item.value().size();
                return ++nodes <= kMaxConfigNodes && bytes <= kMaxConfigBytes;
            },
            "", true);
    }
    // SYS-227: exception words as the us keys that type them (either layout's form
    // matches); invalid entries are dropped. Words the owner wrote into the config,
    // never typed input.
    void rebuildExceptions() {
        exceptionKeys_.clear();
        // The shared rule (src/configrules.h) picks the entries, as the settings app does.
        const auto &words = *config_.exceptions;
        for (const std::size_t i : rasklad::config::keptExceptions({words.begin(), words.end()}))
            exceptionKeys_.push_back(rasklad::manual::exceptionKeys(words[i]));
        std::sort(exceptionKeys_.begin(), exceptionKeys_.end());
    }
    bool exception(const std::string &keys) const {
        return std::binary_search(exceptionKeys_.begin(), exceptionKeys_.end(), keys);
    }
    // Rebuild the application list from config_; the revision always advances so
    // no lease from the previous list stays valid.
    void rebuildApps() {
        auto &apps = productApps();
        permissionStore_.reset(); // nothing refers to the old list while it changes
        apps.adapters.clear();
        apps.ids.clear();
        // The shared rule (src/configrules.h): valid, never terminals, unique, capped.
        const auto &listed = *config_.applications;
        for (const std::size_t i : rasklad::config::keptApplications({listed.begin(), listed.end()}))
            apps.ids.push_back(listed[i]);
        // ids is final before adapters take views into it.
        for (const auto &id : apps.ids)
            apps.adapters.push_back({id, id, policy::Domain::RegularApplication, true, false, true});
        ++apps.revision;
        permissionStore_ = std::make_unique<policy::ApplicationPermissions>(fixtureRegistry());
    }
    // A changed list: discard every composition (no commit, as on focus-out)
    // using the old list, then publish the new one.
    void applyApps() {
        // discard() calls into clients; a nested config change must not rebuild
        // the list underneath this walk. The outer call applies the latest config_.
        if (applying_) return;
        applying_ = true;
        if (!registered_ || !manager_) {
            rebuildApps();
            enableFixturePermissions();
            wasEnabled_ = *config_.enabled;
            applying_ = false;
            return;
        }
        using Watch = fcitx::TrackableObjectReference<fcitx::InputContext>;
        std::vector<Watch> targets;
        manager_->foreach([&](fcitx::InputContext *ic) {
            if (state(ic, false)) targets.push_back(ic->watch());
            return true;
        });
        for (auto &watch : targets) {
            auto *ic = watch.get();
            if (!ic) continue;
            if (auto *s = state(ic, false)) discard(ic, *s);
        }
        rebuildApps();
        enableFixturePermissions();
        if (!*config_.enabled) {
            // Off keeps no names (nor the last status snapshot of them) and stops
            // a cue in flight.
            seenPrograms_.clear();
            status_.seenApplications.setValue(std::vector<std::string>{});
            cue_.stop();
            switchEvent_.reset(); // a layout switch queued before: never runs
        } else if (!wasEnabled_ && enableGeneration_ != std::numeric_limits<std::uint64_t>::max()) {
            // Turned on (usually from the settings window, so the field is not
            // focused now): any field may hold the start of a token typed while
            // off. Each field checks at its next press (handleKey).
            ++enableGeneration_;
        }
        wasEnabled_ = *config_.enabled;
        applying_ = false;
    }
    void enableFixturePermissions() {
        // Compile-time owned fixture contract only, never registry-as-authority
        // for a regular application or an inferred alias.
        for (const auto &adapter : fixtureAdapters())
            permissionStore_->enable(adapter.applicationId, kGrantMode);
    }
    policy::Permission currentPermission(const policy::Context &context) const {
        return permissionStore_->snapshot(context.verifiedApplicationId);
    }

    LabState *state(fcitx::InputContext *ic, bool create = true) {
        if (!registered_ || !ic) return nullptr;
        bool matches = false;
        for (const auto &adapter : fixtureAdapters())
            if (ic->program() == adapter.id) matches = true;
        if (!matches) return nullptr;
        auto *holder = static_cast<LabProperty *>(ic->propertyFor(&factory_));
        if (!holder) return nullptr;
        if (!holder->state && create) holder->state = std::make_unique<LabState>();
        return holder->state.get();
    }

    // Retain the SDK holder and stable hooks. Only terminal Off payloads may
    // retire; pending releases and uncertain originals still own their state.
    void reclaimOffState(fcitx::InputContext *ic) {
        if (!familyOff_) return;
        auto *s = state(ic, false);
        if (!s || s->disable->state() != policy::CompositionDisable::State::Disabled ||
            !s->empty() || s->preeditShown() || s->hasPending()) return;
        auto *holder = static_cast<LabProperty *>(ic->propertyFor(&factory_));
        holder->state.reset();
    }

    // Metadata gates. Must pass before anything is buffered.
    bool eligible(fcitx::InputContext *ic) const {
        if (!ic || !ic->hasFocus()) {
            return false;
        }
        if (!*config_.enabled || !productListed(ic->program())) return false;
        const fcitx::InputMethodEntry *entry = instance_->inputMethodEntry(ic);
        if (!entry || (entry->uniqueName() != kLabEntry && entry->uniqueName() != kRuEntry)) {
            return false;
        }
        const auto caps = ic->capabilityFlags();
        using fcitx::CapabilityFlag;
        if (!caps.test(CapabilityFlag::Preedit)) {
            return false;
        }
        // Protected fields and non-prose purposes (e-mail, URL, numbers, phone,
        // date, time): SYS-269, rasklad::word::refusedField.
        return !rasklad::word::refusedField(caps.toInteger());
    }

    // Called only after the existing profile, program, focus and caps gates.
    // Prose belongs to the explicit owned fixture contract, never real apps.
    static bool productListed(const std::string &program) {
        for (const auto &adapter : fixtureAdapters())
            if (program == adapter.id) return true;
        return false;
    }
    // Field kind from Fcitx capability hints only; the gate in eligible() refuses
    // the same flags first. Fcitx cannot tell prose apart: everything else Unknown.
    static policy::Field fieldFromCaps(const fcitx::CapabilityFlags caps) {
        using fcitx::CapabilityFlag;
        if (caps.test(CapabilityFlag::Password)) return policy::Field::Password;
        if (caps.test(CapabilityFlag::Sensitive) || caps.test(CapabilityFlag::Disable))
            return policy::Field::Protected;
        if (caps.test(CapabilityFlag::Terminal)) return policy::Field::Command;
        if (caps.test(CapabilityFlag::NoSpellCheck)) return policy::Field::Code;
        return policy::Field::Unknown;
    }
    // A listed compositor app id, as Fcitx reports it; never an authentication.
    static policy::Context fixtureContext(fcitx::InputContext *ic, const LabState &s) {
        const std::string_view id = ic->program();
        return {id, id, id, policy::Domain::RegularApplication,
                policy::Identity::CompositorAppId, fieldFromCaps(ic->capabilityFlags()),
                ic->hasFocus(), s.id, s.epoch, s.id};
    }
    static policy::Permission fixturePermission(const policy::Context &context) {
        // Fixed lab permission only; no production preference or runtime mode UI.
        return {true, context.verifiedApplicationId, policy::Mode::Automatic, 1};
    }
    bool validate(fcitx::InputContext *ic, LabState &s) const {
        if (!eligible(ic)) return false;
        const auto context = fixtureContext(ic, s);
        return s.disable->correction(fixtureRegistry(),
            currentPermission(context), context).allowed();
    }

    // LAB-only plumbing, deliberately without a runtime preference trigger.
    // The caller freshly evaluates every metadata gate without a callback.
    // Unknown retains original; Submitted releases local composition using
    // ordinary IME semantics. Neither result proves application acceptance.
    template <typename FinishOriginal>
    static void disableLab(fcitx::InputContext *ic, LabState &s,
                           bool currentlyEligible, FinishOriginal finish) {
        auto operation = s.disable; // Survives destruction of property/context.
        operation->requestOff(); // Stops capture/correction before external code.
        if (!currentlyEligible || !ic || !ic->hasFocus()) {
            operation->takeOriginal(fixtureRegistry(), {}); // Defer, never retry later.
            return;
        }
        const auto ticket = operation->takeOriginal(fixtureRegistry(),
                                                    fixtureContext(ic, s));
        if (!ticket) return;
        struct Original {
            std::string text;
            ~Original() { wipeString(text); }
        } original{s.shown()};
        // Unknown callbacks retain the original; the Submitted callback stages
        // local ownership release before its ordinary IME write. Either callback
        // may reset or destroy ic and s: never access them after it returns.
        const auto result = finish(ic, original.text);
        operation->complete(ticket, result);
    }

    // PRIVATE: only a trusted adapter that observed the original inserted in
    // this exact owned composition may call this. Protocol done/roundtrip and
    // callback return are NOT evidence. No runtime receipt source exists yet.
    static bool acknowledgeLabOriginal(fcitx::InputContext *ic, LabState &s,
                                       policy::CompositionDisable::Token generation,
                                       bool currentlyEligible) {
        if (!currentlyEligible || !ic || !ic->hasFocus() ||
            !generation || generation != s.generation ||
            s.disable->state() != policy::CompositionDisable::State::Deferred)
            return false;
        if (policy::disableDisposition(fixtureRegistry(), fixtureContext(ic, s),
                s.lease, true) != policy::DisableAction::FinishOriginalThenDisable)
            return false;
        if (!s.disable->nativeIdle(generation)) return false;
        s.clearBuffer();
        // Stage all state before external preedit callback. Tracked releases
        // remain drainable; do not re-enable or create a new composition here.
        clearPreedit(ic, s);
        return true;
    }

    static policy::CompositionDisable::Result commitOriginalUnknown(
        fcitx::InputContext *context, const std::string &original) {
        context->commitString(original);
        return policy::CompositionDisable::Result::Unknown;
    }

    static void submitLabOff(fcitx::InputContext *ic, LabState &s, bool current) {
        disableLab(ic, s, current,
            [&s](fcitx::InputContext *context, const std::string &original) {
                return commitAndClear(context, s, original)
                    ? policy::CompositionDisable::Result::Submitted
                    : policy::CompositionDisable::Result::Unknown;
            });
    }

    void requestLabOff(fcitx::InputContext *ic) {
        if (!ic || !manager_) return;
        // Explicit family: both compile-time Chromium fixture aliases belong to
        // this one owned client. Program strings are not process authentication.
        bool target = false;
        for (const auto &adapter : fixtureAdapters())
            if (ic->program() == adapter.id) target = true;
        if (!target) return;
        // Publish EVERY alias before any callback can capture in another context.
        familyOff_ = true;
        for (const auto &adapter : fixtureAdapters())
            permissionStore_->revoke(adapter.applicationId);
        using Watch = fcitx::TrackableObjectReference<fcitx::InputContext>;
        std::vector<Watch> targets;
        manager_->foreach([&](fcitx::InputContext *candidate) {
            bool matches = false;
            for (const auto &adapter : fixtureAdapters())
                if (candidate->program() == adapter.id) matches = true;
            if (matches) {
                if (auto *s = state(candidate, false)) {
                    s->disable->requestOff(); // Callback-free staging for all.
                    targets.push_back(candidate->watch());
                }
            }
            return true;
        });
        // Never carry a raw context/state across a prior target's callback.
        for (auto &watch : targets) {
            auto *current = watch.get();
            if (!current) continue;
            auto *s = state(current, false);
            if (!s) continue;
            submitLabOff(current, *s, eligible(current));
            // Submission can destroy/reset any context; never reuse current/s.
            if (auto *survivor = watch.get()) reclaimOffState(survivor);
        }
    }

    // Owned-fixture lifecycle operation; synthetic LAB trigger only, no user UI.
    // Inspect and stage on the event thread, with no client callbacks between
    // these phases. An uncertain original blocks the entire family.
    bool requestLabOn() {
        if (!registered_ || !manager_ || !familyOff_ || !permissionStore_->ready())
            return false;
        std::vector<LabState *> targets;
        bool safe = true;
        manager_->foreach([&](fcitx::InputContext *candidate) {
            bool matches = false;
            for (const auto &adapter : fixtureAdapters())
                if (candidate->program() == adapter.id) matches = true;
            if (matches) {
                auto *s = state(candidate, false);
                if (!s) return true; // An unallocated holder retains no original.
                const auto phase = s->disable->state();
                if ((phase != policy::CompositionDisable::State::Disabled &&
                     phase != policy::CompositionDisable::State::Idle) ||
                    !s->empty() || s->preeditShown()) safe = false;
                targets.push_back(s);
            }
            return true;
        });
        if (!safe || resumeGeneration_ == std::numeric_limits<std::uint64_t>::max())
            return false; // Generation exhaustion keeps the family Off.
        for (const auto &adapter : fixtureAdapters()) {
            if (!permissionStore_->enable(adapter.applicationId, kGrantMode)) {
                // Revision exhaustion permanently fails closed. Never expose
                // partially enabled aliases, even if publication fails midway.
                for (const auto &alias : fixtureAdapters())
                    permissionStore_->revoke(alias.applicationId);
                return false;
            }
        }
        for (auto *s : targets) {
            s->invalidate(); // No stale lease survives the permission revision.
            s->clearBuffer();
            s->generation = 0;
            s->setBypass(false);
            if (s->disable->state() == policy::CompositionDisable::State::Disabled)
                s->disable->enable();
            // Pending consumed releases remain drainable after re-enable.
        }
        ++resumeGeneration_;
        familyOff_ = false;
        return true;
    }

    // Replace the word this addon just committed with Space by its other form.
    // Only if nothing happened since (enforced by forgetLast on every other key and
    // lifecycle event), the context is focused, and the client's surrounding text
    // shows exactly that word + space right before a collapsed cursor. Unknown
    // surrounding text refuses (fail closed). Deletes exactly that many code points.
    static bool lastMatches(fcitx::InputContext *ic, const LabState &s) {
        const auto &st = ic->surroundingText();
        // SYS-272: an oversized surrounding text counts as unknown (fail closed).
        if (!st.isValid() || st.cursor() != st.anchor() || st.text().size() > kMaxSurroundingBytes) return false;
        std::string text = s.lastText();
        const bool ends = rasklad::manual::endsBeforeCursor(st.text(), st.cursor(), text);
        wipeString(text);
        return ends;
    }
    // Surrounding-text update from the client (fresh by definition).
    static void observeSurrounding(fcitx::InputContext *ic, LabState &s) {
        if (!s.lastValid) return;
        const bool match = lastMatches(ic, s);
        if (!s.lastConfirmed) {
            // Stale pre-commit updates may still arrive: they never confirm and never arm.
            // Fail closed when the pre-commit cursor was unknown.
            const unsigned cursor = ic->surroundingText().cursor();
            const long n = rasklad::manual::codePointCount(s.lastNow()) + rasklad::manual::codePointCount(s.lastSuffix);
            const bool fresh =
                (s.lastRule == LabState::Confirm::AfterReplace) ||
                (s.lastRule == LabState::Confirm::AfterCommit && n > 0 &&
                 static_cast<unsigned long>(cursor) == static_cast<unsigned long>(s.lastPreCursor) + static_cast<unsigned long>(n));
            if (match && fresh) { s.lastConfirmed = true; s.lastCursor = ic->surroundingText().cursor(); }
            return;
        }
        if (!match || ic->surroundingText().cursor() != s.lastCursor) s.forgetLast();
    }
    static bool replaceLast(fcitx::InputContext *ic, LabState &s) {
        std::string newText = rasklad::joined(s.lastOther(), s.lastSuffix);
        const auto &st = ic->surroundingText();
        const long n = rasklad::manual::codePointCount(s.lastNow()) + rasklad::manual::codePointCount(s.lastSuffix);
        const bool ok = ic->hasFocus() && s.lastConfirmed && n > 1 && lastMatches(ic, s) &&
            st.cursor() == s.lastCursor;
        if (!ok) { s.forgetLast(); wipeString(newText); return false; }
        // Staged before any client call: the new form is Pending until the client
        // confirms it with a fresh update (so a repeat toggle never acts on stale text).
        s.lastSwapped = !s.lastSwapped; s.lastOtherRu = !s.lastOtherRu; s.lastConfirmed = false;
        s.lastRule = LabState::Confirm::AfterReplace;
        ic->deleteSurroundingText(-static_cast<int>(n), static_cast<unsigned>(n));
        ic->commitString(newText);
        wipeString(newText);
        return true;
    }
    // Bare Shift_L/Shift_R: no Ctrl/Alt/Super; Shift's own state bit (set on the
    // release, depending on the frontend on the press) is ignored.
    // Enter, Tab or a punctuation character (with at most Shift): the word is over.
    static bool wordEnd(const fcitx::Key &key) {
        if (key.states().testAny(fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
                                                  fcitx::KeyState::Super, fcitx::KeyState::Hyper}))
            return false;
        const auto sym = key.sym();
        if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter || sym == FcitxKey_Tab) return true;
        return rasklad::word::endsWord(fcitx::Key::keySymToUnicode(sym));
    }
    // SYS-226 automatic correction of the current word, unless the owner chose a form
    // with the gesture. Only the reviewed profile's Correct that equals the word's
    // other-layout form is used. whole: the entire word converted (s.converted is set,
    // so the Space undo can restore it). Otherwise a us-typed word ending in keys that
    // carry Russian letters (",", ".", ";", ...) is tried without them, and those keys
    // stay as typed: "ckjdj," -> "слово,".
    // The corrected text goes into the caller's `out` (SYS-272: no returned
    // optional whose moved-from copy could stay in a dead stack frame).
    bool autoText(LabState &s, bool &whole, std::string &out) {
        whole = false;
        if (!*config_.autoCorrect) return false; // SYS-227: gestures only
        if (s.chosen || s.converted || !profile_ || !profile_->ready() || s.empty()) return false;
        // Case: all lowercase, a capital first letter, or all capitals, judged on the
        // keys that are converted (SYS-250: "NS," is all capitals plus a comma).
        const auto caseOk = [&s](std::size_t keys) { return rasklad::word::caseEligible(s.upper, keys); };
        std::string w = s.word();
        bool found = false;
        std::size_t cut = w.size();
        for (int pass = 0; pass < 2 && !found; ++pass) {
            if (pass == 1) {
                if (s.typedRu) break;
                cut = rasklad::word::lettersBeforeTail(w);
                if (cut == 0) break; // no letters, or nothing after them to strip
                // An exception that itself ends in a letter key the fallback strips
                // ("пою" is typed gj.) is never corrected to a shorter word.
                bool kept = false;
                for (std::size_t k = cut + 1; k < w.size() && !kept; ++k) {
                    std::string longer = w.substr(0, k);
                    kept = exception(longer);
                    wipeString(longer);
                }
                if (kept) break;
            }
            std::string keys = w.substr(0, cut);
            // The exception first: an excepted word is never corrected in a shorter
            // form just because its own case was not eligible.
            if (exception(keys)) { wipeString(keys); break; } // SYS-227: never this word
            if (!caseOk(cut)) { wipeString(keys); continue; }
            std::string typed = LabState::form(keys, s.typedRu);
            std::string other = LabState::form(keys, !s.typedRu);
            auto fix = profile_->correctTyped(typed, s.typedRu);
            const bool ok = (fix && *fix == other && other != typed) ||
                            (other != typed && rasklad::frequent::isFrequent(other, s.typedRu));
            if (ok) {
                if (pass == 0) {
                    s.converted = true; whole = true;
                    std::string shown = s.shown();
                    out.assign(shown); // copied, then the local is wiped (SYS-272)
                    wipeString(shown);
                    found = true;
                }
                else {
                    // The pieces are wiped after use (SYS-272).
                    std::string tail = w.substr(cut);
                    std::string both = rasklad::joined(other, tail);
                    std::string shown = LabState::form(both, false, s.upper);
                    out.assign(shown);
                    wipeString(tail); wipeString(both); wipeString(shown);
                    found = true;
                }
            }
            if (fix) wipeString(*fix);
            wipeString(keys); wipeString(typed); wipeString(other);
        }
        wipeString(w);
        return found;
    }
    // A press of a modifier or lock key alone (Ctrl, Alt, Super, Caps Lock, AltGr,
    // ...): it types nothing. The key it is combined with (Ctrl+A) is non-plain and
    // flushes and bypasses itself, so over an empty buffer the press alone must
    // not latch bypass (SYS-272); over a word in progress it still commits it.
    static bool bareModifier(fcitx::KeySym sym) {
        switch (sym) {
        case FcitxKey_Control_L: case FcitxKey_Control_R: case FcitxKey_Alt_L: case FcitxKey_Alt_R:
        case FcitxKey_Meta_L: case FcitxKey_Meta_R: case FcitxKey_Super_L: case FcitxKey_Super_R:
        case FcitxKey_Hyper_L: case FcitxKey_Hyper_R: case FcitxKey_Caps_Lock: case FcitxKey_Shift_Lock:
        case FcitxKey_Num_Lock: case FcitxKey_ISO_Level3_Shift: case FcitxKey_ISO_Level5_Shift:
        case FcitxKey_Mode_switch: case FcitxKey_ISO_Next_Group: case FcitxKey_ISO_Prev_Group:
        case FcitxKey_ISO_First_Group: case FcitxKey_ISO_Last_Group:
            return true;
        default:
            return false;
        }
    }
    static bool isShift(const fcitx::Key &key) {
        if (key.sym() != FcitxKey_Shift_L && key.sym() != FcitxKey_Shift_R) return false;
        return !fcitx::Key(key.sym(), key.states().unset(fcitx::KeyState::Shift)).hasModifier();
    }
    static void showPreedit(fcitx::InputContext *ic, LabState &s) {
        std::string w = s.shown();
        fcitx::Text text;
        const int cursor = static_cast<int>(w.size()); // Fcitx Text cursor is a byte offset
        // Copied, then wiped: copies Fcitx itself makes are beyond our reach (SYS-272).
        text.append(w, fcitx::TextFormatFlag::Underline);
        wipeString(w);
        text.setCursor(cursor);
        ic->inputPanel().setClientPreedit(text);
        s.setPreeditShown(true);
        ic->updatePreedit();
    }

    static void clearPreedit(fcitx::InputContext *ic, LabState &s) {
        if (!s.preeditShown()) {
            return; // never touch a preedit we did not set
        }
        s.setPreeditShown(false);
        ic->inputPanel().setClientPreedit(fcitx::Text());
        ic->updatePreedit();
    }

    // Stage every local mutation and consumption before notifying the client.
    // A notification may destroy both the context and its LabState.
    static void acceptLetter(fcitx::InputContext *ic, LabState &s,
                             fcitx::KeyEvent &event, char letter) {
        s.push(letter);
        s.addPending(event.key());
        event.filterAndAccept();
        showPreedit(ic, s); // No state/event access after this callback.
    }
    static void acceptBackspace(fcitx::InputContext *ic, LabState &s,
                                fcitx::KeyEvent &event) {
        s.pop();
        const bool empty = s.empty();
        if (empty) s.clearBuffer();
        s.addPending(event.key());
        event.filterAndAccept();
        if (empty) clearPreedit(ic, s);
        else showPreedit(ic, s);
    }

    // Drop everything for this context, never commit.
    static void discard(fcitx::InputContext *ic, LabState &s) {
        s.invalidate();
        // An Off request with an uncertain write retains the bounded original.
        // Context destruction still wipes it; no delivery guarantee is claimed.
        if (s.stopping()) return;
        s.clearBuffer();
        s.clearPending();
        s.setBypass(false);
        clearPreedit(ic, s);
    }

    // Single commit to the same, gated, focused context; then clear.
    // All LabState changes are staged locally BEFORE the external commit: the
    // buffer is cleared and, if we own the preedit, preeditShown is reset and
    // the panel preedit is emptied locally without sending it. The commit
    // payload therefore reaches the client while its composition is still
    // shown; the empty-preedit wire update is sent only afterwards, and only
    // if the context still exists. Nothing touches LabState after
    // commitString(); a local bool carries the decision.
    // Returns false when the Slack session gate refused the commit.
    static bool commitAndClear(fcitx::InputContext *ic, LabState &s,
                               std::string text) {
        auto ref = ic->watch();
        auto binding = s.callbackBinding;
        const auto epoch = s.epoch;
        const bool hadPreedit = s.preeditShown();
        s.clearBuffer();
        if (hadPreedit) {
            s.setPreeditShown(false);
            ic->inputPanel().setClientPreedit(fcitx::Text());
        }
        ic->commitString(text);
        if (hadPreedit && binding->alive && binding->epoch == epoch) {
            if (auto *current = ref.get(); current && current->hasFocus()) {
                current->updatePreedit();
            }
        }
        wipeString(text);
        return true;
    }


    void handleKey(fcitx::KeyEvent &event) {
        if (!registered_ || !*config_.enabled || event.filtered()) {
            return;
        }
        fcitx::InputContext *ic = event.inputContext();
        const fcitx::Key &key = event.key();

        if (event.isRelease()) {
            // A bare Shift release may complete the gesture. It is never filtered:
            // the application sees a symmetric modifier press/release.
            if (isShift(key)) {
                auto *s = state(ic, false);
                if (!s) return;
                if (familyOff_ || !eligible(ic) || s->stopping()) {
                    s->gesture.reset(); s->forgetLast();
                    return;
                }
                if (s->empty() && s->lastValid && s->lastExpired()) s->forgetLast();
                if (s->empty() && s->lastValid) {
                    if (s->gesture.feed(rasklad::manual::Input::ShiftRelease, event.time()))
                        scheduleUndo(ic, *s);
                    return;
                }
                if (s->empty() || !validate(ic, *s)) {
                    s->gesture.reset(); // an abandoned release ends the gesture
                    return;
                }
                if (s->gesture.feed(rasklad::manual::Input::ShiftRelease, event.time())) {
                    s->converted = !s->converted;
                    s->chosen = true;
                    showPreedit(ic, *s); // No state/event access after this callback.
                }
                return;
            }
            // Never mutates the buffer; only swallows the release of a press
            // we consumed in this same context.
            if (auto *s = state(ic, false); s && s->takePending(key)) {
                event.filterAndAccept();
                reclaimOffState(ic); // No later state/event access after retirement.
            }
            return;
        }

        // Off presses do no profile/capability work and allocate no state.
        // A new raw press supersedes only its own missing old release, just
        // like the enabled path. Its later raw release must not be swallowed.
        if (familyOff_) {
            if (auto *s = state(ic, false)) s->takePending(key);
            reclaimOffState(ic); // No old-state access after this point.
            return;
        }

        if (!eligible(ic)) {
            // Gates no longer hold: discard without commit, pass the key.
            if (auto *s = state(ic, false)) {
                // discard() forgets the kept word too (invalidate) and may run a
                // client callback that destroys this state: nothing after it.
                if (s->active()) discard(ic, *s);
                else if (s->lastValid) s->forgetLast(); // the kept word alone (SYS-272)
            }
            return;
        }

        LabState *s = state(ic);
        if (!s) {
            return;
        }
        if (s->stopping()) return; // Off: no new capture, correction or buffer clear.
        if (!s->empty() && !validate(ic, *s)) {
            discard(ic, *s);
            return; // In particular, a denied Space remains unconsumed.
        }
        // A new press of a tracked key means its old release was lost.
        s->takePending(key);

        const fcitx::KeySym sym = key.sym();
        const bool plain = !key.hasModifier();
        // A bare Shift press over a non-empty word defers the decision (no flush,
        // no bypass) and passes unfiltered. Every other press breaks the gesture.
        if (isShift(key)) {
            if ((!s->empty() || s->lastValid) && !s->bypass() && !s->needsBoundary && !s->enableGuard) {
                s->gesture.feed(rasklad::manual::Input::ShiftPress, event.time());
                return;
            }
            s->gesture.reset();
            // A bare Shift over an empty buffer must not latch bypass: the next
            // lowercase word is still a whole word. Shift+letter is non-plain and
            // takes the flush/bypass path itself.
            if (s->empty() && !s->bypass() && !s->needsBoundary && !s->enableGuard) return;
        } else {
            s->gesture.feed(rasklad::manual::Input::Other, event.time());
            s->forgetLast(); // any other key (letters, arrows, BackSpace, ...) ends the chance
            // Over an empty buffer: no bypass, so Alt Alt or Caps Lock before a word
            // does not leave that word uncorrected (SYS-272). Over a word in progress
            // the word is still committed in the form shown (as typed, or as double
            // Shift turned it): Alt may open a menu and take the focus, and
            // focus-out would drop the uncommitted letters.
            if (bareModifier(key.sym()) && s->empty()) return;
        }
        const bool boundary = plain && (sym == FcitxKey_space ||
            sym == FcitxKey_Tab || sym == FcitxKey_Return || sym == FcitxKey_KP_Enter);
        if (s->resumeGeneration != resumeGeneration_) {
            s->resumeGeneration = resumeGeneration_;
            s->needsBoundary = true;
        }
        // First press in this field since Rasklad was turned on: unless the cursor
        // is known to start a token, the presses stay raw, so a suffix of a token
        // typed while off is never corrected on its own. Only a Space ends the
        // guard (Enter or Tab may insert nothing), and the press after it checks
        // the field again: a Space the field ignored, with the cursor still after
        // a letter, keeps the guard. A field that reports no text is trusted.
        if (s->enableGeneration != enableGeneration_) {
            s->enableGeneration = enableGeneration_;
            s->enableGuardSpace = false;
            s->enableGuard = tokenStart(ic) != TokenStart::Yes;
        }
        if (s->enableGuard && s->enableGuardSpace) {
            s->enableGuardSpace = false;
            if (tokenStart(ic) != TokenStart::No) s->enableGuard = false;
        }
        if (s->enableGuard) {
            s->enableGuardSpace = plain && sym == FcitxKey_space;
            return;
        }
        if (s->needsBoundary) {
            if (boundary) s->needsBoundary = false;
            return; // The delimiter itself and all preceding presses stay raw.
        }
        // Never begin correcting a suffix of a token already passed through.
        // Mixed tokens, punctuation and overflow bypass until a safe delimiter.
        if (s->bypass()) {
            if (boundary) s->setBypass(false);
            return;
        }

        char manualChar = 0; // , . ; ' [ ] ` carry б ю ж э х ъ ё on the same keys
        switch (sym) {
        case FcitxKey_comma: manualChar = ','; break;
        case FcitxKey_period: manualChar = '.'; break;
        case FcitxKey_semicolon: manualChar = ';'; break;
        case FcitxKey_apostrophe: manualChar = '\''; break;
        case FcitxKey_bracketleft: manualChar = '['; break;
        case FcitxKey_bracketright: manualChar = ']'; break;
        case FcitxKey_grave: manualChar = '`'; break;
        default: break;
        }
        if (plain && sym >= FcitxKey_a && sym <= FcitxKey_z) manualChar = static_cast<char>('a' + (sym - FcitxKey_a));
        bool ruKey = false;
        // SYS-226: a lowercase Russian letter (ru layout) is kept as its us key.
        if (plain && !manualChar) {
            manualChar = rasklad::manual::ruToUsKey(
                static_cast<char32_t>(fcitx::Key::keySymToUnicode(static_cast<fcitx::KeySym>(sym))));
            ruKey = manualChar != 0;
        }
        // A layout switch inside a word ends the word (flushed as typed below).
        // SYS-226: a capital letter (Shift or Caps Lock, no other modifier) is kept as
        // its lowercase key plus a case bit.
        bool upperKey = false;
        if (!manualChar && !key.states().testAny(fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
                                                                 fcitx::KeyState::Super, fcitx::KeyState::Hyper})) {
            if (sym >= FcitxKey_A && sym <= FcitxKey_Z) {
                manualChar = static_cast<char>('a' + (sym - FcitxKey_A));
                upperKey = true;
            } else {
                const char32_t u = static_cast<char32_t>(fcitx::Key::keySymToUnicode(static_cast<fcitx::KeySym>(sym)));
                const char32_t lower = (u >= U'\u0410' && u <= U'\u042f') ? u + 0x20 : (u == U'\u0401' ? U'\u0451' : 0);
                if (lower) {
                    manualChar = rasklad::manual::ruToUsKey(lower);
                    ruKey = upperKey = manualChar != 0;
                }
            }
        }
        if (manualChar && !s->empty() && ruKey != s->typedRu) manualChar = 0;
        const bool letterKey = plain || upperKey;
        if (letterKey && manualChar && !s->full()) {
            if (s->empty()) {
                const auto context = fixtureContext(ic, *s);
                const auto generation = s->disable->bind(fixtureRegistry(),
                    currentPermission(context), context);
                if (!generation) return;
                s->generation = generation;
                s->lease = policy::authorizeCapture(fixtureRegistry(),
                    currentPermission(context), context).lease;
            }
            if (s->empty()) s->typedRu = ruKey;
            if (upperKey) s->upper |= std::uint32_t{1} << s->size();
            acceptLetter(ic, *s, event, manualChar);
            return;
        }

        if (plain && !s->empty()) {
            if (sym == FcitxKey_space) {
                std::string w = s->word();
                // SYS-226: automatic correction only if the owner did not choose a form
                // with the gesture, and only on the reviewed profile's Correct, which
                // must equal this word's other-layout form. Double Shift right after
                // the Space restores the original (replaceLast); SYS-250: also when
                // only the letters were corrected and trailing keys kept ("ns," -> "ты,").
                bool whole = false;
                std::string fixedText;
                const bool fixed = autoText(*s, whole, fixedText);
                // Copied, not moved: a short string's bytes would stay behind (SYS-272).
                std::string shown = fixed ? std::string() : s->shown();
                // Room for the space up front, so push_back does not reallocate (SYS-272).
                std::string out = rasklad::joined(fixed ? fixedText : shown, std::string(), 1);
                wipeString(fixedText);
                wipeString(shown);
                // The other form: after a correction (whole or letters only) the word
                // exactly as typed; otherwise the whole word in the layout not shown.
                const bool otherRu = fixed ? s->typedRu : s->typedRu == s->converted;
                s->keepLast(ic->surroundingText(), out, LabState::form(w, otherRu, s->upper), otherRu,
                            std::string(" "));
                armExpiry(ic, *s);
                wipeString(w);
                out.push_back(' ');
                if (!validate(ic, *s)) {
                    wipeString(out);
                    discard(ic, *s);
                    return;
                }
                // No callbacks between this final check and staged commit.
                s->addPending(key);
                event.filterAndAccept();
                const bool switchAfter = fixed;
                const bool toRu = !s->typedRu;
                auto ref = ic->watch();
                commitAndClear(ic, *s, out);
                wipeString(out);
                if (switchAfter) switchLayout(ref, toRu); // the corrected word's layout
                return;
            }
            if (sym == FcitxKey_BackSpace) {
                acceptBackspace(ic, *s, event);
                return;
            }
        }

        // Every other press (incl. full buffer): flush ORIGINAL letters to this
        // same focused context, then let the key pass unfiltered.
        // Punctuation ends a word and a new one may start right after it (куда-то).
        if (!boundary && !wordEnd(key)) s->setBypass(true);
        if (!s->empty()) {
            if (!validate(ic, *s)) {
                discard(ic, *s);
                return;
            }
            // SYS-226: a punctuation key, Enter or Tab also ends the word: the same
            // automatic correction as Space, then the key passes. SYS-250: after a
            // punctuation character (not Enter or Tab: Enter may already have sent
            // the message) double Shift swaps the word back, with the character the
            // client inserted kept as it is.
            bool whole = false;
            std::string fixedText;
            const bool ends = wordEnd(key);
            const bool fixed = ends && autoText(*s, whole, fixedText);
            const bool toRu = !s->typedRu;
            auto ref = ic->watch();
            const bool switchAfter = fixed;
            std::string out = fixed ? fixedText : s->shown(); // copied, not moved (SYS-272)
            wipeString(fixedText);
            const std::uint32_t mark = fcitx::Key::keySymToUnicode(sym);
            if (ends && mark > 0x20 && mark != 0x7f) {
                const bool otherRu = fixed ? s->typedRu : s->typedRu == s->converted;
                std::string w = s->word();
                s->keepLast(ic->surroundingText(), out, LabState::form(w, otherRu, s->upper), otherRu,
                            fcitx::utf8::UCS4ToUTF8(mark));
                armExpiry(ic, *s);
                wipeString(w);
            }
            commitAndClear(ic, *s, out);
            wipeString(out);
            if (switchAfter) switchLayout(ref, toRu);
        }
    }

    fcitx::Instance *instance_;
    fcitx::InputContextManager *manager_;
    RaskladConfig config_;
    mutable RaskladStatus status_; // filled on each read; see getSubConfig
    std::vector<std::string> seenPrograms_; // SYS-253: names only, memory only
    std::vector<std::string> exceptionKeys_; // sorted
    bool applying_ = false;
    std::unique_ptr<rasklad::fcitxlab::ProfileDecision> profile_;
    std::unique_ptr<fcitx::EventSource> switchEvent_;
    // SYS-271: the double-Shift undo waits kUndoDelayUs, then checks the same context
    // again. A focus change, reset, any key or a new word in the meantime cancels it
    // (the focus or reset invalidates the state; a key forgets the word). Shift is
    // never filtered, so the wait delays no typed character.
    static constexpr std::uint64_t kUndoDelayUs = 40000;
    std::unique_ptr<fcitx::EventSourceTime> undoEvent_;
    // SYS-272: once the most recently kept word's lifetime is over, wipes every
    // expired kept word in all contexts, even if no key follows. Words kept earlier
    // expire earlier, so one timer armed for the latest word covers them all.
    std::unique_ptr<fcitx::EventSourceTime> expireEvent_;
    // One timer for all fields, always set to the earliest kept word's deadline: a
    // word kept later in another field never pushes an earlier wipe back (SYS-272).
    void armExpiry(fcitx::InputContext *, LabState &s) {
        if (!instance_ || !s.lastValid) return;
        const std::uint64_t deadline = s.lastAtUs + LabState::kLastLifetimeUs + 1000;
        if (expireEvent_ && expireEvent_->isEnabled() && expireEvent_->time() <= deadline) return;
        if (expireEvent_) {
            expireEvent_->setTime(deadline);
            expireEvent_->setOneShot();
            return;
        }
        expireEvent_ = instance_->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, deadline, 100000, [this](fcitx::EventSourceTime *src, std::uint64_t) {
                std::uint64_t next = 0;
                instance_->inputContextManager().foreach([this, &next](fcitx::InputContext *c) {
                    if (auto *s = state(c, false); s && s->lastValid) {
                        if (s->lastExpired()) s->forgetLast();
                        else {
                            const std::uint64_t d = s->lastAtUs + LabState::kLastLifetimeUs + 1000;
                            if (!next || d < next) next = d;
                        }
                    }
                    return true;
                });
                if (next) {
                    src->setTime(next);
                    src->setOneShot();
                }
                return true;
            });
    }
    void scheduleUndo(fcitx::InputContext *ic, LabState &s) {
        if (!instance_) return;
        auto ref = ic->watch();
        const auto epoch = s.epoch;
        const auto generation = s.lastGeneration;
        undoEvent_ = instance_->eventLoop().addTimeEvent(
            // Explicit 1 ms tolerance: with 0, sd-event allows 250 ms of slack, and a key
            // typed in that time would silently cancel the undo.
            CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + kUndoDelayUs, 1000,
            [this, ref, epoch, generation](fcitx::EventSourceTime *, std::uint64_t) {
                auto *c = ref.get();
                if (!c) return true;
                auto *s = state(c, false);
                if (!s || s->epoch != epoch || s->lastGeneration != generation || s->lastExpired() ||
                    !s->empty() || familyOff_ || !eligible(c) || s->stopping())
                    return true;
                // The restored form's layout becomes current (SYS-226).
                const bool toRu = s->lastOtherRu;
                if (replaceLast(c, *s)) switchLayout(ref, toRu);
                return true;
            });
    }
    bool selfSwitch_ = false;
    bool wasEnabled_ = false; // Enabled as of the last applied config
    std::uint64_t enableGeneration_ = 0; // bumped each time Rasklad is turned on
    const std::string startId_ = randomId(); // OnEpoch: differs on every addon start
    static std::string randomId() {
        std::random_device device;
        char out[17];
        std::snprintf(out, sizeof out, "%08x%08x", device(), device());
        return out;
    }
    // Yes: the field reports its text and the cursor is at its start or after
    // ASCII whitespace. Unknown: the field reports no text at all. No: anything
    // else, including a report that cannot be used (oversized as in lastMatches,
    // invalid UTF-8, a selection, a cursor out of range), so such a field never
    // lifts the enable guard on a Space it may have ignored.
    enum class TokenStart { Yes, No, Unknown };
    static TokenStart tokenStart(fcitx::InputContext *ic) {
        const auto &st = ic->surroundingText();
        if (!st.isValid()) return TokenStart::Unknown;
        if (st.text().size() > kMaxSurroundingBytes) return TokenStart::No;
        const std::string &text = st.text();
        if (fcitx::utf8::lengthValidated(text) == fcitx::utf8::INVALID_LENGTH) return TokenStart::No;
        if (st.cursor() != st.anchor()) return TokenStart::No;
        if (st.cursor() == 0) return TokenStart::Yes;
        if (st.cursor() > fcitx::utf8::length(text)) return TokenStart::No;
        const auto bytes = fcitx::utf8::ncharByteLength(text.begin(), st.cursor());
        if (bytes <= 0) return TokenStart::No;
        const char before = text[static_cast<std::size_t>(bytes) - 1];
        return before == ' ' || before == '\t' || before == '\n' || before == '\r' ? TokenStart::Yes : TokenStart::No;
    }
    rasklad::cue::LayoutCue cue_; // declared before watchers_: destroyed after them
    // SYS-226 (owner decision 2026-11-21): after a correction the corrected word's
    // layout becomes the current Fcitx input method, so the owner keeps typing in it.
    // Only between keyboard-us and keyboard-ru, only if both are in the current group,
    // deferred to the next loop turn, and only while that context is focused with no
    // word in progress (never drops typed letters).
    // Double Alt is detected by Hyprland, not here: after the first Alt, Electron
    // apps (Slack) move focus to their menu bar and Fcitx never sees the second.
    void switchLayout(fcitx::TrackableObjectReference<fcitx::InputContext> ref, bool toRu) {
        if (!instance_) return;
        const std::string target = toRu ? kRuEntry : kLabEntry;
        bool us = false, ru = false;
        for (const auto &item : instance_->inputMethodManager().currentGroup().inputMethodList()) {
            us = us || item.name() == kLabEntry;
            ru = ru || item.name() == kRuEntry;
        }
        if (!us || !ru) return;
        switchEvent_ = instance_->eventLoop().addDeferEvent([this, ref, target](fcitx::EventSource *) {
            auto *c = ref.get();
            if (!*config_.enabled || !c || !c->hasFocus()) return true; // turned off meanwhile
            if (auto *s = state(c, false); s && !s->empty()) return true;
            const auto *entry = instance_->inputMethodEntry(c);
            if (!entry || entry->uniqueName() == target) return true;
            selfSwitch_ = true;
            instance_->setCurrentInputMethod(c, target, false);
            selfSwitch_ = false;
            return true;
        });
    }
    std::unique_ptr<policy::ApplicationPermissions> permissionStore_ =
        std::make_unique<policy::ApplicationPermissions>(fixtureRegistry());
    bool familyOff_ = false;
    std::uint64_t resumeGeneration_ = 0;
    fcitx::LambdaInputContextPropertyFactory<LabProperty> factory_{
        [](fcitx::InputContext &) { return new LabProperty; }};
    bool registered_ = false;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        watchers_;
};

class RaskladLabFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new RaskladLab(manager->instance());
    }
};

} // namespace

FCITX_ADDON_FACTORY(RaskladLabFactory);
