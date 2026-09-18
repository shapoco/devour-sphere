# PlatformIO pre-build script (platformio.ini, extra_scripts): puts the
# browser's sound pack, docs/play/se.bin (signed 16-bit PCM, built by
# `make` in impl/wasm/), at se/se.bin, from where board_build.embed_files
# links it into the ESP32S3 firmware as _binary_se_se_bin_start. A copy,
# not a symlink: the embed step reads the path relative to this directory.
import shutil
from pathlib import Path

Import("env")  # noqa: F821 (PlatformIO injects it)

here = Path(env["PROJECT_DIR"])
src = here / ".." / ".." / ".." / "docs" / "play" / "se.bin"
dst = here / "se" / "se.bin"
if not src.exists():
    raise SystemExit(f"{src}: missing (run `make` in impl/wasm/ to build the pack)")
dst.parent.mkdir(exist_ok=True)
if not dst.exists() or dst.read_bytes() != src.read_bytes():
    shutil.copyfile(src, dst)
