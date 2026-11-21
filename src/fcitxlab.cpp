// SPDX-License-Identifier: MIT
//
// SYS-244 rasklad Fcitx5 transport LAB addon (developer prototype only).
//
// Purpose: prove the IME transport path (client preedit -> one commit on Space)
// against a fixed, named test harness. This is NOT a corrector, NOT a context
// classifier and NOT installable product code.
//
// Safety boundaries (see SECURITY.md, docs/ARCHITECTURE-RESET-001.md):
//  * Active only when ALL metadata gates hold, checked before any buffering:
//      - fixed lab identity (rasklad-ime-lab, or the explicitly compiled
//        Chromium backend identifiers in separate lab variants;
//        it is never used to guess prose/secret semantics of real apps),
//      - the active input method entry is exactly "keyboard-us",
//      - the context has focus and the Preedit capability,
//      - none of Password / Sensitive / NoSpellCheck / Terminal /
//        Disable capability flags is set.
//  * Default variants use one synthetic rule. The separate PROFILE_LAB build
//    replaces it with the verified local evidence core; no synthetic fallback.
//  * State lives in a per-InputContext property (InputContextProperty factory).
//    There is no global map of raw InputContext pointers. The property does not
//    override needCopy(), so Fcitx never copies a buffer to another context
//    (e.g. under shared input state policies).
//  * Buffer: at most 32 lowercase ASCII letters. Zeroed best effort on clear;
//    std::string temporaries for preedit/commit may leave allocator copies.
//  * deleteSurroundingText only in RASKLAD_MANUAL_LAB replaceLast() (see there).
//  * No logging, stdout/stderr, clipboard,
//    forwardKey, network, IPC of our own, child processes or evaluation.
//    Initialization reads fixed fixture environment labels; only PROFILE_LAB
//    initialization reads fixed verified /lab/profile files;
//    key handling performs no file I/O.
//  * Every commit goes to the SAME context whose key event is being handled,
//    synchronously, while it is focused and gated. Focus-out, focus-in, reset,
//    input method switch (incl. deactivate) and capability change discard the
//    buffer WITHOUT committing. Context destruction needs no handler: Fcitx
//    deletes the property together with the context, and we never call into a
//    destroying context.
//
// RASKLAD_WITNESS_LAB (separate developer variant only, built by
// tools/fcitx_witness_lab.pro on top of PROFILE_LAB):
//  * adds observation-only saturating counters and one read-only GetStats
//    D-Bus method on the private lab bus (tools/fcitxlabwitness.h).
//  * reads fixed environment markers once at initialization (never in the key
//    path) and stays fully inert if any marker differs.
//  * every witness line is under #ifdef; it never changes gates, filtering,
//    return paths, ordering or state creation. Default builds contain none of
//    it: no endpoint, counters or D-Bus dependency. All builds read the
//    fixture-scope labels above once during initialization.
//
// Key policy for a gated key PRESS:
//  * a..z without modifiers (CapsLock/NumLock lock bits are tolerated because
//    Key::hasModifier() ignores them; uppercase syms are not accepted):
//      append if buffer < 32, update preedit, consume.
//      If the buffer is full: flush original (see below), then pass the key.
//  * Space (no modifiers) with non-empty buffer: commit corrected-or-original
//    word plus one space as ONE commitString before notifying preedit end, consume.
//  * BackSpace (no modifiers) with non-empty buffer: drop last letter, consume.
//  * Escape flushes the ORIGINAL word and passes through like other app
//    shortcuts. Clearing composition alone has toolkit-dependent semantics;
//    explicit original commit preserves typed text in both Qt and Chromium.
//  * Anything else (modifier keys themselves, Shift/Ctrl/Alt/Super combos,
//    Enter, Tab, navigation, punctuation, digits, uppercase, ...): if the
//    buffer is non-empty, FLUSH the ORIGINAL (uncorrected) letters as one
//    commit to the same, currently focused, gated context, clear preedit, then
//    let the key pass unfiltered to the keyboard engine / application.
//    Space/BackSpace/Escape with an empty buffer also just pass through.
//  * If a press arrives while the gates no longer hold (e.g. capability or
//    entry changed without an event), the buffer is discarded WITHOUT commit
//    and the key passes.
//
// Unsupported token prefixes and overflow latch passthrough until the next
// plain Space/Tab/Return, so a suffix is never mistaken for a whole new word.
// CapabilityAboutToChange clears visible preedit before loss of Preedit support.
//
// Key RELEASE policy: releases never mutate the buffer. The only action is
// filtering the release of a key whose press we consumed in the same context,
// tracked in a bounded (8 entry) per-context list, so the application does not
// see an orphan release. When the list is full the oldest entry is dropped and
// that release simply leaks to the application (harmless key-up).
//
// Known unresolved limitations (explicit):
//  * An addon or Fcitx crash loses the current uncommitted preedit word.
//  * Focus-out / reset / IM switch lose the uncommitted word by design.
//  * A client advertising ClientUnfocusCommit may itself commit the visible
//    preedit into ITS OWN field on focus-out; the addon never does. Observe this
//    in the transport run.
//  * The keyboard engine also owns the client preedit slot; we only clear the
//    preedit if we set it ourselves (preeditShown flag).

