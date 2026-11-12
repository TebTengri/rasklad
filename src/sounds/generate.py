#!/usr/bin/env python3
"""Generate our original short PCM cue; no samples or external dependencies."""
import math, pathlib, struct, wave
rate = 48000
count = 3360  # 70 ms
samples = []
phase = 0.0
for i in range(count):
    t = i / rate
    # Two chip-like notes, band-limited harmonics and a click-free envelope.
    frequency = 1174.66 if t < 0.024 else 783.99
    phase += 2 * math.pi * frequency / rate
    envelope = min(1.0, t / 0.002) * (1 - i / (count - 1)) ** 2
    tone = (math.sin(phase) + math.sin(3 * phase) / 9 + math.sin(5 * phase) / 25) / 1.152
    samples.append(round(0.24 * 32767 * envelope * tone))
samples[0] = samples[-1] = 0
with wave.open(str(pathlib.Path(__file__).with_name('layout-switch.wav')), 'wb') as out:
    out.setparams((1, 2, rate, count, 'NONE', 'not compressed'))
    out.writeframes(struct.pack('<' + 'h' * count, *samples))
