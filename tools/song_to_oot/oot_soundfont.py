#!/usr/bin/env python3
"""Ocarina of Time soundfont extractor.

Reads the game's own instruments out of YOUR Ocarina of Time ROM and writes them as a SoundFont 2 file (.sf2) that song_to_oot.py (or any
synth or DAW) can play:

    python oot_soundfont.py "Ocarina of Time.z64" -o oot.sf2
    python oot_soundfont.py "Ocarina of Time.z64" --list        (what is in it, without writing anything)

Works with .z64, .v64 and .n64 ROMs (any byte order), compressed or decompressed, and finds the audio tables itself instead of relying on
one ROM version's addresses. Needs Python 3.8+ and numpy. Nothing is downloaded and nothing from the ROM is shipped with this tool: the .sf2
you make is built from your own copy of the game, so keep it to yourself.

What ends up in the .sf2:
  * every soundfont the game has becomes a bank: font 3 instrument 5 is preset 3:5 (bank 3, program 5)
  * each font's drum kit is a percussion preset: bank 128, program = the font number
  * presets are named "OoT f03 i05" / "OoT f03 drums": the ROM does not store instrument names
  * samples, loops, tuning, key splits and drum pans are exact; the game's volume envelopes and release times are converted as closely as
    a SoundFont's attack/decay/sustain/release can follow them
"""
import argparse
import math
import os
import struct
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("This tool needs numpy: run   pip install numpy")

OUTPUT_RATE = 32000          # the rate the game's mixer runs at: a sample with tuning 1.0 plays at 32 kHz on middle C
MIDI_OFFSET = 21             # the game's note 0 is A0 (MIDI 21), so its note 39 is middle C
ENV_TICK = 1.0 / 240         # one envelope step: a quarter of a 60 Hz frame
ROM_EXTENSIONS = (".z64", ".n64", ".v64")

be32 = lambda b, o: struct.unpack_from(">I", b, o)[0]
be16 = lambda b, o: struct.unpack_from(">H", b, o)[0]
s16 = lambda b, o: struct.unpack_from(">h", b, o)[0]


# ---------------------------------------------------------------------------------------------------------------------------------------------
# The ROM file

def normalize_rom(data):
    """Any of the three byte orders people keep N64 ROMs in -> big endian bytes."""
    if len(data) < 0x100000:
        raise ValueError("that file is too small to be an Ocarina of Time ROM")
    magic = be32(data, 0)
    if magic == 0x80371240:
        return bytes(data)
    a = np.frombuffer(bytes(data[:len(data) & ~3]), dtype=np.uint8)
    if magic == 0x37804012:                                      # .v64: every pair of bytes swapped
        return a.reshape(-1, 2)[:, ::-1].tobytes()
    if magic == 0x40123780:                                      # .n64: every word reversed
        return a.reshape(-1, 4)[:, ::-1].tobytes()
    raise ValueError("that does not look like an N64 ROM (unknown header)")


def yaz0(src):
    if src[:4] != b"Yaz0":
        return src
    size = be32(src, 4)
    out = bytearray(size)
    sp, dp = 16, 0
    while dp < size:
        code = src[sp]
        sp += 1
        for bit in range(7, -1, -1):
            if dp >= size:
                break
            if code >> bit & 1:
                out[dp] = src[sp]
                dp += 1
                sp += 1
                continue
            b1, b2 = src[sp], src[sp + 1]
            sp += 2
            dist = ((b1 & 0x0F) << 8 | b2) + 1
            n = b1 >> 4
            if n == 0:
                n = src[sp] + 0x12
                sp += 1
            else:
                n += 2
            n = min(n, size - dp)
            if dist >= n:
                out[dp:dp + n] = out[dp - dist:dp - dist + n]
                dp += n
            else:                                                # overlapping copy repeats the last `dist` bytes
                for _ in range(n):
                    out[dp] = out[dp - dist]
                    dp += 1
    return bytes(out)


