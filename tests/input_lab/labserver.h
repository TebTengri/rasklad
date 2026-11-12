// SYS-223 input-method-v2 wire laboratory: fake compositor side.
// Real libwayland-server display WITHOUT any listening socket: clients are only
// created from one end of a socketpair (createClientFd). Globals: wl_seat v1 with
// capabilities 0 (no devices) and zwp_input_method_manager_v2 v1. A synthetic
// text field stands in for the application's text-input. Unsupported requests
// (preedit, popup surface, keyboard grab, seat devices) end the client with a
// protocol error. No wayland types leak into this header.
#pragma once

#include "labmodel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace inputlab {

class LabServer {
public:
    struct Stats {
        std::uint32_t applied = 0;             // edit reached the synthetic field
        std::uint32_t staleRejected = 0;       // Strict: serial != done count, state unchanged
        std::uint32_t staleForwarded = 0;      // UncheckedModel: stale serial applied anyway
        std::uint32_t inactiveIgnored = 0;     // no eligible field
        std::uint32_t invalidRejected = 0;     // range/UTF-8/size check failed, field unchanged
        std::uint32_t inertIgnored = 0;        // commit on an object that received unavailable
        std::uint32_t emptyCommits = 0;        // commit with no pending edit
        std::uint32_t pendingDroppedOnActivate = 0; // Strict: activate reset pending requests
    };

    explicit LabServer(SerialPolicy policy);
    ~LabServer();
    LabServer(const LabServer&) = delete;
    LabServer& operator=(const LabServer&) = delete;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] SerialPolicy policy() const noexcept;

    // Returns the client end of a new socketpair (caller owns it) or -1.
    [[nodiscard]] int createClientFd();
    // Non-blocking: dispatch pending requests once, then flush events.
    void dispatch();

    // Synthetic compositor/application actions; each emits wire events.
    [[nodiscard]] std::optional<std::size_t> addField(SyntheticField field);
    // Moves focus: deactivate+done if active, then activate, surrounding_text,
    // text_change_cause, content_type, done.
    bool focus(std::size_t fieldId);
    // User types into the focused field; re-sends state with cause "other" + done.
    bool userTypes(std::string_view utf8);
    // Text input disabled; the field stays focused. Sends deactivate + done.
    bool deactivate();
    // Sends unavailable to the active input method; the object becomes inert.
    bool makeUnavailable();

    [[nodiscard]] bool hasInputMethod() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint32_t doneCount() const noexcept;
    [[nodiscard]] std::size_t imObjectCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> focusedId() const noexcept;
    [[nodiscard]] const SyntheticField& field(std::size_t fieldId) const;
    [[nodiscard]] const Stats& stats() const noexcept;

    struct Impl; // opaque; defined in labserver.cpp

private:
    std::unique_ptr<Impl> d_;
};

} // namespace inputlab
