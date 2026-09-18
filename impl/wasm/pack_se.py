#!/usr/bin/env python3
"""Pack the sound effects of materials/se/ into docs/play/se.bin.

Every wav is converted with ffmpeg to mono 16-bit PCM at one sample rate,
the silence at both ends is trimmed (below -50 dB; 20 ms of the tail is
kept), and the results are concatenated behind a small table of contents.
The browser front end (play.js) builds an AudioBuffer per sound straight
from the PCM, so no decoder is involved and one fetch loads them all.
Keeping the sounds in one opaque file is also the "hide the source files
where you can" that the license of the material asks for (materials/se/).

Format (little endian):
  0   "DSSE"
  4   u32 sample rate (Hz)
  8   u32 sound count
  12  u32 total samples
  16  count x { u32 first sample, u32 sample count }
  ... s16le PCM, all sounds back to back

The order of SOUNDS is the order of devoursphere::sim::SoundKind
(core/include/devoursphere/sim/game.hpp): bit N of ds_get_sounds() is
sound N of the pack.

  pack_se.py [-r RATE] [-o OUT] SRC_DIR
"""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

SOUNDS = [
    "shot_vulcan",
    "shot_laser",
    "shot_missile",
    "hit_enemy",
    "hit_player",
    "enemy_killed_small",
    "enemy_killed_big",
    "player_killed",
    "get_fragment",
    "get_upgrade",
    "menu_select",
    "menu_start",
]

TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB,"
        "areverse,"
        "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.02,"
        "areverse")


def convert(path, rate):
    cmd = ["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar",
           str(rate), "-af", TRIM, "-f", "s16le", "-acodec", "pcm_s16le", "-"]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"{path}: ffmpeg failed\n{r.stderr.decode(errors='replace')}")
    pcm = r.stdout
    return pcm[:len(pcm) & ~1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", type=Path, help="directory of the wav files")
    ap.add_argument("-o", "--out", type=Path, default=Path("se.bin"))
    ap.add_argument("-r", "--rate", type=int, default=22050)
    args = ap.parse_args()

    pcms = []
    for name in SOUNDS:
        src = args.src / f"{name}.wav"
        if not src.exists():
            sys.exit(f"{src}: missing (the pack needs every sound of SoundKind)")
        pcms.append(convert(src, args.rate))

    toc = []
    pos = 0
    for pcm in pcms:
        n = len(pcm) // 2
        toc.append((pos, n))
        pos += n
    header = struct.pack("<4sIII", b"DSSE", args.rate, len(SOUNDS), pos)
    header += b"".join(struct.pack("<II", a, n) for a, n in toc)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(header)
        for pcm in pcms:
            f.write(pcm)

    print(f"{args.out}: {len(SOUNDS)} sounds, {args.rate} Hz, "
          f"{len(header) + 2 * pos} bytes")
    for name, (a, n) in zip(SOUNDS, toc):
        print(f"  {name:20s} {n / args.rate:6.3f} s")


if __name__ == "__main__":
    main()
