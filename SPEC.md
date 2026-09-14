# Devour Sphere

## 概要

「Devour Sphere」は、プレイヤーがサイバー空間の中でエンティティとして成長し、
他のエンティティと戦いながらフラグメントを集めて巨大化していくシューティングゲームである。

## プロジェクト構成

### コアプログラム

コアプログラムは、ゲームの基本的なルールやエンティティの挙動、フラグメントの管理などを担当する。
描画や入力処理などのプラットフォーム依存部分は含まれない。
コアプログラムは、異なるプラットフォーム上でも同じ挙動を保証することを目的としている。

コアプログラムは 2 層に分かれる。

- `sim`: ルールと物理。整数演算のみで、どのプラットフォームでも同じ結果になる。
- `render`: ShapoGFX (submodule/shapo-gfx/) で状態を描画する。プラットフォームには依存しないが、
  フレームバッファの用意や表示への転送はプラットフォーム側が行う。帯単位の描画に対応する。

詳細は core/SPEC.md を参照のこと。

### WASM 版

impl/wasm/ 配下に WASM 版の実装が置かれる。
WASM 版は、ブラウザ上でコアプログラムを実行するためのラッパーや入力処理、公開用ページを含む。

詳細は impl/wasm/SPEC.md を参照のこと。

### RP2350 版 (予定)

impl/rp2350/ に置く予定。480x320 の RGB565BE ディスプレイを想定し、
フレームバッファを持たずに帯単位で描画・転送する。

## ディレクトリ構成

```
SPEC.md              この文書
CMakeLists.txt       ネイティブビルド (コア、テスト、確認用フロントエンド)
core/                コアプログラム (core/SPEC.md)
impl/wasm/           WASM 版 (impl/wasm/SPEC.md)
docs/                公開用の静的サイト (docs/play/ がゲーム)
submodule/shapo-gfx/ ShapoGFX (git submodule)
launch_web_server.sh docs/ をローカルで配信する
```

## ビルドとテスト

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure      # コアのテスト
./build/impl/wasm/devoursphere_native 1 300 out.ppm   # 1 フレームを PPM に書き出す
make -C impl/wasm                                # WASM 版 (emcc が必要)
./launch_web_server.sh                           # http://localhost:52980/play/
```

C/C++ のコードは .clang-format (ShapoGFX と同じ設定) で整形する。
