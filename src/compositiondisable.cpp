// SPDX-License-Identifier: MIT
#include "compositiondisable.h"
#include <limits>
namespace rasklad::policy {
CompositionDisable::Token CompositionDisable::next() noexcept
{
    if (sequence_ == std::numeric_limits<Token>::max()) return 0;
    return ++sequence_;
}
void CompositionDisable::clear(State state) noexcept
{
    state_ = state;
    lease_ = {};
    generation_ = ticket_ = 0;
    nativeCompleted_ = false;
}
bool CompositionDisable::enable() noexcept
{
    if (state_ != State::Disabled) return false;
    state_ = State::Idle;
    return true;
}
Decision CompositionDisable::capture(Registry registry, const Permission &permission,
                                     const Context &context) const noexcept
{
    if (state_ == State::Idle) return authorizeCapture(registry, permission, context);
    if (state_ == State::Active) return revalidateCorrection(registry, permission, context, lease_);
    return {};
}
CompositionDisable::Token CompositionDisable::bind(Registry registry, const Permission &permission,
                                                   const Context &context) noexcept
{
    if (state_ != State::Idle) return 0;
    const auto decision = authorizeCapture(registry, permission, context);
    if (!decision.allowed()) return 0;
    const auto generation = next();
    if (!generation) { clear(State::Disabled); return 0; }
    lease_ = decision.lease;
    generation_ = generation;
    state_ = State::Active;
    return generation;
}
Decision CompositionDisable::correction(Registry registry, const Permission &permission,
                                        const Context &context) const noexcept
{
    if (state_ != State::Active) return {};
    return revalidateCorrection(registry, permission, context, lease_);
}
void CompositionDisable::requestOff() noexcept
{
    if (state_ == State::Idle) clear(State::Disabled);
    else if (state_ == State::Active) state_ = State::Pending;
}
CompositionDisable::Token CompositionDisable::takeOriginal(Registry registry,
                                                           const Context &context) noexcept
{
    if (state_ != State::Pending) return 0;
    state_ = State::Deferred;
    if (disableDisposition(registry, context, lease_, true) !=
        DisableAction::FinishOriginalThenDisable) return 0;
    ticket_ = next();
    if (!ticket_) return 0;
    state_ = State::InFlight;
    return ticket_;
}
bool CompositionDisable::complete(Token ticket, Result result) noexcept
{
    if (state_ != State::InFlight || !ticket || ticket != ticket_) return false;
    ticket_ = 0;
    if (result == Result::Completed || nativeCompleted_) clear(State::Disabled);
    else state_ = State::Deferred;
    return true;
}
bool CompositionDisable::nativeIdle(Token generation) noexcept
{
    if (!generation || generation != generation_) return false;
    if (state_ == State::InFlight) {
        // Native completion cannot release resources while external code runs.
        nativeCompleted_ = true;
        return true;
    }
    if (state_ == State::Active) clear(State::Idle);
    else if (state_ == State::Pending || state_ == State::Deferred)
        clear(State::Disabled);
    else return false;
    return true;
}
} // namespace rasklad::policy
