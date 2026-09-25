// Devour Sphere browser front end.
//
// Loads the STANDALONE_WASM module built from impl/wasm/main.cpp, runs the
// simulation at its fixed tick rate, renders through the WASM frame buffer
// (RGB565_SWAPPED) into a <canvas>, and turns keyboard / gamepad / touch input into
// the input of the simulation: two direction axes (-127..127, analog from the
// touch pad and the gamepad stick, full strength from the keys) and the
// button bits.
//
// startDevourSphere({ wasm: 'devoursphere.wasm', se: 'se.bin' })
//   URL parameters: ?screen=WxH        frame buffer size (default 480x320)
//                   ?level=N&weapon=W  skips the menus (debug)
//                   ?seed=N            fixed random seed
//                   ?auto=1            the AI drives the player (demo)

'use strict';

const BTN_LEFT = 1, BTN_RIGHT = 2, BTN_UP = 4, BTN_DOWN = 8, BTN_A = 16, BTN_PAUSE = 32,
  BTN_B = 64;

// RGB565_SWAPPED -> RGBA8888 lookup. The frame buffer is read as native (little
// endian) 16-bit words, so the table is indexed by the byte-swapped value and
// the blit is one lookup and one 32-bit store per pixel. That matters at the
// larger frame buffer sizes, where a per-channel loop would not keep up.
const RGBA_LUT = new Uint32Array(65536);
{
  const lut5 = new Uint8Array(32), lut6 = new Uint8Array(64);
  for (let i = 0; i < 32; i++) lut5[i] = Math.round(i * 255 / 31);
  for (let i = 0; i < 64; i++) lut6[i] = Math.round(i * 255 / 63);
  for (let v = 0; v < 65536; v++) {
    const p = ((v & 0xff) << 8) | (v >>> 8);  // the value as stored (BE)
    RGBA_LUT[v] = (0xff000000 | (lut5[p & 31] << 16) |
                   (lut6[(p >>> 5) & 63] << 8) | lut5[p >>> 11]) >>> 0;
  }
}

// Sound effects. The simulation raises one bit per kind after a tick
// (ds_get_sounds(), the order of devoursphere::sim::SoundKind); the
// waveforms come from se.bin (impl/wasm/pack_se.py: a table of contents and
// mono 16-bit PCM, in the same order) and are played through Web Audio,
// one AudioBufferSourceNode per request, so any number can overlap.
// The AudioContext can only start from a user gesture: it is created on the
// first key or pointer event, and requests before that are dropped.
// The mute lives in the game (ds_set_muted / ds_get_muted: DOWN on the title
// or the pause screen toggles it, and the toolbar button); muted, the game
// raises no sound bits. This page keeps it in localStorage.
const SE_NAMES = ['shot_vulcan', 'shot_laser', 'shot_missile', 'hit_enemy',
  'hit_player', 'enemy_killed_small', 'enemy_killed_big', 'player_killed',
  'get_fragment', 'get_upgrade', 'menu_select', 'menu_start', 'launch', 'arrive',
  'time_alarm', 'dodge'];
// Per-kind gain (the place to balance the material; 1 = as recorded)
const SE_GAIN = {
  shot_vulcan: 1, shot_laser: 1, shot_missile: 1,
  hit_enemy: 1, hit_player: 1,
  enemy_killed_small: 1, enemy_killed_big: 1, player_killed: 1,
  get_fragment: 1, get_upgrade: 1,
  menu_select: 1, menu_start: 1,
  launch: 1, arrive: 1,
  time_alarm: 1, dodge: 1,
};
const SOUND_KEY = 'devoursphere.sound';

class SoundPlayer {
  constructor() {
    this.ctx = null;
    this.master = null;
    this.buffers = null;   // AudioBuffer per kind, once the context exists
    this.pcm = null;       // { rate, sounds: [Float32Array] } from se.bin
    this.unlock = this.unlock.bind(this);
    for (const ev of ['keydown', 'pointerdown', 'touchend']) {
      window.addEventListener(ev, this.unlock, { capture: true, passive: true });
    }
  }

