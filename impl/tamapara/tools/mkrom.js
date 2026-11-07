#!/usr/bin/env node
/*
 * Put a Paradise program into a flash dump: the launcher, the 64 KB image
 * and (for the game) the part that runs from the flash window. The result is
 * a new 16 MB ROM for Paramix Studio to write ("Write new ROM to the
 * device", "Only write changes"). The stock firmware stays; only the four
 * handlers of the home screen are pointed at the launcher, which starts the
 * program when A and C are held and otherwise calls the originals.
 *
 *   node mkrom.js --rom backup.bin --pram pram.bin [--xip xip.bin] --out new.bin [--keep-hiscore]
 *   node mkrom.js --rom new.bin --remove --out restored.bin     # the stock handlers back, our area erased
 *
 * Where things go (the Studio's places, so its own programs would not
 * collide): launcher at 0x0B0000 and image at 0x0A0000 when that area is
 * free (Land, Water, Sky), otherwise the launcher in the first free 4 KB
 * page after the firmware's code (Glacier, Tropical: 0x0B6000) and the
 * image right behind it. The flash part and the high score sector sit where
 * the image's settings block says ("DSPH" at 0x1D8: build.sh chose them),
 * and this script refuses when those places are not free.
 *
 * The launcher template (../hw/stub_tpl.bin), the scene table search and
 * the hook install follow Paramix Studio (mameise, GPL-3.0-or-later):
 * src/emu.js sceneTable(), installStub(), installedOrigs(); src/feld.js
 * freeStubOff(). GPL-3.0-or-later (this directory, see ../hw/NOTICE.md).
 */
'use strict';
const fs = require('fs');
const path = require('path');

const XIP_DEC = 0x11000;                      // the window shows the flash from here
const xip = (off) => 0x10000000 + off - XIP_DEC;
const APP_OFF_STD = 0xA0000, STUB_OFF_STD = 0xB0000;
const CODE_END = 0x10F000;                    // the Studio's six-field page; nothing of ours above
const PMXC = 0x43584D50, DSPH = 0x48505344;
const PH_APP = 0x1234ABCD, PH_AUTO = 0x66666661, PH_ORIG = [0x22222221, 0x33333331, 0x44444441, 0x55555551];
const HOOK_OFF = [0x40, 0x46, 0x4C, 0x52];    // the four hooks in the launcher, the originals at 0x94
const ORIG_OFF = 0x94;

const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const w32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
const hex = (v) => '0x' + v.toString(16).toUpperCase().padStart(6, '0');
function isFF(b, off, len) { for (let i = off; i < off + len; i++) if (b[i] !== 0xFF) return false; return true; }

function args() {
  const a = { keepHiscore: false, remove: false };
  const v = process.argv.slice(2);
  for (let i = 0; i < v.length; i++) {
    const k = v[i];
    if (k === '--rom') a.rom = v[++i]; else if (k === '--pram') a.pram = v[++i]; else if (k === '--xip') a.xip = v[++i];
    else if (k === '--out') a.out = v[++i]; else if (k === '--stub') a.stub = v[++i];
    else if (k === '--keep-hiscore') a.keepHiscore = true; else if (k === '--remove') a.remove = true;
    else throw new Error('unknown argument ' + k);
  }
  if (!a.rom || !a.out || (!a.remove && !a.pram)) {
    console.error('usage: node mkrom.js --rom backup.bin --pram pram.bin [--xip xip.bin] --out new.bin [--keep-hiscore]\n' +
                  '       node mkrom.js --rom new.bin --remove --out restored.bin');
    process.exit(2);
  }
  a.stub = a.stub || path.join(__dirname, '..', 'hw', 'stub_tpl.bin');
  return a;
}

