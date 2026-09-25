# Devour Sphere for PicoSystem

Firmware for [PicoSystem](https://shop.pimoroni.com/products/picosystem)
(Pimoroni's RP2040 handheld) running [Devour Sphere](../../README.md).
It is the tightest of the ports: 264 KB of RAM for the whole game.

- 240x240 ST7789, drawn in two 40-row bands over SPI + DMA with no frame buffer.
- A plain pico-sdk project: the PicoSystem SDK is not used (its 115 KB frame
  buffer would not leave room for the game).
- Both cores: one simulates, the other draws. The simulation runs at 30 Hz.
- Sound effects on the piezo speaker, one voice (PCM from flash to PWM by DMA).
- The high score is kept in the last sector of the flash.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Download

Get the zip from [Releases](https://github.com/shapoco/devour-sphere/releases).
Hold X while switching the PicoSystem on to get the BOOTSEL drive, then copy
`picosystem/devour-sphere.uf2` onto it.

## Controls

| Action | Button |
|---|---|
| Turn | ← → |
| Dash / brake | ↑ / ↓ |
| Fire / confirm | A / Y |
| Emergency dodge | B |
| Pause / resume | X |
| Toggle mute | ↓ on the title or pause screen |
| Timing overlay | ↑ on the title or pause screen |
| Benchmark | hold B for 3 s on the title (A next page, B close) |

## Build

With [pico-sdk](https://github.com/raspberrypi/pico-sdk) (2.x), ffmpeg and
python3 (for packing the sound effects):

```sh
cd impl/picosystem
cmake -S . -B build -DPICO_SDK_PATH=~/path/to/pico-sdk
cmake --build build -j         # build/devoursphere.uf2
```

`build.sh` does the same two steps. A prebuilt picotool can be pointed to
with `-Dpicotool_DIR=...`; otherwise the SDK fetches and builds one.

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
