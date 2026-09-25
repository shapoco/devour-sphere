# Devour Sphere

https://github.com/user-attachments/assets/b8855ba7-7ce3-45a3-9633-efb35ae5db60

English | [日本語](README.ja.md)

A 3D shooter played above "the Sphere", a cyberspace world: fight the other
entities, devour the fragments they drop and grow huge.
The same core program runs on embedded boards (RP2350 / RP2040 / ESP32S3 /
ESP32-P4) and in the browser (WebAssembly).

The game is a sample application written to show off [ShapoGFX](https://github.com/shapoco/shapo-gfx),
a graphics library for embedded systems. All of the drawing goes through it:
scanline 3D that needs no framebuffer, line and point primitives, 2D drawing
and fonts.

- **Play:** https://shapoco.github.io/devour-sphere/play/
- **Specifications (Japanese):** [SPEC.md](SPEC.md), [core/SPEC.md](core/SPEC.md),
  and a SPEC.md in each port's directory (see [Ports](#ports))

## Download

- **M5Stack Tab5:** a prebuilt firmware can be flashed straight from
  [M5Burner](https://docs.m5stack.com/en/download) — look for "Devour Sphere"
  in its ESP32-P4 (Tab5) list.
- **Other boards:** binaries for Xiamocon (RP2350 / ESP32S3), PicoSystem,
  M5StickS3 and ESPboy are
  zipped up under [Releases](https://github.com/shapoco/devour-sphere/releases);
  the README.txt inside says how to flash each one.
- **Browser:** nothing to download, just follow the Play link above.

## Ports

Each port's README says how to flash it, which button does what, and how to
build it.

| Device | Chip | Directory |
|---|---|---|
| Browser | WebAssembly | [impl/wasm/](impl/wasm/README.md) |
| [Xiamocon](https://github.com/shapoco/xiamocon) | RP2350 / ESP32S3 | [impl/xiamocon/](impl/xiamocon/README.md) |
| [PicoSystem](https://shop.pimoroni.com/products/picosystem) | RP2040 | [impl/picosystem/](impl/picosystem/README.md) |
| [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) | ESP32-P4 | [impl/m5tab5/](impl/m5tab5/README.md) |
| [M5StickS3](https://docs.m5stack.com/en/core/M5StickS3) | ESP32-S3 | [impl/m5sticks3/](impl/m5sticks3/README.md) |
| [ESPboy](https://www.espboy.com/) | ESP8266 | [impl/espboy/](impl/espboy/README.md) |
| Terminal | any POSIX | [impl/cli/](impl/cli/README.md) |

## Controls

Every port has the same actions; how they are mapped to its keys, pad or
tilt is in its README.

| Action | |
|---|---|
| Turn | left / right |
| Dash (costs health) | up |
| Brake (turns faster) | down |
| Fire / confirm | A |
| Emergency dodge | B: a 0.3 s invincible barrel roll, once every 3 s |
| Pause / resume | a pause button |
| Toggle mute | down on the title or pause screen |
| Benchmark | hold B for 3 s on the title (results: A next page, B close) |

Analog inputs (a gamepad stick, an on-screen disc, tilt) set how hard you
turn, dash or brake by how far they are pushed.

## Build

The core, its tests and the native tools build on the host with CMake:

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
```

Each port is built in its own directory: see its README.

## How it is built

Devour Sphere comes in two layers. The **core module** on top is plain C++17
with no platform of its own: the rules of the game and the whole 3D renderer
live there. Only the layer below it is written per machine.

![](./docs/image/stack.png)

### The core module (what you do not have to write)

- **Simulator** ([core/src/sim/](core/src/sim/)) is the rules and the physics.
  Integer arithmetic only, so the same input gives the same result on every
  machine. It knows nothing about drawing.
- **Renderer** ([core/src/render/](core/src/render/)) reads the simulation and
  draws it with ShapoGFX. It is platform independent, but the buffer it draws
  into is handed to it from below.
- **ShapoGFX** is a library of its own (a submodule). The platform layer may
  use it directly too, for an on-screen pad or a timing overlay.

The core never allocates and never calls an OS or an SDK. A C++17 compiler is
the whole dependency.

### What a port has to provide

The **main loop, the input and the display** are required; sound and the stored
high score can be left out and the game still plays.

#### Main loop

```cpp
#include "devoursphere/devoursphere.hpp"
namespace ds = devoursphere;
namespace g2 = shapoco::gfx2d;

static ds::sim::Game game;            // ~84 KB -- never on a stack
static ds::render::Renderer renderer; // ~24 KB
static uint8_t arena[64 * 1024];      // working memory of the 3D renderer
static uint8_t band[W * BAND_H * 2];  // a band, not a frame buffer

renderer.init(W, H, arena, sizeof(arena));
game.reset(seed);

for (;;) {
  while (tickIsDue()) {          // fixed rate (60 Hz by default)
    game.tick(readInput());      // the input driver (returns a sim::Input)
    renderer.pollEffects(game);
    playSounds(game.sounds());   // the sound driver (optional)
  }
  renderer.beginFrame(game, dt); // camera and scene for this frame
  for (int y = 0; y < H; y += BAND_H) {
    auto s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, W, BAND_H, band);
    renderer.renderBand(s, y, BAND_H);
    pushBand(band, y, BAND_H);   // the display driver (SPI + DMA, ...)
  }
  renderer.endFrame();
}
```

The simulation steps at a fixed rate (60 Hz, or 30 Hz with
`DEVOURSPHERE_TICK_RATE=30`); the frame rate is whatever the device manages. If
a frame runs several ticks to catch up, call `pollEffects()` after each one --
the events a tick raises are cleared by the next.

#### Input (required)

All `tick()` takes is a `sim::Input`
([entities.hpp](core/include/devoursphere/sim/entities.hpp)): two direction
axes `x` / `y` from -127 to +127, and three buttons (`Button::A / B / PAUSE`).
Positive `x` turns right; negative `y` dashes and positive `y` brakes. How far
the axis is pushed is how hard it acts.

- **A digital d-pad** can hand over its button bits as they are
  (`Button::LEFT / RIGHT / UP / DOWN` included): an `Input` converts from them,
  and each direction becomes full strength, -127 / 0 / +127
  (`game.tick(Button::A | Button::LEFT)` works).
- **An analog control** (a stick, a touch disc, tilt) is mapped onto -127..+127
  by the port, with **a dead zone and a saturation point it chooses for its
  device**. The core takes the value as a linear strength. For a stick in a
  round gate, map it so that a full diagonal is full on both axes: a full turn
  under a full brake is the quick turn (`roundAxes()` in the wasm port is an
  example).
- On the menus the core reads the axes as a d-pad (a direction is pressed from
  64 and released below 40).

The lines of help on the title screen are replaced with
`Renderer::setControlHints()`.

#### Display (required)

**No frame buffer is needed.** `renderBand()` draws any range of rows, so one
or two buffers a few dozen rows tall are enough: draw a band, push it, repeat
(the PicoSystem port alternates two 240x40 bands; the terminal port draws the
whole frame in one call). The target is a ShapoGFX `Surface` in `GRAY1`,
`RGB444`, `ARGB4444` or `RGB565_SWAPPED`. The screen size is just what you pass to
`init()` -- the HUD scales itself to it. If the platform paints part of the
frame itself, `setHudInsets()` keeps the HUD out of the way.

#### Sound (optional)

`Game::sounds()` returns what the last tick asked to be played, one bit per
`SoundKind`. The core only says what to play: the waveforms and the mixing
belong to the platform. A machine with a single voice picks one by priority
(both handheld ports do). The source material is in [assets/se/](assets/se/).

#### Stored high score (optional)

Encode and decode the 16-byte record in
[high_score_record.hpp](core/include/devoursphere/sim/high_score_record.hpp)
(magic, major version, CRC32) and keep it wherever the device keeps things.
Blank flash, a record of another major version and a damaged one all read back
as "no record". When to write it is the platform's call --
`Game::keepHighScore()` returning true is the cue.

### What it costs

| | |
|---|---|
| RAM | ~150 KB and up (`sim::Game` 84 KB + `Renderer` 24 KB + a 40-64 KB arena + the bands) |
| Flash | ~240 KB of code (the sound waveforms are extra) |
| CPU | 28 fps at 240x240 on an RP2040 (Cortex-M0+, 133 MHz) |

Knobs for when it does not fit or does not keep up:

- `DEVOURSPHERE_TICK_RATE=30` -- half the simulation rate, same behaviour
- `DEVOURSPHERE_SUPPRESS_ALPHA=1` -- no blending, so nothing reads the frame back
- `DEVOURSPHERE_MAX_WIRE` / `MAX_LINES2D` / `MAX_POINTS2D` -- the wireframe and
  2D primitive arrays
- `spanCapacity` of `Renderer::init()` and `setDetailTriangles()` -- how the
  arena is divided, and a cap on the triangles spent on enemy bodies (with it,
  the frame time stops depending on how many are in view)

[core/SPEC.md](core/SPEC.md) (in Japanese) covers this in its "メモリ" and
"描画 (render)" sections, as does [impl/xiamocon/SPEC.md](impl/xiamocon/SPEC.md).

### Ports to read

| Directory | What it is an example of |
|---|---|
| [impl/picosystem/](impl/picosystem/) | the tightest port: RP2040, 264 KB, pico-sdk alone, bands over DMA, both cores |
| [impl/xiamocon/](impl/xiamocon/) | one source built for both RP2350 (pico-sdk) and ESP32S3 (Arduino) |
| [impl/m5tab5/](impl/m5tab5/) | ESP-IDF, a full-size frame plus hardware scale and rotate, a touch pad |
| [impl/m5sticks3/](impl/m5sticks3/) | 240x135 on its side, steered by tilting the board (IMU), shake to pause |
| [impl/espboy/](impl/espboy/) | the smallest machine: ESP8266, 96 KB, one core, the core built in a reduced configuration, bands pushed by the CPU |
| [impl/wasm/](impl/wasm/) | the browser (Emscripten): keyboard, gamepad, touch, localStorage |
| [impl/cli/](impl/cli/) | the shortest one: one `renderBand()` for the whole frame, out to a terminal |

### Ports wanted

If you get Devour Sphere running on a new commercial device or on open-source
hardware, please send a pull request. A directory `impl/<device>/` with the
port's code, a README.md (how to flash it, the controls, how to build it) and
a SPEC.md on what you learned about the machine (how the input and the display
are done, what bit you, and benchmark results if you can) would be ideal.
The benchmark starts when B is held for three seconds on the title screen.

## License

MIT

The sound effects (assets/se/, packed into docs/play/se.bin) are from
[効果音ラボ / Sound Effect Lab](https://soundeffect-lab.info/) and are not covered by
the MIT license: they may be used as part of this game but not redistributed as
material. See assets/se/README.md.
