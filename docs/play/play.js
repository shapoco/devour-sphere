// Devour Sphere browser front end.
//
// Loads the STANDALONE_WASM module built from impl/wasm/main.cpp, runs the
// simulation at its fixed tick rate, renders through the WASM frame buffer
// (RGB565BE) into a <canvas>, and turns keyboard / gamepad / touch input into
// the button bits of the simulation.
//
// startDevourSphere({ wasm: 'devoursphere.wasm' })
//   URL parameters: ?level=N&weapon=W  skips the menus (debug)
//                   ?seed=N            fixed random seed
//                   ?auto=1            the AI drives the player (demo)

'use strict';

const BTN_LEFT = 1, BTN_RIGHT = 2, BTN_UP = 4, BTN_DOWN = 8, BTN_A = 16;

// RGB565 -> 8-bit expansion tables
const LUT5 = new Uint8Array(32);
const LUT6 = new Uint8Array(64);
for (let i = 0; i < 32; i++) LUT5[i] = Math.round(i * 255 / 31);
for (let i = 0; i < 64; i++) LUT6[i] = Math.round(i * 255 / 63);

async function startDevourSphere(opts) {
  const statusEl = document.getElementById('status');
  const fpsEl = document.getElementById('fps');
  const canvas = document.getElementById('screen');
  const input = new InputState();

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
    ex.ds_init(seed);
    if (params.has('level')) {
      ex.ds_debug_start(parseInt(params.get('level'), 10) || 1,
                        parseInt(params.get('weapon') || '0', 10) || 0);
    }
    if (params.get('auto') === '1') ex.ds_debug_auto(1);

    // High score: kept in the browser
    const HS_KEY = 'devoursphere.highscore';
    let highScore = 0;
    try { highScore = parseInt(localStorage.getItem(HS_KEY) || '0', 10) >>> 0; } catch (e) { /* ignore */ }
    ex.ds_set_high_score(highScore);
    function updateHighScore() {
      const score = ex.ds_get_score() >>> 0;
      if (score > highScore) {
        highScore = score;
        ex.ds_set_high_score(highScore);
        try { localStorage.setItem(HS_KEY, String(highScore)); } catch (e) { /* ignore */ }
      }
    }

    const W = ex.ds_get_width();
    const H = ex.ds_get_height();
    const fbPtr = ex.ds_get_fb();
    const tickRate = ex.ds_get_tick_rate();
    const tickMs = 1000 / tickRate;

    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext('2d', { alpha: false });
    const imgData = ctx.createImageData(W, H);
    const rgba = imgData.data;

    setupKeyboard(input);
    setupTouchPad(input);
    setupButtons();
    setupMobile();

    let acc = 0;
    let last = performance.now();
    let lastRender = last;
    let frames = 0, fpsTime = last;

    function blit() {
      const fb = new Uint8Array(ex.memory.buffer, fbPtr, W * H * 2);
      for (let i = 0, j = 0; i < W * H * 2; i += 2, j += 4) {
        const b0 = fb[i], b1 = fb[i + 1];
        rgba[j] = LUT5[b0 >> 3];
        rgba[j + 1] = LUT6[((b0 & 7) << 3) | (b1 >> 5)];
        rgba[j + 2] = LUT5[b1 & 31];
        rgba[j + 3] = 255;
      }
      ctx.putImageData(imgData, 0, 0);
    }

    function frame(now) {
      acc += Math.min(now - last, 250);  // cap after a pause (tab hidden)
      last = now;
      let ticked = false;
      let n = 0;
      while (acc >= tickMs && n < 4) {
        pollGamepad(input);
        ex.ds_tick(input.buttons());
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
        if (fpsEl) fpsEl.textContent = `${(frames * 1000 / (now - fpsTime)).toFixed(0)} fps`;
        frames = 0;
        fpsTime = now;
      }
      requestAnimationFrame(frame);
    }
    ex.ds_render(0);
    blit();
    requestAnimationFrame(frame);
    if (statusEl) statusEl.textContent = '';
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

class InputState {
  constructor() {
    this.keys = 0;      // keyboard
    this.touch = 0;     // virtual pad
    this.pad = 0;       // gamepad
  }
  buttons() { return this.keys | this.touch | this.pad; }
}

const KEY_MAP = {
  ArrowLeft: BTN_LEFT, KeyA: BTN_LEFT,
  ArrowRight: BTN_RIGHT, KeyD: BTN_RIGHT,
  ArrowUp: BTN_UP, KeyW: BTN_UP,
  ArrowDown: BTN_DOWN, KeyS: BTN_DOWN,
  Space: BTN_A, KeyI: BTN_A, KeyJ: BTN_A, KeyK: BTN_A, KeyL: BTN_A,
  Enter: BTN_A,
};

function setupKeyboard(input) {
  window.addEventListener('keydown', (e) => {
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

function pollGamepad(input) {
  if (!navigator.getGamepads) return;
  let bits = 0;
  for (const gp of navigator.getGamepads()) {
    if (!gp) continue;
    const ax = gp.axes[0] || 0, ay = gp.axes[1] || 0;
    if (ax < -0.4) bits |= BTN_LEFT;
    if (ax > 0.4) bits |= BTN_RIGHT;
    if (ay < -0.4) bits |= BTN_UP;
    if (ay > 0.4) bits |= BTN_DOWN;
    const b = gp.buttons;
    const pressed = (i) => b[i] && b[i].pressed;
    if (pressed(14)) bits |= BTN_LEFT;
    if (pressed(15)) bits |= BTN_RIGHT;
    if (pressed(12)) bits |= BTN_UP;
    if (pressed(13)) bits |= BTN_DOWN;
    if (pressed(0) || pressed(1) || pressed(2) || pressed(3) || pressed(7)) bits |= BTN_A;
  }
  input.pad = bits;
}

// Virtual game pad: a direction disc (diagonals allowed, so dash + turn
// works) and an A button. Shown on touch devices, or with the toggle button.
function setupTouchPad(input) {
  const dpad = document.getElementById('dpad');
  const abtn = document.getElementById('abtn');
  const knob = dpad ? dpad.querySelector('.knob') : null;
  if (!dpad || !abtn) return;

  const coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
  if (coarse || 'ontouchstart' in window) document.body.classList.add('touch');

  let dir = 0, fire = 0;
  let dpadPointer = null;
  function update() { input.touch = dir | fire; }

  function dirFromEvent(e) {
    const r = dpad.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    let dx = e.clientX - cx, dy = e.clientY - cy;
    const dead = r.width * 0.12;
    const max = r.width * 0.32;
    const len = Math.hypot(dx, dy);
    if (len > max) { dx *= max / len; dy *= max / len; }
    if (knob) knob.style.transform = `translate(${dx}px, ${dy}px)`;
    let d = 0;
    if (len > dead) {
      // Sector test: treat the direction as pressed when it dominates enough
      if (dx < -dead * 0.7) d |= BTN_LEFT;
      if (dx > dead * 0.7) d |= BTN_RIGHT;
      if (dy < -dead * 0.7) d |= BTN_UP;
      if (dy > dead * 0.7) d |= BTN_DOWN;
    }
    return d;
  }
  dpad.addEventListener('pointerdown', (e) => {
    dpadPointer = e.pointerId;
    dpad.setPointerCapture(e.pointerId);
    dpad.classList.add('down');
    dir = dirFromEvent(e);
    update();
    e.preventDefault();
  });
  dpad.addEventListener('pointermove', (e) => {
    if (e.pointerId !== dpadPointer) return;
    dir = dirFromEvent(e);
    update();
    e.preventDefault();
  });
  const dpadUp = (e) => {
    if (e.pointerId !== dpadPointer) return;
    dpadPointer = null;
    dir = 0;
    dpad.classList.remove('down');
    if (knob) knob.style.transform = '';
    update();
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
  for (const el of [dpad, abtn]) {
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