  // Fetch and parse se.bin (no decoder: the file is PCM already)
  async load(url) {
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`fetch failed: ${resp.status}`);
    const data = await resp.arrayBuffer();
    const dv = new DataView(data);
    if (data.byteLength < 16 || dv.getUint32(0, false) !== 0x44535345) {  // "DSSE"
      throw new Error('not a sound pack');
    }
    const rate = dv.getUint32(4, true);
    const count = dv.getUint32(8, true);
    const total = dv.getUint32(12, true);
    const pcmStart = 16 + count * 8;
    const pcm = new Int16Array(data, pcmStart, total);
    const sounds = [];
    for (let i = 0; i < count; i++) {
      const first = dv.getUint32(16 + i * 8, true);
      const n = dv.getUint32(20 + i * 8, true);
      const f = new Float32Array(n);
      for (let k = 0; k < n; k++) f[k] = pcm[first + k] / 32768;
      sounds.push(f);
    }
    this.pcm = { rate, sounds };
    if (this.ctx) this.makeBuffers();
  }

  makeBuffers() {
    const { rate, sounds } = this.pcm;
    this.buffers = sounds.map((f) => {
      const b = this.ctx.createBuffer(1, Math.max(f.length, 1), rate);
      b.copyToChannel(f, 0);
      return b;
    });
  }

  // First user gesture: create (or resume) the context
  unlock() {
    if (!this.ctx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return;
      try { this.ctx = new AC(); } catch (e) { return; }
      this.master = this.ctx.createGain();
      this.master.connect(this.ctx.destination);
      if (this.pcm) this.makeBuffers();
    }
    if (this.ctx.state === 'suspended') this.ctx.resume().catch(() => {});
    if (this.ctx.state === 'running') {
      for (const ev of ['keydown', 'pointerdown', 'touchend']) {
        window.removeEventListener(ev, this.unlock, { capture: true });
      }
    }
  }

  // Play every kind whose bit is set in `bits`. The context only exists
  // after a gesture; a source started while it is still resuming plays as
  // soon as it runs (the first menu sound follows the key that unlocked it)
  play(bits) {
    if (!this.buffers || !this.ctx || this.ctx.state === 'closed') return;
    for (let i = 0; bits; i++, bits >>>= 1) {
      if (!(bits & 1) || i >= this.buffers.length) continue;
      const src = this.ctx.createBufferSource();
      src.buffer = this.buffers[i];
      const gain = SE_GAIN[SE_NAMES[i]];
      if (gain !== undefined && gain !== 1) {
        const g = this.ctx.createGain();
        g.gain.value = gain;
        src.connect(g);
        g.connect(this.master);
      } else {
        src.connect(this.master);
      }
      src.start();
    }
  }
}

// "320x240" from ?screen=, or null
function parseScreenSize(params) {
  const value = params.get('screen');
  const m = value && /^\s*(\d+)\s*[xX*]\s*(\d+)\s*$/.exec(value);
  return m ? { w: parseInt(m[1], 10), h: parseInt(m[2], 10) } : null;
}

