// Tests for tools/rom-extractor.html: builds a tiny fake ROM and checks the extractor reads it back. Run: node tools/test_rom_extractor.js
const fs = require('fs'), vm = require('vm'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'rom-extractor.html'), 'utf8');
const lib = html.split('// BEGIN_LIB')[1].split('// END_LIB')[0];
const ctx = {}; vm.createContext(ctx); vm.runInContext(lib + ';this.R=ROMLIB;', ctx);
const R = ctx.R;
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL: ' + m); } else console.log('ok: ' + m); };
const w32 = (b, o, v) => { b[o] = v >>> 24; b[o + 1] = v >>> 16; b[o + 2] = v >>> 8; b[o + 3] = v; };
const w16 = (b, o, v) => { b[o] = (v >> 8) & 255; b[o + 1] = v & 255; };

// --- Yaz0: one literal run and one back-reference
{
    const src = new Uint8Array(16 + 1 + 3 + 2);
    src.set([0x59, 0x61, 0x7A, 0x30]); w32(src, 4, 6);
    src[16] = 0xE0; src[17] = 1; src[18] = 2; src[19] = 3;  // bits 111 literal x3, then bit 0 = reference
    src[20] = 0x10 | 0; src[21] = 2;                         // n=(1)+2=3 bytes, distance 3
    const o = R.yaz0(src);
    ok(Array.from(o).join() === '1,2,3,1,2,3', 'yaz0 decodes literals and back-references');
}

