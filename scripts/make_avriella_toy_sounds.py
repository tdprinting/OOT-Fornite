#!/usr/bin/env python3
"""Makes the sounds of Avriella's TV and laptop (all original, synthesised here: nothing is taken from any show) and writes shared/avriella_toy_sounds.h.

  assets/avriella/toy-sounds/*.wav   the sounds as 16-bit mono 16 kHz files (written by this script)
  assets/avriella/toy-sounds/custom_NN.wav   (optional) your own clips, 16-bit mono 16 kHz; each is played now and then in place of the jingle

Usage: make_avriella_toy_sounds.py   (needs numpy)
"""
import glob, os, wave
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, "assets", "avriella", "toy-sounds")
HEADER = os.path.join(ROOT, "shared", "avriella_toy_sounds.h")
R = 16000


def t(sec):
    return np.arange(int(sec * R)) / R


def env(n, attack=0.005, decay=6.0):
    x = np.arange(n) / R
    return np.minimum(1.0, x / attack) * np.exp(-decay * x)


def bell(freq, sec, decay=5.0):
    x = t(sec)
    return env(len(x), 0.003, decay) * (np.sin(2 * np.pi * freq * x) + 0.4 * np.sin(2 * np.pi * freq * 2.76 * x) * np.exp(-9 * x) + 0.2 * np.sin(2 * np.pi * freq * 5.4 * x) * np.exp(-14 * x))


def pluck(freq, sec):   # a small ukulele-ish string (Karplus-Strong)
    n = int(sec * R)
    period = int(R / freq)
    rng = np.random.default_rng(int(freq))
    buf = rng.uniform(-1, 1, period)
    out = np.zeros(n)
    for i in range(n):
        out[i] = buf[i % period]
        buf[i % period] = 0.5 * (buf[i % period] + buf[(i + 1) % period]) * 0.996
    return out


def xylo(freq, sec=0.45):   # a bright wooden xylophone bar
    x = t(sec)
    return env(len(x), 0.002, 11.0) * (np.sin(2 * np.pi * freq * x) + 0.35 * np.sin(2 * np.pi * freq * 3.9 * x) * np.exp(-25 * x))


def whistle(f0, f1, sec):   # a slide whistle with a little wobble
    x = t(sec)
    f = f0 + (f1 - f0) * (x / sec) ** 1.5
    ph = 2 * np.pi * np.cumsum(f + 12 * np.sin(2 * np.pi * 7 * x)) / R
    return np.minimum(1.0, x / 0.02) * np.minimum(1.0, (sec - x) / 0.04) * (np.sin(ph) + 0.15 * np.sin(2 * ph))


def boing(sec=0.55):   # a springy "boi-oi-oing"
    x = t(sec)
    f = 140 + 90 * np.sin(2 * np.pi * 9 * x) * np.exp(-3.5 * x) + 40 * np.exp(-4 * x)
    ph = 2 * np.pi * np.cumsum(f) / R
    return env(len(x), 0.004, 4.5) * (np.sin(ph) + 0.5 * np.sin(2 * ph) + 0.25 * np.sin(3 * ph))


def bonk(sec=0.25):
    x = t(sec)
    return env(len(x), 0.001, 20.0) * (np.sin(2 * np.pi * (220 - 400 * x) * x) + 0.3 * np.random.default_rng(3).uniform(-1, 1, len(x)) * np.exp(-60 * x))


def place(track, clip, at):
    i = int(at * R)
    track[i:i + len(clip)] += clip[:len(track) - i]


def hz(note):   # note names like "C4"
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    return 440.0 * 2 ** ((names[note[0]] + 12 * (int(note[1]) - 4) - 9) / 12)


def tune():
    """Eight bars of a bouncy, made-up tune: a xylophone melody over a plucked bass, about 8.5 seconds."""
    beat = 0.26
    melody = "E4 G4 C5 G4 | A4 G4 E4 C4 | D4 F4 A4 F4 | G4 E4 C4 C4 | E4 G4 C5 E5 | D5 C5 A4 G4 | F4 A4 D5 C5 | C5 G4 E4 C4".replace("|", "").split()
    bass = ["C3", "C3", "F3", "C3", "C3", "F3", "G3", "C3"]
    out = np.zeros(int((len(melody) * beat + 1.0) * R))
    for i, n in enumerate(melody):
        place(out, xylo(hz(n)) * 0.9, i * beat)
    for i, n in enumerate(bass):
        place(out, pluck(hz(n), 0.9) * 0.7, i * 4 * beat)
        place(out, pluck(hz(n), 0.5) * 0.4, i * 4 * beat + 2 * beat)
    place(out, bell(hz("C6"), 1.0, 4.0) * 0.35, len(melody) * beat - 0.1)   # a little ding to end on
    return out


