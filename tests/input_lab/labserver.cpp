#include "labserver.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include <wayland-server.h>

#include "input-method-unstable-v2-server-protocol.h"

namespace inputlab {

namespace {
constexpr int kSeatVersion = 1;
constexpr int kManagerVersion = 1;
} // namespace

struct LabServer::Impl {
    struct ImObject {
        Impl* owner = nullptr;
        wl_resource* resource = nullptr;
        bool inert = false;   // unavailable was sent; all further requests ignored
        bool active = false;  // server view after activate/deactivate was sent
        std::uint32_t doneCount = 0; // done events issued by THIS object
        std::optional<std::string> pendingCommit;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> pendingDelete;
    };

    explicit Impl(SerialPolicy p) : policy(p) {}

    ~Impl()
    {
        // Destroying clients runs resource destructors while `ims` is still alive.
        if (display) {
            wl_display_destroy_clients(display);
            wl_display_destroy(display);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    SerialPolicy policy;
    wl_display* display = nullptr;
    wl_global* seatGlobal = nullptr;
    wl_global* managerGlobal = nullptr;
    std::vector<std::unique_ptr<ImObject>> ims;
    ImObject* primary = nullptr; // the input method of the single fake seat
    std::vector<SyntheticField> fields;
    std::optional<std::size_t> focused;
    Stats stats;

    ImObject* liveIm() const noexcept { return primary && !primary->inert ? primary : nullptr; }

    void sendState(ImObject& im, const SyntheticField& f, std::uint32_t cause)
    {
        zwp_input_method_v2_send_surrounding_text(im.resource, f.text.c_str(), f.cursor, f.cursor);
        zwp_input_method_v2_send_text_change_cause(im.resource, cause);
        zwp_input_method_v2_send_content_type(im.resource, f.hint, f.purpose);
    }

    void sendDone(ImObject& im)
    {
        zwp_input_method_v2_send_done(im.resource);
        ++im.doneCount; // unsigned wrap matches the protocol's uint serial
    }

    void sendActivate(ImObject& im, const SyntheticField& f)
    {
        // Spec: activate resets state of commit_string/delete_surrounding_text.
        // The unchecked model keeps it (activate() in the read source only sends
        // the event).
        if (policy == SerialPolicy::Strict) {
            if (im.pendingCommit || im.pendingDelete)
                ++stats.pendingDroppedOnActivate;
            im.pendingCommit.reset();
            im.pendingDelete.reset();
        }
        zwp_input_method_v2_send_activate(im.resource);
        im.active = true;
        sendState(im, f, tiv3::kCauseInputMethod);
        sendDone(im);
    }

    void sendDeactivate(ImObject& im)
    {
        zwp_input_method_v2_send_deactivate(im.resource);
        im.active = false;
        sendDone(im);
    }

    void onCommit(ImObject& im, std::uint32_t serial)
    {
        // commit always consumes pending state, accepted or not.
        std::optional<std::string> text = std::exchange(im.pendingCommit, std::nullopt);
        const auto del = std::exchange(im.pendingDelete, std::nullopt);

        if (im.inert) {
            ++stats.inertIgnored;
            return;
        }

        const bool fresh = serial == im.doneCount;
        if (policy == SerialPolicy::Strict) {
            if (!fresh) {
                ++stats.staleRejected;
                return;
            }
            if (!im.active || !focused) {
                ++stats.inactiveIgnored;
                return;
            }
        } else {
            // Model: forward to whatever field is focused now, active or not.
            if (!focused) {
                ++stats.inactiveIgnored;
                return;
            }
            if (!fresh)
                ++stats.staleForwarded;
        }

        if (!text && !del) {
            ++stats.emptyCommits;
            return;
        }

        const auto range = del.value_or(std::pair<std::uint32_t, std::uint32_t>{0, 0});
        SyntheticField& target = fields[*focused];
        const EditResult result = applyImeEdit(target, range.first, range.second,
                                               text ? std::string_view(*text) : std::string_view{});
        if (result != EditResult::Applied) {
            ++stats.invalidRejected;
            return;
        }
        ++stats.applied;
        // The application reports its new state back through the compositor.
        if (im.active) {
            sendState(im, target, tiv3::kCauseInputMethod);
            sendDone(im);
        }
    }

    // ---- request handlers (C callbacks; noexcept: no exception crosses libwayland) ----

    static void rejectRequest(wl_client* client, const char* request) noexcept
    {
        wl_client_post_implementation_error(client, "input lab rejects unsupported request %s",
                                            request);
    }

    static ImObject* imFrom(wl_resource* resource) noexcept
    {
        return static_cast<ImObject*>(wl_resource_get_user_data(resource));
    }

    static void seatGetPointer(wl_client* client, wl_resource*, std::uint32_t) noexcept
    {
        rejectRequest(client, "wl_seat.get_pointer");
    }
    static void seatGetKeyboard(wl_client* client, wl_resource*, std::uint32_t) noexcept
    {
        rejectRequest(client, "wl_seat.get_keyboard");
    }
    static void seatGetTouch(wl_client* client, wl_resource*, std::uint32_t) noexcept
    {
        rejectRequest(client, "wl_seat.get_touch");
    }
    static void seatRelease(wl_client*, wl_resource* resource) noexcept
    {
        wl_resource_destroy(resource);
    }

    static void seatBind(wl_client* client, void* data, std::uint32_t version,
                         std::uint32_t id) noexcept
    {
        static const struct wl_seat_interface impl = {&seatGetPointer, &seatGetKeyboard,
                                                      &seatGetTouch, &seatRelease};
        wl_resource* resource =
            wl_resource_create(client, &wl_seat_interface, static_cast<int>(version), id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }
        wl_resource_set_implementation(resource, &impl, data, nullptr);
        wl_seat_send_capabilities(resource, 0); // no pointer, keyboard or touch
    }

    static void imCommitString(wl_client* client, wl_resource* resource, const char* text) noexcept
    {
        ImObject* im = imFrom(resource);
        if (!im)
            return;
        try {
            im->pendingCommit = std::string(text ? text : "");
        } catch (const std::exception&) {
            wl_client_post_no_memory(client);
        }
    }
    static void imSetPreeditString(wl_client* client, wl_resource*, const char*, std::int32_t,
                                   std::int32_t) noexcept
    {
        rejectRequest(client, "zwp_input_method_v2.set_preedit_string");
    }
    static void imDeleteSurroundingText(wl_client*, wl_resource* resource, std::uint32_t before,
                                        std::uint32_t after) noexcept
    {
        if (ImObject* im = imFrom(resource))
            im->pendingDelete = std::pair<std::uint32_t, std::uint32_t>{before, after};
    }
    static void imCommit(wl_client* client, wl_resource* resource, std::uint32_t serial) noexcept
    {
        ImObject* im = imFrom(resource);
        if (!im)
            return;
        try {
            im->owner->onCommit(*im, serial);
        } catch (const std::exception&) {
            wl_client_post_no_memory(client);
        }
    }
    static void imGetInputPopupSurface(wl_client* client, wl_resource*, std::uint32_t,
                                       wl_resource*) noexcept
    {
        rejectRequest(client, "zwp_input_method_v2.get_input_popup_surface");
    }
    static void imGrabKeyboard(wl_client* client, wl_resource*, std::uint32_t) noexcept
    {
        rejectRequest(client, "zwp_input_method_v2.grab_keyboard");
    }
    static void imDestroy(wl_client*, wl_resource* resource) noexcept
    {
        wl_resource_destroy(resource);
    }

    static void imResourceDestroyed(wl_resource* resource) noexcept
    {
        ImObject* im = imFrom(resource);
        if (!im)
            return;
        Impl* self = im->owner;
        if (self->primary == im)
            self->primary = nullptr;
        auto& v = self->ims;
        v.erase(std::remove_if(v.begin(), v.end(), [im](const auto& p) { return p.get() == im; }),
                v.end());
    }

    static void managerGetInputMethod(wl_client* client, wl_resource* resource, wl_resource*,
                                      std::uint32_t id) noexcept
    {
        static const struct zwp_input_method_v2_interface impl = {
            &imCommitString, &imSetPreeditString, &imDeleteSurroundingText, &imCommit,
            &imGetInputPopupSurface, &imGrabKeyboard, &imDestroy};

        auto* self = static_cast<Impl*>(wl_resource_get_user_data(resource));
        wl_resource* imResource = wl_resource_create(client, &zwp_input_method_v2_interface,
                                                     wl_resource_get_version(resource), id);
        if (!imResource) {
            wl_client_post_no_memory(client);
            return;
        }
        ImObject* raw = nullptr;
        try {
            auto object = std::make_unique<ImObject>();
            object->owner = self;
            object->resource = imResource;
            raw = object.get();
            self->ims.push_back(std::move(object));
        } catch (const std::exception&) {
            wl_resource_destroy(imResource);
            wl_client_post_no_memory(client);
            return;
        }
        wl_resource_set_implementation(imResource, &impl, raw, &imResourceDestroyed);

        if (!self->primary) {
            self->primary = raw;
        } else {
            // Spec: only event on an object created while the seat already has one.
            raw->inert = true;
            zwp_input_method_v2_send_unavailable(imResource);
        }
    }
    static void managerDestroy(wl_client*, wl_resource* resource) noexcept
    {
        wl_resource_destroy(resource);
    }

    static void managerBind(wl_client* client, void* data, std::uint32_t version,
                            std::uint32_t id) noexcept
    {
        static const struct zwp_input_method_manager_v2_interface impl = {&managerGetInputMethod,
                                                                          &managerDestroy};
        wl_resource* resource = wl_resource_create(client, &zwp_input_method_manager_v2_interface,
                                                   static_cast<int>(version), id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }
        wl_resource_set_implementation(resource, &impl, data, nullptr);
    }
};

LabServer::LabServer(SerialPolicy policy) : d_(std::make_unique<Impl>(policy))
{
    // No wl_display_add_socket*: the display has no listening socket.
    d_->display = wl_display_create();
    if (!d_->display)
        return;
    d_->seatGlobal = wl_global_create(d_->display, &wl_seat_interface, kSeatVersion, d_.get(),
                                      &Impl::seatBind);
    d_->managerGlobal = wl_global_create(d_->display, &zwp_input_method_manager_v2_interface,
                                         kManagerVersion, d_.get(), &Impl::managerBind);
}

LabServer::~LabServer() = default;

bool LabServer::ok() const noexcept
{
    return d_->display && d_->seatGlobal && d_->managerGlobal;
}

SerialPolicy LabServer::policy() const noexcept
{
    return d_->policy;
}

int LabServer::createClientFd()
{
    if (!ok())
        return -1;
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
        return -1;
    if (!wl_client_create(d_->display, fds[0])) {
        // wl_client_create transfers ownership only on success (libwayland API).
        ::close(fds[0]);
        ::close(fds[1]);
        return -1;
    }
    return fds[1];
}

void LabServer::dispatch()
{
    if (!d_->display)
        return;
    wl_event_loop_dispatch(wl_display_get_event_loop(d_->display), 0);
    wl_display_flush_clients(d_->display);
}

std::optional<std::size_t> LabServer::addField(SyntheticField field)
{
    if (!isValidField(field))
        return std::nullopt;
    d_->fields.push_back(std::move(field));
    return d_->fields.size() - 1;
}

bool LabServer::focus(std::size_t fieldId)
{
    if (fieldId >= d_->fields.size())
        return false;
    Impl::ImObject* im = d_->liveIm();
    if (im && im->active)
        d_->sendDeactivate(*im);
    d_->focused = fieldId;
    if (im)
        d_->sendActivate(*im, d_->fields[fieldId]);
    return true;
}

bool LabServer::userTypes(std::string_view utf8)
{
    if (!d_->focused)
        return false;
    SyntheticField& target = d_->fields[*d_->focused];
    if (!insertAtCursor(target, utf8))
        return false;
    if (Impl::ImObject* im = d_->liveIm(); im && im->active) {
        d_->sendState(*im, target, tiv3::kCauseOther);
        d_->sendDone(*im);
    }
    return true;
}

bool LabServer::deactivate()
{
    Impl::ImObject* im = d_->liveIm();
    if (!im || !im->active)
        return false;
    d_->sendDeactivate(*im);
    return true;
}

bool LabServer::makeUnavailable()
{
    Impl::ImObject* im = d_->liveIm();
    if (!im)
        return false;
    zwp_input_method_v2_send_unavailable(im->resource);
    im->inert = true;
    im->active = false;
    d_->primary = nullptr; // inert object no longer represents the seat
    return true;
}

bool LabServer::hasInputMethod() const noexcept
{
    return d_->liveIm() != nullptr;
}

bool LabServer::active() const noexcept
{
    const Impl::ImObject* im = d_->liveIm();
    return im && im->active;
}

std::uint32_t LabServer::doneCount() const noexcept
{
    const Impl::ImObject* im = d_->liveIm();
    return im ? im->doneCount : 0;
}

std::size_t LabServer::imObjectCount() const noexcept
{
    return d_->ims.size();
}

std::optional<std::size_t> LabServer::focusedId() const noexcept
{
    return d_->focused;
}

const SyntheticField& LabServer::field(std::size_t fieldId) const
{
    return d_->fields.at(fieldId);
}

const LabServer::Stats& LabServer::stats() const noexcept
{
    return d_->stats;
}

} // namespace inputlab
