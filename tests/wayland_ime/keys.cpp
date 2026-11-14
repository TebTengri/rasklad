// SPDX-License-Identifier: MIT
// Synthetic virtual keyboard for the private rasklad Wayland lab compositor only.
// Emits a fixed set of US-layout key sequences; never touches host seats or /dev.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-client.h"

static_assert(KEY_LEFTCTRL == 29, "unexpected KEY_LEFTCTRL");
static_assert(KEY_Z == 44, "unexpected KEY_Z");
static_assert(KEY_SPACE == 57, "unexpected KEY_SPACE");
static_assert(KEY_BACKSPACE == 14, "unexpected KEY_BACKSPACE");
static_assert(KEY_ESC == 1, "unexpected KEY_ESC");

namespace {

bool envIs(const char *name, const char *expected)
{
    const char *v = std::getenv(name);
    return v && std::strcmp(v, expected) == 0;
}

bool labGuardsPass()
{
    if (!envIs("RASKLAD_NATIVE_WAYLAND_LAB", "1")) return false;
    if (!envIs("HOME", "/home/lab")) return false;
    if (!envIs("XDG_RUNTIME_DIR", "/run/lab")) return false;
    if (!envIs("WAYLAND_DISPLAY", "wayland-lab")) return false;
    if (std::getenv("DISPLAY")) return false;
    if (std::getenv("WAYLAND_SOCKET")) return false;
    struct stat st {};
    if (stat("/lab/fixture.html", &st) != 0 || !S_ISREG(st.st_mode)) return false;
    return true;
}

struct Globals {
    wl_seat *seat = nullptr;
    zwp_virtual_keyboard_manager_v1 *manager = nullptr;
};

void onGlobal(void *data, wl_registry *registry, uint32_t name, const char *iface, uint32_t)
{
    auto *g = static_cast<Globals *>(data);
    if (!g->seat && std::strcmp(iface, wl_seat_interface.name) == 0) {
        g->seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (!g->manager && std::strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        g->manager = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
}

void onGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

int keymapFd(size_t size)
{
    int fd = memfd_create("rasklad-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        char tmpl[] = "/tmp/rasklad-keymap-XXXXXX";
        fd = mkostemp(tmpl, O_CLOEXEC);
        if (fd < 0) return -1;
        unlink(tmpl);
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

struct Session {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    Globals g;
    zwp_virtual_keyboard_v1 *vk = nullptr;
    xkb_context *ctx = nullptr;
    xkb_keymap *keymap = nullptr;
    xkb_state *state = nullptr;
    uint32_t lastDep = 0, lastLat = 0, lastLock = 0, lastGroup = 0;

    ~Session()
    {
        if (vk) zwp_virtual_keyboard_v1_destroy(vk);
        if (g.manager) zwp_virtual_keyboard_manager_v1_destroy(g.manager);
        if (g.seat) wl_seat_destroy(g.seat);
        if (registry) wl_registry_destroy(registry);
        if (display) {
            wl_display_flush(display);
            wl_display_disconnect(display);
        }
        if (state) xkb_state_unref(state);
        if (keymap) xkb_keymap_unref(keymap);
        if (ctx) xkb_context_unref(ctx);
    }

    bool init()
    {
        display = wl_display_connect("wayland-lab");
        if (!display) return false;
        registry = wl_display_get_registry(display);
        if (!registry) return false;
        wl_registry_add_listener(registry, &kRegistryListener, &g);
        if (wl_display_roundtrip(display) < 0) return false;
        if (!g.seat || !g.manager) return false;

        vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(g.manager, g.seat);
        if (!vk) return false;

        ctx = xkb_context_new(static_cast<xkb_context_flags>(XKB_CONTEXT_NO_ENVIRONMENT_NAMES | XKB_CONTEXT_NO_DEFAULT_INCLUDES));
        if (!ctx) return false;
        if (!xkb_context_include_path_append_default(ctx)) return false;
        const xkb_rule_names names = {"evdev", "pc105", "us", "", ""};
        keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) return false;
        state = xkb_state_new(keymap);
        if (!state) return false;

        char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        if (!text) return false;
        const size_t size = std::strlen(text) + 1; // include NUL
        int fd = keymapFd(size);
        if (fd < 0) {
            std::free(text);
            return false;
        }
        void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            std::free(text);
            close(fd);
            return false;
        }
        std::memcpy(map, text, size);
        munmap(map, size);
        std::free(text);

        zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                       static_cast<uint32_t>(size));
        const int rt = wl_display_roundtrip(display);
        close(fd);
        return rt >= 0;
    }

    static uint32_t nowMs()
    {
        timespec ts {};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
    }

    bool sendKey(uint32_t code, bool down)
    {
        zwp_virtual_keyboard_v1_key(vk, nowMs(), code,
                                    down ? WL_KEYBOARD_KEY_STATE_PRESSED
                                         : WL_KEYBOARD_KEY_STATE_RELEASED);
        const auto changed = xkb_state_update_key(state, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
        if (changed) {
            const uint32_t dep = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
            const uint32_t lat = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
            const uint32_t lock = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
            const uint32_t grp = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
            if (dep != lastDep || lat != lastLat || lock != lastLock || grp != lastGroup) {
                zwp_virtual_keyboard_v1_modifiers(vk, dep, lat, lock, grp);
                lastDep = dep; lastLat = lat; lastLock = lock; lastGroup = grp;
            }
        }
        if (wl_display_roundtrip(display) < 0) return false;
        timespec pause {0, 25 * 1000 * 1000};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
        return true;
    }

    bool tap(uint32_t code) { return sendKey(code, true) && sendKey(code, false); }

    bool chord(uint32_t mod, uint32_t code)
    {
        return sendKey(mod, true) && tap(code) && sendKey(mod, false);
    }
};

// Fixed US evdev code table for the only characters the helper ever types.
uint32_t codeFor(char c)
{
    switch (c) {
    case 'a': return KEY_A;
    case 'b': return KEY_B;
    case 'd': return KEY_D;
    case 'e': return KEY_E;
    case 'g': return KEY_G;
    case 'h': return KEY_H;
    case 'l': return KEY_L;
    case 'n': return KEY_N;
    case 'o': return KEY_O;
    case 't': return KEY_T;
    case 'v': return KEY_V;
    case 'x': return KEY_X;
    case '1': return KEY_1;
    case ' ': return KEY_SPACE;
    default: return 0;
    }
}

bool typeFixed(Session &s, std::string_view seq)
{
    for (char c : seq) {
        const uint32_t code = codeFor(c);
        if (code == 0 || !s.tap(code)) return false;
    }
    return true;
}

enum class Cmd { Word, Space, XSpace, Hello, Sample2, Mixed, Long, Undo, Backspace, Escape, Unknown };

Cmd parse(std::string_view line)
{
    if (line == "word") return Cmd::Word;
    if (line == "space") return Cmd::Space;
    if (line == "xspace") return Cmd::XSpace;
    if (line == "hello") return Cmd::Hello;
    if (line == "sample2") return Cmd::Sample2;
    if (line == "mixed") return Cmd::Mixed;
    if (line == "long") return Cmd::Long;
    if (line == "undo") return Cmd::Undo;
    if (line == "backspace") return Cmd::Backspace;
    if (line == "escape") return Cmd::Escape;
    return Cmd::Unknown;
}

bool run(Session &s, Cmd cmd)
{
    switch (cmd) {
    case Cmd::Word: return typeFixed(s, "ghbdtn");
    case Cmd::Space: return s.tap(KEY_SPACE);
    case Cmd::XSpace: return typeFixed(s, "x ");
    case Cmd::Hello: return typeFixed(s, "hello ");
    case Cmd::Sample2: return typeFixed(s, "ghbvth ");
    case Cmd::Mixed: return typeFixed(s, "1ghbdtn ");
    case Cmd::Long:
        for (int i = 0; i < 33; ++i)
            if (!s.tap(KEY_A)) return false;
        return typeFixed(s, "ghbdtn ");
    case Cmd::Undo: return s.chord(KEY_LEFTCTRL, KEY_Z);
    case Cmd::Backspace: return s.tap(KEY_BACKSPACE);
    case Cmd::Escape: return s.tap(KEY_ESC);
    case Cmd::Unknown: break;
    }
    return false;
}

} // namespace

int main()
{
    if (!labGuardsPass()) {
        std::fputs("{\"ready\":false,\"error\":\"lab-guard\"}\n", stdout);
        std::fflush(stdout);
        return 2;
    }

    Session s;
    if (!s.init()) {
        std::fputs("{\"ready\":false,\"error\":\"init\"}\n", stdout);
        std::fflush(stdout);
        return 3;
    }

    std::fputs("{\"ready\":true}\n", stdout);
    std::fflush(stdout);

    char buf[64];
    while (std::fgets(buf, sizeof buf, stdin)) {
        size_t len = std::strlen(buf);
        if (len == 0 || buf[len - 1] != '\n') return 4; // overlong or unterminated
        buf[--len] = '\0';
        if (len && buf[len - 1] == '\r') buf[--len] = '\0';

        const Cmd cmd = parse(std::string_view(buf, len));
        if (cmd == Cmd::Unknown) return 5;
        if (!run(s, cmd)) return 6;

        std::fputs("{\"ok\":true}\n", stdout);
        std::fflush(stdout);
    }
    return 0;
}
