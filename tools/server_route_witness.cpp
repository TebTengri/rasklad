// SPDX-License-Identifier: MIT
//
// tools/server_route_witness.cpp
//
// rasklad SERVER-ROUTE witness shim (SERVER-ROUTE-WITNESS-001), developer-only.
//
// Strictly bounded, read-only observation of the Wayland *server* protocol
// route inside a private nested Hyprland lab. This is an OBSERVER, not a fix:
// it interposes wl_display_create(), attaches ONE public protocol logger to the
// created display, and tallies a fixed set of Space (vk=57) key transitions and
// metadata events into a tmpfs-backed shared snapshot for an external reader.
//
// Public APIs only: wayland-server-core.h protocol-logger + wl_resource_get_class,
// RTLD_NEXT, mmap. No Hyprland rebuild/patch, no WAYLAND_DEBUG, no raw dumps.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if !defined(__linux__) || !defined(__x86_64__)
#error "server route witness supports pinned Linux x86_64 only"
#endif
#include <wayland-server-core.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstdint>
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
#include "server_grab_epoch.h"
#endif
#include <cstdlib>
#include <cstring>


// 20 native little-endian uint32 words == 80 bytes. 32-bit atomics must be lockfree.
static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "need lockfree 32bit");
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
static constexpr unsigned WORDS=28, BYTES=112;
static constexpr uint32_t SCHEMA=2;
static constinit raskladlab::ServerGrabEpoch g_epoch{};
#else
static constexpr unsigned WORDS=20, BYTES=80;
static constexpr uint32_t SCHEMA=1;
#endif
static constexpr uint32_t MAGIC = 0x52535731u; // 'RSW1'
static constexpr uint32_t SAT   = 65535u;
static constexpr uint32_t SPACE = 57u;         // Linux evdev KEY_SPACE

typedef struct wl_display *(*wl_display_create_t)(void);

static volatile uint32_t *g_map  = nullptr;    // mmap(MAP_SHARED) transport
static wl_display_create_t g_orig = nullptr;   // resolved once in constructor
static pthread_t g_owner;                       // only this thread may mutate/snapshot

// ---- shared-word primitives (atomic builtins; seqlock on word 2) ------------
static inline void set_coverage_zero() noexcept { __atomic_store_n(&g_map[4], 0u, __ATOMIC_SEQ_CST); }

static inline void seq_begin() noexcept {
    uint32_t g = __atomic_load_n(&g_map[2], __ATOMIC_RELAXED);
    if (g >= 0xFFFFFF00u) _exit(125);          // near-wrap -> fail closed
    __atomic_store_n(&g_map[2], g + 1u, __ATOMIC_SEQ_CST); // odd: update in progress
}
static inline void seq_end() noexcept {
    uint32_t g = __atomic_load_n(&g_map[2], __ATOMIC_RELAXED);
    __atomic_store_n(&g_map[2], g + 1u, __ATOMIC_SEQ_CST); // even: stable
}
static inline void bump(unsigned idx) noexcept {
    uint32_t v = __atomic_load_n(&g_map[idx], __ATOMIC_RELAXED);
    if (v >= SAT) { set_coverage_zero(); return; }     // saturate, never wrap
    v += 1u;
    __atomic_store_n(&g_map[idx], v, __ATOMIC_SEQ_CST);
    if (v >= SAT) set_coverage_zero();                 // reaching limit drops coverage
}
static inline void txn_bump(unsigned idx) noexcept { seq_begin(); bump(idx); seq_end(); }
static inline void txn_cov0() { seq_begin(); set_coverage_zero(); seq_end(); }
static inline void txn_key(uint32_t st, unsigned p, unsigned r, wl_resource *resource=nullptr) {
    seq_begin();
    if (st == 1u) bump(p);                             // 1 == press
    else if (st == 0u) bump(r);                        // 0 == release
    else { bump(12); set_coverage_zero(); }            // unknown state, no classification
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
    if(p==8 && r==9)g_epoch.onSpace(resource,st);
#else
    (void)resource;
#endif
    seq_end();
}
static inline bool streq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }

