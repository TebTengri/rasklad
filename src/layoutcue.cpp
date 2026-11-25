// SPDX-License-Identifier: MIT
#include "layoutcue.h"

#include <alsa/asoundlib.h>

#include <cmath>

namespace rasklad::cue {

namespace {
constexpr int kRate = 48000;
constexpr int kCount = 3360; // 70 ms
constexpr auto kMinGap = std::chrono::milliseconds(180);
} // namespace

std::vector<std::int16_t> synthesize(double gain)
{
    // Same formula as the SOUND-001 generator (src/sounds/generate.py, PR #15).
    std::vector<std::int16_t> out(kCount);
    double phase = 0.0;
    for (int i = 0; i < kCount; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double frequency = t < 0.024 ? 1174.66 : 783.99;
        phase += 2 * M_PI * frequency / kRate;
        const double fade = 1.0 - static_cast<double>(i) / (kCount - 1);
        const double envelope = std::min(1.0, t / 0.002) * fade * fade;
        const double tone = (std::sin(phase) + std::sin(3 * phase) / 9 + std::sin(5 * phase) / 25) / 1.152;
        out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::lround(gain * 0.24 * 32767 * envelope * tone));
    }
    out.front() = out.back() = 0;
    return out;
}

LayoutCue::LayoutCue(const char *device) : m_device(device), m_samples(synthesize(0.45)) {}

LayoutCue::~LayoutCue()
{
    m_stop.store(true);
    if (m_worker.joinable()) m_worker.join();
}

void LayoutCue::play()
{
    // Main (Fcitx) thread only. The previous worker is joined only once it has
    // finished (m_busy false), so this never waits on audio.
    const auto now = std::chrono::steady_clock::now();
    if (m_busy.load() || (m_last != std::chrono::steady_clock::time_point{} && now - m_last < kMinGap)) return;
    if (m_worker.joinable()) m_worker.join();
    m_stop.store(false); // no worker runs now: a stop() before this play() is spent
    m_busy.store(true);
    try {
        m_worker = std::thread([this] {
            snd_pcm_t *pcm = nullptr;
            // Non-blocking: a busy or hung sound server never stalls this worker.
            if (snd_pcm_open(&pcm, m_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) == 0) {
                if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, kRate, 1,
                                       200000) == 0 &&
                    snd_pcm_writei(pcm, m_samples.data(), m_samples.size()) > 0) {
                    // 70 ms is below the default start threshold: start explicitly.
                    if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm);
                    // Let it play: at most ~400 ms, or until stopped on unload.
                    for (int i = 0; i < 40 && !m_stop.load(); ++i) {
                        if (snd_pcm_state(pcm) != SND_PCM_STATE_RUNNING) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                snd_pcm_drop(pcm);
                snd_pcm_close(pcm);
            }
            m_busy.store(false);
        });
        m_last = now;
    } catch (...) {
        m_busy.store(false);
    }
}

} // namespace rasklad::cue
