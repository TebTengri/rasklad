#include "labclient.h"

#include "labmodel.h"

#include <algorithm>
#include <cerrno>

#include <poll.h>
#include <unistd.h>

#include <wayland-client.h>

#include "input-method-unstable-v2-client-protocol.h"

namespace inputlab {

struct LabClient::Impl {
    struct ImSlot {
        zwp_input_method_v2* proxy = nullptr;
        ImRecord record;
    };

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        // Requests on an errored display are dropped by libwayland; proxies are freed.
        for (auto& slot : ims) {
            if (slot->proxy)
                zwp_input_method_v2_destroy(slot->proxy);
        }
        for (wl_proxy* proxy : rejectedProxies)
            wl_proxy_destroy(proxy);
        for (wl_callback* callback : pendingCallbacks)
            wl_callback_destroy(callback);
        if (manager)
            zwp_input_method_manager_v2_destroy(manager);
        if (seat)
            wl_seat_destroy(seat);
        if (registry)
            wl_registry_destroy(registry);
        if (display)
            wl_display_disconnect(display);
    }

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    zwp_input_method_manager_v2* manager = nullptr;
    std::optional<std::uint32_t> capabilities;
    std::vector<std::unique_ptr<ImSlot>> ims;
    std::vector<wl_proxy*> rejectedProxies;
    std::vector<wl_callback*> pendingCallbacks;
    std::uint64_t syncRequested = 0;
    std::uint64_t syncDone = 0;

    ImSlot* slot(std::size_t index) const noexcept
    {
        return index < ims.size() ? ims[index].get() : nullptr;
    }

    // ---- listeners (C callbacks; noexcept: allocation failure terminates) ----

    static void onSeatCapabilities(void* data, wl_seat*, std::uint32_t caps) noexcept
    {
        static_cast<Impl*>(data)->capabilities = caps;
    }
    static void onSeatName(void*, wl_seat*, const char*) noexcept {}

    static void onGlobal(void* data, wl_registry* registry, std::uint32_t name,
                         const char* interface, std::uint32_t version) noexcept
    {
        static const struct wl_seat_listener seatListener = {&onSeatCapabilities, &onSeatName};
        auto* self = static_cast<Impl*>(data);
        if (!interface || version < 1)
            return;
        const std::string_view iface(interface);
        if (iface == wl_seat_interface.name && !self->seat) {
            self->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
            if (self->seat)
                wl_seat_add_listener(self->seat, &seatListener, self);
        } else if (iface == zwp_input_method_manager_v2_interface.name && !self->manager) {
            self->manager = static_cast<zwp_input_method_manager_v2*>(
                wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1));
        }
    }
    static void onGlobalRemove(void*, wl_registry*, std::uint32_t) noexcept {}

    static void onSyncDone(void* data, wl_callback* callback, std::uint32_t) noexcept
    {
        auto* self = static_cast<Impl*>(data);
        ++self->syncDone;
        auto& v = self->pendingCallbacks;
        v.erase(std::remove(v.begin(), v.end(), callback), v.end());
        wl_callback_destroy(callback);
    }

    static ImRecord& rec(void* data) noexcept { return static_cast<ImSlot*>(data)->record; }

    static void onActivate(void* data, zwp_input_method_v2*) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::Activate, {}, 0, 0});
        r.pending = ImView{}; // activate resets all previous state
        r.pending.active = true;
    }
    static void onDeactivate(void* data, zwp_input_method_v2*) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::Deactivate, {}, 0, 0});
        r.pending.active = false;
    }
    static void onSurroundingText(void* data, zwp_input_method_v2*, const char* text,
                                  std::uint32_t cursor, std::uint32_t anchor) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::SurroundingText, text ? text : "", cursor, anchor});
        r.pending.hasSurrounding = true;
        r.pending.surrounding = text ? text : "";
        r.pending.cursor = cursor;
        r.pending.anchor = anchor;
    }
    static void onTextChangeCause(void* data, zwp_input_method_v2*, std::uint32_t cause) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::TextChangeCause, {}, cause, 0});
        r.pending.cause = cause;
    }
    static void onContentType(void* data, zwp_input_method_v2*, std::uint32_t hint,
                              std::uint32_t purpose) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::ContentType, {}, hint, purpose});
        r.pending.hint = hint;
        r.pending.purpose = purpose;
    }
    static void onDone(void* data, zwp_input_method_v2*) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::Done, {}, 0, 0});
        r.current = r.pending;
        ++r.doneSeen;
        // surrounding_text and text_change_cause return to initial values after done.
        r.pending.hasSurrounding = false;
        r.pending.surrounding.clear();
        r.pending.cursor = 0;
        r.pending.anchor = 0;
        r.pending.cause = tiv3::kCauseInputMethod;
    }
    static void onUnavailable(void* data, zwp_input_method_v2*) noexcept
    {
        ImRecord& r = rec(data);
        r.events.push_back({ImEventKind::Unavailable, {}, 0, 0});
        r.unavailable = true;
    }
};

LabClient::LabClient(int fd) : d_(std::make_unique<Impl>())
{
    static const struct wl_registry_listener registryListener = {&Impl::onGlobal,
                                                                 &Impl::onGlobalRemove};
    d_->display = wl_display_connect_to_fd(fd);
    if (!d_->display) {
        ::close(fd); // not taken over on failure
        return;
    }
    d_->registry = wl_display_get_registry(d_->display);
    if (d_->registry)
        wl_registry_add_listener(d_->registry, &registryListener, d_.get());
}