async function startDevourSphere(opts) {
  const statusEl = document.getElementById('status');
  const fpsEl = document.getElementById('fps');
  const canvas = document.getElementById('screen');
  const input = new InputState();
  const sound = new SoundPlayer();
  // Loaded alongside the module; a missing pack only means silence
  sound.load(opts.se || 'se.bin').catch((e) => console.warn(`sound: ${e.message}`));

  try {
    const wasiStubs = new Proxy({}, {
      get: (_, name) => (name === 'proc_exit' ? () => { throw new Error('exit'); } : () => 0),
    });
    const resp = await fetch(opts.wasm);
    if (!resp.ok) throw new Error(`fetch failed: ${resp.status}`);
    const { instance } = await WebAssembly.instantiate(await resp.arrayBuffer(), {
      wasi_snapshot_preview1: wasiStubs,
    });
    const ex = instance.exports;
    if (ex._initialize) ex._initialize();

    const params = new URLSearchParams(location.search);
    const seed = params.has('seed') ? (parseInt(params.get('seed'), 10) >>> 0) : (Date.now() >>> 0);
    // The frame buffer size must be set before the game starts: the HUD
    // layout is derived from it once
    const wanted = parseScreenSize(params);
    let sizeError = '';
    if (wanted && !ex.ds_set_screen(wanted.w, wanted.h)) {
      sizeError = `screen ${wanted.w}x${wanted.h} is not supported`;
    }
    ex.ds_init(seed);
    if (params.has('level')) {
      ex.ds_debug_start(parseInt(params.get('level'), 10) || 1,
                        parseInt(params.get('weapon') || '0', 10) || 0);
    }
    if (params.get('auto') === '1') ex.ds_debug_auto(1);
    // ?debug: DEBUG MODE on the HUD, and the number keys cheat (see
    // ds_debug_key in impl/wasm/main.cpp for the key map)
    if (params.has('debug')) {
      ex.ds_set_debug(1);
      input.onDebugKey = (n) => ex.ds_debug_key(n);
    }

    // High score: kept in the browser as {major, score, sphere}. A record
    // from another major version of the game (or the old plain number) is
    // dropped: the scoring changed, the numbers do not compare
    const HS_KEY = 'devoursphere.highscore';
    const major = ex.ds_get_version_major();
    try {
      const rec = JSON.parse(localStorage.getItem(HS_KEY) || 'null');
      if (rec && rec.major === major) {
        ex.ds_set_high_score((rec.score >>> 0) || 0, (rec.sphere | 0) || 0);
      }
    } catch (e) { /* ignore */ }
    // The mute: restored from localStorage, kept there when the game
    // toggles it (DOWN on the title / pause screen) or the button does
    let muted = false;
    try { muted = localStorage.getItem(SOUND_KEY) === '0'; } catch (e) { /* ignore */ }
    ex.ds_set_muted(muted ? 1 : 0);
    const soundBtn = setupSoundToggle(() => ex.ds_get_muted() !== 0,
                                      (m) => ex.ds_set_muted(m ? 1 : 0));
    function keepMute() {
      const now = ex.ds_get_muted() !== 0;
      if (now === muted) return;
      muted = now;
      try { localStorage.setItem(SOUND_KEY, muted ? '0' : '1'); } catch (e) { /* ignore */ }
      if (soundBtn) soundBtn.refresh();
    }
    // The game decides when the score counts (the title demo's never
    // does); this side only stores the record it kept
    function updateHighScore() {
      if (!ex.ds_keep_high_score()) return;
      const rec = { major, score: ex.ds_get_high_score() >>> 0,
                    sphere: ex.ds_get_high_score_sphere() | 0 };
      try { localStorage.setItem(HS_KEY, JSON.stringify(rec)); } catch (e) { /* ignore */ }
    }

    const W = ex.ds_get_width();
    const H = ex.ds_get_height();
    const fbPtr = ex.ds_get_fb();
    const tickRate = ex.ds_get_tick_rate();
    const tickMs = 1000 / tickRate;

    canvas.width = W;
    canvas.height = H;
    // The page scales the canvas by CSS, so it has to follow the size too
    const gameEl = document.getElementById('game');
    if (gameEl) {
      gameEl.style.setProperty('--aspect', `${W} / ${H}`);
      gameEl.style.setProperty('--aspect-num', String(W / H));
      gameEl.style.setProperty('--game-max', `${Math.max(960, W * 2)}px`);
    }
    const ctx = canvas.getContext('2d', { alpha: false });
    const imgData = ctx.createImageData(W, H);
    const rgba32 = new Uint32Array(imgData.data.buffer);
    const pixels = W * H;

    setupKeyboard(input);
    setupTouchPad(input);
    setupButtons();
    setupMobile();

    let acc = 0;
    let last = performance.now();
    let lastRender = last;
    let frames = 0, fpsTime = last;

    function blit() {
      // The view is rebuilt every frame: the WASM memory can grow and
      // invalidate it
      const fb = new Uint16Array(ex.memory.buffer, fbPtr, pixels);
      for (let i = 0; i < pixels; i++) rgba32[i] = RGBA_LUT[fb[i]];
      ctx.putImageData(imgData, 0, 0);
    }

    function frame(now) {
      // The benchmark (B held for three seconds on the title): one tick a
      // frame and no pacing, so as many frames as fit in about 12 ms, only
      // the last of them blitted
      if (ex.ds_bench_running()) {
        const t0 = performance.now();
        do {
          ex.ds_tick(0, 0, 0);
          ex.ds_render(0);
          frames++;
        } while (ex.ds_bench_running() && performance.now() - t0 < 12);
        blit();
        last = lastRender = performance.now();
        acc = 0;
        requestAnimationFrame(frame);
        return;
      }
      acc += Math.min(now - last, 250);  // cap after a pause (tab hidden)
      last = now;
      let ticked = false;
      let n = 0;
      while (acc >= tickMs && n < 4) {
        pollGamepad(input);
        ex.ds_tick(input.buttons(), input.axisX(), input.axisY());
        const bits = ex.ds_get_sounds();
        if (bits) sound.play(bits);
        acc -= tickMs;
        ticked = true;
        n++;
      }
      if (n === 4) acc = 0;  // too slow: drop the backlog
      if (ticked) {
        ex.ds_render((now - lastRender) / 1000);
        lastRender = now;
        blit();
        frames++;
      }
      if (now - fpsTime >= 1000) {
        updateHighScore();
        keepMute();
        if (fpsEl) fpsEl.textContent = `${(frames * 1000 / (now - fpsTime)).toFixed(0)} fps`;
        frames = 0;
        fpsTime = now;
      }
      requestAnimationFrame(frame);
    }
    ex.ds_render(0);
    blit();
    requestAnimationFrame(frame);
    if (statusEl) statusEl.textContent = sizeError ? `${sizeError}, using ${W}x${H}` : '';
  } catch (e) {
    if (statusEl) {
      statusEl.textContent = `Error: ${e.message} - this page does not work from file://.` +
        ' Serve it over HTTP, e.g. "python3 -m http.server -d docs".';
    }
    throw e;
  }
}

