#!/usr/bin/env python3
"""Merge the bootloader, the partition table and the app into one image.

    merge_bin.py OUT 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 app.bin

The result is what a flasher writes at offset 0 in one go: the ESPboy's
Arduino-style single .bin, which is also what the WildCardBoy host streams
into the card (it erases from 0 for the file's length and writes it as it
is, patching nothing). The gaps are 0xFF, so they read as erased flash.

The RTOS SDK's esptool (v2.4) has no merge_bin, and this needs nothing but
Python. The image header's flash mode / size / frequency bytes are the
bootloader's own (the SDK's elf2image wrote them from sdkconfig), so they
are already right for the board.
"""
import sys


def main(argv):
    if len(argv) < 4 or (len(argv) - 2) % 2 != 0:
        sys.stderr.write(__doc__)
        return 2
    out = argv[1]
    parts = [(int(argv[i], 0), argv[i + 1]) for i in range(2, len(argv), 2)]
    parts.sort()
    image = bytearray()
    for offset, path in parts:
        with open(path, "rb") as f:
            data = f.read()
        if offset < len(image):
            sys.stderr.write("%s at 0x%x overlaps the previous part\n" % (path, offset))
            return 1
        image += b"\xff" * (offset - len(image))
        image += data
    with open(out, "wb") as f:
        f.write(image)
    print("%s: %d bytes" % (out, len(image)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
