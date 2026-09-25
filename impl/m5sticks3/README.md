# Devour Sphere for M5StickS3

Firmware for [M5StickS3](https://docs.m5stack.com/en/core/M5StickS3)
(an ESP32-S3 stick) running [Devour Sphere](../../README.md), steered by
tipping the stick itself.

- The 135x240 ST7789 is used on its side as 240x135, drawn in two 45-row
  bands with no frame buffer (the panel does the rotation).
- The direction comes from the IMU (BMI270) as an analog input; shaking the
  stick pauses.
- Sound effects through the speaker with any number of voices. The high
  score is kept in NVS.
- ESP-IDF v5.5.x. M5Unified / M5GFX start up the board; the drawing is done
  by the core with ShapoGFX.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Download

Get the zip from [Releases](https://github.com/shapoco/devour-sphere/releases)
and run `m5sticks3/upload.sh [PORT] [BAUD]` (esptool; PORT defaults to
/dev/ttyACM0). The board resets into the bootloader by itself; if no port
appears at all, try another cable.

## Controls

At start up, lay the stick on its side -- either side will do, the picture
follows -- and hold it still: that attitude becomes the neutral one.

| Action | Input |
|---|---|
| Turn | tip left / right (analog, full at 7 degrees) |
| Dash / brake | tip the far edge away / towards you (analog, full at 10 degrees) |
| Fire / confirm | KEY1 |
| Emergency dodge | KEY2 |
| Pause / resume | shake the stick |
| Timing overlay | KEY2 on the title (a short press) or pause screen |
| Benchmark | hold KEY2 for 3 s on the title (KEY1 next page, KEY2 close) |

## Build

With [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.5.x:

```sh
cd impl/m5sticks3/devoursphere
./build.sh                     # DS_IDF_PATH defaults to ${HOME}/esp/5.5
./run.sh /dev/ttyACM0          # build and flash (the port may be omitted)
./monitor.sh                   # serial log
```

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
