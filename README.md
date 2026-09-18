# Devour Sphere

サイバー空間の「スフィア」上空で、他のエンティティと戦い、フラグメントを喰らって巨大化していく
3D シューティングゲーム。組み込み機器 (RP2350 / ESP32S3 / RP2040) とブラウザ (WebAssembly) で同じ
コアプログラムが動く。

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
| 攻撃・決定 | スペース / I J K L | A / B / X / Y | A / B / Y |
| 計測表示の切り替え | (なし) | FUNC | X |

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
cmake --build build -j         # build/devoursphere.uf2
```

## License

MIT
