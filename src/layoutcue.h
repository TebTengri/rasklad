// SPDX-License-Identifier: MIT
// SYS-226: the project's own short retro cue (docs/SOUND-001.md: 70 ms, two
// chip-like tones 1174.66/783.99 Hz) played when the Fcitx layout changes. The
// samples are synthesised here, no audio file. Playback uses ALSA "default" on a
// short worker thread so key handling never waits (non-blocking device, bounded
// wait, stopped on unload); at most one cue at a time and
// at least 180 ms apart. No recording, volume changes or retries.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace rasklad::cue {

// 48 kHz mono S16 samples of the SOUND-001 cue, scaled by gain (0..1).
std::vector<std::int16_t> synthesize(double gain);

class LayoutCue final
{
public:
    explicit LayoutCue(const char *device = "default"); // tests use the silent "null"
    ~LayoutCue(); // stops a cue in flight; the worker gives up within ~400 ms
    LayoutCue(const LayoutCue &) = delete;
    LayoutCue &operator=(const LayoutCue &) = delete;

    // Starts the cue unless one is playing or the last began < 180 ms ago.
    void play();
    // Main thread: ends a cue in flight within ~10 ms; the next play() starts anew.
    void stop() { m_stop.store(true); }
    bool busy() const { return m_busy.load(); } // for tests

private:
    const char *m_device;
    std::vector<std::int16_t> m_samples;
    std::thread m_worker;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_stop{false};
    std::chrono::steady_clock::time_point m_last{};
};

} // namespace rasklad::cue
