#!/usr/bin/env python3
"""Generate greybox placeholder SFX (16-bit mono WAV) for claude_rts.

Pure stdlib (wave + math + struct + random). Run:

    python3 tools/gen_sfx.py

Writes 6 WAVs into game/assets/sfx/. These are programmer-art placeholders
to give the prototype audible feedback; swap for real CC0 SFX later (T's lane).
Event sounds (hit/train_done/death) are triggered by the sim event channel
(sim_drain_events); command sounds (cmd_*) are played by the view on input.
"""
import math
import os
import random
import struct
import wave

RATE = 22050
OUTDIR = os.path.join(os.path.dirname(__file__), "..", "game", "assets", "sfx")


def env(i, n, attack=0.01, release=0.5):
    """Attack/sustain/release amplitude envelope in [0, 1]."""
    t = i / n
    a = (t / attack) if (attack > 0 and t < attack) else 1.0
    rel_start = 1.0 - release
    r = (1.0 - (t - rel_start) / release) if (release > 0 and t > rel_start) else 1.0
    return max(0.0, min(a, r))


def square(x):
    return 1.0 if math.sin(x) >= 0 else -1.0


def tone(freq, dur, vol=0.3, wave_fn=math.sin, attack=0.005, release=0.6):
    n = int(RATE * dur)
    return [vol * wave_fn(2 * math.pi * freq * (i / RATE)) * env(i, n, attack, release)
            for i in range(n)]


def sweep(f0, f1, dur, vol=0.3, wave_fn=math.sin, attack=0.005, release=0.6):
    n = int(RATE * dur)
    out = []
    for i in range(n):
        t = i / RATE
        f = f0 + (f1 - f0) * (i / n)
        out.append(vol * wave_fn(2 * math.pi * f * t) * env(i, n, attack, release))
    return out


def noise(dur, vol=0.4, release=0.85):
    random.seed(42)  # fixed seed -> committed WAV is reproducible
    n = int(RATE * dur)
    return [vol * random.uniform(-1.0, 1.0) * env(i, n, 0.001, release) for i in range(n)]


def concat(*segs):
    out = []
    for s in segs:
        out.extend(s)
    return out


def write_wav(name, samples):
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, name)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(
            struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples
        )
        w.writeframes(frames)
    print("wrote {} ({} samples)".format(path, len(samples)))


def main():
    # Command-issue sounds (played by the view on input).
    write_wav("cmd_move.wav", tone(440, 0.08, 0.22))                                  # soft blip
    write_wav("cmd_attack.wav", tone(660, 0.09, 0.28, square))                        # sharper blip
    write_wav("cmd_build.wav", concat(tone(330, 0.06, 0.24), tone(440, 0.11, 0.24)))  # two-tone rising
    # Event sounds (played from sim_drain_events).
    write_wav("hit.wav", noise(0.10, 0.32))                                           # noise thud
    write_wav("train_done.wav", concat(tone(523, 0.08, 0.26), tone(784, 0.15, 0.26)))  # rising chime
    write_wav("death.wav", sweep(440, 150, 0.24, 0.28, square))                       # descending blip


if __name__ == "__main__":
    main()