// --- a fake ROM
const FILES = 130, SZ = 0x1000, BASE = 0x2000;
const rom = new Uint8Array(0x300000);
w32(rom, 0, 0x80371240); 'ZELDA'.split('').forEach((c, i) => rom[0x20 + i] = c.charCodeAt(0));
const DMA_AT = 0x1000;
const ent = [];
ent.push([0, 0x1060]);
const start = i => 0x10000 + i * 0x1000;
const CODE = 5;
for (let i = 1; i < FILES; i++) ent.push(i === CODE ? [start(i), start(i) + 0x20000] : [start(i) + (i > CODE ? 0x20000 : 0), start(i) + (i > CODE ? 0x20000 : 0) + SZ]);
ent[1] = [0x1060, 0x2060];                                    // entry 1 must begin where entry 0 ends
const dmaBytes = ent.length * 16;
ent.forEach((e, i) => { w32(rom, DMA_AT + i * 16, e[0]); w32(rom, DMA_AT + i * 16 + 4, e[1]); w32(rom, DMA_AT + i * 16 + 8, e[0]); w32(rom, DMA_AT + i * 16 + 12, 0); });
ok(true, 'fake ROM laid out with ' + ent.length + ' files');
// scene table in the code file: scenes use files 10..49 (40 scenes), objects use files 50..129 (80, padded with empties to 160)
const codeOff = ent[CODE][0];
for (let k = 0; k < 40; k++) { const e = ent[10 + k]; w32(rom, codeOff + 0x100 + k * 0x14, e[0]); w32(rom, codeOff + 0x100 + k * 0x14 + 4, e[1]); }
for (let k = 0; k < 160; k++) { if (k % 2 === 0 && k / 2 + 50 < FILES) { const e = ent[50 + k / 2]; w32(rom, codeOff + 0x2008 + k * 8, e[0]); w32(rom, codeOff + 0x2008 + k * 8 + 4, e[1]); } }
// scene 0 (file 10): header, collision, 1 room (file 60 is used by an object entry too, fine)
const sc = ent[10][0], roomFile = ent[100];
let o = sc;
const cmd = (id, d1, d2) => { rom[o] = id; rom[o + 1] = d1; w32(rom, o + 4, d2); o += 8; };
cmd(0x00, 1, 0x02000100); cmd(0x01, 1, 0x02000120); cmd(0x03, 0, 0x02000200); cmd(0x04, 1, 0x02000140); cmd(0x14, 0, 0);
const put16 = (base, vals) => vals.forEach((v, i) => w16(rom, base + i * 2, v & 0xFFFF));
put16(sc + 0x100, [0, 100, 5, -200, 0, 0, 0, 0]);                       // a spawn (x y z rx ry rz params)
put16(sc + 0x120, [0x0010, 10, 20, 30, 0, 0, 0, 7]);                      // an actor
w32(rom, sc + 0x140, roomFile[0]); w32(rom, sc + 0x144, roomFile[1]);
// collision: 4 verts, 2 polys, surface types, no water
put16(sc + 0x200, [-100, 0, -100, 100, 50, 100, 4, 0]); w32(rom, sc + 0x210, 0x02000300); put16(sc + 0x214, [2, 0]); w32(rom, sc + 0x218, 0x02000340);
w32(rom, sc + 0x21C, 0x02000400); put16(sc + 0x224, [0, 0]);
put16(sc + 0x300, [-100, 0, -100, 100, 0, -100, 100, 0, 100, -100, 0, 100]);
put16(sc + 0x340, [0, 0, 1, 2, 0, 0x7FFF, 0, 0]); put16(sc + 0x350, [1, 0, 2, 3, 0, 0x7FFF, 0, 0]);
w32(rom, sc + 0x400, 0x00000100); w32(rom, sc + 0x404, 0);                                // type 0 has exit bits ... (w0>>8)&0x1F = 1
w32(rom, sc + 0x408, 0); w32(rom, sc + 0x40C, 0);
w32(rom, sc + 0x400 + 8, 0);
// room: header with a mesh (type 0, one entry), whose display list loads 3 verts and draws a triangle
const rm = roomFile[0]; o = rm; cmd(0x0A, 0, 0x03000100); cmd(0x14, 0, 0);
rom[rm + 0x100] = 0; rom[rm + 0x101] = 1; w32(rom, rm + 0x104, 0x03000110);
w32(rom, rm + 0x110, 0x03000200); w32(rom, rm + 0x114, 0);
w32(rom, rm + 0x200, 0x01003006); w32(rom, rm + 0x204, 0x03000300);  // G_VTX n=3 v0=0
w32(rom, rm + 0x208, 0x05000204); w32(rom, rm + 0x20C, 0);           // G_TRI1 0,1,2 (indices*2)
w32(rom, rm + 0x210, 0xDF000000); w32(rom, rm + 0x214, 0);
put16(rm + 0x300, [0, 0, 0, 0, 0, 0, 0xFF00, 0, 0, 0, 100, 0, 0, 0, 0, 0xFF00, 0, 0, 0, 0, 0, 100, 0, 0xFF00]);
// make the vertex colours land in bytes 12..14: re-write explicitly
for (let v = 0; v < 3; v++) { const b = rm + 0x300 + v * 16; rom.fill(0, b, b + 16); w16(rom, b, v === 1 ? 100 : 0); w16(rom, b + 4, v === 2 ? 100 : 0); rom[b + 12] = 255; rom[b + 13] = 128; rom[b + 14] = 0; rom[b + 15] = 255; }

