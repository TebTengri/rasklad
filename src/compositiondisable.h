// SPDX-License-Identifier: MIT
#pragma once
#include "applicationpolicy.h"

namespace rasklad::policy {
// Pure single-threaded lifecycle kernel. Caller owns original text and performs
// adapter I/O. Tokens are instance-local; do not replace/copy a live controller.
class CompositionDisable final {
public:
    enum class State { Disabled, Idle, Active, Pending, InFlight, Deferred };
    enum class Result { Completed, NotWritten, Unknown, Submitted };
    // Submitted releases LOCAL ownership after one native submission attempt;
    // it never asserts delivery. Caller stages buffer/preedit release first.
    using Token = std::uint64_t;
    CompositionDisable() = default;
    CompositionDisable(const CompositionDisable &) = delete;
    CompositionDisable &operator=(const CompositionDisable &) = delete;
    State state() const noexcept { return state_; }
    bool enable() noexcept;
    Decision capture(Registry, const Permission &, const Context &) const noexcept;
    // Authorize freshly at bind; cached capture decisions cannot start composition.
    // The returned generation must accompany explicit adapter native-idle events.
    Token bind(Registry, const Permission &, const Context &) noexcept;
    Decision correction(Registry, const Permission &, const Context &) const noexcept;
    void requestOff() noexcept;
    // Revalidates immediately before caller invokes finish-original. A nonzero
    // ticket grants one attempt only. State changes before external code runs.
    Token takeOriginal(Registry, const Context &) noexcept;
    // Completed means adapter-confirmed native completion, not callback return.
    bool complete(Token ticket, Result) noexcept;
    bool nativeIdle(Token generation) noexcept;
    bool mayTeardown() const noexcept { return state_ == State::Disabled; }
private:
    State state_ = State::Disabled;
    Lease lease_;
    Token sequence_ = 0;
    Token generation_ = 0;
    Token ticket_ = 0;
    bool nativeCompleted_ = false;
    Token next() noexcept;
    void clear(State) noexcept;
};
} // namespace rasklad::policy