def sweep(f0, f1, sec, decay=7.0):
    x = t(sec)
    ph = 2 * np.pi * (f0 * x + (f1 - f0) * x * x / (2 * sec))
    return env(len(x), 0.004, decay) * np.sin(ph)


def sounds():
    s = {}
    s["pop"] = np.concatenate([sweep(250, 900, 0.16, 6.0), np.zeros(int(0.02 * R))]) * 0.9 + 0.0
    two = np.zeros(int(1.0 * R)); place(two, bell(hz("E6"), 0.7), 0.0); place(two, bell(hz("A6"), 0.9), 0.14)
    s["chime"] = two * 0.7                                  # the TV or laptop wakes up
    s["off"] = sweep(900, 200, 0.22, 9.0) * 0.8              # and goes to sleep
    b1 = np.zeros(int(0.5 * R)); place(b1, bell(hz("G5"), 0.3, 9.0), 0.0); place(b1, bell(hz("C6"), 0.3, 9.0), 0.1)
    s["boop"] = b1 * 0.7                                     # a happy little boop from the laptop
    s["tune"] = tune()
    s["slide"] = whistle(500, 1900, 0.8) * 0.8
    s["boing"] = boing() * 0.9
    s["tada"] = np.concatenate([np.zeros(1), (lambda o: o)(np.zeros(int(1.2 * R)))])
    place(s["tada"], xylo(hz("G5"), 0.3), 0.0); place(s["tada"], xylo(hz("G5"), 0.3), 0.12); place(s["tada"], bell(hz("C6"), 1.0, 3.5), 0.26); place(s["tada"], bell(hz("E6"), 1.0, 3.5), 0.26)
    s["bonk"] = bonk() * 0.9
    s["xylo_run"] = np.zeros(int(0.9 * R))
    for i, n in enumerate(["C5", "D5", "E5", "G5", "A5", "C6"]):
        place(s["xylo_run"], xylo(hz(n), 0.4), i * 0.08)
    return s


def main():
    os.makedirs(DIR, exist_ok=True)
    clips = {}
    for name, x in sounds().items():
        x = np.asarray(x, dtype=np.float64)
        x = x / max(1e-9, np.abs(x).max()) * 0.8
        f = int(0.01 * R); x[-f:] *= np.linspace(1, 0, f)
        pcm = np.round(x * 32767).astype("<i2")
        clips[name] = pcm
        with wave.open(os.path.join(DIR, name + ".wav"), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(R); w.writeframes(pcm.tobytes())
    custom = []
    for p in sorted(glob.glob(os.path.join(DIR, "custom_*.wav"))):
        w = wave.open(p)
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, R), p + ": need 16-bit mono 16 kHz"
        custom.append((os.path.splitext(os.path.basename(p))[0], np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")))
    out = ["// Generated by scripts/make_avriella_toy_sounds.py from assets/avriella/toy-sounds/*.wav. Do not edit.",
           "// The TV and laptop sounds (original, synthesised in the style of a children's TV show: xylophone, slide whistle, boing, bonk; 16-bit mono 16 kHz) and any custom_NN.wav clips dropped in that folder.",
           "#pragma once", "#include <cstdint>", "namespace royale {", "namespace avriella_toy_snd {", "constexpr int kRate = 16000;"]
    names = ["pop", "chime", "off", "boop", "tune", "slide", "boing", "tada", "bonk", "xylo_run"]
    for n in names + [c[0] for c in custom]:
        pcm = clips[n] if n in clips else dict(custom)[n]
        out.append("inline constexpr int16_t k_%s[%d] = {" % (n, len(pcm)))
        for i in range(0, len(pcm), 40):
            out.append("    " + ",".join(str(int(v)) for v in pcm[i:i + 40]) + ",")
        out.append("};")
    out.append("struct Clip { const int16_t* data; int count; };")
    out.append("enum Sfx { kPop, kChime, kOff, kBoop, kTune, kSlide, kBoing, kTada, kBonk, kXyloRun, kSfxCount };")
    out.append("inline constexpr Clip kClips[kSfxCount] = {")
    for n in names:
        out.append("    { k_%s, static_cast<int>(sizeof(k_%s) / sizeof(int16_t)) }," % (n, n))
    out.append("};")
    out.append("constexpr int kCustomCount = %d;" % len(custom))
    out.append("inline constexpr Clip kCustom[%d] = {" % max(1, len(custom)))
    for n, _ in custom:
        out.append("    { k_%s, static_cast<int>(sizeof(k_%s) / sizeof(int16_t)) }," % (n, n))
    if not custom:
        out.append("    { nullptr, 0 },")
    out.append("};")
    out.append("} // namespace avriella_toy_snd")
    out.append("} // namespace royale")
    with open(HEADER, "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", HEADER, {n: round(len(clips[n]) / R, 2) for n in names}, "custom:", len(custom))


main()
