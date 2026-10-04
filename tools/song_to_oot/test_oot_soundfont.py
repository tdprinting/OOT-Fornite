#!/usr/bin/env python3
"""Self-test for oot_soundfont.py: builds a small made-up ROM laid out like Ocarina of Time's (file table, audio files, audio tables in a
compressed code file), extracts it to an .sf2 and plays notes from it with song_to_oot.py. No game data is used or needed."""
import os, struct, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oot_soundfont as x
import song_to_oot as s

fails = 0


def ok(cond, msg):
    global fails
    print(("ok: " if cond else "FAIL: ") + msg)
    fails += 0 if cond else 1


# --- a VADPCM decoder written the slow, obvious way (the N64 tools' version), to check the fast one against
def reference_vadpcm(raw, book, order):
    npred = book.shape[0]
    table = np.zeros((npred, 8, order + 8), dtype=np.int64)
    for p in range(npred):
        for k in range(8):
            for j in range(order):
                table[p][k][j] = book[p][j][k]
        for k in range(1, 8):
            table[p][k][order] = table[p][k - 1][order - 1]
        table[p][0][order] = 1 << 11
        for k in range(1, 8):
            for j in range(k):
                table[p][j][k + order] = 0
            for j in range(k, 8):
                table[p][j][k + order] = table[p][j - k][order]
    state = [0] * 16
    out = []
    for f in range(len(raw) // 9):
        fr = raw[f * 9:f * 9 + 9]
        scale, pred = 1 << (fr[0] >> 4), fr[0] & 15
        ix = []
        for b in fr[1:]:
            for nb in (b >> 4, b & 15):
                ix.append((nb - 16 if nb >= 8 else nb) * scale)
        for half in range(2):
            vec = [state[(16 if half == 0 else 8) - order + i] for i in range(order)]
            for i in range(8):
                ind = half * 8 + i
                vec.append(ix[ind])
                acc = sum(int(table[pred][i][j]) * vec[j] for j in range(order + i))
                v = (acc >> 11) + ix[ind]
                state[ind] = max(-32768, min(32767, v))
        out.extend(state)
    return np.array(out, dtype=np.int16)


rng = np.random.default_rng(7)
book = rng.integers(-3000, 3000, size=(4, 2, 8)).astype(np.int64)
book[:, 1, 0] = 2400                                                  # a typical, stable-ish predictor
raw = bytearray(rng.integers(0, 256, size=9 * 60, dtype=np.uint8).tobytes())
for f in range(60):
    raw[f * 9] = (raw[f * 9] & 0x03) | (rng.integers(0, 10) << 4)     # predictor 0-3, scale 0-9
raw = bytes(raw)
fast, ref = x.decode_vadpcm(raw, book, 2), reference_vadpcm(raw, book, 2)
ok(len(fast) == 960 and np.array_equal(fast, ref), "VADPCM decoder matches the reference decoder sample for sample")

# --- Yaz0: a literal-only file and one with an overlapping back-reference
ok(x.yaz0(b"Yaz0" + struct.pack(">I", 3) + b"\0" * 8 + bytes([0xE0, 1, 2, 3])) == b"\x01\x02\x03", "yaz0 literals")
ok(x.yaz0(b"Yaz0" + struct.pack(">I", 7) + b"\0" * 8 + bytes([0x80, 9, 0x40, 0x00])) == b"\x09" * 7, "yaz0 overlapping back-reference")


def yaz0_literal(data):
    out = bytearray(b"Yaz0" + struct.pack(">I", len(data)) + b"\0" * 8)
    for i in range(0, len(data), 8):
        part = data[i:i + 8]
        out.append(0xFF)
        out += part
    return bytes(out)


# --- a made-up ROM
SR = 16000
sine = (np.sin(2 * np.pi * 440 * np.arange(SR) / SR) * 20000).astype(">i2").tobytes()     # 1 s of A440 recorded at 16 kHz
adpcm = raw                                                                               # 60 frames of the ADPCM above

table = bytearray(0x10000)
table[0:len(sine)] = sine
ADPCM_AT = 0x8000
table[ADPCM_AT:ADPCM_AT + len(adpcm)] = adpcm

font = bytearray(0x400)
w32 = lambda b, o, v: struct.pack_into(">I", b, o, v)
w32(font, 0, 0x100)                    # drums
w32(font, 4, 0)                        # no sound effects
w32(font, 8, 0x40)                     # instrument 0: the sine, split in three
w32(font, 12, 0)                       # instrument 1: empty slot
w32(font, 16, 0x60)                    # instrument 2: the ADPCM sample, looped
# instrument 0: notes below 30 use the low sample, 30..50 normal, above 50 high (all the same sine, tuned an octave apart)
struct.pack_into(">BBBBI", font, 0x40, 0, 30, 50, 200, 0x180)
struct.pack_into(">IfIfIf", font, 0x48, 0x200, 1.0, 0x200, 0.5, 0x200, 0.25)
struct.pack_into(">BBBBI", font, 0x60, 0, 0, 127, 10, 0x180)
struct.pack_into(">IfIfIf", font, 0x68, 0, 0.0, 0x220, 1.0, 0, 0.0)
# drums: two pointers, drum 1 empty
w32(font, 0x100, 0x110); w32(font, 0x104, 0)
struct.pack_into(">BBBBIfI", font, 0x110, 251, 0, 0, 0, 0x200, 0.5, 0x180)
# envelope: up in 2 steps, down to half over 240 steps, hold
struct.pack_into(">hhhhhh", font, 0x180, 2, 32700, 240, 16350, -1, 0)
# samples: S16 sine in bank 1 (which points at bank 0) and ADPCM in bank 0
struct.pack_into(">IIII", font, 0x200, (x.CODEC_S16 << 28) | (1 << 26) | len(sine), 0, 0x240, 0)
struct.pack_into(">IIII", font, 0x220, (x.CODEC_ADPCM << 28) | (0 << 26) | len(adpcm), ADPCM_AT, 0x260, 0x2A0)
struct.pack_into(">IIii", font, 0x240, 0, SR, 0, 0)                  # no loop
struct.pack_into(">IIii", font, 0x260, 320, 960, -1, 0)              # loop 320..960 forever
struct.pack_into(">ii", font, 0x2A0, 2, 4)
font[0x2A8:0x2A8 + 4 * 2 * 8 * 2] = book.astype(">i2").tobytes()

bank = bytearray(0x800)
bank[0x400:0x800] = font               # font 0 is a dummy with no instruments, font 1 is ours

code = bytearray(0x90000)


def table_at(o, rows):
    struct.pack_into(">hhI", code, o, len(rows), 0, 0)
    for k, r in enumerate(rows):
        struct.pack_into(">IIbbhhh", code, o + 16 + k * 16, *r)


table_at(0x1000, [(k * 0x100, 0x80, 2, 0, 0, 0, 0) for k in range(12)])                  # a sequence table: same shape, no instruments
fonts = [(0, 0x400, 2, 0, 0x00FF, 0, 0)] + [(0x400, 0x400, 2, 0, 0x0001, (3 << 8) | 2, 0)] + [(0, 0x10, 2, 0, 0x00FF, 0, 0)] * 8
table_at(0x2000, fonts)
table_at(0x3000, [(0, 0x10000, 2, 0, 0, 0, 0), (0, 0, 2, 0, 0, 0, 0)])

files = {3: bytes(bank), 4: b"\0" * 0x100, 5: bytes(table)}
CODE = 20
rom = bytearray(0x400000)
rom[0:4] = struct.pack(">I", 0x80371240)
rom[0x20:0x25] = b"ZELDA"
DMA = 0x7430
rows = [(0, 0x1060, 0, 0), (0x1060, 0x7430, 0x1060, 0), (DMA, DMA + 0x1000, DMA, 0)]
pos = 0x10000
vpos = 0x10000
for i in range(3, 140):
    data = files.get(i, b"\x11" * 0x100)
    if i == CODE:
        comp = yaz0_literal(bytes(code))
        rows.append((vpos, vpos + len(code), pos, pos + len(comp)))
        rom[pos:pos + len(comp)] = comp
        pos += (len(comp) + 15) & ~15
        vpos += len(code)
        continue
    rows.append((vpos, vpos + len(data), pos, 0))
    rom[pos:pos + len(data)] = data
    pos += (len(data) + 15) & ~15
    vpos += (len(data) + 15) & ~15
for i, r in enumerate(rows):
    struct.pack_into(">IIII", rom, DMA + i * 16, *r)
rom = bytes(rom)

logs = []
samples, presets = x.extract(rom, log=logs.append)
ok(any("Soundfont table" in m and "10 soundfonts" in m for m in logs), "finds the soundfont table (not the sequence table) in the compressed code file")
ok(any("2 banks" in m for m in logs), "finds the sample bank table")
names = sorted(p["name"] for p in presets)
ok(names == ["OoT f01 drums", "OoT f01 i00", "OoT f01 i02"], "presets: two instruments and the drum kit of font 1, empty slots skipped: %s" % names)
ok(len(samples) == 2, "each sample stored once even when used by several zones")
inst0 = next(p for p in presets if p["name"] == "OoT f01 i00")
ok([z["keys"] for z in inst0["zones"]] == [(0, 50), (51, 71), (72, 127)], "key splits follow the game's note ranges (game note 39 = middle C)")

# the other byte orders give the same thing
v64 = np.frombuffer(rom, dtype=np.uint8).reshape(-1, 2)[:, ::-1].tobytes()
n64 = np.frombuffer(rom, dtype=np.uint8).reshape(-1, 4)[:, ::-1].tobytes()
ok(len(x.extract(v64)[1]) == 3 and len(x.extract(n64)[1]) == 3, ".v64 and .n64 byte orders read the same")

try:
    x.extract(b"\x80\x37\x12\x40" + b"\0" * 0x200000)
    ok(False, "a ROM with no file table is refused")
except ValueError as e:
    ok("file table" in str(e), "a ROM with no file table is refused with a clear message")

with tempfile.TemporaryDirectory() as d:
    rp = os.path.join(d, "game.z64")
    open(rp, "wb").write(rom)
    sf2 = x.rom_to_sf2(rp, os.path.join(d, "oot.sf2"), log=lambda m: None)
    font_ = s.SoundFont(sf2)
    ok(sorted(font_.presets()) == [(1, 0, "OoT f01 i00"), (1, 2, "OoT f01 i02"), (128, 1, "OoT f01 drums")], "the .sf2 reads back with the same presets")

    def pitch(bank_, prog, key):
        l, r = font_.render_note(bank_, prog, key, 100, 0.5)
        y = (l + r)[2000:2000 + 16384]
        spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
        return np.argmax(spec) * s.SR_OUT / len(y)

    # tuning 0.5 on a 16 kHz recording = its own pitch on middle C; the low and high zones are the same sine tuned 1.0 and 0.25
    p60, p71, p48, p80 = pitch(1, 0, 60), pitch(1, 0, 71), pitch(1, 0, 48), pitch(1, 0, 80)
    ok(abs(p60 - 440) < 4, "middle C plays the sample at its own pitch (%.1f Hz)" % p60)
    ok(abs(p71 - 440 * 2 ** (11 / 12)) < 6, "eleven semitones up, still in the normal zone (%.1f Hz)" % p71)
    ok(abs(p48 - 440) < 4, "the low zone (tuning 1.0 = an octave higher) on C3 lands on 440 again (%.1f Hz)" % p48)
    ok(abs(p80 - 440 * 2 ** (20 / 12) / 2) < 6, "the high zone (tuning 0.25) is an octave lower (%.1f Hz)" % p80)
    pd1, pd2 = pitch(128, 1, 21), pitch(128, 1, 21)
    ok(abs(pd1 - 440) < 4 and font_.render_note(128, 1, 22, 100, 0.2) is None, "the drum plays at its own tuning on its key, and the empty drum is silent")
    l, r = font_.render_note(1, 2, 60, 100, 1.0)
    ok(np.abs(l[int(0.8 * s.SR_OUT):int(0.9 * s.SR_OUT)]).max() > 0.01, "the looped ADPCM instrument keeps sounding past its sample length")

    # song_to_oot takes the ROM directly in place of the .sf2
    mid = os.path.join(d, "tune.mid")
    notes = [s.Note(i * 0.25, i * 0.25 + 0.2, 60 + i, 100, "melody") for i in range(4)]
    s.write_midi(mid, notes)
    args = s.argparse.Namespace(melody="1:0", harmony="1:0", bass="1:0", reverb=0.0, voices=5, min_note_ms=90, sensitivity=0.22)
    try:
        s.convert(mid, os.path.join(d, "tune_oot"), rp, args, log=lambda m: None)
        ok(os.path.exists(os.path.join(d, "tune_oot.wav")) and os.path.exists(os.path.join(d, "oot.sf2")), "song_to_oot.py renders a song straight from the ROM")
    except SystemExit as e:
        ok(False, "song_to_oot.py from the ROM: %s" % e)

print("\n%s" % ("ALL PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
