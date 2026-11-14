// SPDX-License-Identifier: MIT
//
// SYS-242 rasklad Fcitx5 LAB Space-delivery witness (developer prototype only).
//
// Compiled ONLY into the RASKLAD_WITNESS_LAB variant (tools/fcitx_witness_lab.pro).
// Default and plain PROFILE_LAB builds never include this header, so they have
// no endpoint, no counters, no D-Bus module dependency and no environment reads.
//
// What it is: 20 process-wide saturating (max 255) observation counters plus a
// single read-only D-Bus method GetStats() -> au (22 x uint32):
//   [0] schema = 1, [1] enabled = 1, [2..21] counters in Counter order.
// What it is NOT: no setter, no reset method, no properties, no signals, no
// text, key codes, keysyms, timestamps, pointers, files, logging or payload.
// Serialization happens only when GetStats is called; the key path performs
// only bounded in-place uint8 increments (no heap, no I/O).
//
// Fail closed: the endpoint is registered only when the fixed private lab
// environment markers match exactly; otherwise counters stay disabled (inc is
// a no-op) and nothing is exported.

#pragma once

#ifndef RASKLAD_WITNESS_LAB
#error "fcitxlabwitness.h is only for the RASKLAD_WITNESS_LAB developer variant"
#endif

#include <fcitx-module/dbus/dbus_public.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace rasklad::fcitxlab::witness {

// Exact index order is part of schema 1. Do not reorder.
enum class Counter : std::size_t {
    spaceCodePressSeen = 0,  // press with raw code 65, before filtered/gates
    spaceCodeReleaseSeen,    // release with raw code 65, before filtered/gates
    normalizedSpacePress,    // code-65 press whose (normalized) sym is space
    priorFilteredPress,      // code-65 press already filtered on arrival;
                             // no further per-press counter follows for it
    focusedPress,            // code-65 press, ic present and hasFocus()
    eligiblePress,           // code-65 press that passed eligible()
    bufferNonemptyPress,     // code-65 press, after bypass check, buffer != 0
    bypassPress,             // code-65 press seen with bypass latched
    passIneligible,          // code-65 press passed: gates failed
    passBypass,              // code-65 press passed: bypass branch return
    passEmpty,               // code-65 press passed at tail with empty buffer
    commitSpaceBranch,       // code-65 press entered the plain Space commit
    flushOriginalSpaceCode,  // code-65 press flushed original at tail
    commitStringCalls,       // every ic->commitString() by this addon (any key)
    focusIn,                 // InputContextFocusIn (any context)
    focusOut,                // InputContextFocusOut (any context)
    reset,                   // InputContextReset (any context)
    capabilityChange,        // CapabilityAboutToChange + CapabilityChanged
    noStatePress,            // code-65 eligible press without LabState
    releaseFiltered,         // code-65 release swallowed via pending list
    count
};

constexpr std::size_t kCounterCount = static_cast<std::size_t>(Counter::count);
static_assert(kCounterCount == 20, "schema 1 has exactly 20 counters");
constexpr std::size_t kReplyWords = 2 + kCounterCount;
static_assert(kReplyWords == 22, "schema 1 reply has exactly 22 words");
constexpr std::uint32_t kSchema = 1;
constexpr std::uint8_t kSaturate = 255;
// evdev KEY_SPACE (57) + 8 (XKB offset) as delivered in fcitx::Key::code().
constexpr int kSpaceKeyCode = 65;

constexpr char kObjectPath[] = "/org/rasklad/LabWitness";
constexpr char kInterface[] = "org.rasklad.LabWitness1";

namespace detail {
// Namespace-owned static storage. Fcitx dispatches key events and D-Bus calls
// on its single event loop thread, so plain storage is sufficient.
inline std::array<std::uint8_t, kCounterCount> counters{};
inline bool enabled = false; // true only while the endpoint is registered
inline bool owned = false;   // at most one registered endpoint per process

inline bool envEquals(const char *name, const char *want) {
    const char *v = std::getenv(name);
    return v && std::strcmp(v, want) == 0;
}

inline bool envAbsent(const char *name) { return std::getenv(name) == nullptr; }

// If an explicit session bus address is present it must point into the
// private runtime dir; unset means the library default under XDG_RUNTIME_DIR.
inline bool busAddressPrivate() {
    const char *v = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!v) {
        return true;
    }
    constexpr char kPrefix[] = "unix:path=/run/lab/";
    return std::strncmp(v, kPrefix, sizeof(kPrefix) - 1) == 0 &&
           std::strchr(v, ';') == nullptr;
}

inline bool privateLabEnvironment() {
    return envEquals("HOME", "/home/lab") &&
           envEquals("XDG_RUNTIME_DIR", "/run/lab") &&
           envEquals("RASKLAD_NATIVE_WAYLAND_LAB", "1") &&
           envEquals("RASKLAD_ADDON_WITNESS_LAB", "1") &&
           envAbsent("DISPLAY") && envAbsent("WAYLAND_SOCKET") &&
           envAbsent("RASKLAD_PARENT_FD") && busAddressPrivate();
}
} // namespace detail

// Bounded saturating increment. No heap, no I/O, no-op unless enabled.
inline void inc(Counter c) noexcept {
    if (!detail::enabled) {
        return;
    }
    const auto i = static_cast<std::size_t>(c);
    if (i >= kCounterCount) {
        return;
    }
    if (detail::counters[i] < kSaturate) {
        ++detail::counters[i];
    }
}

// Read-only endpoint: exactly one method, no properties/signals.
class Endpoint final : public fcitx::dbus::ObjectVTable<Endpoint> {
public:
    std::vector<std::uint32_t> getStats() {
        std::array<std::uint32_t, kReplyWords> snapshot{};
        snapshot[0] = kSchema;
        snapshot[1] = detail::enabled ? 1U : 0U;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            snapshot[2 + i] = detail::counters[i];
        }
        return std::vector<std::uint32_t>(snapshot.begin(), snapshot.end());
    }

private:
    FCITX_OBJECT_VTABLE_METHOD(getStats, "GetStats", "", "au");
};

// Owned by RaskladLab. stop() (also run by the destructor) unregisters the
// object before the dbus addon, which RaskladLab depends on, is unloaded.
class Owner final {
public:
    Owner() = default;
    Owner(const Owner &) = delete;
    Owner &operator=(const Owner &) = delete;
    ~Owner() { stop(); }

    bool start(fcitx::Instance *instance) {
        if (endpoint_ || detail::owned || !instance) {
            return false;
        }
        if (!detail::privateLabEnvironment()) {
            return false; // markers wrong: no endpoint, counters stay off
        }
        fcitx::AddonInstance *dbusAddon =
            instance->addonManager().addon("dbus", true);
        if (!dbusAddon) {
            return false;
        }
        fcitx::dbus::Bus *bus = dbusAddon->call<fcitx::IDBusModule::bus>();
        if (!bus) {
            return false;
        }
        auto endpoint = std::make_unique<Endpoint>();
        detail::counters.fill(0);
        if (!bus->addObjectVTable(kObjectPath, kInterface, *endpoint)) {
            return false;
        }
        endpoint_ = std::move(endpoint);
        detail::owned = true;
        detail::enabled = true; // only after successful registration
        return true;
    }

    void stop() noexcept {
        if (!endpoint_) {
            return;
        }
        detail::enabled = false;
        endpoint_.reset(); // ObjectVTableBase dtor releases the bus slot
        detail::owned = false;
    }

private:
    std::unique_ptr<Endpoint> endpoint_;
};

} // namespace rasklad::fcitxlab::witness
