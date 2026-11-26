#!/usr/bin/env python3
"""Admiral 1506 - erzeugt die eingebauten Drum-Kits per Klangsynthese.

Alle Klänge werden hier mathematisch erzeugt (Sinus, Rechteck, Rauschen, Filter, Hüllkurven).
Es werden keine fremden Samples verwendet - die Kits gehören damit vollständig dem Projekt.

Aufruf:  python3 tools/make_kits.py Resources/Kits
Ergebnis: <Kit>_<Instrument>.wav (44,1 kHz, 16 Bit, mono)
"""
import sys
import os
import numpy as np
from scipy.signal import butter, sosfilt
import wave

SR = 44100
rng = np.random.default_rng(1507)  # Zufalls-Seed (keine Modellnummer)


def t_axis(sec):
    return np.arange(int(SR * sec)) / SR


def env_exp(sec, decay, attack=0.0005):
    t = t_axis(sec)
    e = np.exp(-t / decay)
    a = np.clip(t / attack, 0, 1) if attack > 0 else 1
    return e * a


def bp(x, lo, hi, order=2):
    sos = butter(order, [lo, hi], btype="band", fs=SR, output="sos")
    return sosfilt(sos, x)


def hp(x, f, order=2):
    return sosfilt(butter(order, f, btype="high", fs=SR, output="sos"), x)


def lp(x, f, order=2):
    return sosfilt(butter(order, f, btype="low", fs=SR, output="sos"), x)


def noise(sec):
    return rng.uniform(-1, 1, int(SR * sec))


def sweep_sine(sec, f0, f1, tau):
    """Sinus mit exponentiell fallender Tonhöhe f0 -> f1 (Zeitkonstante tau)."""
    t = t_axis(sec)
    f = f1 + (f0 - f1) * np.exp(-t / tau)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph)


def square(sec, f):
    t = t_axis(sec)
    return np.sign(np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)))


def metal(sec, freqs):
    """808-artiges Metall: sechs verstimmte Rechtecke."""
    return sum(square(sec, f) for f in freqs) / len(freqs)


def soft_clip(x, drive=1.0):
    return np.tanh(x * drive) / np.tanh(drive)


def fade_out(x, ms=4):
    n = int(SR * ms / 1000)
    if len(x) > n:
        x[-n:] *= np.linspace(1, 0, n)
    return x


def normalise(x, peak=0.89):
    m = np.max(np.abs(x))
    return x / m * peak if m > 0 else x


METAL_808 = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
METAL_909 = [263.0, 400.0, 421.0, 531.0, 588.0, 852.0]


# ---------------------------------------------------------------- Kit ANALOG (weich, boomig, 80er)
def analog():
    k = {}
    s = 1.2
    k["Kick"] = sweep_sine(s, 140, 47, 0.035) * env_exp(s, 0.42) + 0.25 * lp(noise(s), 3000) * env_exp(s, 0.003)

    s = 0.45
    tone = 0.6 * sweep_sine(s, 260, 185, 0.02) * env_exp(s, 0.07) + 0.35 * sweep_sine(s, 410, 330, 0.02) * env_exp(s, 0.05)
    nz = bp(noise(s), 1800, 9000) * env_exp(s, 0.12)
    k["Snare"] = tone + 0.9 * nz

    s = 0.6
    n = bp(noise(s), 900, 2600, 3)
    e = np.zeros(int(SR * s))
    for i, off in enumerate([0.0, 0.011, 0.022, 0.033]):
        st = int(off * SR)
        seg = env_exp(s - off, 0.007 if i < 3 else 0.17)
        e[st:st + len(seg)] = np.maximum(e[st:st + len(seg)], seg)
    k["Clap"] = n * e

    s = 0.25
    k["ClosedHat"] = hp(bp(metal(s, METAL_808), 3000, 14000), 7000) * env_exp(s, 0.035)

    s = 1.0
    k["OpenHat"] = hp(bp(metal(s, METAL_808), 3000, 14000), 7000) * env_exp(s, 0.26)

    s = 0.8
    k["LowTom"] = sweep_sine(s, 130, 92, 0.08) * env_exp(s, 0.24) + 0.05 * lp(noise(s), 2000) * env_exp(s, 0.02)
    s = 0.7
    k["HighTom"] = sweep_sine(s, 230, 165, 0.07) * env_exp(s, 0.19) + 0.05 * lp(noise(s), 3000) * env_exp(s, 0.02)

    s = 0.12
    k["Rim"] = (0.7 * np.sin(2 * np.pi * 1700 * t_axis(s)) + 0.5 * np.sin(2 * np.pi * 480 * t_axis(s))) * env_exp(s, 0.012) \
        + 0.3 * hp(noise(s), 4000) * env_exp(s, 0.004)

    s = 0.6
    k["Cowbell"] = bp(0.6 * square(s, 540) + 0.4 * square(s, 800), 400, 3500) * (0.6 * env_exp(s, 0.02) + 0.4 * env_exp(s, 0.16))

    s = 2.6
    k["Crash"] = hp(0.55 * metal(s, [f * 1.9 for f in METAL_808]) + 0.45 * noise(s), 4500) * env_exp(s, 0.75, 0.002)
    return k


