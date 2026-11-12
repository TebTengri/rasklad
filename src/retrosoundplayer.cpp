#include "retrosoundplayer.h"
#include "soundfeedback.h"
#include <QSoundEffect>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QUrl>

namespace rasklad::app {
RetroSoundPlayer::RetroSoundPlayer(SoundFeedback *feedback, QObject *parent)
    : QObject(parent), m_feedback(feedback)
{
    connect(feedback, &SoundFeedback::requested, this, &RetroSoundPlayer::request);
    connect(feedback, &SoundFeedback::enabledChanged, this, [this] {
        if (!m_feedback->enabled()) {
            m_pending = false;
            if (m_effect) m_effect->stop();
        }
    });
}
RetroSoundPlayer::~RetroSoundPlayer() = default;
void RetroSoundPlayer::request()
{
    if (!m_feedback->enabled()) return;
    m_pending = true;
    m_requestAge.start();
    if (!m_effect || m_effect->status() == QSoundEffect::Error) {
        // Lazy: normal launch and disabled sound never open an audio device.
        const QAudioDevice output = QMediaDevices::defaultAudioOutput();
        if (output.isNull()) {
            m_pending = false;
            m_feedback->noteUnavailable(true);
            return;
        }
        m_effect = std::make_unique<QSoundEffect>(output);
        m_effect->setLoopCount(1);
        m_effect->setVolume(0.45f);
        connect(m_effect.get(), &QSoundEffect::statusChanged, this, [this] {
            if (m_effect->status() == QSoundEffect::Error) {
                m_pending = false;
                m_feedback->noteUnavailable(true);
            } else {
                playIfReady();
            }
        });
        m_effect->setSource(QUrl(QStringLiteral("qrc:/sounds/layout-switch.wav")));
    }
    if (m_effect->status() == QSoundEffect::Error) {
        m_pending = false;
        m_feedback->noteUnavailable(true);
        return;
    }
    playIfReady();
}
void RetroSoundPlayer::playIfReady()
{
    if (!m_pending || !m_effect || m_effect->status() != QSoundEffect::Ready) return;
    m_pending = false;
    // No late surprise cue after slow loading, disabling or a device error.
    if (m_feedback->enabled() && m_requestAge.elapsed() <= 300)
        m_effect->play();
}
} // namespace rasklad::app