#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
static void epoch_check_thread() noexcept {if(!pthread_equal(pthread_self(),g_owner))_exit(125);}
static void epoch_max(unsigned index,uint32_t value) noexcept {
    if(value>=SAT){set_coverage_zero();return;}
    if(value>__atomic_load_n(&g_map[index],__ATOMIC_RELAXED))
        __atomic_store_n(&g_map[index],value,__ATOMIC_SEQ_CST);
}
#endif

// ---- protocol logger: receives ALL messages; ignore everything unrelated -----
static void logger_cb(void *, enum wl_protocol_logger_type dir,
                      const struct wl_protocol_logger_message *msg) noexcept {
    if (!pthread_equal(pthread_self(), g_owner)) _exit(125); // foreign thread: fail closed
    if (!msg) return;
    const struct wl_resource *res = msg->resource;
    const struct wl_message  *m   = msg->message;
    if (!res || !m || !m->name || !m->signature) return;      // header guard
    if (dir != WL_PROTOCOL_LOGGER_REQUEST && dir != WL_PROTOCOL_LOGGER_EVENT) return;
    const char *cls = wl_resource_get_class(const_cast<struct wl_resource *>(res));
    if (!cls) return;
    const char *nm  = m->name;
    const char *sig = m->signature;
    const int   op  = msg->message_opcode;
    const int   ac  = msg->arguments_count;
    const union wl_argument *a = msg->arguments;

    if (dir == WL_PROTOCOL_LOGGER_REQUEST) {
        // zwp_virtual_keyboard_v1.key  (VK KEY opcode is 1, NOT 0) sig uuu argc3
        if (streq(cls, "zwp_virtual_keyboard_v1") && streq(nm, "key")) {
            if (op != 1 || !streq(sig, "uuu") || ac != 3 || !a) { txn_cov0(); return; }
            if (a[1].u == SPACE) txn_key(a[2].u, 6, 7);        // nonSpace ignored
            return;
        }
        // zwp_input_method_keyboard_grab_v2.release  metadata opcode0 argc0
        if (streq(cls, "zwp_input_method_keyboard_grab_v2") && streq(nm, "release")) {
            if (op != 0 || !streq(sig, "") || ac != 0) { txn_cov0(); return; }
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
            seq_begin();bump(15);g_epoch.onRelease(msg->resource);seq_end();
#else
            txn_bump(15);
#endif
            return;
        }
        // zwp_input_method_manager_v2.get_input_method  metadata opcode0 sig on argc2 (no args read)
        if (streq(cls, "zwp_input_method_manager_v2") && streq(nm, "get_input_method")) {
            if (op != 0 || !streq(sig, "on") || ac != 2) { txn_cov0(); return; }
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
            seq_begin();bump(16);g_epoch.onRequester(msg->resource);seq_end();
#else
            txn_bump(16);
#endif
            return;
        }
        return;
    }

#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
    if(streq(cls,"zwp_input_method_keyboard_grab_v2") && streq(nm,"keymap")){
        if(op!=0 || !streq(sig,"uhu") || ac!=3){txn_cov0();return;}
        seq_begin();g_epoch.onKeymap(msg->resource);seq_end();return;
    }
#endif
    // EVENT (emission attempt only -- NOT flush/transmit/client-receipt)
    // zwp_input_method_keyboard_grab_v2.key  opcode1 sig uuuu argc4; key idx2 state idx3
    if (streq(cls, "zwp_input_method_keyboard_grab_v2") && streq(nm, "key")) {
        if (op != 1 || !streq(sig, "uuuu") || ac != 4 || !a) { txn_cov0(); return; }
        if (a[2].u == SPACE) txn_key(a[3].u, 8, 9,msg->resource);
        return;
    }
    // wl_keyboard.key  opcode3 sig uuuu argc4; key idx2 state idx3 (may duplicate resources)
    if (streq(cls, "wl_keyboard") && streq(nm, "key")) {
        if (op != 3 || !streq(sig, "uuuu") || ac != 4 || !a) { txn_cov0(); return; }
        if (a[2].u == SPACE) txn_key(a[3].u, 10, 11);
        return;
    }
    // zwp_input_method_v2 metadata-only events
    if (streq(cls, "zwp_input_method_v2")) {
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
        if(streq(nm,"activate")){
            if(op!=0 || !streq(sig,"") || ac!=0){txn_cov0();return;}
            seq_begin();g_epoch.onIM(msg->resource,raskladlab::IMEvent::Activate);seq_end();return;
        }
#endif
        if (streq(nm, "deactivate")) {
            if (op != 1 || !streq(sig, "") || ac != 0) { txn_cov0(); return; }
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
            seq_begin();bump(13);g_epoch.onIM(msg->resource,raskladlab::IMEvent::Deactivate);seq_end();
#else
            txn_bump(13);
#endif
            return;
        }
        if (streq(nm, "done")) {
            if (op != 5 || !streq(sig, "") || ac != 0) { txn_cov0(); return; }
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
            seq_begin();bump(14);g_epoch.onIM(msg->resource,raskladlab::IMEvent::Done);seq_end();
#else
            txn_bump(14);
#endif
            return;
        }
        return; // other im_v2 events not of interest
    }
}