#include "applicationpolicy.h"
#include "applicationpermissions.h"
#include "compositiondisable.h"
#if defined(RASKLAD_SLACK_GATE_TEST_PROGRAM) && !defined(RASKLAD_SLACK_SESSION_LAB)
#error "RASKLAD_SLACK_GATE_TEST_PROGRAM only modifies the Slack session gate build"
#endif
#ifdef RASKLAD_SLACK_SESSION_LAB
#if defined(RASKLAD_CHROMIUM_LAB) || defined(RASKLAD_PROFILE_LAB) || defined(RASKLAD_DISABLE_TEST_LAB) || \
    defined(RASKLAD_WITNESS_LAB) || defined(RASKLAD_APPLICATION_OFF_TEST) || defined(RASKLAD_PREEDIT_REENTRY_TEST) || \
    defined(RASKLAD_DISABLE_REENTRY_TEST)
#error "RASKLAD_SLACK_SESSION_LAB is a separate artifact"
#endif
#include "sessionarm.h"
#endif

#ifdef RASKLAD_PROFILE_LAB
#include "fcitxlabprofile.h"
#endif

#ifdef RASKLAD_MANUAL_LAB
// SYS-225 slice 1 (docs/SYS-225-MANUAL-GESTURE.md): no automatic correction;
// a double Shift toggles the CURRENT preedit word us<->ru view, Space commits
// what is shown. Never touches text the application already accepted.
#if defined(RASKLAD_PROFILE_LAB) || defined(RASKLAD_WITNESS_LAB) || defined(RASKLAD_DISABLE_TEST_LAB)
#error "RASKLAD_MANUAL_LAB is a separate artifact"
#endif
#include "manualgesture.h"
#endif

#ifdef RASKLAD_WITNESS_LAB
#ifndef RASKLAD_PROFILE_LAB
#error "RASKLAD_WITNESS_LAB requires RASKLAD_PROFILE_LAB (use tools/fcitx_witness_lab.pro)"
#endif
#include "fcitxlabwitness.h"
#endif

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace {

#ifdef RASKLAD_WITNESS_LAB
namespace lw = rasklad::fcitxlab::witness;
#endif

constexpr std::size_t kMaxWord = 32;
constexpr std::size_t kMaxPendingReleases = 8;
// Separate compile-time lab variant; never an environment-controlled app list.
#ifdef RASKLAD_CHROMIUM_LAB
constexpr const char kLabProgram[] = "chromium";
#elif defined(RASKLAD_SLACK_SESSION_LAB) && defined(RASKLAD_SLACK_GATE_TEST_PROGRAM)
// Offline gate-test artifact only: the same gates against the owned Chromium
// fixture (its observed native Wayland program). Never used with Slack.
constexpr const char kLabProgram[] = "chromium-browser";
#elif defined(RASKLAD_SLACK_SESSION_LAB)
// Measured Fcitx input-context program of Slack's native Wayland window
// (data/slack-ime-milestone1-dryrun-result.json). Not an authentication.
constexpr const char kLabProgram[] = "slack";
#else
constexpr const char kLabProgram[] = "rasklad-ime-lab";
#endif
namespace policy = rasklad::policy;
#ifdef RASKLAD_SLACK_SESSION_LAB
// Supervisor arm pipe, read once from the environment; latches off on revoke.
rasklad::sessionarm::SessionArm &sessionArm() {
    static auto arm = rasklad::sessionarm::SessionArm::fromEnvironment();
    return arm;
}
// Set when the revoke watcher could not be installed: then nothing is armed.
bool sessionWatchFailed = false;
bool sessionArmed() { return !sessionWatchFailed && sessionArm().armed(); }
rasklad::sessionarm::ReceiptSink &receiptSink() {
    static auto sink = rasklad::sessionarm::ReceiptSink::fromEnvironment();
    return sink;
}
#endif
// Fixed fixture labels only: these do not authenticate a process or namespace.
bool fixtureScope() {
    const auto equal = [](const char *key, const char *value) {
        const char *actual = std::getenv(key);
        return actual && std::strcmp(actual, value) == 0;
    };
    return equal("HOME", "/home/lab") && equal("XDG_RUNTIME_DIR", "/run/lab") &&
#ifdef RASKLAD_CHROMIUM_LAB
           equal("RASKLAD_CHROMIUM_IME_LAB", "1");
#elif defined(RASKLAD_SLACK_SESSION_LAB)
           equal("RASKLAD_SLACK_SESSION_LAB", "1") && receiptSink().valid() && sessionArmed();
#else
           equal("RASKLAD_PRIVATE_LAB", "1");
#endif
}
constexpr policy::Adapter kFixtureAdapters[] = {
    {kLabProgram, kLabProgram, policy::Domain::OwnedFixture, true, true},
#ifdef RASKLAD_CHROMIUM_LAB
    {"chromium-browser", "chromium-browser", policy::Domain::OwnedFixture, true, true},
#endif
};
constexpr policy::Registry kFixtureRegistry{kFixtureAdapters,
    sizeof(kFixtureAdapters) / sizeof(kFixtureAdapters[0]), 1};
constexpr const char kLabEntry[] = "keyboard-us";
constexpr const char kPropertyName[] = "raskladlabState";

// Synthetic test rule only. No dictionaries, model or other strings.
#ifndef RASKLAD_PROFILE_LAB
constexpr const char kRuleFrom[] = "ghbdtn";
constexpr const char kRuleTo[] = "привет";
#endif

void wipeString(std::string &s) {
    volatile char *p = s.empty() ? nullptr : &s[0];
    for (std::size_t i = 0; p && i < s.size(); ++i) {
        p[i] = '\0';
    }
    s.clear();
}

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
#ifdef RASKLAD_MANUAL_LAB
        gesture.reset();
        forgetLast();
#endif
    }