def find_dma_table(rom):
    """The ROM's file table: rows of (virtual start, virtual end, physical start, physical end). The first row is always 0..0x1060."""
    for o in range(0, min(len(rom) - 0x40, 0x400000), 0x10):
        if rom[o:o + 16] == b"\0\0\0\0\0\0\x10\x60\0\0\0\0\0\0\0\0" and be32(rom, o + 16) == 0x1060:
            rows = []
            for i in range(6000):
                p = o + i * 16
                if p + 16 > len(rom):
                    break
                vs, ve, ps, pe = struct.unpack_from(">IIII", rom, p)
                if i > 2 and vs == ve == ps == pe == 0:
                    break
                if ve < vs or vs > 0x10000000:
                    break
                rows.append((vs, ve, ps, pe))
            if len(rows) > 100:
                return rows
    raise ValueError("could not find the ROM's file table. Is this an Ocarina of Time ROM?")


def file_bytes(rom, row):
    vs, ve, ps, pe = row
    if ps == 0xFFFFFFFF or ve <= vs:
        return None
    if pe == 0:
        return rom[ps:ps + (ve - vs)]
    return yaz0(rom[ps:pe])


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Finding the audio tables
#
# Audiobank (the instruments) and Audiotable (the samples) are files 3 and 5 of every version. Which part of each belongs to which soundfont
# and sample bank is in two tables in the code file, at addresses that differ between versions, so they are found by their shape:
#   header: s16 count, s16, u32, 8 bytes padding; then count rows of u32 offset, u32 size, s8 medium, s8 cache policy, s16 x3

def _table_rows(data, o, n):
    return [struct.unpack_from(">IIbbhhh", data, o + 16 + k * 16) for k in range(n)]


def _is_font_table(data, o, bank_size):
    n = s16(data, o)
    if not 8 <= n <= 0x80 or o + 16 + n * 16 > len(data):
        return False
    rows = _table_rows(data, o, n)
    if rows[0][0] != 0:
        return False
    end = 0
    for off, size, medium, cache, sd1, sd2, sd3 in rows:
        if size == 0 or off + size > bank_size or not 0 <= medium <= 3 or sd3 < 0:
            return False
        if (sd2 >> 8 & 0xFF) > 0x7F or (sd2 & 0xFF) > 0x7F or (sd1 >> 8 & 0xFF) > 0x20:
            return False
        end = max(end, off + size)
    return end >= bank_size * 0.9 and sum(r[5] >> 8 & 0xFF for r in rows) > 0      # the sequence table has the same shape but no instruments


def _is_sample_bank_table(data, o, table_size):
    n = s16(data, o)
    if not 1 <= n <= 0x20 or o + 16 + n * 16 > len(data):
        return False
    rows = _table_rows(data, o, n)
    if rows[0][0] != 0 or rows[0][1] == 0:
        return False
    end = 0
    for off, size, medium, cache, *_ in rows:
        if size == 0:
            if off >= n:                                         # a size of 0 points at another bank by number
                return False
        elif off + size > table_size or not 0 <= medium <= 3:
            return False
        end = max(end, off + size)
    return end >= table_size * 0.5