// ---- interposed wl_display_create -------------------------------------------
extern "C" WL_EXPORT struct wl_display *wl_display_create(void) {
    if (!pthread_equal(pthread_self(), g_owner)) _exit(125);  // foreign thread: fail closed
    struct wl_display *d = g_orig();                          // preserve: single call + result
    seq_begin();
    bump(5);
    if (__atomic_load_n(&g_map[5], __ATOMIC_RELAXED) > 1u) set_coverage_zero(); // multiple creations
    if (!d) {set_coverage_zero();__atomic_store_n(&g_map[3],0u,__ATOMIC_SEQ_CST);}                              // null original: coverage0, ready stays 0
    seq_end();
    if (!d) return d;                                         // delegate unchanged

    struct wl_protocol_logger *lg = wl_display_add_protocol_logger(d, logger_cb, nullptr);
    seq_begin();
    if (lg) {
        bump(17);
        uint32_t lc = __atomic_load_n(&g_map[17], __ATOMIC_RELAXED);
        if (lc == 1u) __atomic_store_n(&g_map[3], 1u, __ATOMIC_SEQ_CST); // ready=1 after ONE success
        if (lc > 1u) set_coverage_zero();
    } else {
        set_coverage_zero();
        __atomic_store_n(&g_map[3],0u,__ATOMIC_SEQ_CST);      // attach failed: coverage0, ready unchanged
    }
    seq_end();
    return d;
}

// ---- owner-thread snapshot (exact 20 words; file is external-reader transport)
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
#define RASKLAD_SNAPSHOT rasklad_server_route_snapshot_v2
#else
#define RASKLAD_SNAPSHOT rasklad_server_route_snapshot_v1
#endif
extern "C" WL_EXPORT uint32_t RASKLAD_SNAPSHOT(uint32_t *out, uint32_t cap) noexcept {
    if (!pthread_equal(pthread_self(), g_owner)) return 0;    // foreign thread: fail closed
    if (!out || cap < WORDS || !g_map) return 0;
    for (unsigned i = 0; i < WORDS; ++i)
        out[i] = __atomic_load_n(&g_map[i], __ATOMIC_ACQUIRE);
    return WORDS;
}

// ---- constructor: admission markers + single-shot setup. Only file-IO site. --
static bool env_is(const char *n, const char *v) { const char *e = getenv(n); return e && std::strcmp(e, v) == 0; }

