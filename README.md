# Devour Sphere

サイバー空間の「スフィア」上空で、他のエンティティと戦い、
フラグメントを喰らって巨大化していく3D シューティングゲームです。
組み込み機器 (RP2350 / RP2040 / ESP32S3) とブラウザ (WebAssembly) で
同じコアプログラムが動きます。

このゲームは組み込み向けグラフィックスライブラリ [ShapoGFX](https://github.com/shapoco/shapo-gfx)
のデモとして作られたサンプルアプリケーションで、描画 (フレームバッファ不要のスキャンライン 3D、
線・点プリミティブ、2D 描画とフォント) はすべて ShapoGFX で行っている。

- **遊ぶ:** https://shapoco.github.io/devour-sphere/play/
- **仕様:** [SPEC.md](SPEC.md), [core/SPEC.md](core/SPEC.md),
  [impl/wasm/SPEC.md](impl/wasm/SPEC.md), [impl/xiamocon/SPEC.md](impl/xiamocon/SPEC.md),
  [impl/picosystem/SPEC.md](impl/picosystem/SPEC.md)

## 操作

| 操作 | キー | Xiamocon | PicoSystem |
|---|---|---|---|
| 旋回 | ← → / A D | ← → | ← → |
| ダッシュ (体力を消費) | ↑ / W | ↑ | ↑ |
| ブレーキ (旋回が速くなる) | ↓ / S | ↓ | ↓ |
| 攻撃・決定 | スペース / J L | A / Y | A / Y |
| 緊急回避 (0.3 秒無敵のバレルロール、3 秒に 1 回) | I K / C V B N M | B | B |
| ポーズ / 再開 | Esc / P | X | X |
| ミュート切り替え | タイトル / ポーズ画面で ↓ | 同左 | 同左 |
| 計測表示の切り替え | (なし) | タイトル / ポーズ画面で ↑、または FUNC | タイトル / ポーズ画面で ↑ |

ブラウザ版はゲームパッドとタッチ操作 (仮想パッド) にも対応。

## ビルド

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
make -C impl/wasm        # WASM 版 (Emscripten)
./launch_web_server.sh   # http://localhost:52980/play/
```

実機 ([Xiamocon](https://github.com/shapoco/xiamocon) / XIAO RP2350・ESP32S3) 版:

```sh
source ~/path/to/xiamocon/setup.shrc
cd impl/xiamocon/devoursphere
xmc build                      # 両方のターゲット
```

PicoSystem (RP2040) 版 (pico-sdk のみ):

```sh
cd impl/picosystem
cmake -S . -B build -DPICO_SDK_PATH=~/path/to/pico-sdk
cmake --build build -j         # build/devoursphere.uf2 (効果音のパックに ffmpeg と python3 が要る)
```

## License

MIT

The sound effects (assets/se/, packed into docs/play/se.bin) are from
[効果音ラボ / Sound Effect Lab](https://soundeffect-lab.info/) and are not covered by
the MIT license: they may be used as part of this game but not redistributed as
material. See assets/se/README.md.
