# Devour Sphere for Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

Firmware for the
[Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm)
(an ESP32-P4 board with a 4.3-inch 480x800 touch screen, "wave_43" in Kern)
running [Devour Sphere](../../README.md).

- A port of the [M5Stack Tab5 version](../m5tab5/README.md) to the same chip.
- The game draws a 640x384 landscape frame in bands; the PPA (the ESP32-P4's
  2D accelerator) scales each band by 5/4, rotates it and swaps its bytes into
  the 480x800 MIPI-DSI panel in one operation.
- Played with an on-screen pad in the same layout as the browser's landscape
  mode.
- Sound effects through the ES8311 codec and the on-board speaker amplifier,
  up to 8 at once, like the browser version. The high score is kept in NVS.
- ESP-IDF v6.1 and its own drivers (no board library): the panel setup comes
  from Kern's BSP, the audio wiring from Waveshare's.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

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

With [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v6.1:

```sh
cd impl/wave43/devoursphere
./build.sh                     # DS_IDF_PATH defaults to ${HOME}/esp
./run.sh /dev/ttyACM0          # build and flash (the port may be omitted)
./monitor.sh                   # serial log
```

`DS_IDF_PATH` is the directory that holds `esp-idf/`.

Flashing replaces whatever is on the board (Kern, say) and resets the NVS
partition on the first boot. A board with flash encryption enabled cannot be
written this way.

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
