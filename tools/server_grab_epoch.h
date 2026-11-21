// SPDX-License-Identifier: MIT
#pragma once
// tools/server_grab_epoch.h — private Wayland SERVER-side diagnostic (MIT).
//
// Purpose: relate the server-observed creation of an input-method keyboard
// grab resource to the latest input_method.deactivate + done that the SERVER
// SENT. It is a diagnostic, not a fix. It does NOT prove client delivery,
// client acknowledgement, or client proxy destruction.
//
// Sources of truth:
//  - doneOrdinal / lastDeactivateDone: done EVENTS SENT by the server.
//  - Slot creation: the first keymap event the server sends on a grab resource.
//  - Slot lifetime: the public wl_resource destroy listener, i.e. SERVER
//    resource lifetime only. Not the client's proxy lifetime.
//  - "old" means only that the slot was created before the latest
//    deactivate+done the server sent. It is a server temporal relation.
//
// Rules: no allocation, I/O, text, clocks, or payload/argument access. Resource
// and client pointers stay internal and are never exported. A saved pointer is
// only compared, never dereferenced. Slots are monotonic and never reused.
// Public on* methods run INSIDE the root's existing seqlock transaction and
// never call begin/end. The destroy callback runs outside it and brackets its
// own update. The instance must outlive the wl_display (static storage).
//
// Shared schema 2 (root): 28 x uint32 = 112 B. Indices 0..17 are unchanged.
// 18..27 are owned by the counters below. Ops::bump saturates at 65535 and
// clears coverage at the limit; Ops::storeMax is bounded (live <= 128).

#include <wayland-server-core.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>

namespace raskladlab {

enum class IMEvent : std::uint8_t { Activate, Deactivate, Done };

class ServerGrabEpoch {
public:
    struct Ops {
        void (*begin)() noexcept = nullptr;
        void (*end)() noexcept = nullptr;
        void (*bump)(unsigned index) noexcept = nullptr;
        void (*storeMax)(unsigned index, std::uint32_t value) noexcept = nullptr;
        void (*invalidate)() noexcept = nullptr;
        void (*checkThread)() noexcept = nullptr;
    };

    static constexpr unsigned kOldPress = 18;
    static constexpr unsigned kOldRelease = 19;
    static constexpr unsigned kCurrentPress = 20;
    static constexpr unsigned kCurrentRelease = 21;
    static constexpr unsigned kUnknownPress = 22;
    static constexpr unsigned kUnknownRelease = 23;
    static constexpr unsigned kMultipleLivePress = 24;
    static constexpr unsigned kPeakLive = 25;
    static constexpr unsigned kRegistrations = 26;
    static constexpr unsigned kDestroyed = 27;
    static constexpr std::uint32_t kMaxSlots = 128;

    constexpr ServerGrabEpoch() noexcept = default;
    ServerGrabEpoch(const ServerGrabEpoch&) = delete;
    ServerGrabEpoch& operator=(const ServerGrabEpoch&) = delete;

    // Call once. A repeated call or any null op terminates with _exit(125).
    void init(const Ops& ops) noexcept {
        if (inited_ || !ops.begin || !ops.end || !ops.bump || !ops.storeMax ||
            !ops.invalidate || !ops.checkThread)
            ::_exit(125);
        ops_ = ops;
        inited_ = true;
        ops_.checkThread();
    }

    // Handles exactly one input-method manager get_input_method request.
    // Only request metadata is used; no arguments are read. The requester is
    // not proven to be the accepted owner. Calibration and a unique actual IM
    // resource are still required.
    void onRequester(wl_resource* resource) noexcept {
        enter();
        if (requesterSeen_) { fail(); return; }
        requesterSeen_ = true;
        wl_client* client = resource ? wl_resource_get_client(resource) : nullptr;
        if (!client) { fail(); return; }
        expected_ = client;
    }

    // Handles input_method events the server sends. Epoch = done SENT.
    void onIM(wl_resource* resource, IMEvent event) noexcept {
        enter();
        if (!fromExpected(resource)) { fail(); return; }
        if (!im_) im_ = resource;
        else if (im_ != resource) { fail(); return; }  // compare only
        switch (event) {
        case IMEvent::Activate:
            return;  // no epoch reset
        case IMEvent::Deactivate:
            pending_ = true;
            return;
        case IMEvent::Done:
            if (doneOrdinal_ == UINT32_MAX) { fail(); return; }
            ++doneOrdinal_;
            if (pending_) { lastDeactivateDone_ = doneOrdinal_; pending_ = false; }
            return;
        }
        fail();  // out-of-range enum value
    }

