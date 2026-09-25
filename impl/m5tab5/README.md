# Devour Sphere for M5Stack Tab5

Firmware for [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5)
(an ESP32-P4 5-inch tablet) running [Devour Sphere](../../README.md).

- The game draws a 640x360 landscape frame in 45-row bands; the PPA (the
  ESP32-P4's 2D accelerator) scales each band 2x, rotates it and swaps its
  bytes into the 720x1280 MIPI-DSI panel in one operation.
- Played with an on-screen pad in the same layout as the browser's landscape
  mode.
- Sound effects through the speaker with any number of voices, like the
  browser version. The high score is kept in NVS.
- ESP-IDF v5.5.x. M5Unified / M5GFX start up the panel, touch, speaker and
  power; the drawing is done by the core with ShapoGFX.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Download

- **M5Burner:** look for "Devour Sphere" in the ESP32-P4 (Tab5) list of
  [M5Burner](https://docs.m5stack.com/en/download).
- **Releases:** get the zip from
  [Releases](https://github.com/shapoco/devour-sphere/releases) and run
  `m5tab5/upload.sh [PORT] [BAUD]` (esptool; PORT defaults to /dev/ttyACM0).
  If the port does not appear, hold the BOOT button next to the USB-C port,
  tap RESET and release BOOT.

## Controls

| Action | On-screen pad |
|---|---|
| Turn | direction disc (bottom left), left / right (analog) |
| Dash / brake | direction disc, up / down (analog) |
| Fire / confirm | A (bottom right) |
| Emergency dodge | the small circle above and left of A |
| Pause / resume | the button in the top right corner |
| Toggle mute | disc down on the title or pause screen |
| Timing overlay | disc up on the title or pause screen |
| Benchmark | hold the dodge button for 3 s on the title (A next page, B close) |

The disc is analog: how far you push sets how hard you turn, dash or brake,
and a full diagonal is full on both axes.

## Build

With [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.5.x
(5.4 or later for the PPA driver; M5GFX is not verified on 6.0):

```sh
cd impl/m5tab5/devoursphere
./build.sh                     # DS_IDF_PATH defaults to ${HOME}/esp/5.5
./run.sh /dev/ttyACM0          # build and flash (the port may be omitted)
./monitor.sh                   # serial log
```

`DS_IDF_PATH` is the directory that holds `esp-idf/`.

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
