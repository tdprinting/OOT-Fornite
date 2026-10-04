// Tests for tools/song-to-oot.html: builds a small made-up ROM laid out like Ocarina of Time's (no game data), reads its instruments back,
// writes the .sf2 and plays notes and a whole song with it. Run: node tools/test_song_to_oot_html.js
const fs = require('fs'), vm = require('vm'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'song-to-oot.html'), 'utf8');
const lib = html.split('// BEGIN_LIB')[1].split('// END_LIB')[0];
const ctx = {}; vm.createContext(ctx); vm.runInContext(lib + ';this.S=OOTSONG;', ctx);
const S = ctx.S;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL: ' + m); } else console.log('ok: ' + m); };
const w32 = (b, o, v) => { b[o] = v >>> 24; b[o + 1] = (v >>> 16) & 255; b[o + 2] = (v >>> 8) & 255; b[o + 3] = v & 255; };
const w16 = (b, o, v) => { b[o] = (v >> 8) & 255; b[o + 1] = v & 255; };
const wf = (b, o, v) => { const d = new DataView(new ArrayBuffer(4)); d.setFloat32(0, v, false); for (let i = 0; i < 4; i++) b[o + i] = d.getUint8(i); };
let seed = 3;
const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF; return seed % n; };

// --- VADPCM against the slow, obvious decoder (the N64 tools' table method)
function reference(raw, book, order) {
    const table = book.map(b => { const t = []; for (let k = 0; k < 8; k++) { t.push(new Array(order + 8).fill(0)); for (let j = 0; j < order; j++) t[k][j] = b[j][k]; }
        for (let k = 1; k < 8; k++) t[k][order] = t[k - 1][order - 1];
        t[0][order] = 2048;
        for (let k = 1; k < 8; k++) for (let j = k; j < 8; j++) t[j][k + order] = t[j - k][order];
        return t; });
    const state = new Array(16).fill(0), out = [];
    for (let f = 0; f < raw.length / 9; f++) {
        const h = raw[f * 9], scale = 1 << (h >> 4), p = h & 15, ix = [];
        for (let i = 1; i < 9; i++) for (const nb of [raw[f * 9 + i] >> 4, raw[f * 9 + i] & 15]) ix.push((nb >= 8 ? nb - 16 : nb) * scale);
        for (let half = 0; half < 2; half++) {
            const vec = [];
            for (let i = 0; i < order; i++) vec.push(state[(half ? 8 : 16) - order + i]);
            for (let i = 0; i < 8; i++) {
                const ind = half * 8 + i; vec.push(ix[ind]);
                let acc = 0; for (let j = 0; j < order + i; j++) acc += table[p][i][j] * vec[j];
                state[ind] = Math.max(-32768, Math.min(32767, Math.floor(acc / 2048) + ix[ind]));
            }
        }
        out.push(...state);
    }
    return out;
}
const book = [];
for (let p = 0; p < 4; p++) book.push([0, 1].map(j => Array.from({ length: 8 }, (_, i) => (j === 1 && i === 0) ? 2400 : rnd(6000) - 3000)));
const raw = new Uint8Array(9 * 60);
for (let i = 0; i < raw.length; i++) raw[i] = rnd(256);
for (let f = 0; f < 60; f++) raw[f * 9] = (rnd(10) << 4) | rnd(4);
const fast = S.decodeVadpcm(raw, book, 2), ref = reference(raw, book, 2);
ok(fast.length === 960 && ref.every((v, i) => v === fast[i]), 'VADPCM decoder matches the reference decoder sample for sample');

