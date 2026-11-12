#include "soundfeedback.h"

namespace rasklad::app {
void SoundFeedback::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
}
void SoundFeedback::noteUnavailable(bool unavailable)
{
    if (m_unavailable == unavailable) return;
    m_unavailable = unavailable;
    Q_EMIT unavailableChanged();
}
bool SoundFeedback::trigger()
{
    if (!m_enabled || (m_sinceCue.isValid() && m_sinceCue.elapsed() < 180)) return false;
    m_sinceCue.start();
    noteUnavailable(false);
    Q_EMIT requested();
    return true;
}
bool SoundFeedback::preview() { return trigger(); }
bool SoundFeedback::notifyLayoutConfirmed(quint64 sequence, Layout before, Layout after)
{
    if (sequence == 0 || sequence <= m_lastSequence) return false;
    // Consume identities even while muted: re-enabling must not replay history.
    m_lastSequence = sequence;
    const bool knownBefore = before == Layout::Us || before == Layout::Ru;
    const bool knownAfter = after == Layout::Us || after == Layout::Ru;
    if (!knownBefore || !knownAfter || before == after) return false;
    return trigger();
}
} // namespace rasklad::app
