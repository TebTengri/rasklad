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
//      - ic->program() == "rasklad-ime-lab" exactly (a fixed test harness name;
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
//  * No logging, stdout/stderr, env, clipboard, deleteSurroundingText,
//    forwardKey, network, IPC of our own, child processes or evaluation.
//    Only PROFILE_LAB initialization reads fixed verified /lab/profile files;
//    key handling performs no file I/O.
//  * Every commit goes to the SAME context whose key event is being handled,
//    synchronously, while it is focused and gated. Focus-out, focus-in, reset,
//    input method switch (incl. deactivate) and capability change discard the
//    buffer WITHOUT committing. Context destruction needs no handler: Fcitx
//    deletes the property together with the context, and we never call into a
//    destroying context.
//
// Key policy for a gated key PRESS:
//  * a..z without modifiers (CapsLock/NumLock lock bits are tolerated because
//    Key::hasModifier() ignores them; uppercase syms are not accepted):
//      append if buffer < 32, update preedit, consume.
//      If the buffer is full: flush original (see below), then pass the key.
//  * Space (no modifiers) with non-empty buffer: clear preedit, commit
//    corrected-or-original word plus one space as ONE commitString, consume.
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

#ifdef RASKLAD_PROFILE_LAB
#include "fcitxlabprofile.h"
#endif

#include <fcitx-utils/capabilityflags.h>
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
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxWord = 32;
constexpr std::size_t kMaxPendingReleases = 8;
// Separate compile-time lab variant; never an environment-controlled app list.
#ifdef RASKLAD_CHROMIUM_LAB
constexpr const char kLabProgram[] = "chromium";
#else
constexpr const char kLabProgram[] = "rasklad-ime-lab";
#endif
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
    ~LabState() override { clearBuffer(); }

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
    }

    void clearPending() { pendingCount_ = 0; }

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

class RaskladLab final : public fcitx::AddonInstance {
public:
    explicit RaskladLab(fcitx::Instance *instance) : instance_(instance) {
#ifdef RASKLAD_PROFILE_LAB
        if (!profile_.ready()) return; // Invalid/missing profile: no interception.
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
                    auto *ic =
                        static_cast<fcitx::InputContextEvent &>(event).inputContext();
                    if (auto *s = state(ic); s && s->active()) {
                        discard(ic, *s);
                    }
                }));
        }
    }

    // Members are destroyed in reverse order: watchers first (no more
    // callbacks), then the factory (unregisters and deletes all properties).
    ~RaskladLab() override = default;

private:
    LabState *state(fcitx::InputContext *ic) {
        if (!registered_ || !ic) {
            return nullptr;
        }
        return static_cast<LabState *>(ic->propertyFor(&factory_));
    }

    // Metadata gates. Must pass before anything is buffered.
    bool eligible(fcitx::InputContext *ic) const {
#ifdef RASKLAD_PROFILE_LAB
        if (!profile_.ready()) return false;
#endif
        if (!ic || !ic->hasFocus()) {
            return false;
        }
        if (ic->program() != kLabProgram) {
            return false;
        }
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

    static void showPreedit(fcitx::InputContext *ic, LabState &s) {
        std::string w = s.word();
        fcitx::Text text;
        const int cursor = static_cast<int>(w.size()); // ASCII: bytes == chars
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

    // Drop everything for this context, never commit.
    static void discard(fcitx::InputContext *ic, LabState &s) {
        s.clearBuffer();
        s.clearPending();
        s.setBypass(false);
        clearPreedit(ic, s);
    }

    // Single commit to the same, gated, focused context; then clear.
    static void commitAndClear(fcitx::InputContext *ic, LabState &s,
                               std::string text) {
        clearPreedit(ic, s);
        s.clearBuffer();
        ic->commitString(text);
        wipeString(text);
    }

    void handleKey(fcitx::KeyEvent &event) {
        if (!registered_ || event.filtered()) {
            return;
        }
        fcitx::InputContext *ic = event.inputContext();
        const fcitx::Key &key = event.key();

        if (event.isRelease()) {
            // Never mutates the buffer; only swallows the release of a press
            // we consumed in this same context.
            if (auto *s = state(ic); s && s->takePending(key)) {
                event.filterAndAccept();
            }
            return;
        }

        if (!eligible(ic)) {
            // Gates no longer hold: discard without commit, pass the key.
            if (auto *s = state(ic); s && s->active()) {
                discard(ic, *s);
            }
            return;
        }

        LabState *s = state(ic);
        if (!s) {
            return;
        }
        // A new press of a tracked key means its old release was lost.
        s->takePending(key);

        const fcitx::KeySym sym = key.sym();
        const bool plain = !key.hasModifier();
        const bool boundary = plain && (sym == FcitxKey_space ||
            sym == FcitxKey_Tab || sym == FcitxKey_Return || sym == FcitxKey_KP_Enter);
        // Never begin correcting a suffix of a token already passed through.
        // Mixed tokens, punctuation and overflow bypass until a safe delimiter.
        if (s->bypass()) {
            if (boundary) s->setBypass(false);
            return;
        }

        if (plain && sym >= FcitxKey_a && sym <= FcitxKey_z && !s->full()) {
            s->push(static_cast<char>('a' + (sym - FcitxKey_a)));
            showPreedit(ic, *s);
            s->addPending(key);
            event.filterAndAccept();
            return;
        }

        if (plain && !s->empty()) {
            if (sym == FcitxKey_space) {
                std::string w = s->word();
#ifdef RASKLAD_PROFILE_LAB
                auto corrected = profile_.correct(w);
                std::string out = corrected ? std::move(*corrected) : w;
#else
                std::string out = (w == kRuleFrom) ? std::string(kRuleTo) : w;
#endif
                wipeString(w);
                out.push_back(' ');
                commitAndClear(ic, *s, std::move(out));
                s->addPending(key);
                event.filterAndAccept();
                return;
            }
            if (sym == FcitxKey_BackSpace) {
                s->pop();
                if (s->empty()) {
                    clearPreedit(ic, *s);
                } else {
                    showPreedit(ic, *s);
                }
                s->addPending(key);
                event.filterAndAccept();
                return;
            }
        }

        // Every other press (incl. full buffer): flush ORIGINAL letters to this
        // same focused context, then let the key pass unfiltered.
        if (!s->empty()) {
            commitAndClear(ic, *s, s->word());
        }
        if (!boundary) s->setBypass(true);
    }

    fcitx::Instance *instance_;
#ifdef RASKLAD_PROFILE_LAB
    rasklad::fcitxlab::ProfileDecision profile_;
#endif
    fcitx::LambdaInputContextPropertyFactory<LabState> factory_{
        [](fcitx::InputContext &) { return new LabState; }};
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
