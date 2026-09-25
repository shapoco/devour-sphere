# Devour Sphere for ESPboy

Firmware for [ESPboy](https://www.espboy.com/) (an ESP8266 handheld with a
128x128 ST7735) running [Devour Sphere](../../README.md). It is the smallest
machine of the ports: 96 KB of RAM and one core without an FPU.

- The core is built in a reduced configuration: a quarter-size sphere with
  48 enemies, no depth buffer, flat shading.
- One 16-row band, drawn and pushed to the panel by the CPU (no DMA).
  The simulation runs at 30 Hz.
- Sound effects are a one-voice square wave on the speaker.
  The high score is kept in a flash partition of its own.
- An ESP8266_RTOS_SDK v3.4 project -- not Arduino, which leaves too little RAM.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Download

Get the zip from [Releases](https://github.com/shapoco/devour-sphere/releases)
and run `espboy/upload.sh [PORT] [BAUD]` (esptool; PORT defaults to
/dev/ttyUSB0). The image is written in one go at 0x0, so the WildCardBoy
host can load it too: put the .bin under `/WCB/Cards/ESPboy/Apps/` on its TF
card and pick it from the Apps menu.

## Controls

| Action | Button |
|---|---|
| Turn | ← → |
| Dash / brake | ↑ / ↓ |
| Fire / confirm | A (ACT) |
| Emergency dodge | B (ESC) |
| Pause / resume | left shoulder (LFT) |
| Toggle mute | ↓ on the title or pause screen |
| Timing overlay | right shoulder (RGT) on the title or pause screen |
| Benchmark | hold B for 3 s on the title (A next page, B close) |

## Build

With [ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK) v3.4
and its toolchain (setting them up has a few snags on current systems: see
SPEC.md):

```sh
cd impl/espboy/devoursphere
./build.sh                     # IDF_PATH defaults to ${HOME}/esp/ESP8266_RTOS_SDK
./flash.sh /dev/ttyUSB0        # esptool over the WeMos D1 mini's USB serial
./monitor.sh /dev/ttyUSB0      # start-up log
```

`build.sh` also makes `build/devoursphere.factory.bin`, a single image to be
written at 0x0. After changing `sdkconfig.defaults`, run `./build.sh clean`.

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
