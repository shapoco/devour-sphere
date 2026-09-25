# Devour Sphere in a terminal

[Devour Sphere](../../README.md) played in a terminal, half as a joke: each
frame is drawn into a 320x240 frame buffer and turned into colored ASCII art
(braille and half blocks as extras).

- C++17, cmake and POSIX are all it needs: no libraries, no packages to install.
  The same code for Linux, macOS and the BSDs.
- Key presses and releases come from the kitty keyboard protocol; on a
  terminal without it they are guessed from the key repeat, which feels
  sluggish.
- The only sound is the terminal bell. The high score is kept in
  `devoursphere.highscore` in the current directory.

Implementation details (in Japanese): [SPEC.md](SPEC.md)

## Controls

The same keys as the browser version, plus quit and redraw.

| Action | Key |
|---|---|
| Turn | ← → / A D |
| Dash / brake | ↑ / W, ↓ / S |
| Fire / confirm | Space / J L / Enter |
| Emergency dodge | I K / C V B N M |
| Pause / resume | Esc / P |
| Toggle mute | ↓ on the title or pause screen |
| Quit | Q / Ctrl-C |
| Redraw | Ctrl-L |
| Benchmark | hold a B key for 3 s on the title |

## Build

```sh
cd impl/cli
make                           # build/devoursphere
./build/devoursphere           # play
./build/devoursphere --help    # --mode=braille / --mode=half, --fps, --stats, ...
```

## License

MIT, except the sound effects: see [the top-level README](../../README.md#license).