#ifdef RASKLAD_MANUAL_LAB
    rasklad::manual::DoubleShift gesture;
    bool converted = false; // the preedit shows usToRu(word()) instead of word()
    // Text to show/commit; empty conversion (never for a..z) keeps the original.
    std::string shown() const { return form(word(), converted); }
    static std::string form(const std::string &typed, bool asRu) {
        if (!asRu) return typed;
        std::string out = rasklad::manual::usToRu(typed);
        return out.empty() ? typed : out;
    }
    // The last word this addon committed with Space, kept only until any other
    // key, lifecycle event or failed check. Only it may be replaced after Space.
    // Pending: committed, not yet confirmed by a FRESH surrounding-text update from
    // the client. Confirmed: such an update showed exactly that word + space right
    // before a collapsed cursor at lastCursor; any later update that differs
    // (text or cursor, e.g. a mouse click) forgets it. Only Confirmed may replace.
    std::string lastTyped;
    bool lastConverted = false;
    bool lastValid = false;
    bool lastConfirmed = false;
    unsigned lastCursor = 0;
    // How a Pending word may confirm. AfterCommit: the update's cursor must be the
    // pre-commit cursor advanced by exactly the committed length (a stale pre-commit
    // repeat, or a click onto the same text elsewhere, never confirms). AfterReplace:
    // the old-form text cannot match the new form, and same-length forms keep the
    // cursor in place, so no cursor rule. Never: the pre-commit cursor was unknown.
    enum class Confirm { Never, AfterCommit, AfterReplace };
    Confirm lastRule = Confirm::Never;
    unsigned lastPreCursor = 0;
    std::string lastText() const { return form(lastTyped, lastConverted) + ' '; }
    void forgetLast() {
        volatile char *p = lastTyped.data();
        for (std::size_t i = 0; i < lastTyped.size(); ++i) p[i] = '\0';
        lastTyped.clear(); lastConverted = false; lastValid = false; lastConfirmed = false; lastCursor = 0;
        lastRule = Confirm::Never; lastPreCursor = 0;
    }
#endif
    const std::uint64_t id;
    std::uint64_t epoch = 1;
    // Survives lifecycle invalidation: an On transition must never capture
    // a suffix of raw input typed while Off, including in a later new context.
    std::uint64_t resumeGeneration = 0;
    bool needsBoundary = false;
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
        }
    }
    std::string word() const { return std::string(buf_.data(), len_); }

    void clearBuffer() {
        volatile char *p = buf_.data();
        for (std::size_t i = 0; i < buf_.size(); ++i) {
            p[i] = '\0';
        }
        len_ = 0;
        lease = {};
#ifdef RASKLAD_MANUAL_LAB
        converted = false;
        gesture.reset();
#endif
        // Normal local-buffer completion only. NEVER acknowledge a disabled
        // composition from buffer reset/destruction: that is not native delivery.
        if (disable->state() == policy::CompositionDisable::State::Active) {
            disable->nativeIdle(generation);
            generation = 0;
        }
    }

    void clearPending() { pendingCount_ = 0; }
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
                --pendingCount_;
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
        enableFixturePermissions();
#ifdef RASKLAD_PROFILE_LAB
        profile_ = std::make_unique<rasklad::fcitxlab::ProfileDecision>();
        if (!profile_->ready()) return; // Invalid/missing profile: no interception.
#endif
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
#ifdef RASKLAD_WITNESS_LAB
                    // Counted before the existing state lookup/discard.
                    witnessLifecycle(event.type());
#endif
                    auto *ic =
                        static_cast<fcitx::InputContextEvent &>(event).inputContext();
                    if (auto *s = state(ic, false)) {
                        discard(ic, *s); // Invalidates even an idle context first.
                    }
#ifdef RASKLAD_SLACK_SESSION_LAB
                    reportContext(event.type(), ic);
#endif
                }));
        }
#ifdef RASKLAD_MANUAL_LAB
        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextSurroundingTextUpdated, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &event) {
                auto *ic = static_cast<fcitx::InputContextEvent &>(event).inputContext();
                if (auto *s = state(ic, false)) observeSurrounding(ic, *s);
            }));
#endif
#ifdef RASKLAD_SLACK_SESSION_LAB
        // Field content changes are reported as one empty/non-empty bit only; they
        // neither discard state nor count as a lifecycle change.
        watchers_.emplace_back(instance_->watchEvent(
            fcitx::EventType::InputContextSurroundingTextUpdated, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &event) {
                reportField(static_cast<fcitx::InputContextEvent &>(event).inputContext());
            }));
#endif

#ifdef RASKLAD_SLACK_SESSION_LAB
        // Revoke must act at once, not on the next key: clear every shown
        // preedit as soon as the supervisor closes (or writes to) the pipe.
        (void)receiptSink();  // set CLOEXEC on the receipt end as soon as the addon loads
        if (sessionArm().fd() >= 0) {
            armWatch_ = instance_->eventLoop().addIOEvent(
                sessionArm().fd(),
                {fcitx::IOEventFlag::In, fcitx::IOEventFlag::Hup, fcitx::IOEventFlag::Err},
                [this](fcitx::EventSourceIO *source, int, fcitx::IOEventFlags) {
                    if (!sessionArmed()) {
                        source->setEnabled(false);
                        revokeAll();
                    }
                    return true;
                });
        }
        if (!armWatch_) sessionWatchFailed = true; // no immediate revoke path: fail closed
#endif
#ifdef RASKLAD_WITNESS_LAB
        // Only after profile.ready() and successful property registration.
        // Fails closed (no endpoint, counters disabled) outside the fixed
        // private lab environment or without the dbus addon.
        witness_.start(instance_);
#endif
    }

    // Members are destroyed in reverse order: watchers first (no more
    // callbacks), then the factory (unregisters and deletes all properties).