/** The firmware's scene table: name -> offset of the entry (28 bytes: name pointer, ?, ?, four handlers) */
function sceneTable(d) {
  const names = new Map();
  for (let i = 0; i + 5 < d.length; i++) {
    if (d[i] !== 0x50 || d[i + 1] !== 0x53 || d[i + 2] !== 0x49 || d[i + 3] !== 0x44 || d[i + 4] !== 0x5F) continue;   // "PSID_"
    let j = i + 5;
    while (j < d.length && ((d[j] >= 0x41 && d[j] <= 0x5A) || (d[j] >= 0x30 && d[j] <= 0x39) || d[j] === 0x5F)) j++;
    if (d[j] !== 0) continue;
    names.set(xip(i), Buffer.from(d.buffer, d.byteOffset + i, j - i).toString('latin1'));
    i = j;
  }
  const ptrs = [];
  for (let o = 0; o + 4 <= d.length; o += 4) { const w = u32(d, o); if (names.has(w)) ptrs.push([o, w]); }
  if (!ptrs.length) throw new Error('scene table not found (is this a Paradise dump?)');
  let best = [], run = [ptrs[0]];
  for (let k = 1; k < ptrs.length; k++) {
    if (ptrs[k][0] - ptrs[k - 1][0] === 28) run.push(ptrs[k]);
    else { if (run.length > best.length) best = run; run = [ptrs[k]]; }
  }
  if (run.length > best.length) best = run;
  const base = best[0][0] - 4, tab = {};
  best.forEach(([o, w], i) => { tab[names.get(w)] = base + i * 28; });
  return tab;
}

/** Where the launcher goes: the Studio's place when free, else the first free page after the code */
function stubPlace(rom) {
  if (isFF(rom, STUB_OFF_STD, 0x2000) && isFF(rom, APP_OFF_STD, 0x10000)) return { stub: STUB_OFF_STD, app: APP_OFF_STD };
  let p = CODE_END;
  while (p > 0x90000 && isFF(rom, p - 0x1000, 0x1000)) p -= 0x1000;
  return { stub: p, app: p + 0x1000 };
}

/** Our launcher already installed? Returns { stub, app, origs } or null */
function installed(rom, tab) {
  const home = tab.PSID_HOME;
  const cur = [0, 1, 2, 3].map((k) => u32(rom, home + 12 + 4 * k));
  for (const stub of [STUB_OFF_STD, ...Array.from({ length: (CODE_END - 0x90000) / 0x1000 }, (_, i) => 0x90000 + i * 0x1000)]) {
    const lo = xip(stub), hi = lo + 0x1000;
    if (!cur.every((v) => v >= lo && v < hi)) continue;
    const origs = PH_ORIG.map((_, k) => u32(rom, stub + ORIG_OFF + 4 * k));
    const app = (u32(rom, stub + 0x08) - 0x10000000 + XIP_DEC) >>> 0;   // the stub's APP_SRC literal (ldr r4, =APP_SRC)
    return { stub, app, origs };
  }
  return null;
}

