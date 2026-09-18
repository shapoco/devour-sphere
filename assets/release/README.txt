Devour Sphere - @DATE@
Built from commit @COMMIT@
https://github.com/shapoco/devour-sphere

Firmware for Xiamocon (a XIAO RP2350 / ESP32S3 handheld) and for PicoSystem
(Pimoroni, RP2040). The browser version needs no download: see docs/play/ on
the project page.

Controls (all three): LEFT / RIGHT turn, UP dashes, DOWN brakes, A / B / Y
fire and confirm, X pauses. On the title or pause screen DOWN toggles mute
and UP toggles the timing overlay (Xiamocon: FUNC toggles it any time).

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