# ---------------------------------------------------------------- Kit PUNCH (hart, trocken, 909-artig)
def punch():
    k = {}
    s = 0.7
    body = sweep_sine(s, 230, 52, 0.022) * env_exp(s, 0.2)
    click = hp(noise(s), 2000) * env_exp(s, 0.0025)
    k["Kick"] = soft_clip(1.25 * body + 0.35 * click, 1.8)

    s = 0.4
    tone = 0.55 * sweep_sine(s, 290, 205, 0.015) * env_exp(s, 0.05) + 0.3 * sweep_sine(s, 480, 360, 0.015) * env_exp(s, 0.04)
    nz = hp(noise(s), 2500) * env_exp(s, 0.13)
    k["Snare"] = soft_clip(tone + 1.0 * nz, 1.4)

    s = 0.45
    n = bp(noise(s), 1100, 4200, 3)
    e = np.zeros(int(SR * s))
    for i, off in enumerate([0.0, 0.008, 0.016, 0.026]):
        st = int(off * SR)
        seg = env_exp(s - off, 0.005 if i < 3 else 0.11)
        e[st:st + len(seg)] = np.maximum(e[st:st + len(seg)], seg)
    k["Clap"] = n * e

    s = 0.2
    k["ClosedHat"] = hp(0.6 * metal(s, METAL_909) + 0.4 * noise(s), 8500) * env_exp(s, 0.025)

    s = 0.9
    k["OpenHat"] = hp(0.6 * metal(s, METAL_909) + 0.4 * noise(s), 8000) * env_exp(s, 0.2)

    s = 0.6
    k["LowTom"] = soft_clip(sweep_sine(s, 190, 105, 0.03) * env_exp(s, 0.18) + 0.1 * hp(noise(s), 3000) * env_exp(s, 0.01), 1.3)
    s = 0.5
    k["HighTom"] = soft_clip(sweep_sine(s, 300, 190, 0.03) * env_exp(s, 0.14) + 0.1 * hp(noise(s), 3000) * env_exp(s, 0.01), 1.3)

    s = 0.1
    k["Rim"] = soft_clip((np.sin(2 * np.pi * 2100 * t_axis(s)) + 0.6 * np.sin(2 * np.pi * 620 * t_axis(s))) * env_exp(s, 0.008), 2.0)

    s = 0.45
    k["Cowbell"] = bp(0.55 * square(s, 587) + 0.45 * square(s, 845), 500, 4500) * (0.5 * env_exp(s, 0.012) + 0.5 * env_exp(s, 0.1))

    s = 2.2
    k["Crash"] = hp(0.35 * metal(s, [f * 2.3 for f in METAL_909]) + 0.65 * noise(s), 5500) * env_exp(s, 0.6, 0.001)
    return k


def write_wav(path, x):
    x = fade_out(normalise(np.asarray(x, dtype=np.float64)))
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "Resources/Kits"
    os.makedirs(out, exist_ok=True)
    for kit_name, fn in (("Analog", analog), ("Punch", punch)):
        for inst, x in fn().items():
            write_wav(os.path.join(out, f"{kit_name}_{inst}.wav"), x)
            print(f"{kit_name}_{inst}.wav  {len(x) / SR:.2f} s")


if __name__ == "__main__":
    main()