def find_audio(rom, log=lambda m: None):
    dma = find_dma_table(rom)
    if len(dma) < 30:
        raise ValueError("the ROM's file table is too short for Ocarina of Time")
    bank, table = file_bytes(rom, dma[3]), file_bytes(rom, dma[5])
    if not bank or not table:
        raise ValueError("the audio files are missing from this ROM")
    fonts = samples = None
    # the code file is one of the big files near the start; only those are opened (some are compressed)
    for i in range(6, min(len(dma), 60)):
        vs, ve, ps, pe = dma[i]
        if not 0x80000 <= ve - vs <= 0x400000:
            continue
        data = file_bytes(rom, dma[i])
        if not data:
            continue
        for o in range(0, len(data) - 0x20, 4):
            if fonts is None and data[o] == 0 and _is_font_table(data, o, len(bank)):
                fonts = [(r[0], r[1], r[5] >> 8 & 0xFF, r[5] & 0xFF, r[4] >> 8 & 0xFF, r[4] & 0xFF) for r in _table_rows(data, o, s16(data, o))]
                log("Soundfont table: file %d at 0x%X, %d soundfonts" % (i, o, len(fonts)))
            elif samples is None and data[o] == 0 and _is_sample_bank_table(data, o, len(table)):
                samples = [(r[0], r[1]) for r in _table_rows(data, o, s16(data, o))]
                log("Sample bank table: file %d at 0x%X, %d banks" % (i, o, len(samples)))
            if fonts is not None and samples is not None:
                break
        if fonts is not None and samples is not None:
            break
    if fonts is None or samples is None:
        raise ValueError("could not find the game's audio tables in this ROM. Is it Ocarina of Time?")
    return bank, table, fonts, samples


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Samples (VADPCM, the N64's compressed audio)

def _unpack_codes(raw, frame_size):
    """frames of compressed bytes -> (predictor index per frame, scaled residuals (frames x 16))."""
    frames = np.frombuffer(raw[:len(raw) // frame_size * frame_size], dtype=np.uint8).reshape(-1, frame_size)
    head = frames[:, 0].astype(np.int64)
    if frame_size == 9:
        b = frames[:, 1:].astype(np.int64)
        nib = np.stack([b >> 4, b & 15], axis=2).reshape(-1, 16)
        codes = np.where(nib >= 8, nib - 16, nib)
    else:                                                        # the small 5-byte kind: 2 bits a sample, in the top of a nibble
        b = frames[:, 1:].astype(np.int64)
        two = np.stack([b >> 6, b >> 4 & 3, b >> 2 & 3, b & 3], axis=2).reshape(-1, 16) << 2
        codes = np.where(two >= 8, two - 16, two)
    return head & 15, codes << (head >> 4)[:, None]


def decode_vadpcm(raw, book, order, frame_size=9):
    """VADPCM -> int16 samples. book: (predictors, order, 8). Each 8-sample half frame is predicted from the last `order` outputs."""
    preds, x = _unpack_codes(raw, frame_size)
    if len(preds) == 0:
        return np.zeros(0, dtype=np.int16)
    npred = book.shape[0]
    preds = np.minimum(preds, npred - 1)
    A = book.transpose(0, 2, 1).astype(np.int64)                 # A[p][i][j]: weight of previous output j on output i
    B = np.zeros((npred, 8, 8), dtype=np.int64)                  # B[p][i][k]: weight of this half's residual k on output i (k < i)
    for i in range(8):
        for k in range(i):
            B[:, i, k] = book[:, order - 1, i - k - 1]
    g = x.reshape(-1, 8)
    gp = np.repeat(preds, 2)
    bx = np.einsum("gik,gk->gi", B[gp], g)
    # Only the last `order` outputs of each half feed the next one, so those are run one half at a time in plain Python; the rest is
    # then filled in for all halves at once.
    tail = range(8 - order, 8)
    Al = A.tolist()
    bxl = bx[:, 8 - order:].tolist()
    gl = g[:, 8 - order:].tolist()
    prevs = np.zeros((len(g), order), dtype=np.int64)
    prev = [0] * order
    for h, p in enumerate(gp.tolist()):
        prevs[h] = prev
        a = Al[p]
        out = []
        for t, i in enumerate(tail):
            row = a[i]
            v = (sum(row[j] * prev[j] for j in range(order)) + bxl[h][t] >> 11) + gl[h][t]
            out.append(-32768 if v < -32768 else 32767 if v > 32767 else v)
        prev = out
    full = (np.einsum("gij,gj->gi", A[gp], prevs) + bx >> 11) + g
    return np.clip(full, -32768, 32767).astype(np.int16).reshape(-1)


CODEC_ADPCM, CODEC_S8, CODEC_S16_INMEMORY, CODEC_SMALL_ADPCM, CODEC_REVERB, CODEC_S16 = range(6)


def read_sample(font, ptr, banks, table):
    """A Sample struct in a soundfont -> dict(pcm, loop) or None. Pointers in a font are offsets from the font's start."""
    if ptr == 0 or ptr + 16 > len(font):
        return None
    w0, addr, loop_ptr, book_ptr = struct.unpack_from(">IIII", font, ptr)
    codec, medium, size = w0 >> 28, w0 >> 26 & 3, w0 & 0xFFFFFF
    if medium > 1 or banks[medium] is None:
        return None
    base = banks[medium]
    raw = table[base + addr:base + addr + size]
    key = (base + addr, size, codec)
    if codec in (CODEC_ADPCM, CODEC_SMALL_ADPCM):
        order, npred = struct.unpack_from(">ii", font, book_ptr)
        if not 1 <= order <= 8 or not 1 <= npred <= 16:
            return None
        book = np.frombuffer(font[book_ptr + 8:book_ptr + 8 + npred * order * 16], dtype=">i2").astype(np.int64).reshape(npred, order, 8)
        pcm = decode_vadpcm(raw, book, order, 9 if codec == CODEC_ADPCM else 5)
    elif codec in (CODEC_S16, CODEC_S16_INMEMORY):
        pcm = np.frombuffer(raw[:len(raw) & ~1], dtype=">i2").astype(np.int16)
    elif codec == CODEC_S8:
        pcm = np.frombuffer(raw, dtype=np.int8).astype(np.int16) << 8
    else:
        return None
    start, end, count = struct.unpack_from(">IIi", font, loop_ptr)
    loop = (start, min(end, len(pcm))) if count != 0 and end > start + 8 and start < len(pcm) else None
    return dict(key=key, pcm=pcm, loop=loop)


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Instruments

def release_seconds(index):
    """The game's release rates (an index into its decay table) -> how long the note takes to fade out after it is let go."""
    if index == 0:
        return 0.25
    if index >= 251:
        frames = {251: 0.75, 252: 0.66, 253: 0.5, 254: 0.33, 255: 0.25}[index]
    elif index >= 128:
        frames = 252 - index
    elif index >= 16:
        frames = 4 * (143 - index)
    else:
        frames = 60 * (23 - index)
    return frames / 60.0


def read_envelope(font, ptr):
    """The game's envelope (pairs of: steps to take, level to reach) -> (attack s, decay s, sustain level 0..1, peak level 0..1)."""
    if ptr == 0 or ptr + 4 > len(font):
        return 0.0, 0.0, 1.0, 1.0
    t, points, silent_end = 0.0, [], False
    for k in range(32):
        if ptr + 4 * k + 4 > len(font):
            break
        delay, level = struct.unpack_from(">hh", font, ptr + 4 * k)
        if delay > 0:
            t += delay * ENV_TICK
            points.append((t, max(0, level) / 32767.0))
            continue
        silent_end = delay == 0                                  # 0 ends the note; -1 holds, -2 jumps back, -3 restarts: all keep sounding
        break
    if not points:
        return 0.0, 0.0, 1.0, 1.0
    peak_i = max(range(len(points)), key=lambda i: points[i][1])
    peak = points[peak_i][1]
    attack = points[peak_i][0]
    decay = points[-1][0] - attack
    sustain = 0.0 if silent_end else points[-1][1] / peak if peak > 0 else 0.0
    return attack, decay, sustain, peak


def tuning_to_key(tuning):
    """A sample's tuning (playback speed on middle C at 32 kHz) -> (root key, fine tune cents) for a 32 kHz SoundFont sample."""
    s = 12.0 * math.log2(tuning) if tuning > 0 else 0.0
    r = int(round(s))
    return max(0, min(127, 60 - r)), int(round((s - r) * 100))


def extract(rom_bytes, log=lambda m: None):
    """ROM bytes -> (samples, presets) ready for write_sf2."""
    rom = normalize_rom(rom_bytes)
    bank, table, fonts, sample_banks = find_audio(rom, log)

    def bank_offset(i, depth=0):
        if i == 0xFF or i >= len(sample_banks) or depth > 4:
            return None
        off, size = sample_banks[i]
        return bank_offset(off, depth + 1) if size == 0 else off

    samples, sample_index, presets = [], {}, []

    def zone_sample(font, banks, ptr):
        s = read_sample(font, ptr, banks, table)
        if s is None or len(s["pcm"]) < 2:
            return None
        if s["key"] not in sample_index:
            sample_index[s["key"]] = len(samples)
            samples.append(dict(name="s%05X" % (s["key"][0] & 0xFFFFF), pcm=s["pcm"], loop=s["loop"]))
        return sample_index[s["key"]]

    def envelope_gens(font, env_ptr, release_index):
        attack, decay, sustain, peak = read_envelope(font, env_ptr)
        tc = lambda sec: max(-12000, min(8000, int(round(1200 * math.log2(max(sec, 0.001))))))
        cb = lambda lvl: 1440 if lvl <= 0 else max(0, min(1440, int(round(-200 * math.log10(lvl)))))
        gens = [(34, tc(attack)), (36, tc(decay)), (37, cb(sustain)), (38, tc(release_seconds(release_index)))]
        if peak < 1.0:
            gens.append((48, cb(peak)))
        return gens

    for f, (off, size, nins, ndrums, b1, b2) in enumerate(fonts):
        font = bank[off:off + size]
        banks = (bank_offset(b1), bank_offset(b2))
        if len(font) < 8:
            continue
        drums_ptr, _sfx_ptr = struct.unpack_from(">II", font, 0)
        for i in range(nins):
            if 8 + 4 * i + 4 > len(font):
                break
            ip = be32(font, 8 + 4 * i)
            if ip == 0 or ip + 0x20 > len(font):
                continue
            _reloc, lo, hi, rel = struct.unpack_from(">BBBB", font, ip)
            env = be32(font, ip + 4)
            ranges = [(0, lo + MIDI_OFFSET - 1) if lo > 0 else None, (lo + MIDI_OFFSET, hi + MIDI_OFFSET), (hi + MIDI_OFFSET + 1, 127) if hi < 127 else None]
            zones = []
            for slot, kr in enumerate(ranges):
                sp, tuning = struct.unpack_from(">If", font, ip + 8 + slot * 8)
                if kr is None or sp == 0:
                    continue
                klo, khi = max(0, kr[0]), min(127, kr[1])
                if klo > khi:
                    continue
                si = zone_sample(font, banks, sp)
                if si is None:
                    continue
                root, fine = tuning_to_key(tuning)
                zones.append(dict(keys=(klo, khi), sample=si, gens=[(58, root), (52, fine)] + envelope_gens(font, env, rel)))
            if zones:
                presets.append(dict(name="OoT f%02d i%02d" % (f, i), bank=f, program=i, zones=zones))
        if ndrums and drums_ptr and drums_ptr + 4 * ndrums <= len(font):
            zones = []
            for d in range(ndrums):
                dp = be32(font, drums_ptr + 4 * d)
                if dp == 0 or dp + 0x10 > len(font):
                    continue
                rel, pan = font[dp], font[dp + 1]
                sp, tuning = struct.unpack_from(">If", font, dp + 4)
                env = be32(font, dp + 12)
                key = d + MIDI_OFFSET
                if key > 127:
                    break
                si = zone_sample(font, banks, sp)
                if si is None:
                    continue
                root, fine = tuning_to_key(tuning)
                # a drum always plays at its own tuning, whatever key it is on: root key = its key, then the tuning on top
                gens = [(58, max(0, min(127, key - (60 - root)))), (52, fine), (17, int(round((pan - 64) / 64.0 * 500)))] + envelope_gens(font, env, rel)
                zones.append(dict(keys=(key, key), sample=si, gens=gens))
            if zones:
                presets.append(dict(name="OoT f%02d drums" % f, bank=128, program=f, zones=zones))
        log("  font %2d: %3d instruments, %2d drums" % (f, nins, ndrums))
    if not presets:
        raise ValueError("no instruments were found in this ROM's audio")
    log("%d presets, %d samples" % (len(presets), len(samples)))
    return samples, presets


# ---------------------------------------------------------------------------------------------------------------------------------------------
# Writing the .sf2

def _chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def _name(s):
    return s.encode("latin-1")[:19].ljust(20, b"\0")


def write_sf2(path, samples, presets, title="Ocarina of Time"):
    smpl, shdr = [], b""
    pos = 0
    for s in samples:
        pcm = s["pcm"]
        n = len(pcm)
        ls, le = s["loop"] if s["loop"] else (0, n)
        shdr += _name(s["name"]) + struct.pack("<IIIIIBbHH", pos, pos + n, pos + ls, pos + le, OUTPUT_RATE, 60, 0, 0, 1)
        smpl.append(pcm.astype("<i2").tobytes() + b"\0" * 92)        # 46 silent samples after each one, as the format asks
        pos += n + 46
    shdr += _name("EOS") + struct.pack("<IIIIIBbHH", 0, 0, 0, 0, 0, 0, 0, 0, 0)

    inst, ibag, igen, phdr, pbag, pgen = b"", b"", b"", b"", b"", b""
    ngen = nbag = npgen = npbag = 0
    for k, p in enumerate(presets):
        inst += _name(p["name"]) + struct.pack("<H", nbag)
        for z in p["zones"]:
            ibag += struct.pack("<HH", ngen, 0)
            nbag += 1
            loop = samples[z["sample"]]["loop"] is not None
            gens = [struct.pack("<HBB", 43, z["keys"][0], z["keys"][1])]
            gens += [struct.pack("<Hh", op, v) for op, v in z["gens"]]
            gens += [struct.pack("<Hh", 54, 1 if loop else 0), struct.pack("<HH", 53, z["sample"])]
            igen += b"".join(gens)
            ngen += len(gens)
        phdr += _name(p["name"]) + struct.pack("<HHHIII", p["program"], p["bank"], npbag, 0, 0, 0)
        pbag += struct.pack("<HH", npgen, 0)
        npbag += 1
        pgen += struct.pack("<HH", 41, k)
        npgen += 1
    inst += _name("EOI") + struct.pack("<H", nbag)
    ibag += struct.pack("<HH", ngen, 0)
    igen += struct.pack("<HH", 0, 0)
    phdr += _name("EOP") + struct.pack("<HHHIII", 0, 0, npbag, 0, 0, 0)
    pbag += struct.pack("<HH", npgen, 0)
    pgen += struct.pack("<HH", 0, 0)

    info = b"INFO" + _chunk(b"ifil", struct.pack("<HH", 2, 1)) + _chunk(b"isng", b"EMU8000\0") + _chunk(b"INAM", title.encode() + b"\0") + \
        _chunk(b"ICMT", b"Made by oot_soundfont.py from the owner's own ROM. Do not share.\0")
    sdta = b"sdta" + _chunk(b"smpl", b"".join(smpl))
    pdta = b"pdta" + _chunk(b"phdr", phdr) + _chunk(b"pbag", pbag) + _chunk(b"pmod", b"\0" * 10) + _chunk(b"pgen", pgen) + \
        _chunk(b"inst", inst) + _chunk(b"ibag", ibag) + _chunk(b"imod", b"\0" * 10) + _chunk(b"igen", igen) + _chunk(b"shdr", shdr)
    body = b"sfbk" + _chunk(b"LIST", info) + _chunk(b"LIST", sdta) + _chunk(b"LIST", pdta)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def rom_to_sf2(rom_path, sf2_path, log=print):
    log("Reading %s..." % rom_path)
    with open(rom_path, "rb") as f:
        data = f.read()
    samples, presets = extract(data, log=lambda m: log("  " + m))
    write_sf2(sf2_path, samples, presets)
    log("Wrote %s (%.1f MB)" % (sf2_path, os.path.getsize(sf2_path) / 1e6))
    return sf2_path


def is_rom(path):
    return bool(path) and os.path.splitext(path)[1].lower() in ROM_EXTENSIONS


def main():
    ap = argparse.ArgumentParser(description="Make a SoundFont (.sf2) of the Ocarina of Time instruments from your own ROM.")
    ap.add_argument("rom", help="your Ocarina of Time ROM (.z64, .v64 or .n64)")
    ap.add_argument("-o", "--out", help="the .sf2 to write (default: oot.sf2 next to the ROM)")
    ap.add_argument("--list", action="store_true", help="only list the instruments found, write nothing")
    args = ap.parse_args()
    try:
        if args.list:
            with open(args.rom, "rb") as f:
                samples, presets = extract(f.read(), log=print)
            for p in presets:
                print("%3d:%-3d %-16s %d zone(s)" % (p["bank"], p["program"], p["name"], len(p["zones"])))
            return
        rom_to_sf2(args.rom, args.out or os.path.join(os.path.dirname(os.path.abspath(args.rom)), "oot.sf2"))
    except (ValueError, OSError) as e:
        sys.exit("Error: %s" % e)


if __name__ == "__main__":
    main()
