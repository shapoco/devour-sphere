# Devour Sphere for the Tamagotchi Paradise

[Devour Sphere](../../README.md) as a guest program of the Tamagotchi
Paradise (Bandai, 2025; a Sonix SNC73410 with two Cortex-M3 cores at 96 MHz,
a 128x128 panel, three buttons, a dial and a buzzer). The stock firmware
stays on the device: the program is written into free flash, and holding
**A and C on the home screen** starts it. The dial held for two seconds on
the title or the pause screen goes back to the Tamagotchi.

- The core is built in the ESPboy's reduced configuration: a quarter-size
  sphere with 48 enemies, no depth buffer, flat shading, 30 Hz simulation.
- Two 32-row bands: one goes to the panel by DMA while the next is drawn. The
  hot code runs from the 64 KB program RAM, the rest from the cached flash window.
- Sound effects are one-voice tunes on the buzzer. The high score lasts
  until you leave. The only flash write is on the way back: the Paradise's
  clock moves on by the time spent here, so it does not live that time at once.
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
  the ROM (`make_bin.sh` runs both). Optional: `pip install unicorn pillow` for `tools/sim.py`.

Nothing here contains or needs the firmware; the program is put into
**your own dump**.

## Build and write

Put your backup at `roms/golden.bin` (the `roms/` directory is kept out of
git) and run the script at the top of this directory:

```sh
cd impl/tamapara
./make_bin.sh        # builds hello/ and devoursphere/, then writes
                     # roms/hello.bin and roms/devoursphere.bin
```

Each output is your backup with one program added: `roms/hello.bin` holds
the test program, `roms/devoursphere.bin` the game. Then in Paramix Studio:
open the file, "Device", put the Paradise on its download screen (Planet, A,
Connect, Download, Download), Connect, and "Write new ROM to the device"
with "Only write changes" ticked (18 sectors of 4 KB for hello, about 50
for the game). On the device: home screen, hold A and C for about two
seconds.

Try `roms/hello.bin` first: it shows the buttons, the dial's steps, a
blinking dot and beeps on B, and the dial held 2 s goes back. Read SPEC.md
"書き込みの手順と注意" before the first write.

The script works for every edition: where the program goes depends on how
far the firmware reaches (Land, Water and Sky have the Studio's DOOM place
free; Glacier and Tropical do not), and `mkrom.js --place` reads that off
the dump before the game is built for it. Another dump can be given as the
first argument. The pieces by hand: `hello/build.sh`, `devoursphere/build.sh`
(`XIP_FLASH_OFF` from `--place`) and `tools/mkrom.js` (see SPEC.md).

**Back to the stock state:** write your backup the same way (only the
changed sectors go), or `node tools/mkrom.js --rom roms/devoursphere.bin --remove --out roms/restored.bin`
and write that. The firmware itself is never modified except for the four
home screen handlers the launcher hooks; Paramix Studio's own rescue (hold C
while switching on) stays available if it was installed.

## Controls

| Action | On the Paradise |
|---|---|
| Turn left / right | turn the dial: counterclockwise left, clockwise right, five clicks to the stop, back to the middle for straight ahead |
| Menus | one click of the dial = one press of left / right |
| Dash / brake | B / C |
| Fire / confirm | A |
| Emergency dodge | press the dial |
| Pause / resume | B and C pressed together (within 0.2 s of each other) |
| Toggle mute | a short press of C on the title or pause screen |
| Timing overlay | hold the dial, press A on the title or pause screen |
| Back to the Tamagotchi | hold the dial 2 s on the title or pause screen |
| Benchmark | hold B and C 3 s on the title (A next page, dial close) |

The benchmark's result lines also go out of the dongle's UART at 460800 baud.

## License

GPL-3.0-or-later for this directory. The game core and ShapoGFX are MIT;
the sound effects of the other builds are not used here.
