#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <memory>
class QSoundEffect;
namespace rasklad::app {
class SoundFeedback;
class RetroSoundPlayer final : public QObject
{
public:
    explicit RetroSoundPlayer(SoundFeedback *feedback, QObject *parent = nullptr);
    ~RetroSoundPlayer() override;
private:
    void request();
    void playIfReady();
    SoundFeedback *m_feedback;
    std::unique_ptr<QSoundEffect> m_effect;
    QElapsedTimer m_requestAge;
    bool m_pending = false;
};
} // namespace rasklad::app