LabClient::~LabClient() = default;

bool LabClient::connected() const noexcept
{
    return d_->display && d_->registry && wl_display_get_error(d_->display) == 0;
}

int LabClient::displayError() const noexcept
{
    return d_->display ? wl_display_get_error(d_->display) : EBADF;
}

bool LabClient::hasGlobals() const noexcept
{
    return d_->seat && d_->manager;
}

std::optional<std::uint32_t> LabClient::seatCapabilities() const noexcept
{
    return d_->capabilities;
}

bool LabClient::flush()
{
    if (!connected())
        return false;
    // EAGAIN: socket buffer full; the rest is sent on the next pump iteration.
    return wl_display_flush(d_->display) >= 0 || errno == EAGAIN;
}

bool LabClient::readAndDispatch(int timeoutMs)
{
    if (!connected())
        return false;
    wl_display* display = d_->display;
    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0)
            return false;
    }
    pollfd pfd{wl_display_get_fd(display), POLLIN, 0};
    int ready = 0;
    ready = ::poll(&pfd, 1, timeoutMs);
    if (ready > 0) {
        if (wl_display_read_events(display) < 0)
            return false;
    } else {
        wl_display_cancel_read(display);
        // Let the bounded outer pump retry after a signal; never restart poll
        // indefinitely inside a single iteration.
        if (ready < 0 && errno != EINTR)
            return false;
    }
    return wl_display_dispatch_pending(display) >= 0;
}

std::optional<std::uint64_t> LabClient::requestSync()
{
    static const struct wl_callback_listener callbackListener = {&Impl::onSyncDone};
    if (!connected())
        return std::nullopt;
    wl_callback* callback = wl_display_sync(d_->display);
    if (!callback)
        return std::nullopt;
    d_->pendingCallbacks.push_back(callback);
    wl_callback_add_listener(callback, &callbackListener, d_.get());
    return ++d_->syncRequested;
}

bool LabClient::syncCompleted(std::uint64_t token) const noexcept
{
    return d_->syncDone >= token;
}

std::optional<std::size_t> LabClient::createInputMethod()
{
    static const struct zwp_input_method_v2_listener imListener = {
        &Impl::onActivate, &Impl::onDeactivate, &Impl::onSurroundingText, &Impl::onTextChangeCause,
        &Impl::onContentType, &Impl::onDone, &Impl::onUnavailable};
    if (!connected() || !hasGlobals())
        return std::nullopt;
    auto slot = std::make_unique<Impl::ImSlot>();
    slot->proxy = zwp_input_method_manager_v2_get_input_method(d_->manager, d_->seat);
    if (!slot->proxy)
        return std::nullopt;
    zwp_input_method_v2_add_listener(slot->proxy, &imListener, slot.get());
    d_->ims.push_back(std::move(slot));
    return d_->ims.size() - 1;
}

const ImRecord& LabClient::record(std::size_t im) const
{
    return d_->ims.at(im)->record;
}

bool LabClient::commitString(std::size_t im, std::string_view utf8)
{
    Impl::ImSlot* slot = d_->slot(im);
    // Wire strings are NUL-terminated; refuse embedded NUL instead of truncating.
    if (!slot || utf8.find('\0') != std::string_view::npos)
        return false;
    const std::string text(utf8);
    zwp_input_method_v2_commit_string(slot->proxy, text.c_str());
    return true;
}

bool LabClient::deleteSurroundingText(std::size_t im, std::uint32_t before, std::uint32_t after)
{
    Impl::ImSlot* slot = d_->slot(im);
    if (!slot)
        return false;
    zwp_input_method_v2_delete_surrounding_text(slot->proxy, before, after);
    return true;
}

bool LabClient::commit(std::size_t im, std::uint32_t serial)
{
    Impl::ImSlot* slot = d_->slot(im);
    if (!slot)
        return false;
    zwp_input_method_v2_commit(slot->proxy, serial);
    return true;
}

bool LabClient::commitWithLatestSerial(std::size_t im)
{
    Impl::ImSlot* slot = d_->slot(im);
    return slot && commit(im, slot->record.doneSeen);
}

bool LabClient::sendUnsupported(std::size_t im, UnsupportedRequest request)
{
    Impl::ImSlot* slot = d_->slot(im);
    if (!slot)
        return false;
    switch (request) {
    case UnsupportedRequest::GrabKeyboard: {
        auto* grab = zwp_input_method_v2_grab_keyboard(slot->proxy);
        if (!grab)
            return false;
        d_->rejectedProxies.push_back(reinterpret_cast<wl_proxy*>(grab));
        return true;
    }
    case UnsupportedRequest::SetPreeditString:
        zwp_input_method_v2_set_preedit_string(slot->proxy, "lab", 0, 0);
        return true;
    case UnsupportedRequest::SeatGetKeyboard: {
        wl_keyboard* keyboard = wl_seat_get_keyboard(d_->seat);
        if (!keyboard)
            return false;
        d_->rejectedProxies.push_back(reinterpret_cast<wl_proxy*>(keyboard));
        return true;
    }
    }
    return false;
}

} // namespace inputlab
