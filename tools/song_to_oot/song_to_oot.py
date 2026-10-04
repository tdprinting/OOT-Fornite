#!/usr/bin/env python3
"""Song to Ocarina of Time.

Takes any song (mp3, wav, flac, ogg, m4a... or a MIDI file), works out which notes are being played, and plays them again with the instruments of a
SoundFont (an .sf2 file): use the Ocarina of Time one and it sounds like the game. Writes a .wav and an .mp3 (and the .mid it worked out).

    python song_to_oot.py song.mp3 --soundfont oot.sf2
    python song_to_oot.py song.mp3 --soundfont oot.sf2 --melody ocarina --harmony harp --bass bass
    python song_to_oot.py --list-presets --soundfont oot.sf2       (shows the instruments in the soundfont)
    python song_to_oot.py                                            (no arguments: pick the files in a small window)

Needs: Python 3.8+, numpy (pip install numpy), and ffmpeg on the PATH (to read and write mp3). No other packages.

About the soundfont: the Ocarina of Time instruments are the game's own samples, so they come from your copy of the game, not from this program.
Use an Ocarina of Time .sf2 you already have, or one made from your ROM's audio. Without --soundfont the tool still runs, with a plain built-in
ocarina-like sound so you can hear the transcription, but that is NOT the game's sound.

How it works, honestly: finding the notes in a finished recording is hard (the computer has to un-mix the song). This does a good job on clear melodies
and simple music and a rougher job on dense, loud mixes: expect the right tune and harmony, not a perfect copy. A MIDI file as input skips that step
and is rebuilt exactly.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave

try:
    import numpy as np
except ImportError:
    sys.exit("This tool needs numpy: run   pip install numpy")

SR_ANALYSIS = 22050
SR_OUT = 44100


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Reading audio

def need_ffmpeg():
    if shutil.which("ffmpeg") is None:
        sys.exit("ffmpeg was not found. Install it (https://ffmpeg.org) and make sure it is on the PATH.")


def load_audio(path, sr=SR_ANALYSIS):
    need_ffmpeg()
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", str(sr), "-"]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0 or not proc.stdout:
        sys.exit("Could not read %s: %s" % (path, proc.stderr.decode(errors="replace").strip()))
    return np.frombuffer(proc.stdout, dtype=np.float32).astype(np.float64)


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Finding the notes

class Note:
    __slots__ = ("start", "end", "pitch", "vel", "part")

    def __init__(self, start, end, pitch, vel, part="harmony"):
        self.start, self.end, self.pitch, self.vel, self.part = start, end, pitch, vel, part


LOW, HIGH = 31, 100            # G1 to E7: the notes that are looked for
N_FFT = 4096
HOP = 512


def stft_mag(y):
    pad = N_FFT // 2
    y = np.pad(y, (pad, pad))
    n = 1 + (len(y) - N_FFT) // HOP
    if n <= 0:
        return np.zeros((0, N_FFT // 2 + 1))
    win = np.hanning(N_FFT)
    idx = np.arange(N_FFT)[None, :] + HOP * np.arange(n)[:, None]
    frames = y[idx] * win
    return np.abs(np.fft.rfft(frames, axis=1))


def transcribe(y, sr=SR_ANALYSIS, max_voices=5, min_note_ms=90, sensitivity=0.22, progress=None):
    """Audio samples -> a list of Note. Harmonic-summing pitch salience with iterative cancellation, then note tracking."""
    mag = stft_mag(y)
    frames = mag.shape[0]
    if frames == 0:
        return []
    mag = np.log1p(40.0 * mag / (mag.max() + 1e-9))                 # compress: quiet notes count too
    # whiten each frequency a little so a loud bass does not hide everything
    mag = mag - 0.6 * np.minimum(mag, np.median(mag, axis=0, keepdims=True))
    mag = np.maximum(mag, 0)
    binhz = sr / N_FFT
    pitches = np.arange(LOW, HIGH + 1)
    f0 = 440.0 * 2.0 ** ((pitches - 69) / 12.0)
    nh = 7
    weights = 0.82 ** np.arange(nh)
    # for each pitch and harmonic: the bins within +- a third of a semitone
    centres = f0[:, None] * np.arange(1, nh + 1)[None, :] / binhz
    valid = centres < (N_FFT // 2 - 2)
    lo = np.clip(np.floor(centres * 2.0 ** (-1.0 / 36)).astype(int), 0, N_FFT // 2)
    hi = np.clip(np.ceil(centres * 2.0 ** (1.0 / 36)).astype(int), 0, N_FFT // 2)
    hi = np.maximum(hi, lo)

    def salience(m):
        s = np.zeros((m.shape[0], len(pitches)))
        for h in range(nh):
            best = np.zeros((m.shape[0], len(pitches)))
            span = int((hi[:, h] - lo[:, h]).max()) + 1
            for k in range(span):
                col = np.minimum(lo[:, h] + k, hi[:, h])
                best = np.maximum(best, m[:, col])
            s += weights[h] * best * valid[:, h][None, :]
        return s

    residual = mag.copy()
    roll = np.zeros((len(pitches), frames))
    gmax = None
    for voice in range(max_voices):
        s = salience(residual)
        if gmax is None:
            gmax = np.percentile(s.max(axis=1), 95) + 1e-9
        pick = s.argmax(axis=1)
        val = s[np.arange(frames), pick]
        take = val > sensitivity * gmax * (0.8 ** voice)
        for fr in np.nonzero(take)[0]:
            roll[pick[fr], fr] = max(roll[pick[fr], fr], val[fr] / gmax)
            c = centres[pick[fr]]
            for h in range(nh):
                if valid[pick[fr], h]:
                    residual[fr, lo[pick[fr], h]:hi[pick[fr], h] + 1] *= 0.15
        if progress:
            progress("voice %d/%d" % (voice + 1, max_voices))
        if not take.any():
            break
    # clean the piano roll: fill 1-2 frame gaps, then track runs of frames into notes
    on = roll > 0
    for p in range(len(pitches)):
        row = on[p]
        if not row.any():
            continue
        idx = np.nonzero(row)[0]
        gaps = np.nonzero(np.diff(idx) > 1)[0]
        for g in gaps:
            a, b = idx[g], idx[g + 1]
            if b - a <= 3:
                row[a:b + 1] = True
    notes = []
    sec = HOP / float(sr)
    min_frames = max(2, int(round(min_note_ms / 1000.0 / sec)))
    for p in range(len(pitches)):
        row = on[p]
        fr = 0
        while fr < frames:
            if not row[fr]:
                fr += 1
                continue
            a = fr
            while fr < frames and row[fr]:
                fr += 1
            b = fr
            if b - a < min_frames:
                continue
            level = roll[p, a:b]
            # split a long run where the level jumps up again (the same note struck twice)
            cut = [a]
            for k in range(a + 6, b - 3):
                before = roll[p, k - 4:k].mean() + 1e-9
                if roll[p, k] > 1.7 * before and roll[p, k] >= roll[p, k - 1] and k - cut[-1] >= 5:
                    cut.append(k)
            cut.append(b)
            for i in range(len(cut) - 1):
                seg = roll[p, cut[i]:cut[i + 1]]
                if len(seg) < min_frames:
                    continue
                vel = int(np.clip(45 + 80 * float(seg.max()), 35, 120))
                notes.append(Note((cut[i]) * sec, (cut[i + 1]) * sec, int(pitches[p]), vel))
    notes.sort(key=lambda n: (n.start, n.pitch))
    return notes


def assign_parts(notes, bass_below=50):
    """Bass is the low notes, melody the top voice, harmony what is left."""
    if not notes:
        return notes
    for n in notes:
        n.part = "bass" if n.pitch < bass_below else "harmony"
    # top voice: for each note, is it the highest sounding note for most of its length?
    live = [n for n in notes if n.part != "bass"]
    events = sorted(set([n.start for n in live] + [n.end for n in live]))
    top = {}
    for a, b in zip(events, events[1:]):
        mid = 0.5 * (a + b)
        best = None
        for n in live:
            if n.start <= mid < n.end and (best is None or n.pitch > best.pitch):
                best = n
        if best is not None:
            top[id(best)] = top.get(id(best), 0.0) + (b - a)
    for n in live:
        if top.get(id(n), 0.0) > 0.5 * (n.end - n.start):
            n.part = "melody"
    return notes


# ---------------------------------------------------------------------------------------------------------------------------------------------
# MIDI files

def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def write_midi(path, notes, programs=None):
    """notes with .part in melody/harmony/bass -> a format 1 MIDI file, one channel per part."""
    programs = programs or {"melody": 79, "harmony": 46, "bass": 32}
    ppq, tempo = 480, 500000                                   # 120 bpm: a second is 960 ticks
    tick = lambda t: int(round(t * 960))
    tracks = []
    meta = bytearray()
    meta += vlq(0) + b"\xff\x51\x03" + tempo.to_bytes(3, "big")
    meta += vlq(0) + b"\xff\x2f\x00"
    tracks.append(bytes(meta))
    for ch, part in enumerate(("melody", "harmony", "bass")):
        ev = []
        for n in notes:
            if n.part == part:
                ev.append((tick(n.start), 1, n.pitch, n.vel))
                ev.append((tick(n.end), 0, n.pitch, 0))
        ev.sort()
        data = bytearray()
        data += vlq(0) + bytes([0xC0 | ch, programs.get(part, 0) & 0x7F])
        last = 0
        for t, on, pitch, vel in ev:
            data += vlq(t - last) + bytes([(0x90 if on else 0x80) | ch, pitch & 0x7F, vel & 0x7F])
            last = t
        data += vlq(0) + b"\xff\x2f\x00"
        tracks.append(bytes(data))
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), ppq))
        for t in tracks:
            f.write(b"MTrk" + struct.pack(">I", len(t)) + t)


def read_midi(path):
    """A MIDI file -> notes in seconds (tempo changes honoured). Channel 10 (drums) is skipped; parts are guessed like for audio."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    fmt, ntracks, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("SMPTE time MIDI files are not supported")
    pos = 14
    events = []       # (tick, kind, a, b, c)
    for _ in range(ntracks):
        if data[pos:pos + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        i, end, tick, run = pos + 8, pos + 8 + ln, 0, 0
        while i < end:
            d = 0
            while True:
                b = data[i]; i += 1
                d = (d << 7) | (b & 0x7F)
                if not b & 0x80:
                    break
            tick += d
            st = data[i]
            if st == 0xFF:
                typ = data[i + 1]; i += 2
                l = 0
                while True:
                    b = data[i]; i += 1
                    l = (l << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                if typ == 0x51 and l == 3:
                    events.append((tick, "tempo", int.from_bytes(data[i:i + 3], "big"), 0, 0))
                i += l
                continue
            if st in (0xF0, 0xF7):
                i += 1
                l = 0
                while True:
                    b = data[i]; i += 1
                    l = (l << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                i += l
                continue
            if st & 0x80:
                run = st; i += 1
            kind = run & 0xF0
            ch = run & 0x0F
            if kind in (0xC0, 0xD0):
                i += 1
                continue
            a, b2 = data[i], data[i + 1]; i += 2
            if kind == 0x90 and b2 > 0:
                events.append((tick, "on", ch, a, b2))
            elif kind == 0x80 or (kind == 0x90 and b2 == 0):
                events.append((tick, "off", ch, a, 0))
        pos = end
    events.sort(key=lambda e: (e[0], 0 if e[1] == "tempo" else 1))
    tempo, last_tick, secs = 500000, 0, 0.0
    open_notes, notes = {}, []
    for tick, kind, a, b, c in events:
        secs += (tick - last_tick) * tempo / 1e6 / division
        last_tick = tick
        if kind == "tempo":
            tempo = a
        elif kind == "on" and a != 9:
            open_notes[(a, b)] = (secs, c)
        elif kind == "off" and (a, b) in open_notes:
            s, v = open_notes.pop((a, b))
            if secs - s > 0.03:
                notes.append(Note(s, secs, b, v))
    notes.sort(key=lambda n: (n.start, n.pitch))
    return notes


# ---------------------------------------------------------------------------------------------------------------------------------------------
# SoundFont (.sf2) reading and playing

def _cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1")


class SoundFont:
    """Enough of SoundFont 2 to play notes: presets, key/velocity zones, sample loops, tuning, volume envelope, pan, attenuation."""

    def __init__(self, path):
        data = open(path, "rb").read()
        if data[:4] != b"RIFF" or data[8:12] != b"sfbk":
            raise ValueError("%s is not a SoundFont (.sf2) file" % path)
        chunks = {}
        pos = 12
        while pos + 8 <= len(data):
            tag, ln = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
            body = data[pos + 8:pos + 8 + ln]
            if tag == b"LIST":
                sub = body[:4]
                p2 = 4
                while p2 + 8 <= len(body):
                    t2, l2 = body[p2:p2 + 4], struct.unpack("<I", body[p2 + 4:p2 + 8])[0]
                    chunks[sub + b":" + t2] = body[p2 + 8:p2 + 8 + l2]
                    p2 += 8 + l2 + (l2 & 1)
            pos += 8 + ln + (ln & 1)
        smpl = chunks.get(b"sdta:smpl")
        if smpl is None:
            raise ValueError("this SoundFont has no sample data (compressed .sf3 files are not supported)")
        self.pcm = np.frombuffer(smpl, dtype="<i2").astype(np.float32) / 32768.0
        sm24 = chunks.get(b"sdta:sm24")
        if sm24 is not None and len(sm24) >= len(self.pcm):
            self.pcm = self.pcm + np.frombuffer(sm24, dtype=np.uint8)[:len(self.pcm)].astype(np.float32) / (32768.0 * 256.0)
        pd = lambda name: chunks.get(b"pdta:" + name, b"")
        self.phdr = [(_cstr(r[:20]),) + struct.unpack("<HHH", r[20:26]) for r in (pd(b"phdr")[i:i + 38] for i in range(0, len(pd(b"phdr")) - 37, 38))]
        self.pbag = [struct.unpack("<HH", pd(b"pbag")[i:i + 4]) for i in range(0, len(pd(b"pbag")) - 3, 4)]
        self.pgen = [struct.unpack("<Hh", pd(b"pgen")[i:i + 4]) + (pd(b"pgen")[i + 2], pd(b"pgen")[i + 3]) for i in range(0, len(pd(b"pgen")) - 3, 4)]
        self.inst = [(_cstr(r[:20]),) + struct.unpack("<H", r[20:22]) for r in (pd(b"inst")[i:i + 22] for i in range(0, len(pd(b"inst")) - 21, 22))]
        self.ibag = [struct.unpack("<HH", pd(b"ibag")[i:i + 4]) for i in range(0, len(pd(b"ibag")) - 3, 4)]
        self.igen = [struct.unpack("<Hh", pd(b"igen")[i:i + 4]) + (pd(b"igen")[i + 2], pd(b"igen")[i + 3]) for i in range(0, len(pd(b"igen")) - 3, 4)]
        self.shdr = []
        for i in range(0, len(pd(b"shdr")) - 45, 46):
            r = pd(b"shdr")[i:i + 46]
            name = _cstr(r[:20])
            start, end, ls, le, rate = struct.unpack("<IIIII", r[20:40])
            root, corr = struct.unpack("<Bb", r[40:42])
            self.shdr.append(dict(name=name, start=start, end=end, ls=ls, le=le, rate=rate or 44100, root=root if root < 128 else 60, corr=corr))
        self._cache = {}

    # -- listing
    def presets(self):
        return [(bank, prog, name) for name, prog, bank, _ in self.phdr[:-1]]

    def find_preset(self, spec):
        """spec: 'bank:program', a program number, or part of a name (case does not matter)."""
        pres = self.presets()
        if isinstance(spec, str) and ":" in spec:
            b, p = spec.split(":", 1)
            for pr in pres:
                if pr[0] == int(b) and pr[1] == int(p):
                    return pr
        elif isinstance(spec, int) or (isinstance(spec, str) and spec.isdigit()):
            for pr in pres:
                if pr[1] == int(spec) and pr[0] == 0:
                    return pr
        else:
            s = spec.lower()
            for pr in pres:
                if s in pr[2].lower():
                    return pr
        return None

    # -- zones
    def _gens(self, gens, a, b):
        out = {}
        for oper, amount, lo, hi in gens[a:b]:
            if oper in (43, 44):
                out[oper] = (lo, hi)
            else:
                out[oper] = amount
        return out

    def zones(self, bank, prog):
        key = (bank, prog)
        if key in self._cache:
            return self._cache[key]
        zones = []
        for pi, (name, p, bk, bag) in enumerate(self.phdr[:-1]):
            if p != prog or bk != bank:
                continue
            end_bag = self.phdr[pi + 1][3]
            pglobal = {}
            for bi in range(bag, end_bag):
                g0, g1 = self.pbag[bi][0], self.pbag[bi + 1][0]
                pg = self._gens(self.pgen, g0, g1)
                if 41 not in pg:
                    pglobal = pg
                    continue
                pz = dict(pglobal)
                pz.update(pg)
                inst = pz[41]
                iend = self.inst[inst + 1][1]
                iglobal = {}
                for ib in range(self.inst[inst][1], iend):
                    i0, i1 = self.ibag[ib][0], self.ibag[ib + 1][0]
                    ig = self._gens(self.igen, i0, i1)
                    if 53 not in ig:
                        iglobal = ig
                        continue
                    iz = dict(iglobal)
                    iz.update(ig)
                    z = dict(iz)
                    for k, v in pz.items():           # preset level: ranges narrow, the others add
                        if k in (43, 44):
                            lo = max(z.get(k, (0, 127))[0], v[0]); hi = min(z.get(k, (0, 127))[1], v[1])
                            z[k] = (lo, hi)
                        elif k not in (41, 53, 58, 54):
                            z[k] = z.get(k, 0) + v
                    zones.append(z)
            break
        self._cache[key] = zones
        return zones

    # -- one note
    def render_note(self, bank, prog, key, vel, dur, sr=SR_OUT):
        """-> (left, right) float arrays for a note of the given length (the release tail is included)."""
        outs = []
        for z in self.zones(bank, prog):
            klo, khi = z.get(43, (0, 127)); vlo, vhi = z.get(44, (0, 127))
            if not (klo <= key <= khi and vlo <= vel <= vhi):
                continue
            sh = self.shdr[z[53]]
            start = sh["start"] + z.get(0, 0) + 32768 * z.get(4, 0)
            end = sh["end"] + z.get(1, 0) + 32768 * z.get(12, 0)
            ls = sh["ls"] + z.get(2, 0) + 32768 * z.get(45, 0)
            le = sh["le"] + z.get(3, 0) + 32768 * z.get(50, 0)
            if end <= start:
                continue
            root = z.get(58, -1)
            root = sh["root"] if root is None or root < 0 else root
            cents = (key - root) * z.get(56, 100) + z.get(51, 0) * 100 + z.get(52, 0) + sh["corr"]
            ratio = 2.0 ** (cents / 1200.0) * sh["rate"] / float(sr)
            loop = (z.get(54, 0) & 3) in (1, 3) and le > ls + 8
            tc = lambda v, default: 2.0 ** (z.get(v, default) / 1200.0)
            attack, hold, decay, release = min(tc(34, -12000), 20), min(tc(35, -12000), 20), min(tc(36, -12000), 60), min(tc(38, -12000), 20)
            release = max(release, 0.04)
            sustain_db = max(0.0, z.get(37, 0) / 10.0)
            total = dur + release
            n = int(total * sr) + 1
            pos = np.arange(n) * ratio + start
            if loop:
                span = le - ls
                over = pos >= le
                pos = np.where(over, ls + (pos - ls) % span, pos)
                gone = np.zeros(n, dtype=bool)
            else:
                gone = pos >= end - 1
                pos = np.minimum(pos, end - 2)
            i0 = pos.astype(np.int64)
            fr = (pos - i0).astype(np.float32)
            s = self.pcm[i0] * (1 - fr) + self.pcm[i0 + 1] * fr
            s[gone] = 0.0
            t = np.arange(n) / float(sr)
            env = np.where(t < attack, t / max(attack, 1e-4), 1.0)
            td = t - attack - hold
            decay_amount = np.clip(td / max(decay, 1e-3), 0, 1)
            env = np.where(td > 0, 10.0 ** (-(decay_amount * sustain_db) / 20.0), env)
            rel = np.clip((t - dur) / release, 0, 1)
            env = env * np.where(t > dur, 10.0 ** (-60.0 * rel / 20.0) * (1 - rel), 1.0)
            amp = 10.0 ** (-(z.get(48, 0) / 10.0) / 20.0) * (vel / 127.0) ** 1.2
            sig = (s * env * amp).astype(np.float32)
            pan = np.clip(z.get(17, 0) / 1000.0, -0.5, 0.5) + 0.5
            outs.append((sig * math.cos(pan * math.pi / 2), sig * math.sin(pan * math.pi / 2)))
        if not outs:
            return None
        n = max(len(o[0]) for o in outs)
        left = np.zeros(n, dtype=np.float32); right = np.zeros(n, dtype=np.float32)
        for l, r in outs:
            left[:len(l)] += l; right[:len(r)] += r
        return left, right


class StandInSynth:
    """No soundfont given: a plain ocarina-ish voice (a sine with a touch of vibrato and breath) so the tune can be heard. NOT the game's sound."""

    def render_note(self, bank, prog, key, vel, dur, sr=SR_OUT):
        f = 440.0 * 2.0 ** ((key - 69) / 12.0)
        n = int((dur + 0.15) * sr) + 1
        t = np.arange(n) / float(sr)
        vib = 1.0 + 0.004 * np.sin(2 * np.pi * 5.2 * t) * np.clip(t / 0.4, 0, 1)
        ph = 2 * np.pi * np.cumsum(f * vib) / sr
        sig = np.sin(ph) + 0.16 * np.sin(2 * ph) + 0.05 * np.sin(3 * ph)
        env = np.clip(t / 0.03, 0, 1) * np.where(t > dur, np.clip(1 - (t - dur) / 0.15, 0, 1), 1.0)
        sig = (sig * env * (vel / 127.0) * 0.35).astype(np.float32)
        return sig, sig


def reverb(left, right, amount, sr=SR_OUT):
    if amount <= 0:
        return left, right
    rng = np.random.RandomState(7)
    n = int(sr * 1.4)
    t = np.arange(n) / float(sr)
    ir = (rng.randn(n) * np.exp(-t * 3.4)).astype(np.float32)
    ir[:int(0.012 * sr)] = 0
    size = 1 << int(math.ceil(math.log2(len(left) + n)))
    out = []
    for ch, x in enumerate((left, right)):
        ir_c = np.roll(ir, 37 * ch)
        wet = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir_c, size), size)[:len(x)]
        out.append(x * (1 - 0.5 * amount) + wet * amount * 0.12)
    return out[0].astype(np.float32), out[1].astype(np.float32)


def render(notes, font, programs, reverb_amount=0.25, sr=SR_OUT, progress=None):
    """notes (with .part) -> stereo float array (n, 2)."""
    if not notes:
        raise ValueError("no notes were found in the song")
    total = max(n.end for n in notes) + 3.0
    left = np.zeros(int(total * sr), dtype=np.float32)
    right = np.zeros(int(total * sr), dtype=np.float32)
    gain = {"melody": 1.0, "harmony": 0.55, "bass": 0.8}
    for i, n in enumerate(notes):
        bank, prog = programs.get(n.part, (0, 0))
        r = font.render_note(bank, prog, n.pitch, n.vel, max(0.05, n.end - n.start), sr)
        if r is None:
            continue
        a = int(n.start * sr)
        g = gain.get(n.part, 0.7)
        l, rr = r
        m = min(len(l), len(left) - a)
        if m <= 0:
            continue
        left[a:a + m] += l[:m] * g
        right[a:a + m] += rr[:m] * g
        if progress and i % 200 == 0:
            progress("playing the notes %d/%d" % (i, len(notes)))
    left, right = reverb(left, right, reverb_amount, sr)
    peak = max(float(np.abs(left).max()), float(np.abs(right).max()), 1e-6)
    out = np.stack([left, right], axis=1) * (0.89 / peak)
    return out


def write_wav(path, stereo, sr=SR_OUT):
    pcm = np.clip(stereo * 32767.0, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def write_mp3(wav_path, mp3_path):
    need_ffmpeg()
    proc = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wav_path, "-codec:a", "libmp3lame", "-q:a", "2", mp3_path], stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg could not write the mp3: " + proc.stderr.decode(errors="replace"))


# ---------------------------------------------------------------------------------------------------------------------------------------------

def pick_programs(font, args):
    """Which soundfont instrument plays each part. Defaults look for an ocarina, a harp and a bass by name, then fall back to the first instruments."""
    if font is None:
        return {"melody": (0, 0), "harmony": (0, 0), "bass": (0, 0)}
    pres = font.presets()
    if not pres:
        raise ValueError("the soundfont has no instruments")
    wanted = {"melody": args.melody, "harmony": args.harmony, "bass": args.bass}
    fallback_names = {"melody": ["ocarina", "flute", "recorder"], "harmony": ["harp", "lute", "guitar", "piano"], "bass": ["bass", "pizz", "cello"]}
    out = {}
    for part, spec in wanted.items():
        pr = font.find_preset(spec) if spec else None
        if spec and pr is None:
            raise ValueError("no instrument called %r in the soundfont (try --list-presets)" % spec)
        if pr is None:
            for nm in fallback_names[part]:
                pr = font.find_preset(nm)
                if pr:
                    break
        if pr is None:
            pr = pres[min({"melody": 0, "harmony": 1, "bass": 2}[part], len(pres) - 1)]
        out[part] = (pr[0], pr[1])
    return out


def convert(path, out_base, soundfont=None, args=None, log=print):
    args = args or argparse.Namespace(melody=None, harmony=None, bass=None, reverb=0.25, voices=5, min_note_ms=90, sensitivity=0.22)
    font = SoundFont(soundfont) if soundfont else None
    if font is None:
        log("No soundfont given: using the plain built-in voice (NOT the Ocarina of Time sound). Pass --soundfont oot.sf2 for the real thing.")
    ext = os.path.splitext(path)[1].lower()
    if ext in (".mid", ".midi"):
        log("Reading the MIDI file...")
        notes = read_midi(path)
    else:
        log("Reading the song...")
        y = load_audio(path)
        log("Finding the notes (this takes a while for long songs)...")
        notes = transcribe(y, max_voices=args.voices, min_note_ms=args.min_note_ms, sensitivity=args.sensitivity, progress=lambda m: log("  " + m))
    if not notes:
        raise ValueError("no notes were found. If the song is very quiet, try --sensitivity 0.1")
    assign_parts(notes)
    programs = pick_programs(font, args)
    log("Found %d notes (%d melody, %d harmony, %d bass)." % (len(notes), sum(n.part == "melody" for n in notes), sum(n.part == "harmony" for n in notes), sum(n.part == "bass" for n in notes)))
    write_midi(out_base + ".mid", notes)
    stereo = render(notes, font or StandInSynth(), programs, args.reverb, progress=lambda m: log("  " + m))
    write_wav(out_base + ".wav", stereo)
    write_mp3(out_base + ".wav", out_base + ".mp3")
    log("Done:\n  %s.wav\n  %s.mp3\n  %s.mid (the notes it found)" % (out_base, out_base, out_base))
    return notes


def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox
    root = tk.Tk()
    root.withdraw()
    song = filedialog.askopenfilename(title="Pick a song", filetypes=[("Audio or MIDI", "*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.mid *.midi"), ("All files", "*.*")])
    if not song:
        return
    sf = filedialog.askopenfilename(title="Pick the Ocarina of Time soundfont (.sf2) - Cancel to use the plain stand-in voice", filetypes=[("SoundFont", "*.sf2")])
    base = os.path.splitext(song)[0] + "_oot"
    try:
        convert(song, base, sf or None)
        messagebox.showinfo("Song to Ocarina of Time", "Done!\n\n%s.wav\n%s.mp3" % (base, base))
    except Exception as e:   # shown to the person, not hidden
        messagebox.showerror("Song to Ocarina of Time", str(e))


def main():
    ap = argparse.ArgumentParser(description="Rebuild any song with the Ocarina of Time soundfont (see the top of this file).")
    ap.add_argument("song", nargs="?", help="the song (mp3, wav, flac, ogg, m4a or a MIDI file)")
    ap.add_argument("-o", "--out", help="output name without an extension (default: next to the song, ending _oot)")
    ap.add_argument("--soundfont", help="the .sf2 to play it with (the Ocarina of Time one)")
    ap.add_argument("--melody", help="instrument for the tune: a name, a number or bank:program (default: an ocarina if there is one)")
    ap.add_argument("--harmony", help="instrument for the chords and inner voices (default: a harp)")
    ap.add_argument("--bass", help="instrument for the low notes (default: a bass)")
    ap.add_argument("--reverb", type=float, default=0.25, help="0 none to 1 a lot (default 0.25)")
    ap.add_argument("--voices", type=int, default=5, help="most notes found at once (default 5)")
    ap.add_argument("--min-note-ms", type=int, default=90, help="ignore notes shorter than this (default 90)")
    ap.add_argument("--sensitivity", type=float, default=0.22, help="lower finds quieter notes, higher keeps only clear ones (default 0.22)")
    ap.add_argument("--list-presets", action="store_true", help="list the instruments in the soundfont and stop")
    args = ap.parse_args()
    if args.list_presets:
        if not args.soundfont:
            sys.exit("--list-presets needs --soundfont")
        for bank, prog, name in SoundFont(args.soundfont).presets():
            print("%3d:%-3d %s" % (bank, prog, name))
        return
    if not args.song:
        gui()
        return
    base = args.out or (os.path.splitext(args.song)[0] + "_oot")
    try:
        convert(args.song, base, args.soundfont, args)
    except (ValueError, RuntimeError) as e:
        sys.exit("Error: %s" % e)


if __name__ == "__main__":
    main()