__attribute__((constructor))
static void rasklad_server_route_init() {
    // Required admission env (markers only; runner supplies the actual isolation).
    if (!env_is("HOME", "/home/lab"))                      _exit(125);
    if (!env_is("XDG_RUNTIME_DIR", "/run/lab"))            _exit(125);
    if (!env_is("RASKLAD_NATIVE_WAYLAND_LAB", "1"))        _exit(125);
    if (!env_is("RASKLAD_SERVER_ROUTE_WITNESS_LAB", "1"))  _exit(125);
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
    if(!env_is("RASKLAD_GRAB_EPOCH_WITNESS_LAB","1")) _exit(125);
#endif
    // Forbidden env.
    if (getenv("DISPLAY"))          _exit(125);
    if (getenv("WAYLAND_DEBUG"))    _exit(125);
    if (getenv("RASKLAD_PARENT_FD"))_exit(125);
    // WAYLAND_SOCKET may exist (nested compositor's granted parent FD): never read/close/use it.
    if (unsetenv("LD_PRELOAD")!=0) _exit(125); // do not leak the preload into descendants

    void *sym = dlsym(RTLD_NEXT, "wl_display_create");
    if (!sym) _exit(125);                                     // missing original
    wl_display_create_t self=&wl_display_create;void *self_address=nullptr;
    static_assert(sizeof(self)==sizeof(sym));
    std::memcpy(&self_address,&self,sizeof(self_address));
    if (sym==self_address) _exit(125);
    std::memcpy(&g_orig,&sym,sizeof(g_orig));
    g_owner = pthread_self();

    int fd = open("/run/lab/server-route-witness.bin",
                  O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) _exit(125);
    if (ftruncate(fd, (off_t)BYTES) != 0) _exit(125);
    void *p = mmap(nullptr, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) _exit(125);
    close(fd);                                               // fd not retained
    g_map = reinterpret_cast<volatile uint32_t *>(p);

    for(unsigned i=0;i<WORDS;++i) __atomic_store_n(&g_map[i],0u,__ATOMIC_SEQ_CST);
    __atomic_store_n(&g_map[0],MAGIC,__ATOMIC_SEQ_CST);
    __atomic_store_n(&g_map[1],SCHEMA,__ATOMIC_SEQ_CST);
    __atomic_store_n(&g_map[4],1u,__ATOMIC_SEQ_CST);      // all counters/reserved start 0
#ifdef RASKLAD_GRAB_EPOCH_WITNESS_LAB
    g_epoch.init({seq_begin,seq_end,bump,epoch_max,set_coverage_zero,epoch_check_thread});
#endif
    // No destructor: OS unmaps on exit; never touch a freed wl_display.
}

// reviewNotes:
// - Opcodes/signatures taken from the pinned public definitions. Applied the
//   stated correction: zwp_virtual_keyboard_v1.key uses opcode 1 (keymap0,key1,
//   modifiers2,destroy3), not the originally-proposed 0. grab key event opcode 1,
//   wl_keyboard.key opcode 3, im_v2 deactivate 1 / done 5, grab release 0,
//   manager get_input_method 0 (sig "on"). No guessing; if an installed build's
//   generated signature carries version/nullable annotations (e.g. a leading
//   digit) a matched class+name would mismatch the exact signature and we
//   fail-closed by zeroing coverage rather than mis-counting -- flagged as a
//   potential false-negative blocker to confirm against the real libwayland.
// - Observation only: counts an *emission attempt* for events, never flush /
//   transmission / client receipt. Lifecycle totals cannot identify an old grab
//   generation and do not prove a destroyed-proxy cause.
// - Virtual-keyboard tallies include ALL private virtual keyboards (Fcitx
//   forwarding included); not helper-only. wl_keyboard counts may duplicate
//   across resources; not 1:1.
// - Snapshot/seqlock: single writer thread (owner); external readers must take
//   two byte-identical 80-byte samples with an even word[2] generation. No reset
//   API; counters saturate at 65535 and saturation/near-wrap/validation failure
//   latch coverage=0 (word 4) without wrapping. No pointers/serials/time/
//   keycodes/text are exported. mmap persists for process lifetime by design.