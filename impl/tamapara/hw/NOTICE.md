# Notice: where this directory comes from

Everything in `impl/tamapara/` is licensed under the GNU General Public
License, version 3 or (at your option) any later version (`../LICENSE`),
because the hardware layer in this directory is derived from
**Paramix Studio** by mameise (https://github.com/mameise/Paramix-Studio,
GPL-3.0-or-later), version 1.7.2, commit 4c6cc18005ecb8447bdf05a394bfd0e350bd7ca3.

| File | Origin in Paramix Studio | Changes |
|---|---|---|
| `io.h`, `flash.h`, `flash.c`, `disp_power.h` | `doom/src/pdoom/dev/` | none |
| `hw.c` | `doom/src/pdoom/dev/hw.c` | DOOM's settings block and play-time counter removed; `hw_tone()` restarts the counter when the new period is below it (the TONE_SAFE fix of `emu/src/emu/hw.c`); `hw_px_raw()` sends a whole run in one SPI pass under `LCD_STREAM` |
| `hw.h` | `doom/src/pdoom/dev/hw.h` | `hw_encoder()` added, the FAST_LCD switch dropped (always on) |
| `paraclock.h`, `paraback.h` | `emu/src/emu/` | none |
| `stub_tpl.bin` | `emu/stub_tpl.bin` | none (the launcher template; `emu/README.md` has its build line) |

The register sequences in `hw.c` and `disp_power.h` were traced from the
Paradise's own firmware and flash utility by mameise and confirmed on the
device; nothing here comes from a Sonix SDK.

The game itself (`../../core/`, `../../submodule/shapo-gfx/`) stays under
its own license (MIT); this directory links against it.
