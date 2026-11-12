#include "labharness.h"

namespace inputlab {

LabHarness::LabHarness(SerialPolicy policy) : server_(policy) {}

LabHarness::~LabHarness()
{
    disconnectClient();
}

bool LabHarness::start()
{
    if (!server_.ok() || client_)
        return false;
    const int fd = server_.createClientFd();
    if (fd < 0)
        return false;
    client_ = std::make_unique<LabClient>(fd);
    if (!client_->connected())
        return false;
    // First round trip delivers globals (binds are queued from the callback),
    // the second delivers wl_seat.capabilities.
    if (!roundtrip() || !roundtrip())
        return false;
    return client_->hasGlobals() && client_->seatCapabilities() == std::optional<std::uint32_t>(0u);
}

bool LabHarness::roundtrip()
{
    if (!client_ || !client_->connected())
        return false;
    const auto token = client_->requestSync();
    if (!token)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + kRoundtripBudget;
    for (int i = 0; i < kMaxPumpIterations; ++i) {
        if (!client_->flush())
            return false;
        server_.dispatch();
        if (!client_->readAndDispatch(kPollSliceMs))
            return false;
        if (client_->syncCompleted(*token))
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
    }
    return false;
}

void LabHarness::disconnectClient()
{
    if (!client_)
        return;
    client_.reset();
    // Hang-up is reported on the next dispatch; a second pass is harmless.
    server_.dispatch();
    server_.dispatch();
}

} // namespace inputlab
