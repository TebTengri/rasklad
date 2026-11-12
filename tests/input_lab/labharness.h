// SYS-223 input-method-v2 wire laboratory: single-threaded harness that owns the
// fake server and one IM client joined by a socketpair. Every pump is bounded by
// an iteration cap and a wall-clock budget; nothing blocks indefinitely.
#pragma once

#include "labclient.h"
#include "labserver.h"

#include <chrono>
#include <memory>

namespace inputlab {

class LabHarness {
public:
    static constexpr int kMaxPumpIterations = 256;
    static constexpr int kPollSliceMs = 5;
    static constexpr std::chrono::milliseconds kRoundtripBudget{2000};

    explicit LabHarness(SerialPolicy policy);
    ~LabHarness();
    LabHarness(const LabHarness&) = delete;
    LabHarness& operator=(const LabHarness&) = delete;

    // Connects the client, binds wl_seat and the manager, checks capabilities == 0.
    [[nodiscard]] bool start();
    // wl_display.sync round trip, pumping both ends; false on error or budget exhaustion.
    [[nodiscard]] bool roundtrip();
    // Closes the client connection without flushing queued requests, then lets
    // the server observe the hang-up.
    void disconnectClient();

    [[nodiscard]] LabServer& server() noexcept { return server_; }
    [[nodiscard]] LabClient& client() { return *client_; } // precondition: hasClient()
    [[nodiscard]] bool hasClient() const noexcept { return client_ != nullptr; }

private:
    LabServer server_;                  // destroyed last
    std::unique_ptr<LabClient> client_;
};

} // namespace inputlab