function main() {
  const a = args();
  const rom = new Uint8Array(fs.readFileSync(a.rom));
  if (rom.length !== 0x1000000) throw new Error('the Paradise image must be 16 MB (got ' + rom.length + ')');
  if (Buffer.from(rom.subarray(0, 8)).toString('latin1') !== 'SONIXDEV') throw new Error('not a Paradise flash dump (SONIXDEV header missing)');
  const tab = sceneTable(rom);
  if (!tab.PSID_HOME) throw new Error('home screen not found in the scene table');
  const prev = installed(rom, tab);
  const out = new Uint8Array(rom);

  if (a.remove) {
    if (!prev) throw new Error('no launcher of ours is installed');
    PH_ORIG.forEach((_, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, prev.origs[k]));
    const img = out.subarray(prev.app, prev.app + 0x10000);
    let end = prev.app + 0x10000;
    if (u32(img, 0x1D8) === DSPH) end = Math.max(end, u32(img, 0x1E8) + 0x1000, u32(img, 0x1E0) + u32(img, 0x1E4));
    out.fill(0xFF, prev.stub, prev.stub + 0x1000); out.fill(0xFF, prev.app, (end + 0xFFF) & ~0xFFF);
    fs.writeFileSync(a.out, out);
    console.log('removed: handlers restored, ' + hex(prev.stub) + '..' + hex((end + 0xFFF) & ~0xFFF) + ' erased -> ' + a.out);
    return;
  }

  const pram = new Uint8Array(fs.readFileSync(a.pram));
  if (pram.length > 0x10000) throw new Error('the PRAM image is over 64 KB');
  if (u32(pram, 0x140) !== PMXC) throw new Error('the image has no PMXC block at 0x140');
  const hasCfg = u32(pram, 0x1D8) === DSPH;
  const xipOff = hasCfg ? u32(pram, 0x1E0) : 0, xipLen = hasCfg ? u32(pram, 0x1E4) : 0, hisOff = hasCfg ? u32(pram, 0x1E8) : 0;
  const xipBin = a.xip ? new Uint8Array(fs.readFileSync(a.xip)) : null;
  if (xipBin && !hasCfg) throw new Error('an .xip part needs the DSPH block in the image');
  if (xipBin && xipBin.length !== xipLen) throw new Error('xip.bin is ' + xipBin.length + ' bytes, the image expects ' + xipLen);
  if (hasCfg && xipLen && !xipBin) throw new Error('the image expects an .xip part of ' + xipLen + ' bytes: pass --xip');

  // the places: a previous install of ours counts as free
  const place = prev || stubPlace(rom);
  const stub = place.stub, app = place.app;
  const clean = new Uint8Array(rom);
  if (prev) { clean.fill(0xFF, prev.stub, prev.stub + 0x1000); clean.fill(0xFF, prev.app, prev.app + 0x10000); if (xipOff) clean.fill(0xFF, xipOff, xipOff + Math.max(xipLen, 1)); }
  const need = [[stub, 0x1000, 'launcher'], [app, 0x10000, 'image']];
  if (xipLen) need.push([xipOff, xipLen, 'flash part']);
  if (hisOff) need.push([hisOff, 0x1000, 'high score sector']);
  for (const [o, n, what] of need) {
    if (o + n > CODE_END) throw new Error(what + ' at ' + hex(o) + ' reaches past ' + hex(CODE_END));
    if (what === 'high score sector' && a.keepHiscore) continue;
    if (!isFF(clean, o, n) && !(what === 'high score sector' && prev)) throw new Error(what + ' at ' + hex(o) + ' (' + n + ' bytes): the area is in use');
  }
  if (xipLen && xipOff !== app + 0x10000) {
    throw new Error('the image was built for its flash part at ' + hex(xipOff) + ', but this ROM puts the image at ' + hex(app) +
                    ' (flash part would be ' + hex(app + 0x10000) + '). Rebuild with XIP_FLASH_OFF=' + hex(app + 0x10000));
  }
  for (const [o, n] of need) for (const [p, m] of need) if (o !== p && o < p + m && p < o + n) throw new Error('areas overlap');

  // the launcher: the template with its placeholders filled in
  const st = new Uint8Array(fs.readFileSync(a.stub));
  const origs = prev ? prev.origs : [0, 1, 2, 3].map((k) => u32(rom, tab.PSID_HOME + 12 + 4 * k));
  const put = (ph, v) => { let n = 0; for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === ph) { w32(st, i, v); n++; } if (n !== 1) throw new Error('launcher template does not fit (placeholder ' + hex(ph) + ' found ' + n + ' times)'); };
  put(PH_APP, xip(app));
  PH_ORIG.forEach((ph, k) => put(ph, origs[k]));
  for (let i = 0; i + 4 <= st.length; i += 2) if (u32(st, i) === PH_AUTO) w32(st, i, 0);
  if (prev) { out.fill(0xFF, prev.stub, prev.stub + 0x1000); out.fill(0xFF, prev.app, prev.app + 0x10000); }
  out.fill(0xFF, stub, stub + 0x1000); out.set(st, stub);
  out.fill(0x00, app, app + 0x10000); out.set(pram, app);
  if (xipBin) { out.fill(0xFF, xipOff, xipOff + xipLen); out.set(xipBin, xipOff); }
  if (hisOff && !a.keepHiscore) out.fill(0xFF, hisOff, hisOff + 0x1000);
  HOOK_OFF.forEach((h, k) => w32(out, tab.PSID_HOME + 12 + 4 * k, (xip(stub) + h) | 1));

  fs.writeFileSync(a.out, out);
  let changed = 0; for (let i = 0; i < out.length; i += 0x1000) { for (let j = i; j < i + 0x1000; j++) if (out[j] !== rom[j]) { changed++; break; } }
  console.log('home screen handlers: ' + origs.map((v) => '0x' + v.toString(16)).join(' ') + ' -> launcher at ' + hex(stub));
  console.log('image ' + hex(app) + ' (' + pram.length + ' B)' + (xipBin ? ', flash part ' + hex(xipOff) + ' (' + xipLen + ' B)' : '') + (hisOff ? ', high score ' + hex(hisOff) : ''));
  console.log(changed + ' sectors of 4 KB differ from the input -> ' + a.out);
}

try { main(); } catch (e) { console.error('mkrom.js: ' + e.message); process.exit(1); }
