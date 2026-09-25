# Devour Sphere for Xiamocon

Firmware for [Xiamocon](https://github.com/shapoco/xiamocon), a handheld
motherboard for the XIAO RP2350 and the XIAO ESP32S3, running
[Devour Sphere](../../README.md). One source builds for both boards.

- 240x240 ST7789, drawn in two 40-row bands over DMA with no frame buffer.
- RP2350: pico-sdk, both cores (one simulates, the other draws).
  ESP32S3: PlatformIO + Arduino.
- Sound effects on both (RP2350: PCM streamed to PWM by DMA; ESP32S3: I2S PDM).
- The high score is kept in flash (RP2350: the last sector; ESP32S3: NVS).
- Only the display transfer, DMA, input and power management come from the
  Xiamocon SDK; everything is drawn by the core with ShapoGFX.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Download

Get the zip from [Releases](https://github.com/shapoco/devour-sphere/releases).

- **XIAO RP2350** (`xiamocon-rp2350/devour-sphere.uf2`): put the board in mass
  storage mode (hold Down, hold the power button for 3 seconds, then release
  Down) and copy the .uf2 onto the drive that appears.
- **XIAO ESP32S3** (`xiamocon-esp32s3/`): `./upload.sh [PORT] [BAUD]` writes
  `devour-sphere.factory.bin` at 0x0 with esptool (PORT defaults to
  /dev/ttyACM0). If the port does not appear, hold BOOT, tap RESET, release
  BOOT and try again.

## Controls

| Action | Button |
|---|---|
| Turn | ← → |
| Dash / brake | ↑ / ↓ |
| Fire / confirm | A / Y |
| Emergency dodge | B |
| Pause / resume | X |
| Toggle mute | ↓ on the title or pause screen |
| Timing overlay | FUNC, or ↑ on the title or pause screen |
| Benchmark | hold B for 3 s on the title (A next page, B close) |

## Build

With the [Xiamocon SDK](https://github.com/shapoco/xiamocon) set up
(`xmc` on the PATH):

```sh
source ~/path/to/xiamocon/setup.shrc
cd impl/xiamocon/devoursphere
xmc build                                  # both targets
xmc build -p rp2350_pico_sdk               # .cmake/devoursphere.uf2
xmc build -p esp32s3_pio_arduino           # .pio/build/esp32s3_arduino/firmware.bin
xmc run   -p rp2350_pico_sdk -d E                  # build and flash (E: is the drive, on WSL2)
xmc run   -p esp32s3_pio_arduino -s /dev/ttyACM0   # build and flash
```

Put the board in mass storage mode before flashing the RP2350 (see
Download above).

The RP2350 build packs the sound effects and needs ffmpeg and python3.
After updating the ShapoGFX submodule or core/, run `rm -rf .pio/libdeps`
before building for the ESP32S3 (see SPEC.md).

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
