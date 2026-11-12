#pragma once

#include <QElapsedTimer>
#include <QObject>

namespace rasklad::app {

// No audio device, text, layout observation or settings I/O here. The backend
// must supply a confirmed change; preview direction is not such a change.
class SoundFeedback final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool unavailable READ unavailable NOTIFY unavailableChanged)
public:
    enum class Layout { Unknown, Us, Ru };
    explicit SoundFeedback(QObject *parent = nullptr) : QObject(parent) {}
    bool enabled() const { return m_enabled; }
    bool unavailable() const { return m_unavailable; }
    void setEnabled(bool enabled);
    void noteUnavailable(bool unavailable);
    Q_INVOKABLE bool preview();
    bool notifyLayoutConfirmed(quint64 sequence, Layout before, Layout after);
Q_SIGNALS:
    void enabledChanged();
    void unavailableChanged();
    void requested();
private:
    bool trigger();
    bool m_enabled = true;
    bool m_unavailable = false;
    quint64 m_lastSequence = 0;
    QElapsedTimer m_sinceCue;
};

} // namespace rasklad::app