    // Handles a keymap the server sends on a keyboard grab resource. The first
    // keymap for a resource creates its slot. Repeats are no-ops. No arguments
    // are read and no FD operations happen.
    void onKeymap(wl_resource* resource) noexcept {
        enter();
        if (!im_ || !fromExpected(resource)) { fail(); return; }
        if (findActive(resource)) return;
        if (nextSlot_ >= kMaxSlots) { fail(); return; }
        Slot& s = slots_[nextSlot_++];
        s.owner = this;
        s.resource = resource;
        s.createdDoneOrdinal = doneOrdinal_;
        s.active = true;
        s.listener.notify = &ServerGrabEpoch::onDestroy;
        wl_list_init(&s.listener.link);
        wl_resource_add_destroy_listener(resource, &s.listener);
        ++live_;
        ops_.bump(kRegistrations);
        ops_.storeMax(kPeakLive, live_);
    }

    // Only fixed Space (evdev 57) reaches this method. state: 0 = release,
    // 1 = press. Any other state invalidates without classification.
    void onSpace(wl_resource* resource, std::uint32_t state) noexcept {
        enter();
        if (state > 1) { fail(); return; }
        const bool press = state == 1;
        Slot* s = findActive(resource);
        if (!s || !fromExpected(resource)) {
            ops_.bump(press ? kUnknownPress : kUnknownRelease);
            fail();
            return;
        }
        const bool old = s->createdDoneOrdinal < lastDeactivateDone_;
        if (press) ops_.bump(old ? kOldPress : kCurrentPress);
        else ops_.bump(old ? kOldRelease : kCurrentRelease);
        if (press && live_ >= 2) ops_.bump(kMultipleLivePress);
    }

    // Handles the grab release request (metadata only). It does not clear
    // active; only the destroy listener does that. The root counts the release.
    void onRelease(wl_resource* resource) noexcept {
        enter();
        if (!findActive(resource) || !fromExpected(resource)) fail();
    }

private:
    struct Slot {
        wl_listener listener{};  // must stay the first member
        ServerGrabEpoch* owner = nullptr;
        wl_resource* resource = nullptr;  // compared only, never dereferenced
        std::uint32_t createdDoneOrdinal = 0;
        bool active = false;
    };
    static_assert(offsetof(Slot, listener) == 0, "listener must be first");

    // Public destroy listener: SERVER resource lifetime. It runs outside the
    // root transaction, so it brackets its own update. data is not read.
    static void onDestroy(wl_listener* listener, void*) noexcept {
        Slot* s = reinterpret_cast<Slot*>(listener);  // first member
        ServerGrabEpoch* self = s->owner;
        self->ops_.checkThread();
        self->ops_.begin();
        wl_list_remove(&listener->link);
        wl_list_init(&listener->link);
        if (!s->active || self->live_ == 0) {
            self->fail();
        } else {
            --self->live_;
        }
        s->active = false;
        s->resource = nullptr;
        self->ops_.bump(kDestroyed);
        self->ops_.end();
    }

    void enter() const noexcept {
        if (!inited_) ::_exit(125);
        ops_.checkThread();
    }

    void fail() const noexcept { ops_.invalidate(); }

    // Only queries the live, caller-supplied resource. It never touches a
    // saved pointer.
    bool fromExpected(wl_resource* resource) const noexcept {
        return resource && expected_ && wl_resource_get_client(resource) == expected_;
    }

    Slot* findActive(wl_resource* resource) noexcept {
        if (!resource) return nullptr;
        for (std::uint32_t i = 0; i < nextSlot_; ++i)
            if (slots_[i].active && slots_[i].resource == resource) return &slots_[i];
        return nullptr;
    }

    Ops ops_{};
    bool inited_ = false;
    bool requesterSeen_ = false;
    bool pending_ = false;
    wl_client* expected_ = nullptr;   // internal, never exported
    wl_resource* im_ = nullptr;       // compared only, never dereferenced
    std::uint32_t doneOrdinal_ = 0;
    std::uint32_t lastDeactivateDone_ = 0;
    std::uint32_t live_ = 0;
    std::uint32_t nextSlot_ = 0;      // monotonic, never reused
    Slot slots_[kMaxSlots]{};
};

namespace detail {
consteval bool serverGrabEpochConstantInitializable() {
    ServerGrabEpoch probe{};
    (void)probe;
    return true;
}
static_assert(serverGrabEpochConstantInitializable(),
              "ServerGrabEpoch must support constant initialization");
}  // namespace detail

}  // namespace raskladlab