#ifdef RASKLAD_WITNESS_LAB
    // Witness endpoint is unregistered first, while the dbus addon we depend
    // on is still loaded; the remaining members then follow the order above.
    ~RaskladLab() override { witness_.stop(); }
#else
    ~RaskladLab() override = default;
#endif

#ifdef RASKLAD_APPLICATION_OFF_TEST
    explicit RaskladLab(fcitx::InputContextManager &manager)
        : instance_(nullptr), manager_(&manager) {
        enableFixturePermissions();
        registered_ = manager_->registerProperty(kPropertyName, &factory_);
    }
#endif
private:
#ifdef RASKLAD_APPLICATION_OFF_TEST
    friend struct ApplicationOffTest;
#endif
    void enableFixturePermissions() {
        // Compile-time owned fixture contract only, never registry-as-authority
        // for a regular application or an inferred alias.
        for (const auto &adapter : kFixtureAdapters)
            permissions_.enable(adapter.applicationId);
    }
    policy::Permission currentPermission(const policy::Context &context) const {
        return permissions_.snapshot(context.verifiedApplicationId);
    }
#ifdef RASKLAD_PREEDIT_REENTRY_TEST
    friend struct PreeditReentryTest;
#endif
#ifdef RASKLAD_DISABLE_REENTRY_TEST
    friend struct DisableReentryTest;
#endif
#ifdef RASKLAD_WITNESS_LAB
    static void witnessLifecycle(fcitx::EventType type) {
        switch (type) {
        case fcitx::EventType::InputContextFocusIn:
            lw::inc(lw::Counter::focusIn);
            break;
        case fcitx::EventType::InputContextFocusOut:
            lw::inc(lw::Counter::focusOut);
            break;
        case fcitx::EventType::InputContextReset:
            lw::inc(lw::Counter::reset);
            break;
        case fcitx::EventType::InputContextCapabilityAboutToChange:
        case fcitx::EventType::InputContextCapabilityChanged:
            lw::inc(lw::Counter::capabilityChange);
            break;
        default:
            break;
        }
    }
#endif

    LabState *state(fcitx::InputContext *ic, bool create = true) {
        if (!registered_ || !ic) return nullptr;
        bool matches = false;
        for (const auto &adapter : kFixtureAdapters)
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
#ifdef RASKLAD_PROFILE_LAB
        if (!profile_ || !profile_->ready()) return false;
#endif
        if (!ic || !ic->hasFocus()) {
            return false;
        }
#ifdef RASKLAD_SLACK_SESSION_LAB
        // Revoked or never armed: intercept nothing (buffer is discarded by the caller).
        if (!sessionArmed()) return false;
#endif
#ifdef RASKLAD_CHROMIUM_LAB
        // GTK/X11 supplies chromium; the observed native Wayland app-id is
        // chromium-browser. Both designate only this lab's controlled client.
        if (ic->program() != kLabProgram && ic->program() != "chromium-browser") {
            return false;
        }
#else
        if (ic->program() != kLabProgram) {
            return false;
        }
#endif
        #ifdef RASKLAD_APPLICATION_OFF_TEST
        // In-memory owned fixture seam; no Instance/daemon/engine is created.
        if (!instance_) return ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit) &&
            !ic->capabilityFlags().test(fcitx::CapabilityFlag::Password) &&
            !ic->capabilityFlags().test(fcitx::CapabilityFlag::Sensitive) &&
            !ic->capabilityFlags().test(fcitx::CapabilityFlag::NoSpellCheck) &&
            !ic->capabilityFlags().test(fcitx::CapabilityFlag::Terminal) &&
            !ic->capabilityFlags().test(fcitx::CapabilityFlag::Disable);
#endif
        const fcitx::InputMethodEntry *entry = instance_->inputMethodEntry(ic);
        if (!entry || entry->uniqueName() != kLabEntry) {
            return false;
        }
        const auto caps = ic->capabilityFlags();
        using fcitx::CapabilityFlag;
        if (!caps.test(CapabilityFlag::Preedit)) {
            return false;
        }
        if (caps.test(CapabilityFlag::Password) ||
            caps.test(CapabilityFlag::Sensitive) ||
            caps.test(CapabilityFlag::NoSpellCheck) ||
            caps.test(CapabilityFlag::Terminal) ||
            caps.test(CapabilityFlag::Disable)) {
            return false;
        }
        return true;
    }

    // Called only after the existing profile, program, focus and caps gates.
    // Prose belongs to the explicit owned fixture contract, never real apps.
    static policy::Context fixtureContext(fcitx::InputContext *ic, const LabState &s) {
        const char *label = kLabProgram;
#ifdef RASKLAD_CHROMIUM_LAB
        if (ic->program() == "chromium-browser") label = "chromium-browser";
#endif
        return {label, label, label, policy::Domain::OwnedFixture,
                policy::Identity::OwnedFixtureContract, policy::Field::Prose,
                ic->hasFocus(), s.id, s.epoch, s.id};
    }
    static policy::Permission fixturePermission(const policy::Context &context) {
        // Fixed lab permission only; no production preference or runtime mode UI.
        return {true, context.verifiedApplicationId, policy::Mode::Automatic, 1};
    }
    bool validate(fcitx::InputContext *ic, LabState &s) const {
        if (!eligible(ic)) return false;
        const auto context = fixtureContext(ic, s);
        return s.disable->correction(kFixtureRegistry,
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
            operation->takeOriginal(kFixtureRegistry, {}); // Defer, never retry later.
            return;
        }
        const auto ticket = operation->takeOriginal(kFixtureRegistry,
                                                    fixtureContext(ic, s));
        if (!ticket) return;
        struct Original {
            std::string text;
            ~Original() { wipeString(text); }
#ifdef RASKLAD_MANUAL_LAB
        } original{s.shown()};
#else
        } original{s.word()};