// --- a made-up ROM: same layout as the Python test's
const SR = 16000, sine = new Uint8Array(SR * 2);
for (let i = 0; i < SR; i++) w16(sine, i * 2, Math.round(Math.sin(2 * Math.PI * 440 * i / SR) * 20000) & 0xFFFF);
const table = new Uint8Array(0x10000), ADPCM_AT = 0x8000;
table.set(sine, 0); table.set(raw, ADPCM_AT);
const font = new Uint8Array(0x400);
w32(font, 0, 0x100); w32(font, 8, 0x40); w32(font, 16, 0x60);
font.set([0, 30, 50, 200], 0x40); w32(font, 0x44, 0x180);
w32(font, 0x48, 0x200); wf(font, 0x4C, 1.0); w32(font, 0x50, 0x200); wf(font, 0x54, 0.5); w32(font, 0x58, 0x200); wf(font, 0x5C, 0.25);
font.set([0, 0, 127, 10], 0x60); w32(font, 0x64, 0x180); w32(font, 0x70, 0x220); wf(font, 0x74, 1.0);
w32(font, 0x100, 0x110);
font.set([251, 0, 0, 0], 0x110); w32(font, 0x114, 0x200); wf(font, 0x118, 0.5); w32(font, 0x11C, 0x180);
[2, 32700, 240, 16350, -1, 0].forEach((v, i) => w16(font, 0x180 + i * 2, v & 0xFFFF));
w32(font, 0x200, ((S.CODEC.S16 << 28) | (1 << 26) | sine.length) >>> 0); w32(font, 0x208, 0x240);
w32(font, 0x220, ((S.CODEC.ADPCM << 28) | raw.length) >>> 0); w32(font, 0x224, ADPCM_AT); w32(font, 0x228, 0x260); w32(font, 0x22C, 0x2A0);
w32(font, 0x244, SR);
w32(font, 0x260, 320); w32(font, 0x264, 960); w32(font, 0x268, 0xFFFFFFFF);
w32(font, 0x2A0, 2); w32(font, 0x2A4, 4);
book.forEach((b, p) => b.forEach((r, j) => r.forEach((v, i) => w16(font, 0x2A8 + ((p * 2 + j) * 8 + i) * 2, v & 0xFFFF))));
const bank = new Uint8Array(0x800); bank.set(font, 0x400);
const code = new Uint8Array(0x90000);
const tableAt = (o, rows) => { w16(code, o, rows.length); rows.forEach((r, k) => { const p = o + 16 + k * 16; w32(code, p, r[0]); w32(code, p + 4, r[1]); code[p + 8] = r[2]; w16(code, p + 10, r[4]); w16(code, p + 12, r[5]); }); };
tableAt(0x1000, Array.from({ length: 12 }, (_, k) => [k * 0x100, 0x80, 2, 0, 0, 0]));
tableAt(0x2000, [[0, 0x400, 2, 0, 0x00FF, 0], [0x400, 0x400, 2, 0, 0x0001, 0x0302]].concat(Array(8).fill([0, 0x10, 2, 0, 0x00FF, 0])));
tableAt(0x3000, [[0, 0x10000, 2, 0, 0, 0], [0, 0, 2, 0, 0, 0]]);
const yaz0Literal = d => { const out = [0x59, 0x61, 0x7A, 0x30, d.length >>> 24, (d.length >> 16) & 255, (d.length >> 8) & 255, d.length & 255, 0, 0, 0, 0, 0, 0, 0, 0];
    for (let i = 0; i < d.length; i += 8) { out.push(0xFF); for (let k = i; k < Math.min(i + 8, d.length); k++) out.push(d[k]); } return Uint8Array.from(out); };
const files = { 3: bank, 4: new Uint8Array(0x100), 5: table }, CODE = 20, DMA = 0x7430;
const rom = new Uint8Array(0x400000);
w32(rom, 0, 0x80371240); 'ZELDA'.split('').forEach((c, i) => rom[0x20 + i] = c.charCodeAt(0));
const rows = [[0, 0x1060, 0, 0], [0x1060, DMA, 0x1060, 0], [DMA, DMA + 0x1000, DMA, 0]];
let pos = 0x10000, vpos = 0x10000;
for (let i = 3; i < 140; i++) {
    if (i === CODE) { const c = yaz0Literal(code); rows.push([vpos, vpos + code.length, pos, pos + c.length]); rom.set(c, pos); pos += (c.length + 15) & ~15; vpos += code.length; continue; }
    const d = files[i] || new Uint8Array(0x100).fill(0x11);
    rows.push([vpos, vpos + d.length, pos, 0]); rom.set(d, pos); pos += (d.length + 15) & ~15; vpos += (d.length + 15) & ~15;
}
rows.forEach((r, i) => r.forEach((v, k) => w32(rom, DMA + i * 16 + k * 4, v)));
if (process.argv[2]) fs.writeFileSync(process.argv[2], rom);     // lets the Python and page versions be compared on the same ROM

const logs = [];
const m = S.extract(rom, t => logs.push(t));
ok(logs.some(t => /Soundfont table.*10 soundfonts/.test(t)), 'finds the soundfont table (not the sequence table) in the compressed code file');
ok(logs.some(t => /2 banks/.test(t)), 'finds the sample bank table');
ok(m.presets.map(p => p.name).sort().join() === 'OoT f01 drums,OoT f01 i00,OoT f01 i02', 'two instruments and a drum kit, empty slots skipped');
ok(m.samples.length === 2, 'each sample stored once');
const i0 = m.presets.find(p => p.name === 'OoT f01 i00');
ok(i0.zones.map(z => z.lo + '-' + z.hi).join() === '0-50,51-71,72-127', 'key splits follow the game\'s note ranges (game note 39 = middle C)');
const v64 = new Uint8Array(rom.length); for (let i = 0; i < rom.length; i += 2) { v64[i] = rom[i + 1]; v64[i + 1] = rom[i]; }
ok(S.extract(v64).presets.length === 3, '.v64 byte order reads the same');
try { S.extract(new Uint8Array(0x200000).fill(0).map((v, i) => i === 0 ? 0x80 : i === 1 ? 0x37 : i === 2 ? 0x12 : i === 3 ? 0x40 : 0)); ok(false, 'a ROM with no file table is refused'); }
catch (e) { ok(/file table/.test(e.message), 'a ROM with no file table is refused with a clear message'); }

