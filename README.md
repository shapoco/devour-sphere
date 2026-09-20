# Devour Sphere

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
  [impl/wasm/SPEC.md](impl/wasm/SPEC.md), [impl/xiamocon/SPEC.md](impl/xiamocon/SPEC.md),
  [impl/picosystem/SPEC.md](impl/picosystem/SPEC.md), [impl/m5tab5/SPEC.md](impl/m5tab5/SPEC.md)

## Download

- **M5Stack Tab5:** a prebuilt firmware can be flashed straight from
  [M5Burner](https://docs.m5stack.com/en/download) — look for "Devour Sphere"
  in its ESP32-P4 (Tab5) list.
- **Other boards:** binaries for Xiamocon (RP2350 / ESP32S3) and PicoSystem are
  zipped up under [Releases](https://github.com/shapoco/devour-sphere/releases);
  the README.txt inside says how to flash each one.
- **Browser:** nothing to download, just follow the Play link above.

## Controls

| Action | Keyboard | Xiamocon | PicoSystem |
|---|---|---|---|
| Turn | ← → / A D | ← → | ← → |
| Dash (costs health) | ↑ / W | ↑ | ↑ |
| Brake (turns faster) | ↓ / S | ↓ | ↓ |
| Fire / confirm | Space / J L | A / Y | A / Y |
| Emergency dodge (0.3 s invincible barrel roll, once every 3 s) | I K / C V B N M | B | B |
| Pause / resume | Esc / P | X | X |
| Toggle mute | ↓ on the title or pause screen | same | same |
| Toggle the timing overlay | (none) | ↑ on the title or pause screen, or FUNC | ↑ on the title or pause screen |

The browser version also takes a gamepad and touch input (an on-screen pad).
The M5Tab5 version is played with that same landscape pad layout: a direction
disc in the bottom left, A in the bottom right, the dodge button above and left
of it, and pause in the top right corner.

## Build

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
make -C impl/wasm        # the WASM version (Emscripten)
./launch_web_server.sh   # http://localhost:52980/play/
```

For [Xiamocon](https://github.com/shapoco/xiamocon) (a XIAO RP2350 / ESP32S3 handheld):

```sh
source ~/path/to/xiamocon/setup.shrc
cd impl/xiamocon/devoursphere
xmc build                      # both targets
```

For PicoSystem (RP2040), with pico-sdk alone:

```sh
cd impl/picosystem
cmake -S . -B build -DPICO_SDK_PATH=~/path/to/pico-sdk
cmake --build build -j         # build/devoursphere.uf2 (packing the sound effects needs ffmpeg and python3)
```

For [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) (ESP32-P4), with ESP-IDF v5.5.x:

```sh
cd impl/m5tab5/devoursphere
./build.sh                     # DS_IDF_PATH defaults to ${HOME}/esp/5.5
./run.sh /dev/ttyACM0          # build and flash (the port may be omitted)
```

## License

MIT

The sound effects (assets/se/, packed into docs/play/se.bin) are from
[効果音ラボ / Sound Effect Lab](https://soundeffect-lab.info/) and are not covered by
the MIT license: they may be used as part of this game but not redistributed as
material. See assets/se/README.md.
