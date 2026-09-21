Devour Sphere - @DATE@
Built from commit @COMMIT@
https://github.com/shapoco/devour-sphere

Firmware for Xiamocon (a XIAO RP2350 / ESP32S3 handheld), for PicoSystem
(Pimoroni, RP2040), for M5Stack Tab5 (ESP32-P4) and for M5StickS3
(ESP32-S3). The browser version needs no download: see docs/play/ on the
project page.

Controls on the handhelds: LEFT / RIGHT turn, UP dashes, DOWN brakes, A / Y
fire and confirm, B is the emergency dodge (a 0.3 s barrel roll that enemy
bullets pass through, once every 3 s), X pauses. On the title or pause screen DOWN toggles mute
and UP toggles the timing overlay (Xiamocon: FUNC toggles it any time).

The Tab5 is played with an on-screen pad: a direction disc in the bottom
left (push it the way you want to go; up dashes, down brakes), A in the
bottom right, the emergency dodge above and left of it, and pause in the top
right corner. Down on the disc toggles mute and up toggles the timing
overlay, on the title or pause screen.

The StickS3 is played by tipping the stick itself. At start up it asks you
to lay it on its side -- either side will do, the picture follows -- and
whatever attitude you hold it still in becomes the neutral one. Tipping it
from there steers; tipping its far edge away dashes and tipping it towards
you brakes. KEY1 fires and confirms, KEY2 is the emergency dodge, and
shaking the stick pauses (shake again to resume). On the title or pause
screen, tipping it towards you toggles mute and KEY2 toggles the timing
overlay.

xiamocon-rp2350/  - for XIAO RP2350
  devour-sphere.uf2
    Put the board in mass storage mode (hold Down, hold the power button for
    3 seconds, then release Down) and copy the .uf2 onto the drive that
    appears. The board reboots into the game by itself.

xiamocon-esp32s3/ - for XIAO ESP32S3
  devour-sphere.factory.bin, upload.sh
    ./upload.sh [PORT] [BAUD]        (PORT defaults to /dev/ttyACM0)
    The script calls esptool ("pip install esptool" if you do not have it).
    The image is bootloader + partition table + boot_app0 + app merged at
    their offsets, so it is written in one go at 0x0. If the port does not
    appear, hold BOOT, tap RESET, release BOOT and try again.

picosystem/       - for PicoSystem
  devour-sphere.uf2
    Hold X while switching the PicoSystem on to get the BOOTSEL drive, then
    copy the .uf2 onto it.

m5tab5/           - for M5Stack Tab5
  devour-sphere.factory.bin, upload.sh
    ./upload.sh [PORT] [BAUD]        (PORT defaults to /dev/ttyACM0)
    The script calls esptool ("pip install esptool" if you do not have it).
    The image is bootloader + partition table + app merged at their offsets,
    so it is written in one go at 0x0. If the port does not appear, hold the
    BOOT button next to the USB-C port, tap RESET, release BOOT.

m5sticks3/        - for M5StickS3
  devour-sphere.factory.bin, upload.sh
    ./upload.sh [PORT] [BAUD]        (PORT defaults to /dev/ttyACM0)
    The script calls esptool ("pip install esptool" if you do not have it).
    The image is bootloader + partition table + app merged at their offsets,
    so it is written in one go at 0x0. The board talks over USB-Serial/JTAG,
    so esptool resets it into the bootloader by itself; if no port appears
    at all, try another cable.