// ---------------------------------------------------------------------------
// Input

// Every source keeps its own buttons (A / B / PAUSE, and the direction bits
// of the keys and the gamepad's d-pad) and axes; the direction the game gets
// is, per axis, the strongest of them. The dead zones and the saturation
// are this front end's (the core takes the strength as it is).
const AXIS_MAX = 127;

class InputState {
  constructor() {
    this.keys = 0;      // keyboard (bits)
    this.onDebugKey = null;  // set in debug mode: number key -> cheat
    this.touch = 0;     // virtual pad (bits)
    this.touchX = 0; this.touchY = 0;
    this.pad = 0;       // gamepad (bits)
    this.padX = 0; this.padY = 0;
  }
  bits() { return this.keys | this.touch | this.pad; }
  // The buttons without the directions: those go as the axes
  buttons() { return this.bits() & (BTN_A | BTN_B | BTN_PAUSE); }
  axisX() {
    const b = this.bits();
    return strongest(((b & BTN_RIGHT) ? AXIS_MAX : 0) - ((b & BTN_LEFT) ? AXIS_MAX : 0),
                     this.touchX, this.padX);
  }
  axisY() {
    const b = this.bits();
    return strongest(((b & BTN_DOWN) ? AXIS_MAX : 0) - ((b & BTN_UP) ? AXIS_MAX : 0),
                     this.touchY, this.padY);
  }
}

function strongest(...vs) {
  let best = 0;
  for (const v of vs) if (Math.abs(v) > Math.abs(best)) best = v;
  return best;
}

// A round analog control (the gamepad's stick, the touch disc) as the two
// axes, -127..127 each. The strength is the length of the vector (dx, dy),
// in any unit: nothing up to `dead`, full from `full` on, linear in between.
// The direction is carried from the circle onto a square (the larger
// component becomes 1), so that the control pushed all the way on a
// diagonal is full on both axes: a quick turn is a full turn under a full
// brake, and the two multiply (half of each is well under half the turn).
// Read each axis on its own, a full diagonal gave only about 0.7 per axis.
// Each component of the square is then 0 up to AXIS_DEAD of the larger one
// (about 8.5 degrees off an axis: a push a little off an axis does not leak
// a weak dash or brake into a turn) and full from AXIS_FULL (about 31
// degrees), linear in between. So anywhere within about 14 degrees of a
// diagonal is full on both axes, the same as two keys: a thumb pushing
// "down and to the side" is rarely at 45 degrees, and at 30 degrees a plain
// square still gave only half the brake (a circle in 2.7 s against 2 s).
const AXIS_DEAD = 0.15, AXIS_FULL = 0.6;

function roundAxes(dx, dy, dead, full) {
  const len = Math.hypot(dx, dy);
  if (len <= dead) return [0, 0];
  const k = Math.min(1, (len - dead) / (full - dead));
  const m = Math.max(Math.abs(dx), Math.abs(dy));
  const axis = (c) => {
    const a = Math.min(1, Math.max(0, (Math.abs(c) / m - AXIS_DEAD) / (AXIS_FULL - AXIS_DEAD)));
    return Math.sign(c) * Math.round(a * k * AXIS_MAX);
  };
  return [axis(dx), axis(dy)];
}

