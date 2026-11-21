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

LayoutCue::LayoutCue() : m_samples(synthesize(0.45)) {}

LayoutCue::~LayoutCue()
{
    if (m_worker.joinable()) m_worker.join();
}

void LayoutCue::play()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_busy.load() || (m_last != std::chrono::steady_clock::time_point{} && now - m_last < kMinGap)) return;
    if (m_worker.joinable()) m_worker.join(); // finished: m_busy is false
    m_last = now;
    m_busy.store(true);
    try {
        m_worker = std::thread([this] {
            snd_pcm_t *pcm = nullptr;
            if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) == 0) {
                if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, kRate, 1,
                                       100000) == 0) {
                    snd_pcm_sframes_t n = snd_pcm_writei(pcm, m_samples.data(), m_samples.size());
                    if (n < 0) n = snd_pcm_recover(pcm, static_cast<int>(n), 1);
                    if (n >= 0) snd_pcm_drain(pcm);
                }
                snd_pcm_close(pcm);
            }
            m_busy.store(false);
        });
    } catch (...) {
        m_busy.store(false);
    }
}

} // namespace rasklad::cue