// pitch of a rendered note: zero crossings over half a second
const pitch = (p, key) => { const r = S.renderNote(m, p, key, 100, 0.6, 44100); let c = 0; for (let i = 4411; i < 4411 + 11025; i++) if (r[0][i - 1] <= 0 && r[0][i] > 0) c++; return c * 4; };
const near = (a, b, tol) => Math.abs(a - b) <= tol;
ok(near(pitch(i0, 60), 440, 8), 'middle C plays the sample at its own pitch (' + pitch(i0, 60) + ' Hz)');
ok(near(pitch(i0, 71), 440 * Math.pow(2, 11 / 12), 8), 'eleven semitones up (' + pitch(i0, 71) + ' Hz)');
ok(near(pitch(i0, 48), 440, 8), 'the low zone (tuning 1.0) on C3 lands on 440 (' + pitch(i0, 48) + ' Hz)');
ok(near(pitch(i0, 80), 440 * Math.pow(2, 20 / 12) / 2, 8), 'the high zone (tuning 0.25) is an octave lower (' + pitch(i0, 80) + ' Hz)');
const drums = m.presets.find(p => p.bank === 128);
ok(near(pitch(drums, 21), 440, 8) && S.renderNote(m, drums, 22, 100, 0.2, 44100) === null, 'the drum plays at its own tuning on its key; the empty drum is silent');
const looped = S.renderNote(m, m.presets.find(p => p.name === 'OoT f01 i02'), 60, 100, 1.0, 44100);
ok(Math.max(...looped[0].subarray(35000, 39000).map(Math.abs)) > 0.001, 'the looped ADPCM instrument keeps sounding past its sample length');

// the .sf2
const sf2 = S.writeSf2(m);
const txt = (o, n) => String.fromCharCode(...sf2.subarray(o, o + n));
const le32 = o => sf2[o] | (sf2[o + 1] << 8) | (sf2[o + 2] << 16) | (sf2[o + 3] << 24);
ok(txt(0, 4) === 'RIFF' && txt(8, 4) === 'sfbk' && le32(4) === sf2.length - 8, '.sf2 has a valid RIFF header and length');
let p = 12; const lists = [];
while (p < sf2.length) { lists.push(txt(p + 8, 4)); p += 8 + le32(p + 4); }
ok(lists.join() === 'INFO,sdta,pdta' && p === sf2.length, '.sf2 has INFO, sdta and pdta and nothing left over');

// MIDI round trip, a whole song, transcription of a clean tune
const notes = [60, 64, 67, 72].map((k, i) => ({ start: i * 0.25, end: i * 0.25 + 0.2, pitch: k, vel: 100, part: 'melody' }));
const back = S.readMidi(S.writeMidi(notes));
ok(back.map(n => n.pitch).join() === '60,64,67,72' && near(back[3].start, 0.75, 0.01), 'MIDI written and read back');
const [L, R] = S.render(back.map(n => ({ ...n, part: 'melody' })), m, { melody: i0 }, 0.3, 22050);
ok(L.length > 22050 * 3 && Math.max(...L.map(Math.abs)) > 0.5 && Math.max(...R.map(Math.abs)) > 0.5, 'a song renders with reverb, normalised');
ok(S.wav(L, R, 22050).length === 44 + L.length * 4, 'WAV written');
const sr = 22050, tune = [72, 76, 79, 84], y = new Float32Array(sr * 2.2);
tune.forEach((k, i) => { const f = 440 * Math.pow(2, (k - 69) / 12); for (let t = 0; t < 0.45 * sr; t++) { const a = Math.min(1, t / 400) * Math.min(1, (0.45 * sr - t) / 1000);
    y[Math.floor(i * 0.5 * sr) + t] += 0.4 * a * (Math.sin(2 * Math.PI * f * t / sr) + 0.4 * Math.sin(4 * Math.PI * f * t / sr)); } });
const found = S.assignParts(S.transcribe(y, sr)).filter(n => n.part === 'melody' && n.end - n.start > 0.3).map(n => n.pitch);
ok(found.join() === tune.join(), 'a clean tune is transcribed note for note (short blips at the note changes aside): ' + found.join());

console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
process.exit(fails ? 1 : 0);