const KEY_MAP = {
  ArrowLeft: BTN_LEFT, KeyA: BTN_LEFT,
  ArrowRight: BTN_RIGHT, KeyD: BTN_RIGHT,
  ArrowUp: BTN_UP, KeyW: BTN_UP,
  ArrowDown: BTN_DOWN, KeyS: BTN_DOWN,
  Space: BTN_A, KeyJ: BTN_A, KeyL: BTN_A, Enter: BTN_A,
  // Dodge: I / K, and the row next to the space bar
  KeyI: BTN_B, KeyK: BTN_B,
  KeyC: BTN_B, KeyV: BTN_B, KeyB: BTN_B, KeyN: BTN_B, KeyM: BTN_B,
  Escape: BTN_PAUSE, KeyP: BTN_PAUSE,
};

function setupKeyboard(input) {
  window.addEventListener('keydown', (e) => {
    const digit = /^Digit([0-9])$/.exec(e.code);
    if (digit && input.onDebugKey && !e.repeat) {
      input.onDebugKey(parseInt(digit[1], 10));
      return;
    }
    const b = KEY_MAP[e.code];
    if (!b) return;
    input.keys |= b;
    e.preventDefault();
  });
  window.addEventListener('keyup', (e) => {
    const b = KEY_MAP[e.code];
    if (!b) return;
    input.keys &= ~b;
    e.preventDefault();
  });
  window.addEventListener('blur', () => { input.keys = 0; });
}

// The left stick is analog (roundAxes): nothing within 0.1 of the center
// (sticks do not return to exactly 0), full from 0.9. The d-pad is digital.
const PAD_DEAD = 0.1, PAD_FULL = 0.9;

function pollGamepad(input) {
  if (!navigator.getGamepads) return;
  let bits = 0, x = 0, y = 0;
  for (const gp of navigator.getGamepads()) {
    if (!gp) continue;
    const [sx, sy] = roundAxes(gp.axes[0] || 0, gp.axes[1] || 0, PAD_DEAD, PAD_FULL);
    x = strongest(x, sx);
    y = strongest(y, sy);
    const b = gp.buttons;
    const pressed = (i) => b[i] && b[i].pressed;
    if (pressed(14)) bits |= BTN_LEFT;
    if (pressed(15)) bits |= BTN_RIGHT;
    if (pressed(12)) bits |= BTN_UP;
    if (pressed(13)) bits |= BTN_DOWN;
    if (pressed(0) || pressed(2) || pressed(3) || pressed(7)) bits |= BTN_A;
    if (pressed(1) || pressed(4) || pressed(5)) bits |= BTN_B;  // B, shoulders
    if (pressed(9)) bits |= BTN_PAUSE;  // Start
  }
  input.pad = bits;
  input.padX = x;
  input.padY = y;
}

