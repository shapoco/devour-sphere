# Devour Sphere

サイバー空間の「プラネット」上空で、他の生命体と戦い、部品を喰らって巨大化していく
3D シューティングゲーム。組み込み機器 (RP2350) とブラウザ (WebAssembly) で同じ
コアプログラムが動く。描画は [ShapoGFX](https://github.com/shapoco/shapo-gfx)。

- **遊ぶ:** https://shapoco.github.io/devour-sphere/play/
- **仕様:** [SPEC.md](SPEC.md), [core/SPEC.md](core/SPEC.md), [impl/wasm/SPEC.md](impl/wasm/SPEC.md)

## 操作

| 操作 | キー |
|---|---|
| 旋回 | ← → / A D |
| ダッシュ (体力を消費) | ↑ / W |
| ブレーキ (旋回が速くなる) | ↓ / S |
| 攻撃・決定 | スペース / I J K L |

ゲームパッドとタッチ操作 (仮想パッド) にも対応。

## ビルド

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
make -C impl/wasm        # WASM 版 (Emscripten)
./launch_web_server.sh   # http://localhost:52980/play/
```

## License

MIT
