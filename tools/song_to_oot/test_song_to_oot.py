#!/usr/bin/env python3
"""Self-test for song_to_oot.py: builds a tiny SoundFont and a test song, converts it, and checks the notes that come out."""
import os, struct, sys, tempfile, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import song_to_oot as s


def chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def build_sf2(path):
    rate = 22050
    t = np.arange(rate) / rate
    waves = [np.sin(2 * np.pi * 440 * t), 2 * np.abs(2 * ((440 * t) % 1) - 1) - 1, np.sin(2 * np.pi * 440 * t) + 0.5 * np.sin(2 * np.pi * 880 * t)]
    pcm = np.concatenate([np.concatenate([w * 0.6, np.zeros(46)]) for w in waves])
    smpl = (pcm * 32767).astype("<i2").tobytes()
    names = ["Ocarina", "Harp", "Bass"]
    progs = [79, 46, 32]
    shdr = b""; inst = b""; ibag = b""; igen = b""; phdr = b""; pbag = b""; pgen = b""
    for i, nm in enumerate(names):
        start = i * (rate + 46)
        shdr += nm.encode().ljust(20, b"\0") + struct.pack("<IIIIIBbHH", start, start + rate, start + 1000, start + rate - 1000, rate, 69, 0, 0, 1)
        inst += nm.encode().ljust(20, b"\0") + struct.pack("<H", i)
        ibag += struct.pack("<HH", len(igen) // 4, 0)
        igen += struct.pack("<Hh", 54, 1) + struct.pack("<Hh", 58, 69) + struct.pack("<Hh", 53, i)
        phdr += nm.encode().ljust(20, b"\0") + struct.pack("<HHHIII", progs[i], 0, i, 0, 0, 0)
        pbag += struct.pack("<HH", len(pgen) // 4, 0)
        pgen += struct.pack("<Hh", 41, i)
    shdr += b"EOS".ljust(20, b"\0") + struct.pack("<IIIIIBbHH", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    inst += b"EOI".ljust(20, b"\0") + struct.pack("<H", len(names))
    ibag += struct.pack("<HH", len(igen) // 4, 0)
    igen += struct.pack("<Hh", 0, 0)
    phdr += b"EOP".ljust(20, b"\0") + struct.pack("<HHHIII", 0, 0, len(names), 0, 0, 0)
    pbag += struct.pack("<HH", len(pgen) // 4, 0)
    pgen += struct.pack("<Hh", 0, 0)
    pdta = b"pdta" + chunk(b"phdr", phdr) + chunk(b"pbag", pbag) + chunk(b"pmod", b"\0" * 10) + chunk(b"pgen", pgen) + chunk(b"inst", inst) + chunk(b"ibag", ibag) + \
        chunk(b"imod", b"\0" * 10) + chunk(b"igen", igen) + chunk(b"shdr", shdr)
    info = b"INFO" + chunk(b"ifil", struct.pack("<HH", 2, 1))
    sdta = b"sdta" + chunk(b"smpl", smpl)
    body = b"sfbk" + chunk(b"LIST", info) + chunk(b"LIST", sdta) + chunk(b"LIST", pdta)
    open(path, "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)


def tone(f, dur, sr, amp):
    t = np.arange(int(dur * sr)) / sr
    env = np.minimum(1, t / 0.02) * np.minimum(1, (dur - t) / 0.05)
    return amp * env * (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(4 * np.pi * f * t) + 0.2 * np.sin(6 * np.pi * f * t))


def main():
    tmp = tempfile.mkdtemp()
    sf2 = os.path.join(tmp, "test.sf2")
    build_sf2(sf2)
    font = s.SoundFont(sf2)
    assert [p[2] for p in font.presets()] == ["Ocarina", "Harp", "Bass"], font.presets()
    assert font.find_preset("harp")[1] == 46 and font.find_preset("79")[2] == "Ocarina"
    # a test song: a tune over a held bass note
    sr = s.SR_ANALYSIS
    tune = [72, 76, 79, 84, 79, 76, 72]
    y = np.zeros(int(sr * (0.5 * len(tune) + 0.5)))
    for i, m in enumerate(tune):
        w = tone(440 * 2 ** ((m - 69) / 12), 0.45, sr, 0.5)
        y[int(i * 0.5 * sr):int(i * 0.5 * sr) + len(w)] += w
    b = tone(65.41, len(y) / sr - 0.2, sr, 0.4)
    y[:len(b)] += b
    wav = os.path.join(tmp, "song.wav")
    with wave.open(wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.clip(y / np.abs(y).max(), -1, 1) * 30000).astype("<i2").tobytes())
    notes = s.transcribe(s.load_audio(wav))
    s.assign_parts(notes)
    mel = [n for n in notes if n.part == "melody"]
    found = [n.pitch for n in sorted(mel, key=lambda n: n.start)]
    print("tune found:", found, "expected:", tune)
    hits = sum(1 for a, b2 in zip(found, tune) if abs(a - b2) <= 1)
    assert len(found) >= len(tune) - 1 and hits >= len(tune) - 1, (found, tune)
    assert any(n.part == "bass" and abs(n.pitch - 36) <= 1 for n in notes), [(n.pitch, n.part) for n in notes if n.pitch < 55]
    # MIDI round trip
    mid = os.path.join(tmp, "x.mid")
    s.write_midi(mid, notes)
    back = s.read_midi(mid)
    assert len(back) == len(notes) and all(abs(a.start - b2.start) < 0.01 for a, b2 in zip(sorted(notes, key=lambda n: (n.start, n.pitch)), back)), (len(back), len(notes))
    # the whole conversion, to wav and mp3, with the test soundfont and without
    for sfont in (sf2, None):
        out = os.path.join(tmp, "out_sf" if sfont else "out_plain")
        s.convert(wav, out, sfont, log=lambda m: None)
        assert os.path.getsize(out + ".wav") > 10000 and os.path.getsize(out + ".mp3") > 1000 and os.path.getsize(out + ".mid") > 50
        with wave.open(out + ".wav") as w:
            data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
            assert w.getnchannels() == 2 and np.abs(data).max() > 5000
    print("song_to_oot self-test passed")


if __name__ == "__main__":
    main()