// Virtual game pad: an analog direction disc (both axes at once, so dash +
// turn works), an A button (fire) and a B button (dodge). Shown on touch
// devices, or with the toggle button.
//
// The knob travels 32% of the disc's width from the center. It reads like
// the gamepad's stick (roundAxes): nothing within 6% of the width, full from
// 28% (a little short of the end of the travel, so a thumb that does not
// quite reach the rim still gets it), in any direction.
const DISC_TRAVEL = 0.32, DISC_DEAD = 0.06, DISC_FULL = 0.28;
function setupTouchPad(input) {
  const dpad = document.getElementById('dpad');
  const abtn = document.getElementById('abtn');
  const bbtn = document.getElementById('bbtn');
  const knob = dpad ? dpad.querySelector('.knob') : null;
  if (!dpad || !abtn || !bbtn) return;

  const coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
  if (coarse || 'ontouchstart' in window) document.body.classList.add('touch');

  let fire = 0, dodge = 0;
  let dpadPointer = null;
  function update() { input.touch = fire | dodge; }

  function dirFromEvent(e) {
    const r = dpad.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    let dx = e.clientX - cx, dy = e.clientY - cy;
    const max = r.width * DISC_TRAVEL;
    const len = Math.hypot(dx, dy);
    if (len > max) { dx *= max / len; dy *= max / len; }
    if (knob) knob.style.transform = `translate(${dx}px, ${dy}px)`;
    [input.touchX, input.touchY] =
      roundAxes(dx, dy, r.width * DISC_DEAD, r.width * DISC_FULL);
  }
  dpad.addEventListener('pointerdown', (e) => {
    dpadPointer = e.pointerId;
    dpad.setPointerCapture(e.pointerId);
    dpad.classList.add('down');
    dirFromEvent(e);
    e.preventDefault();
  });
  dpad.addEventListener('pointermove', (e) => {
    if (e.pointerId !== dpadPointer) return;
    dirFromEvent(e);
    e.preventDefault();
  });
  const dpadUp = (e) => {
    if (e.pointerId !== dpadPointer) return;
    dpadPointer = null;
    input.touchX = input.touchY = 0;
    dpad.classList.remove('down');
    if (knob) knob.style.transform = '';
  };
  dpad.addEventListener('pointerup', dpadUp);
  dpad.addEventListener('pointercancel', dpadUp);
  dpad.addEventListener('lostpointercapture', dpadUp);

  abtn.addEventListener('pointerdown', (e) => {
    fire = BTN_A;
    abtn.classList.add('down');
    abtn.setPointerCapture(e.pointerId);
    update();
    e.preventDefault();
  });
  const aUp = () => { fire = 0; abtn.classList.remove('down'); update(); };
  abtn.addEventListener('pointerup', aUp);
  abtn.addEventListener('pointercancel', aUp);
  abtn.addEventListener('lostpointercapture', aUp);
  bbtn.addEventListener('pointerdown', (e) => {
    dodge = BTN_B;
    bbtn.classList.add('down');
    bbtn.setPointerCapture(e.pointerId);
    update();
    e.preventDefault();
  });
  const bUp = () => { dodge = 0; bbtn.classList.remove('down'); update(); };
  bbtn.addEventListener('pointerup', bUp);
  bbtn.addEventListener('pointercancel', bUp);
  bbtn.addEventListener('lostpointercapture', bUp);
  for (const el of [dpad, abtn, bbtn]) {
    el.addEventListener('contextmenu', (e) => e.preventDefault());
  }
}

// Phones: the page becomes the console (see the CSS for body.mobile). The
// first tap enters fullscreen where the browser allows it (Android); iPhones
// have no element fullscreen, there the page fills the visible viewport and
// "Add to Home Screen" (manifest.json) gives a real fullscreen app.
function setupMobile() {
  const coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
  const small = Math.min(window.innerWidth, window.innerHeight) < 700;
  if (!coarse || !small) return;
  document.body.classList.add('mobile');
  document.body.classList.add('touch');
  const overlay = document.getElementById('tapstart');
  const stage = document.getElementById('stage');
  if (!overlay) return;
  // Already running as an installed app: no need for the tap
  const standalone = (window.matchMedia && window.matchMedia('(display-mode: standalone)').matches) ||
    (window.matchMedia && window.matchMedia('(display-mode: fullscreen)').matches) ||
    window.navigator.standalone === true;
  if (standalone) return;
  overlay.hidden = false;
  overlay.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    overlay.hidden = true;
    if (stage && stage.requestFullscreen) {
      stage.requestFullscreen({ navigationUI: 'hide' }).catch(() => {});
    } else if (stage && stage.webkitRequestFullscreen) {
      try { stage.webkitRequestFullscreen(); } catch (err) { /* ignore */ }
    }
  });
}

function setupButtons() {
  const fs = document.getElementById('fullscreen');
  const stage = document.getElementById('stage');
  if (fs && stage) {
    if (!stage.requestFullscreen) fs.style.display = 'none';
    fs.addEventListener('click', () => {
      if (document.fullscreenElement) document.exitFullscreen();
      else stage.requestFullscreen({ navigationUI: 'hide' }).catch(() => {});
    });
  }
  const tt = document.getElementById('touchtoggle');
  if (tt) tt.addEventListener('click', () => document.body.classList.toggle('touch'));
}

// The toolbar's sound button: reads and writes the game's mute through the
// two callbacks; refresh() relabels it when the game toggled the mute itself
function setupSoundToggle(isMuted, setMuted) {
  const btn = document.getElementById('soundtoggle');
  if (!btn) return null;
  const ja = /^ja\b/i.test(navigator.language || '');
  const refresh = () => {
    btn.textContent = ja ? (isMuted() ? 'サウンド OFF' : 'サウンド ON')
                         : (isMuted() ? 'Sound OFF' : 'Sound ON');
  };
  refresh();
  btn.addEventListener('click', () => {
    setMuted(!isMuted());
    refresh();
  });
  return { refresh };
}