if (process.env.WRITE_FAKE_ROM) fs.writeFileSync(process.env.WRITE_FAKE_ROM, rom);
// 1. header and byte orders
const n1 = R.normalizeRom(rom); ok(n1.isZelda && n1.title.startsWith('ZELDA'), 'z64 detected, title read');
const v64 = new Uint8Array(rom.length); for (let i = 0; i < rom.length; i += 2) { v64[i] = rom[i + 1]; v64[i + 1] = rom[i]; }
ok(R.normalizeRom(v64).rom[0] === 0x80, 'v64 converted');
const n64 = new Uint8Array(rom.length); for (let i = 0; i < rom.length; i += 4) for (let k = 0; k < 4; k++) n64[i + k] = rom[i + 3 - k];
ok(R.normalizeRom(n64).rom[0] === 0x80, 'n64 converted');
let threw = false; try { R.normalizeRom(new Uint8Array(0x200000)); } catch (e) { threw = true; } ok(threw, 'junk file rejected');
// 2. dma and tables
const dma = R.findDmaTable(n1.rom); ok(dma.entries.length === FILES && dma.offset === DMA_AT, 'dmadata found at ' + dma.offset + ' with ' + dma.entries.length + ' entries');
const st = R.findSceneTable(n1.rom, dma); ok(st.scenes.length === 40 && st.offset === 0x100 && st.codeFile === CODE, 'scene table found: ' + st.scenes.length);
const ot = R.findObjectTable(n1.rom, dma, st); ok(ot && ot.objects.length >= 150 && ot.objects[3].start === ent[51][0], 'object table found: ' + (ot && ot.objects.length));
// 3. scene parse
const sb = R.fileBytes(n1.rom, dma.entries.find(e => e.vs === st.scenes[0].sceneStart));
const scene = R.parseScene(sb, (vs) => R.fileBytes(n1.rom, dma.entries.find(e => e.vs === vs)));
ok(scene.spawns.length === 1 && scene.spawns[0].x === 100 && scene.spawns[0].z === -200, 'spawn parsed');
ok(scene.actors.length === 1 && scene.actors[0].id === 0x10 && scene.actors[0].params === 7, 'actor parsed');
ok(scene.collision && scene.collision.verts.length === 12 && scene.collision.tris.length === 2, 'collision parsed (' + (scene.collision && scene.collision.tris.length) + ' tris)');
ok(scene.collision.tris[0][3] === 1, 'exit bits read from surface type');
ok(scene.rooms.length === 1 && scene.rooms[0].meshes.length === 1, 'room mesh list found');
// 4. display list
const rmesh = R.buildRoomMesh(scene.rooms[0], sb);
ok(rmesh.tris === 1 && rmesh.pos[3] === 100 && rmesh.pos[8] === 100, 'display list -> 1 triangle with right positions');
ok(Math.abs(rmesh.col[1] - 128 / 255) < 0.01, 'vertex colours carried through');
// 5. analysis and exports
const rep = R.analyzeScene({ id: 0, name: 'Test', spec: 'test' }, scene, { actors: [] });
ok(rep.triangles === 2 && rep.exitTriangles === 1 && rep.spawns.length === 1 && rep.floorAreaUnits2 === 40000, 'analysis report (floor area ' + rep.floorAreaUnits2 + ')');
ok(R.toObj(rmesh, 't').split('\n').filter(l => l.startsWith('f ')).length === 1, 'OBJ export has one face');
ok(R.toPly(rmesh).includes('element face 1'), 'PLY export has one face');
// 6. skeleton finder
{
    const obj = new Uint8Array(0x400);
    w32(obj, 0x00, 0x06000100); obj[0x04] = 3;                               // header: limb table at 0x100, 3 limbs
    [0x200, 0x20C, 0x218].forEach((lo, i) => w32(obj, 0x100 + i * 4, 0x06000000 | lo));
    // limb: x y z (3 s16), child, sibling, dl
    const limb = (lo, x, y, z, ch, sib, dl) => { w16(obj, lo, x); w16(obj, lo + 2, y); w16(obj, lo + 4, z); obj[lo + 6] = ch; obj[lo + 7] = sib; w32(obj, lo + 8, dl); };
    limb(0x200, 0, 0, 0, 1, 0xFF, 0); limb(0x20C, 0, 50, 0, 2, 0xFF, 0x06000300); limb(0x218, 0, 50, 0, 0xFF, 0xFF, 0x06000300);
    w32(obj, 0x300, 0x01003006); w32(obj, 0x304, 0x06000330); w32(obj, 0x308, 0x05000204); w32(obj, 0x30C, 0); w32(obj, 0x310, 0xDF000000);
    for (let v = 0; v < 3; v++) { const b = 0x330 + v * 16; w16(obj, b, v === 1 ? 10 : 0); w16(obj, b + 4, v === 2 ? 10 : 0); }
    const sk = R.findSkeletons(obj);
    ok(sk.length >= 1 && sk[0].limbCount === 3 && sk[0].stride === 12, 'skeleton found');
    const m = R.buildSkeletonMesh(obj, sk[0]);
    ok(m.tris === 2 && Math.max(...Array.from(m.pos).filter((_, i) => i % 3 === 1)) === 100, 'skeleton limbs posed by parent offsets');
    ok(R.findDisplayLists(obj).length >= 1, 'loose display list scanner finds the list');
}
console.log(fails ? fails + ' FAILED' : 'all rom extractor tests passed');
process.exit(fails ? 1 : 0);
