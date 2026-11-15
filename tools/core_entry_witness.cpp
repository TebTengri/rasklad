// tools/core_entry_witness.cpp
// SPDX-License-Identifier: MIT
// Lab-only Fcitx5 core-entry witness shim. Linux x86_64 ONLY.
// Purpose: diagnostic preemption of InputContext::keyEvent to count
// Space press/release delivery and focus outcomes for the documented
// "missing Space" trace. Captures AGGREGATE counters only: no key
// text, no raw code values, no syms, no pointers, no timestamps.
// The runner confines this library to its private lab child. Environment
// markers are admission checks, not proof of namespace isolation.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if !defined(__linux__) || !defined(__x86_64__)
#error "core-entry witness supports only the pinned Linux x86_64 ABI"
#endif
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcitx/inputcontext.h>
#include <fcitx/event.h>

static_assert(sizeof(void*) == sizeof(bool(*)(fcitx::InputContext*, fcitx::KeyEvent&)),
              "function/object pointer size mismatch");

namespace {

using KeyEventFn = bool (*)(fcitx::InputContext*, fcitx::KeyEvent&);

// Constant-initialized (no dynamic init), file-local (hidden).
KeyEventFn g_orig = nullptr;
bool       g_ready = false;
pthread_t  g_owner_thread{};
// 10 saturating uint8 counters, zero-initialized.
uint8_t    g_counters[10] = {};

constexpr const char* kSym = "_ZN5fcitx12InputContext8keyEventERNS_8KeyEventE";

inline void sat(uint8_t& c) noexcept { if (c != 255u) ++c; }
bool owner_thread() noexcept { return pthread_equal(pthread_self(),g_owner_thread)!=0; }

inline bool eq(const char* v, const char* want) noexcept {
    return v != nullptr && std::strcmp(v, want) == 0;
}

bool env_ok() noexcept {
    // Required lab identity.
    if (!eq(std::getenv("HOME"), "/home/lab")) return false;
    if (!eq(std::getenv("XDG_RUNTIME_DIR"), "/run/lab")) return false;
    if (!eq(std::getenv("RASKLAD_NATIVE_WAYLAND_LAB"), "1")) return false;
    if (!eq(std::getenv("RASKLAD_ADDON_WITNESS_LAB"), "1")) return false;
    if (!eq(std::getenv("RASKLAD_CORE_ENTRY_WITNESS_LAB"), "1")) return false;
    // Must NOT be attached to any real session.
    if (std::getenv("DISPLAY") != nullptr) return false;
    if (std::getenv("WAYLAND_SOCKET") != nullptr) return false;
    if (std::getenv("RASKLAD_PARENT_FD") != nullptr) return false;
    return true;
}

} // namespace

// extern "C" wrapper aliased to the exact mangled member symbol via asm
// label, with default visibility so our linker map can export it.
extern "C" __attribute__((visibility("default")))
bool rasklad_core_entry_keyEvent(fcitx::InputContext* ic, fcitx::KeyEvent& event)
    asm("_ZN5fcitx12InputContext8keyEventERNS_8KeyEventE");

// Resolve original ONCE, before any input, in a load-time constructor.
// No input work happens here.
__attribute__((constructor))
static void core_entry_init() noexcept {
    if (!env_ok()) _exit(125);
    void* sym = dlsym(RTLD_NEXT, kSym);
    if (sym == nullptr) _exit(125);
    // Self-detection: refuse to recurse into our own interposer.
    KeyEventFn self = &rasklad_core_entry_keyEvent;
    void* selfp = nullptr;
    std::memcpy(&selfp, &self, sizeof(selfp));
    if (sym == selfp) _exit(125);
    // Local ABI-safe cast of the resolved address to the call signature.
    std::memcpy(&g_orig, &sym, sizeof(g_orig));
    g_owner_thread = pthread_self();
    // Prevent descendant inheritance; only the already-resident private
    // Fcitx load keeps the shim mapped.
    if (unsetenv("LD_PRELOAD") != 0) _exit(125);
    g_ready = true;
}

// Wrapper body: NO getenv/dlsym/allocation/IO/timers/logs/key formatting.
extern "C"
bool rasklad_core_entry_keyEvent(fcitx::InputContext* ic, fcitx::KeyEvent& event) {
    // Plain counters are confined to the initializing Fcitx event-loop thread.
    // Unexpected concurrent entry aborts only this admitted lab process.
    if (!g_ready || !owner_thread()) _exit(125);
    // Read entry facts BEFORE calling original.
    const bool isRel   = event.isRelease();
    const bool isSpace = (event.rawKey().code() == 65u);
    const bool focused = (ic != nullptr && ic->hasFocus());

    // Entry counters must advance BEFORE dispatching to the original.
    if (isRel) sat(g_counters[1]); else sat(g_counters[0]);
    if (isSpace) {
        if (isRel) sat(g_counters[3]);
        else {
            sat(g_counters[2]);
            if (focused) sat(g_counters[4]); else sat(g_counters[5]);
        }
    }
    // Call original EXACTLY once; return its bool unchanged.
    const bool r = g_orig(ic, event);
    // No ic/event access after the callback: only saved facts and result.
    if (isSpace) {
        if (isRel) { if (r) sat(g_counters[8]); else sat(g_counters[9]); }
        else { if (r) sat(g_counters[6]); else sat(g_counters[7]); }
    }
    return r;
}

// Read-only snapshot. No reset, no setter, no input API.
// Layout: out[0]=1 (schema), out[1]=ready, out[2..11]=10 counters.
extern "C" __attribute__((visibility("default")))
uint32_t rasklad_core_entry_snapshot_v1(uint32_t* out, uint32_t capacity) noexcept {
    if (!g_ready || !owner_thread() || out == nullptr || capacity < 12u) return 0u;
    uint32_t buf[12];
    buf[0] = 1u;
    buf[1] = g_ready ? 1u : 0u;
    for (int i = 0; i < 10; ++i) buf[2 + i] = static_cast<uint32_t>(g_counters[i]);
    std::memcpy(out, buf, sizeof(buf));
    return 12u;
}