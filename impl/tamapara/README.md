# Devour Sphere for the Tamagotchi Paradise

[Devour Sphere](../../README.md) as a guest program of the Tamagotchi
Paradise (Bandai, 2025; a Sonix SNC73410 with two Cortex-M3 cores at 96 MHz,
a 128x128 panel, three buttons, a dial and a buzzer). The stock firmware
stays on the device: the program is written into free flash, and holding
**A and C on the home screen** starts it. The dial held for two seconds on
the title or the pause screen goes back to the Tamagotchi.

- The core is built in the ESPboy's reduced configuration: a quarter-size
  sphere with 48 enemies, no depth buffer, flat shading, 30 Hz simulation.
- One 32-row band, drawn and pushed to the panel by the CPU. The hot code
  runs from the 64 KB program RAM, the rest from the cached flash window.
- Sound effects are one-voice tunes on the buzzer. The high score is kept in
  a flash sector of its own.
- No SDK: bare metal, the hardware layer comes from
  [Paramix Studio](https://github.com/mameise/Paramix-Studio) (mameise).

Implementation details (in Japanese): [SPEC.md](SPEC.md).

**This directory is GPL-3.0-or-later** ([LICENSE](LICENSE), [hw/NOTICE.md](hw/NOTICE.md)),
unlike the rest of the repository (MIT): the hardware layer is derived from
Paramix Studio. The game core it links against stays MIT.

## What you need

- A Tamagotchi Paradise, a backup of its 16 MB flash (read with Paramix
  Studio's "Device" button) and the UART dongle Paramix Studio uses
  (3.3 V, the three contacts at the top; Chrome or Edge for Web Serial).
- `arm-none-eabi-gcc` (13 is known to work) and `node` to build and to make
  the ROM. Optional: `pip install unicorn pillow` for `tools/sim.py`.

Nothing here contains or needs the firmware; the program is put into
**your own dump**.

## Build and write

```sh
cd impl/tamapara/devoursphere
./build.sh                              # build/pram.bin, build/xip.bin  (Glacier / Tropical layout)
# XIP_FLASH_OFF=0xB1000 ./build.sh      # Land / Water / Sky (see SPEC.md)
node ../tools/mkrom.js --rom /path/to/backup.bin --pram build/pram.bin --xip build/xip.bin --out /path/to/new.bin
```

Then in Paramix Studio: open `new.bin`, "Device", put the Paradise on its
download screen (Planet, A, Connect, Download, Download), Connect, and
"Write new ROM to the device" with "Only write changes" ticked. About 50
sectors of 4 KB are written. On the device: home screen, hold A and C for
about two seconds.

**Back to the stock state:** write your backup the same way (only the
changed sectors go), or `node tools/mkrom.js --rom new.bin --remove --out restored.bin`
and write that. The firmware itself is never modified except for the four
home screen handlers the launcher hooks; Paramix Studio's own rescue (hold C
while switching on) stays available if it was installed.

Try `hello/` first (`./build.sh`, then `mkrom.js` with `--pram hello/build/hello.bin`
and no `--xip`): it shows the buttons, the dial's steps, a blinking dot and
beeps on B, and the dial held 2 s goes back. Read SPEC.md "書き込みの手順と注意" before the
first write.

## Controls

| Action | On the Paradise |
|---|---|
| Turn left / right | A / B |
| Fire / confirm | C |
| Emergency dodge | press the dial |
| Dash / brake | turn the dial clockwise / back (a throttle: it stays where you leave it) |
| Pause / resume | A and B together |
| Toggle mute | turn the dial on the title or pause screen |
| Timing overlay | hold A, press C on the title or pause screen |
| Back to the Tamagotchi | hold the dial 2 s on the title or pause screen |
| Benchmark | hold A and B 3 s on the title (C next page, dial close) |

The benchmark's result lines also go out of the dongle's UART at 460800 baud.

## License

GPL-3.0-or-later for this directory. The game core and ShapoGFX are MIT;
the sound effects of the other builds are not used here.