#endif
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
        if (policy::disableDisposition(kFixtureRegistry, fixtureContext(ic, s),
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
#ifdef RASKLAD_SLACK_SESSION_LAB
        if (!sessionArmed() || !rasklad::sessionarm::safeCommitText(original))
            return policy::CompositionDisable::Result::Unknown;
#endif
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
        for (const auto &adapter : kFixtureAdapters)
            if (ic->program() == adapter.id) target = true;
        if (!target) return;
        // Publish EVERY alias before any callback can capture in another context.
        familyOff_ = true;
        for (const auto &adapter : kFixtureAdapters)
            permissions_.revoke(adapter.applicationId);
        using Watch = fcitx::TrackableObjectReference<fcitx::InputContext>;
        std::vector<Watch> targets;
        manager_->foreach([&](fcitx::InputContext *candidate) {
            bool matches = false;
            for (const auto &adapter : kFixtureAdapters)
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
        if (!registered_ || !manager_ || !familyOff_ || !permissions_.ready())
            return false;
        std::vector<LabState *> targets;
        bool safe = true;
        manager_->foreach([&](fcitx::InputContext *candidate) {
            bool matches = false;
            for (const auto &adapter : kFixtureAdapters)
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
        for (const auto &adapter : kFixtureAdapters) {
            if (!permissions_.enable(adapter.applicationId)) {
                // Revision exhaustion permanently fails closed. Never expose
                // partially enabled aliases, even if publication fails midway.
                for (const auto &alias : kFixtureAdapters)
                    permissions_.revoke(alias.applicationId);
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

#ifdef RASKLAD_MANUAL_LAB
    // Replace the word this addon just committed with Space by its other form.
    // Only if nothing happened since (enforced by forgetLast on every other key and
    // lifecycle event), the context is focused, and the client's surrounding text
    // shows exactly that word + space right before a collapsed cursor. Unknown
    // surrounding text refuses (fail closed). Deletes exactly that many code points.
    static bool lastMatches(fcitx::InputContext *ic, const LabState &s) {
        const auto &st = ic->surroundingText();
        return st.isValid() && st.cursor() == st.anchor() &&
            rasklad::manual::endsBeforeCursor(st.text(), st.cursor(), s.lastText());
    }
    // Surrounding-text update from the client (fresh by definition).
    static void observeSurrounding(fcitx::InputContext *ic, LabState &s) {
        if (!s.lastValid) return;
        const bool match = lastMatches(ic, s);
        if (!s.lastConfirmed) {
            // Stale pre-commit updates may still arrive: they never confirm and never arm.
            // Fail closed when the pre-commit cursor was unknown.
            const unsigned cursor = ic->surroundingText().cursor();
            const long n = rasklad::manual::codePointCount(s.lastText());
            const bool fresh =
                (s.lastRule == LabState::Confirm::AfterReplace) ||
                (s.lastRule == LabState::Confirm::AfterCommit && n > 0 &&
                 static_cast<unsigned long>(cursor) == static_cast<unsigned long>(s.lastPreCursor) + static_cast<unsigned long>(n));
            if (match && fresh) { s.lastConfirmed = true; s.lastCursor = ic->surroundingText().cursor(); }
            return;
        }
        if (!match || ic->surroundingText().cursor() != s.lastCursor) s.forgetLast();
    }
    static void replaceLast(fcitx::InputContext *ic, LabState &s) {
        const std::string oldText = s.lastText();
        std::string newText = LabState::form(s.lastTyped, !s.lastConverted) + ' ';
        const auto &st = ic->surroundingText();
        const long n = rasklad::manual::codePointCount(oldText);
        const bool ok = ic->hasFocus() && s.lastConfirmed && n > 1 && lastMatches(ic, s) &&
            st.cursor() == s.lastCursor;
#ifdef RASKLAD_SLACK_SESSION_LAB
        // Supervised session: the same final gate as every commit, checked last.
        const bool gated = sessionArmed() && rasklad::sessionarm::safeCommitText(newText);
#else
        const bool gated = true;
#endif
        if (!ok || !gated) { s.forgetLast(); wipeString(newText); return; }
        // Staged before any client call: the new form is Pending until the client
        // confirms it with a fresh update (so a repeat toggle never acts on stale text).
        s.lastConverted = !s.lastConverted; s.lastConfirmed = false;
        s.lastRule = LabState::Confirm::AfterReplace;
        ic->deleteSurroundingText(-static_cast<int>(n), static_cast<unsigned>(n));
        ic->commitString(newText);
#ifdef RASKLAD_SLACK_SESSION_LAB
        // Receipt: kind and new length only. Undeliverable -> fail closed.
        if (!receiptSink().sendReplace(static_cast<std::size_t>(rasklad::manual::codePointCount(newText)))) {
            sessionArm().latch();
            receiptSink().fail();
        }
#endif
        wipeString(newText);
    }
    // Bare Shift_L/Shift_R: no Ctrl/Alt/Super; Shift's own state bit (set on the
    // release, depending on the frontend on the press) is ignored.
    static bool isShift(const fcitx::Key &key) {
        if (key.sym() != FcitxKey_Shift_L && key.sym() != FcitxKey_Shift_R) return false;
        return !fcitx::Key(key.sym(), key.states().unset(fcitx::KeyState::Shift)).hasModifier();
    }
#endif
    static void showPreedit(fcitx::InputContext *ic, LabState &s) {
#ifdef RASKLAD_MANUAL_LAB
        std::string w = s.shown();
#else
        std::string w = s.word();
#endif
        fcitx::Text text;
        const int cursor = static_cast<int>(w.size()); // Fcitx Text cursor is a byte offset
        text.append(std::move(w), fcitx::TextFormatFlag::Underline);
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
#ifdef RASKLAD_WITNESS_LAB
        lw::inc(lw::Counter::commitStringCalls); // any key, immediately before
#endif
#ifdef RASKLAD_SLACK_SESSION_LAB
        // Final gate: state is already staged; on refusal nothing is committed,
        // but the client still gets the empty preedit (no stale composition).
        if (!sessionArmed() || !rasklad::sessionarm::safeCommitText(text)) {
            wipeString(text);
#ifdef RASKLAD_MANUAL_LAB
            s.forgetLast(); // nothing committed: nothing of ours to replace later
#endif
            if (hadPreedit && binding->alive && binding->epoch == epoch) {
                if (auto *current = ref.get(); current && current->hasFocus()) current->updatePreedit();
            }
            return false;
        }
#endif
        ic->commitString(text);
#ifdef RASKLAD_SLACK_SESSION_LAB
        {
            // Receipt: kind and length only. Undeliverable → fail closed.
            const std::string corrected = std::string(kRuleTo) + ' ';
            const bool isCorrection = text == kRuleTo || text == corrected;
            if (!receiptSink().send(isCorrection, rasklad::sessionarm::codePoints(text))) {
                sessionArm().latch();
                receiptSink().fail();
            }
        }
#endif
        if (hadPreedit && binding->alive && binding->epoch == epoch) {
            if (auto *current = ref.get(); current && current->hasFocus()) {
                current->updatePreedit();
            }
        }
        wipeString(text);
        return true;
    }

#ifdef RASKLAD_SLACK_SESSION_LAB
    static char purposeCode(fcitx::InputContext *ic) {
        // Fail closed: 'n'/'a' only for a preedit-capable plain text field with
        // none of the flags that eligible() or the session policy forbid.
        const auto flags = ic->capabilityFlags();
        if (flags.testAny(fcitx::CapabilityFlag::PasswordOrSensitive)) return 'p';
        if (!flags.test(fcitx::CapabilityFlag::Preedit)
            || flags.testAny(fcitx::CapabilityFlags{fcitx::CapabilityFlag::Email, fcitx::CapabilityFlag::Digit,
                                                    fcitx::CapabilityFlag::Url, fcitx::CapabilityFlag::Dialable,
                                                    fcitx::CapabilityFlag::Number, fcitx::CapabilityFlag::Terminal,
                                                    fcitx::CapabilityFlag::Name, fcitx::CapabilityFlag::NoSpellCheck,
                                                    fcitx::CapabilityFlag::Disable})) return 'x';
        if (flags.test(fcitx::CapabilityFlag::Alpha)) return 'a';
        return 'n';
    }

    // Lifecycle of the lab client's contexts → supervisor (event, purpose, id).
    // Undeliverable → the session gate latches off.
    void reportContext(fcitx::EventType type, fcitx::InputContext *ic) {
        char event = 0;
        switch (type) {
        case fcitx::EventType::InputContextFocusIn: event = 'i'; break;
        case fcitx::EventType::InputContextFocusOut: event = 'u'; break;
        case fcitx::EventType::InputContextReset: event = 'r'; break;
        case fcitx::EventType::InputContextCapabilityAboutToChange:
        case fcitx::EventType::InputContextCapabilityChanged: event = 'k'; break;
        case fcitx::EventType::InputContextSwitchInputMethod: event = 's'; break;
        default: return;
        }
        auto *s = state(ic, true);
        if (!s) return;  // not the lab client
        if (!receiptSink().sendContext(event, purposeCode(ic), s->id)) {
            sessionArm().latch();
            receiptSink().fail();
            return;
        }
        reportField(ic);
    }

    // Empty / has text / unknown, from the surrounding text the client already sends
    // to Fcitx. The text itself stays in Fcitx; only this bit leaves the addon.
    static char fieldCode(fcitx::InputContext *ic) {
        if (!ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) return 'u';
        const auto &surrounding = ic->surroundingText();
        if (!surrounding.isValid()) return 'u';
        return rasklad::sessionarm::blankText(surrounding.text()) ? 'e' : 't';
    }

    void reportField(fcitx::InputContext *ic) {
        auto *s = state(ic, true);
        if (!s) return;  // not the lab client
        if (!receiptSink().sendField(fieldCode(ic), s->id)) {
            sessionArm().latch();
            receiptSink().fail();
        }
    }

    // Two passes: discard() notifies clients, so never call it inside foreach.
    // (discard's stopping() early return is unreachable here: the Off paths
    // exist only in the test labs excluded by #error above.)
    void revokeAll() {
        using Watch = fcitx::TrackableObjectReference<fcitx::InputContext>;
        std::vector<Watch> targets;
        manager_->foreach([&](fcitx::InputContext *ic) {
            if (state(ic, false)) targets.push_back(ic->watch());
            return true;
        });
        for (auto &target : targets) {
            if (auto *ic = target.get()) {
                if (auto *s = state(ic, false)) discard(ic, *s);
            }
        }
    }
#endif

    void handleKey(fcitx::KeyEvent &event) {
#ifdef RASKLAD_SLACK_SESSION_LAB
        // After revoke nothing may type through this private Fcitx: no engine
        // pass-through, compose or forward. Presses are swallowed; releases
        // pass so a key already down before revoke cannot stay stuck/repeat.
        if (!sessionArmed()) {
            auto *revoked = event.inputContext();
            if (auto *s = state(revoked, false)) discard(revoked, *s);
            if (!event.isRelease()) event.filterAndAccept();
            return;
        }
#endif
#ifdef RASKLAD_WITNESS_LAB
        // Schema 2 totals: no code, sym, text or timing read or stored here.
        lw::inc(event.isRelease() ? lw::Counter::allKeyReleaseSeen
                                  : lw::Counter::allKeyPressSeen);
        // Observation only, BEFORE the filtered check and all gates: raw code
        // 65 (evdev KEY_SPACE 57 + 8). Code and sym are compared, never stored
        // or exported. A prior-filtered press returns below, so no further
        // per-press counter is incremented for it.
        const bool wSpaceCode = event.rawKey().code() == lw::kSpaceKeyCode;
        const bool wSpacePress = wSpaceCode && !event.isRelease();
        if (wSpaceCode) {
            if (event.isRelease()) {
                lw::inc(lw::Counter::spaceCodeReleaseSeen);
            } else {
                lw::inc(lw::Counter::spaceCodePressSeen);
                if (event.key().sym() == FcitxKey_space) {
                    lw::inc(lw::Counter::normalizedSpacePress);
                }
                if (event.filtered()) {
                    lw::inc(lw::Counter::priorFilteredPress);
                }
            }
        }
#endif
        if (!registered_ || event.filtered()) {
            return;
        }
        fcitx::InputContext *ic = event.inputContext();
        const fcitx::Key &key = event.key();

        if (event.isRelease()) {
#ifdef RASKLAD_MANUAL_LAB
            // A bare Shift release may complete the gesture. It is never filtered:
            // the application sees a symmetric modifier press/release.
            if (isShift(key)) {
                auto *s = state(ic, false);
                if (!s) return;
                if (familyOff_ || !eligible(ic) || s->stopping()) {
                    s->gesture.reset(); s->forgetLast();
                    return;
                }
                if (s->empty() && s->lastValid) {
                    if (s->gesture.feed(rasklad::manual::Input::ShiftRelease, event.time()))
                        replaceLast(ic, *s); // No state/event access after this call.
                    return;
                }
                if (s->empty() || !validate(ic, *s)) {
                    s->gesture.reset(); // an abandoned release ends the gesture
                    return;
                }
                if (s->gesture.feed(rasklad::manual::Input::ShiftRelease, event.time())) {
                    s->converted = !s->converted;
                    showPreedit(ic, *s); // No state/event access after this callback.
                }
                return;
            }
#endif
            // Never mutates the buffer; only swallows the release of a press
            // we consumed in this same context.
            if (auto *s = state(ic, false); s && s->takePending(key)) {
#ifdef RASKLAD_WITNESS_LAB
                if (wSpaceCode) {
                    lw::inc(lw::Counter::releaseFiltered);
                }
#endif
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
#ifdef RASKLAD_DISABLE_TEST_LAB
            const char *testFlag = std::getenv("RASKLAD_DISABLE_TEST_LAB");
            const bool testControl = testFlag && std::strcmp(testFlag, "1") == 0 &&
                !key.hasModifier() && (key.sym() == FcitxKey_F12 || key.sym() == FcitxKey_F10);
            if (!testControl) return;
#else
            return;
#endif
        }

#ifdef RASKLAD_WITNESS_LAB
        // Read-only focus observation; not a gate, creates no state.
        if (wSpacePress && ic && ic->hasFocus()) {
            lw::inc(lw::Counter::focusedPress);
        }
#endif
        if (!eligible(ic)) {
#ifdef RASKLAD_WITNESS_LAB
            if (wSpacePress) {
                lw::inc(lw::Counter::passIneligible);
            }
#endif
            // Gates no longer hold: discard without commit, pass the key.
            if (auto *s = state(ic, false); s && s->active()) {
                discard(ic, *s);
            }
            return;
        }
#ifdef RASKLAD_WITNESS_LAB
        // Existing single eligible() evaluation above; not re-evaluated.
        if (wSpacePress) {
            lw::inc(lw::Counter::eligiblePress);
        }
#endif

        LabState *s = state(ic);
        if (!s) {
#ifdef RASKLAD_WITNESS_LAB
            if (wSpacePress) {
                lw::inc(lw::Counter::noStatePress);
            }
#endif
            return;
        }
#ifdef RASKLAD_DISABLE_TEST_LAB
        // Separate owned test artifact only. These are synthetic test controls,
        // not user shortcuts. No default artifact contains this branch.
        if (std::getenv("RASKLAD_DISABLE_TEST_LAB") &&
            std::strcmp(std::getenv("RASKLAD_DISABLE_TEST_LAB"), "1") == 0 &&
            !key.hasModifier() && (key.sym() == FcitxKey_F12 || key.sym() == FcitxKey_F10)) {
            s->addPending(key);
            event.filterAndAccept();
            if (key.sym() == FcitxKey_F12) requestLabOff(ic);
            else requestLabOn();
            return;
        }
#endif
        if (s->stopping()) return; // Off: no new capture, correction or buffer clear.
        if (!s->empty() && !validate(ic, *s)) {
            discard(ic, *s);
            return; // In particular, a denied Space remains unconsumed.
        }
        // A new press of a tracked key means its old release was lost.
        s->takePending(key);

        const fcitx::KeySym sym = key.sym();
        const bool plain = !key.hasModifier();
#ifdef RASKLAD_MANUAL_LAB
        // A bare Shift press over a non-empty word defers the decision (no flush,
        // no bypass) and passes unfiltered. Every other press breaks the gesture.
        if (isShift(key)) {
            if ((!s->empty() || s->lastValid) && !s->bypass() && !s->needsBoundary) {
                s->gesture.feed(rasklad::manual::Input::ShiftPress, event.time());
                return;
            }
            s->gesture.reset();
            // A bare Shift over an empty buffer must not latch bypass: the next
            // lowercase word is still a whole word. Shift+letter is non-plain and
            // takes the flush/bypass path itself.
            if (s->empty() && !s->bypass() && !s->needsBoundary) return;
        } else {
            s->gesture.feed(rasklad::manual::Input::Other, event.time());
            s->forgetLast(); // any other key (letters, arrows, BackSpace, ...) ends the chance
        }
#endif
        const bool boundary = plain && (sym == FcitxKey_space ||
            sym == FcitxKey_Tab || sym == FcitxKey_Return || sym == FcitxKey_KP_Enter);
        if (s->resumeGeneration != resumeGeneration_) {
            s->resumeGeneration = resumeGeneration_;
            s->needsBoundary = true;
        }
        if (s->needsBoundary) {
            if (boundary) s->needsBoundary = false;
            return; // The delimiter itself and all preceding presses stay raw.
        }
        // Never begin correcting a suffix of a token already passed through.
        // Mixed tokens, punctuation and overflow bypass until a safe delimiter.
        if (s->bypass()) {
#ifdef RASKLAD_WITNESS_LAB
            // This branch always returns unfiltered: both points coincide.
            if (wSpacePress) {
                lw::inc(lw::Counter::bypassPress);
                lw::inc(lw::Counter::passBypass);
            }
#endif
            if (boundary) s->setBypass(false);
            return;
        }
#ifdef RASKLAD_WITNESS_LAB
        if (wSpacePress && !s->empty()) {
            lw::inc(lw::Counter::bufferNonemptyPress);
        }
#endif

#ifdef RASKLAD_MANUAL_LAB
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
        if (plain && manualChar && !s->full()) {
#else
        if (plain && sym >= FcitxKey_a && sym <= FcitxKey_z && !s->full()) {
#endif
            if (s->empty()) {
                const auto context = fixtureContext(ic, *s);
                const auto generation = s->disable->bind(kFixtureRegistry,
                    currentPermission(context), context);
                if (!generation) return;
                s->generation = generation;
                s->lease = policy::authorizeCapture(kFixtureRegistry,
                    currentPermission(context), context).lease;
            }
#ifdef RASKLAD_MANUAL_LAB
            acceptLetter(ic, *s, event, manualChar);
#else
            acceptLetter(ic, *s, event, static_cast<char>('a' + (sym - FcitxKey_a)));
#endif
            return;
        }

        if (plain && !s->empty()) {
            if (sym == FcitxKey_space) {
#ifdef RASKLAD_WITNESS_LAB
                if (wSpacePress) {
                    lw::inc(lw::Counter::commitSpaceBranch);
                }
#endif
                std::string w = s->word();
#ifdef RASKLAD_PROFILE_LAB
                const auto context = fixtureContext(ic, *s);
                auto corrected = profile_->correct(w, context.field);
                std::string out = corrected ? std::move(*corrected) : w;
#elif defined(RASKLAD_MANUAL_LAB)
                std::string out = s->shown(); // only what the owner chose to see
                s->forgetLast();
                s->lastTyped = w; s->lastConverted = s->converted; s->lastValid = true; // Pending
                {
                    const auto &pre = ic->surroundingText();
                    const bool known = pre.isValid() && pre.cursor() == pre.anchor();
                    s->lastRule = known ? LabState::Confirm::AfterCommit : LabState::Confirm::Never;
                    s->lastPreCursor = known ? pre.cursor() : 0;
                }
#else
                std::string out = (w == kRuleFrom) ? std::string(kRuleTo) : w;
#endif
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
                commitAndClear(ic, *s, std::move(out));
                return;
            }
            if (sym == FcitxKey_BackSpace) {
                acceptBackspace(ic, *s, event);
                return;
            }
        }

        // Every other press (incl. full buffer): flush ORIGINAL letters to this
        // same focused context, then let the key pass unfiltered.
        if (!boundary) s->setBypass(true);
#ifdef RASKLAD_WITNESS_LAB
        if (wSpacePress) {
            lw::inc(s->empty() ? lw::Counter::passEmpty
                               : lw::Counter::flushOriginalSpaceCode);
        }
#endif
        if (!s->empty()) {
            if (!validate(ic, *s)) {
                discard(ic, *s);
                return;
            }
#ifdef RASKLAD_MANUAL_LAB
            commitAndClear(ic, *s, s->shown()); // what the owner sees, never a hidden form
#else
            commitAndClear(ic, *s, s->word());
#endif
        }
    }

    fcitx::Instance *instance_;
    fcitx::InputContextManager *manager_;
    policy::ApplicationPermissions permissions_{kFixtureRegistry};
    bool familyOff_ = false;
    std::uint64_t resumeGeneration_ = 0;
#ifdef RASKLAD_PROFILE_LAB
    std::unique_ptr<rasklad::fcitxlab::ProfileDecision> profile_;
#endif
    fcitx::LambdaInputContextPropertyFactory<LabProperty> factory_{
        [](fcitx::InputContext &) { return new LabProperty; }};
    bool registered_ = false;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        watchers_;
#ifdef RASKLAD_SLACK_SESSION_LAB
    std::unique_ptr<fcitx::EventSourceIO> armWatch_; // destroyed before watchers_/factory_
#endif
#ifdef RASKLAD_WITNESS_LAB
    rasklad::fcitxlab::witness::Owner witness_;
#endif
};

class RaskladLabFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new RaskladLab(manager->instance());
    }
};

} // namespace

FCITX_ADDON_FACTORY(RaskladLabFactory);
