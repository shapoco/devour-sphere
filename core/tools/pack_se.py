#!/usr/bin/env python3
"""Pack the sound effects of materials/se/ into one PCM file.

Every wav is converted with ffmpeg to mono 16-bit PCM at one sample rate,
the silence at both ends is trimmed (below -50 dB; 20 ms of the tail is
kept), and the results are concatenated behind a small table of contents.
Keeping the sounds in one opaque file is also the "hide the source files
where you can" that the license of the material asks for (materials/se/).

Two flavours:

- Signed PCM (the default; docs/play/se.bin for the browser, which builds an
  AudioBuffer per sound straight from it, so no decoder is involved and one
  fetch loads them all):
    0   "DSSE"
    4   u32 sample rate (Hz)
    8   u32 sound count
    12  u32 total samples
    16  count x { u32 first sample, u32 sample count }
    ... s16le PCM, all sounds back to back

- PWM levels (--pwm WRAP; the PicoSystem streams them by DMA into a PWM
  compare register, so the values are unsigned, silence is the middle
  level (WRAP+1)/2, and each sound starts with a ramp from 0 up to the
  middle and ends with one back down to 0, so that the pin can rest low
  between sounds without a click):
    0   "DSPW"
    4   u32 sample rate (Hz)
    8   u32 sound count
    12  u32 total samples
    16  u32 wrap (levels run 0 .. wrap+1)
    20  u32 ramp length (samples, at both ends of every sound)
    24  count x { u32 first sample, u32 sample count }
    ... u16le levels, all sounds back to back

All fields are little endian. The order of SOUNDS is the order of
devoursphere::sim::SoundKind (core/include/devoursphere/sim/game.hpp): bit N
of Game::sounds() is sound N of the pack.

Loudness (meant for the piezo, which renders nothing below a few hundred
hertz and is quiet): --highpass drops the band the speaker cannot play so
that it does not eat the headroom, --peak normalizes every sound's peak to
that level (dBFS) after the high-pass, and --gain multiplies on top with a
limiter catching what would clip. The browser pack uses none of these and
keeps the material as recorded.

  pack_se.py [-r RATE] [--pwm WRAP [--ramp MS]] [--highpass HZ] [--peak DB]
             [--gain X] [-o OUT] SRC_DIR
"""

import argparse
import math
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
    "launch",
    "arrive",
]

TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB,"
        "areverse,"
        "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.02,"
        "areverse")


def run_ffmpeg(path, rate, filters, output):
    cmd = ["ffmpeg", "-v", "info", "-i", str(path), "-ac", "1", "-ar",
           str(rate), "-af", filters] + output
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"{path}: ffmpeg failed\n{r.stderr.decode(errors='replace')}")
    return r


def peak_db(path, rate, filters):
    """Peak level (dBFS) of the sound after `filters`"""
    r = run_ffmpeg(path, rate, f"{filters},volumedetect", ["-f", "null", "-"])
    for line in r.stderr.decode(errors="replace").splitlines():
        if "max_volume:" in line:
            return float(line.split("max_volume:")[1].split("dB")[0])
    sys.exit(f"{path}: no peak from volumedetect")


def convert(path, rate, highpass=0.0, peak=None, gain=1.0):
    filters = TRIM
    if highpass > 0:
        filters += f",highpass=f={highpass}"
    gain_db = 20 * math.log10(gain) if gain > 0 else 0.0
    if peak is not None:
        gain_db += peak - peak_db(path, rate, filters)
    if abs(gain_db) > 0.01:
        filters += f",volume={gain_db:.2f}dB"
        if gain_db > 0:
            # What the gain pushes past full scale is limited, not clipped
            filters += ",alimiter=limit=0.97:attack=2:release=30:level=false"
    r = run_ffmpeg(path, rate, filters,
                   ["-f", "s16le", "-acodec", "pcm_s16le", "-"])
    pcm = r.stdout
    return pcm[:len(pcm) & ~1]


def to_pwm(pcm, wrap, ramp):
    """s16le bytes -> u16le PWM levels with the ramps at both ends"""
    mid = (wrap + 1) // 2
    n = len(pcm) // 2
    samples = struct.unpack(f"<{n}h", pcm)
    levels = [mid * i // ramp for i in range(ramp)]
    levels += [mid + s * mid // 32768 for s in samples]
    levels += [mid * (ramp - 1 - i) // ramp for i in range(ramp)]
    return struct.pack(f"<{len(levels)}H", *levels)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", type=Path, help="directory of the wav files")
    ap.add_argument("-o", "--out", type=Path, default=Path("se.bin"))
    ap.add_argument("-r", "--rate", type=int, default=22050)
    ap.add_argument("--pwm", type=int, metavar="WRAP",
                    help="write PWM levels 0..WRAP+1 instead of signed PCM")
    ap.add_argument("--ramp", type=float, default=2.0, metavar="MS",
                    help="PWM: ramp at both ends of every sound (default 2 ms)")
    ap.add_argument("--highpass", type=float, default=0.0, metavar="HZ",
                    help="drop the band below HZ (12 dB/oct)")
    ap.add_argument("--peak", type=float, metavar="DB",
                    help="normalize every sound's peak to DB dBFS")
    ap.add_argument("--gain", type=float, default=1.0, metavar="X",
                    help="multiply by X on top (limited, not clipped)")
    args = ap.parse_args()

    pcms = []
    for name in SOUNDS:
        src = args.src / f"{name}.wav"
        if not src.exists():
            sys.exit(f"{src}: missing (the pack needs every sound of SoundKind)")
        pcms.append(convert(src, args.rate, args.highpass, args.peak, args.gain))

    ramp = 0
    if args.pwm is not None:
        if not 1 <= args.pwm <= 65534:
            sys.exit("--pwm: WRAP must be 1..65534")
        ramp = max(0, round(args.rate * args.ramp / 1000))  # 0: no ramps
        pcms = [to_pwm(pcm, args.pwm, ramp) for pcm in pcms]

    toc = []
    pos = 0
    for pcm in pcms:
        n = len(pcm) // 2
        toc.append((pos, n))
        pos += n
    if args.pwm is not None:
        header = struct.pack("<4sIIIII", b"DSPW", args.rate, len(SOUNDS), pos,
                             args.pwm, ramp)
    else:
        header = struct.pack("<4sIII", b"DSSE", args.rate, len(SOUNDS), pos)
    header += b"".join(struct.pack("<II", a, n) for a, n in toc)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(header)
        for pcm in pcms:
            f.write(pcm)

    kind = f"PWM levels 0..{args.pwm + 1}" if args.pwm is not None else "s16"
    print(f"{args.out}: {len(SOUNDS)} sounds, {args.rate} Hz, {kind}, "
          f"{len(header) + 2 * pos} bytes")
    for name, (a, n) in zip(SOUNDS, toc):
        print(f"  {name:20s} {n / args.rate:6.3f} s")


if __name__ == "__main__":
    main()
