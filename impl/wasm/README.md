# Devour Sphere for the browser (WebAssembly)

The browser version of [Devour Sphere](../../README.md). The core program is
built to WebAssembly with Emscripten and drawn into a canvas; the page takes
a keyboard, a gamepad and touch (an on-screen pad), and keeps the high score
and the sound setting in localStorage.

- The screen is 480x320 by default. `?screen=WxH` picks another size (64 to
  1280 per side, up to 1280x720 pixels): the HUD lays itself out for any
  shape.
- On a phone the page turns into a handheld: the canvas and an on-screen pad,
  full screen after the first tap (or when started from the home screen).

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Play

https://shapoco.github.io/devour-sphere/play/ -- nothing to download.

## Controls

| Action | Keyboard | Gamepad | On-screen pad |
|---|---|---|---|
| Turn | ← → / A D | left stick (analog), d-pad | direction disc (analog) |
| Dash / brake | ↑ / W, ↓ / S | left stick (analog), d-pad | direction disc (analog) |
| Fire / confirm | Space / J L / Enter | buttons 0, 2, 3, 7 | A |
| Emergency dodge | I K / C V B N M | buttons 1, 4, 5 (B and the shoulders) | B (the small circle above and left of A) |
| Pause / resume | Esc / P | Start | -- |
| Toggle mute | ↓ on the title or pause screen, or the Sound button | same | same |
| Benchmark | hold a B key for 3 s on the title | | |

The stick and the disc are analog: how far you push sets how hard you turn,
dash or brake, and a full diagonal is full on both axes.

## Build

Emscripten (`emcc`) and ffmpeg are needed.

```sh
cd impl/wasm
make            # docs/play/devoursphere.wasm and docs/play/se.bin
make serve      # serves docs/ at http://localhost:52980/ (fetch does not work over file://)
```

The native build (renders one frame to a PPM, for checking without a
browser) comes with the top-level CMake build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/impl/wasm/devoursphere_native [level] [script] [out.ppm] [auto] [seed]
```

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
