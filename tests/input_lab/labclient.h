// SYS-223 input-method-v2 wire laboratory: input-method client side.
// Connects ONLY through a file descriptor handed over by LabServer
// (wl_display_connect_to_fd); never wl_display_connect / WAYLAND_DISPLAY.
// Records received events in order and keeps double-buffered state applied on
// done, as the protocol requires. No wayland types leak into this header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inputlab {

enum class ImEventKind { Activate, Deactivate, SurroundingText, TextChangeCause, ContentType, Done, Unavailable };

struct ImEvent {
    ImEventKind kind = ImEventKind::Done;
    std::string text;          // surrounding_text only
    std::uint32_t first = 0;   // cursor | cause | hint
    std::uint32_t second = 0;  // anchor | purpose
};

struct ImView {
    bool active = false;
    bool hasSurrounding = false;
    std::string surrounding;
    std::uint32_t cursor = 0;
    std::uint32_t anchor = 0;
    std::uint32_t cause = 0;
    std::uint32_t hint = 0;
    std::uint32_t purpose = 0;
};

struct ImRecord {
    std::vector<ImEvent> events;
    ImView pending;
    ImView current;            // replaced by pending on each done
    std::uint32_t doneSeen = 0; // serial for the next commit
    bool unavailable = false;
};

// Requests the fake server must refuse. Sent only to verify the refusal; the lab
// implements none of them.
enum class UnsupportedRequest { GrabKeyboard, SetPreeditString, SeatGetKeyboard };

class LabClient {
public:
    explicit LabClient(int fd); // takes ownership of fd
    ~LabClient();
    LabClient(const LabClient&) = delete;
    LabClient& operator=(const LabClient&) = delete;

    [[nodiscard]] bool connected() const noexcept; // display exists and has no error
    [[nodiscard]] int displayError() const noexcept;
    [[nodiscard]] bool hasGlobals() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> seatCapabilities() const noexcept;

    // Low-level pumping used by LabHarness.
    [[nodiscard]] bool flush();
    [[nodiscard]] bool readAndDispatch(int timeoutMs);
    [[nodiscard]] std::optional<std::uint64_t> requestSync();
    [[nodiscard]] bool syncCompleted(std::uint64_t token) const noexcept;

    [[nodiscard]] std::optional<std::size_t> createInputMethod();
    [[nodiscard]] const ImRecord& record(std::size_t im) const;

    // Requests are queued; they reach the server on the next flush.
    bool commitString(std::size_t im, std::string_view utf8);
    bool deleteSurroundingText(std::size_t im, std::uint32_t before, std::uint32_t after);
    bool commit(std::size_t im, std::uint32_t serial);
    bool commitWithLatestSerial(std::size_t im);
    bool sendUnsupported(std::size_t im, UnsupportedRequest request);

    struct Impl; // opaque; defined in labclient.cpp

private:
    std::unique_ptr<Impl> d_;
};

} // namespace inputlab
