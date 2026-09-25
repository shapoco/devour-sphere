# Devour Sphere

[English](README.md) | 日本語

https://github.com/user-attachments/assets/b8855ba7-7ce3-45a3-9633-efb35ae5db60

サイバー空間の「スフィア」上空で、他のエンティティと戦い、
フラグメントを喰らって巨大化していく3D シューティングゲームです。
組み込み機器 (RP2350 / RP2040 / ESP32S3 / ESP32-P4) とブラウザ (WebAssembly) で
同じコアプログラムが動きます。

このゲームは組み込み向けグラフィックスライブラリ [ShapoGFX](https://github.com/shapoco/shapo-gfx)
のデモとして作られたサンプルアプリケーションで、描画 (フレームバッファ不要のスキャンライン 3D、
線・点プリミティブ、2D 描画とフォント) はすべて ShapoGFX で行っている。

- **ブラウザで遊ぶ:** https://shapoco.github.io/devour-sphere/play/
- **仕様:** [SPEC.md](SPEC.md)、[core/SPEC.md](core/SPEC.md)、
  各移植のディレクトリの SPEC.md ([移植](#移植) を参照)

## ダウンロード

- **M5Stack Tab5:** ビルド済みのファームウェアを [M5Burner](https://docs.m5stack.com/en/download)
  から書き込めます。M5Burner の ESP32-P4 (Tab5) のリストで "Devour Sphere" を探してください。
- **その他のボード:** Xiamocon (RP2350 / ESP32S3)、PicoSystem、M5StickS3、ESPboy のバイナリは
  [Releases](https://github.com/shapoco/devour-sphere/releases) に zip で置いてあります
  (書き込み方は zip 内の README.txt)。
- **ブラウザ版:** ダウンロード不要。上の「遊ぶ」のリンクから。

## 移植

書き込み方、操作の割り当て、ビルド手順は各移植の README (英語) にあります。

| デバイス | チップ | ディレクトリ |
|---|---|---|
| ブラウザ | WebAssembly | [impl/wasm/](impl/wasm/README.md) |
| [Xiamocon](https://github.com/shapoco/xiamocon) | RP2350 / ESP32S3 | [impl/xiamocon/](impl/xiamocon/README.md) |
| [PicoSystem](https://shop.pimoroni.com/products/picosystem) | RP2040 | [impl/picosystem/](impl/picosystem/README.md) |
| [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) | ESP32-P4 | [impl/m5tab5/](impl/m5tab5/README.md) |
| [M5StickS3](https://docs.m5stack.com/en/core/M5StickS3) | ESP32-S3 | [impl/m5sticks3/](impl/m5sticks3/README.md) |
| [ESPboy](https://www.espboy.com/) | ESP8266 | [impl/espboy/](impl/espboy/README.md) |
| 端末 | POSIX | [impl/cli/](impl/cli/README.md) |

## 操作

操作の種類はどの版も同じです。キー・仮想パッド・傾きへの割り当ては各移植の README を見てください。

| 操作 | |
|---|---|
| 旋回 | 左右 |
| ダッシュ (体力を消費) | 上 |
| ブレーキ (旋回が速くなる) | 下 |
| 攻撃・決定 | A |
| 緊急回避 | B (0.3 秒無敵のバレルロール、3 秒に 1 回) |
| ポーズ / 再開 | ポーズボタン |
| ミュート切り替え | タイトル / ポーズ画面で下 |
| ベンチマーク | タイトル画面で B を 3 秒 (結果は A で次のページ、B で閉じる) |

アナログの入力 (ゲームパッドのスティック、仮想パッドの方向ディスク、傾き) は、
倒した量に応じて旋回・ダッシュ・ブレーキの強さが変わる。

## ビルド

コア、テスト、確認用のネイティブツールはホストの CMake でビルドする:

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
```

各移植はそれぞれのディレクトリでビルドする (各 README を参照)。

## 実装

Devour Sphere は 2 つの層に分かれています。上の **コアモジュール** はプラットフォームに
依存しない純粋な C++17 で、ゲームのルールも 3D の描画もすべてここにあります。
機種ごとに書くのは下の層だけです。

![](./docs/image/stack.png)

### コアモジュール (書かなくてよい部分)

- **Simulator** ([core/src/sim/](core/src/sim/)) はゲームのルールと物理。整数演算だけで動き、
  同じ入力からはどの機種でも同じ結果になります。描画のことは何も知りません。
- **Renderer** ([core/src/render/](core/src/render/)) は Simulator の状態を読んで ShapoGFX で描きます。
  プラットフォームには依存しませんが、描き込む先のバッファは下の層から渡されます。
- **ShapoGFX** は別リポジトリのライブラリ (submodule)。コアだけでなく、
  プラットフォーム側から直接使うこともできます (仮想パッドや計測表示など)。

コアは動的確保をせず、OS も SDK も呼びません。必要なのは C++17 のコンパイラだけです。

### 移植するときに書く部分

必須なのは **メインループ・入力・表示** の 3 つで、効果音とハイスコアの保存は無くても遊べます。

#### メインループ

```cpp
#include "devoursphere/devoursphere.hpp"
namespace ds = devoursphere;
namespace g2 = shapoco::gfx2d;

static ds::sim::Game game;            // 約 84 KB。絶対にスタックに置かない
static ds::render::Renderer renderer; // 約 24 KB
static uint8_t arena[64 * 1024];      // Renderer の作業領域 (三角形バッファなど)
static uint8_t band[W * BAND_H * 2];  // 帯バッファ (フレームバッファは不要)

renderer.init(W, H, arena, sizeof(arena));
game.reset(seed);

for (;;) {
  while (tickIsDue()) {          // 固定レート (既定 60 Hz)
    game.tick(readInput());      // 入力ドライバ (sim::Input を返す)
    renderer.pollEffects(game);
    playSounds(game.sounds());   // サウンドドライバ (任意)
  }
  renderer.beginFrame(game, dt); // カメラとシーンの構築
  for (int y = 0; y < H; y += BAND_H) {
    auto s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, W, BAND_H, band);
    renderer.renderBand(s, y, BAND_H);
    pushBand(band, y, BAND_H);   // ディスプレイドライバ (SPI + DMA など)
  }
  renderer.endFrame();
}
```

シミュレーションは固定レート (既定 60 Hz、`DEVOURSPHERE_TICK_RATE=30` で 30 Hz) で刻みますが、
フレームレートは出せるだけで構いません。遅れを取り戻すために 1 フレームで複数 tick 走らせるときは、
tick のたびに `pollEffects()` を呼んでください (tick の出すイベントは次の tick で消えるため)。

#### 入力 (必須)

`tick()` に渡すのは `sim::Input` ([entities.hpp](core/include/devoursphere/sim/entities.hpp)) で、
方向の 2 軸 `x` / `y` (-127〜+127) と 3 つのボタン (`Button::A / B / PAUSE`) だけです。
`x` は正が右旋回、`y` は負がダッシュで正がブレーキ。倒した量がそのまま強さになります。

- **デジタルの方向キー**なら、ボタンのビット (`Button::LEFT / RIGHT / UP / DOWN` を含む) を
  そのまま渡せます。`Input` はビットから作れて、方向は -127 / 0 / +127 の最大の強さになります
  (`game.tick(Button::A | Button::LEFT)` のように書けます)。
- **アナログの入力** (スティック、タッチのディスク、傾きなど) は、機器に合わせた**不感帯と飽和を
  移植側で決めて** -127〜+127 に写します。コアは値をそのまま線形の強さとして使います。
  丸い可動域のスティックは、斜めいっぱいで両軸が最大になるように写すのがおすすめです
  (最大の旋回と最大のブレーキでクイックターンになるため。wasm 版の `roundAxes()` が例)。
- メニューでは、コアが軸を方向キーとして読みます (強さ 64 で入り、40 で抜ける)。

タイトル画面に出る操作の案内文は `Renderer::setControlHints()` で差し替えます。

#### 表示 (必須)

**フレームバッファは要りません。** `renderBand()` は任意の行範囲だけを描けるので、
数十行ぶんのバッファを 1〜2 枚用意して「描いては転送」を繰り返せば足ります
(PicoSystem 版は 240x40 の帯を 2 枚で交互に、CLI 版は全画面を 1 回で描いています)。
描き込み先は ShapoGFX の `Surface` で、`GRAY1` / `RGB444` / `ARGB4444` / `RGB565_SWAPPED` に対応。
画面サイズは `init()` に渡すだけでよく、HUD は自動で縮尺されます。
画面の一部をプラットフォーム側で塗る場合 (仮想パッドなど) は `setHudInsets()` で HUD がそこを避けます。

#### 効果音 (任意)

`Game::sounds()` が「直前の tick で何を鳴らすべきか」を `SoundKind` のビットで返します。
コアは鳴らすものを言うだけで、波形も再生もプラットフォームの担当です。
同時発音数が 1 しかない機種は優先度で 1 つ選びます (Xiamocon 版と PicoSystem 版がそうしています)。
波形の素材は [assets/se/](assets/se/) にあります。

#### ハイスコアの保存 (任意)

[high_score_record.hpp](core/include/devoursphere/sim/high_score_record.hpp) の 16 バイトのレコード
(マジック + メジャーバージョン + CRC32) を encode / decode して、好きな不揮発領域に置くだけです。
消去済みのフラッシュも、別バージョンのレコードも、壊れたレコードも「記録なし」として読めます。
いつ書くかはプラットフォームが決めます (`Game::keepHighScore()` が真を返したときが目安)。

### 必要なリソース

| | |
|---|---|
| RAM | 約 150 KB 〜 (`sim::Game` 84 KB + `Renderer` 24 KB + アリーナ 40〜64 KB + 帯バッファ) |
| フラッシュ | コードが約 240 KB (効果音の波形は別) |
| CPU | RP2040 (Cortex-M0+ 133 MHz、240x240) で 28 fps |

載らない・遅いときの調整箇所:

- `DEVOURSPHERE_TICK_RATE=30` — シミュレーションを 30 Hz に (挙動は同じ)
- `DEVOURSPHERE_SUPPRESS_ALPHA=1` — 半透明合成をやめる (フレームの読み戻しが消える)
- `DEVOURSPHERE_MAX_WIRE` / `MAX_LINES2D` / `MAX_POINTS2D` — ワイヤーフレームと 2D 線分の配列
- `Renderer::init()` の `spanCapacity`、`setDetailTriangles()` — アリーナの配分と、
  敵の機体に使う三角形の上限 (混雑してもフレーム時間が伸びなくなる)

詳しくは [core/SPEC.md](core/SPEC.md) の「メモリ」「描画 (render)」と
[impl/xiamocon/SPEC.md](impl/xiamocon/SPEC.md) を参照してください。

### 参考にする実装

| ディレクトリ | 何の例として読むか |
|---|---|
| [impl/picosystem/](impl/picosystem/) | 一番制約の厳しい移植 (RP2040 264KB、pico-sdk だけ、帯 + DMA、2 コア) |
| [impl/xiamocon/](impl/xiamocon/) | 同じソースで RP2350 (pico-sdk) と ESP32S3 (Arduino) の両方を焼く |
| [impl/m5tab5/](impl/m5tab5/) | ESP-IDF、フルサイズのフレーム + ハードウェアの拡大回転、タッチの仮想パッド |
| [impl/m5sticks3/](impl/m5sticks3/) | 横倒しの 240x135、傾きで操作 (IMU)、振ってポーズ |
| [impl/espboy/](impl/espboy/) | 一番小さな機械 (ESP8266、96KB、1 コア)。core を縮小構成でビルドし、帯を CPU で転送する |
| [impl/wasm/](impl/wasm/) | ブラウザ (Emscripten)、キーボード / ゲームパッド / タッチ、localStorage |
| [impl/cli/](impl/cli/) | 一番短い例。全画面を 1 回の `renderBand()` で描いて端末に流すだけ |

### 移植を募集しています

新しい市販のデバイスや OSS ハードウェアで Devour Sphere が動いたら、ぜひプルリクエストをください。
`impl/<デバイス名>/` に移植側のコードと、README.md (書き込み方、操作、ビルドの手順) と、
その機種について分かったこと (入力と表示のやり方、ハマった点、できればベンチマークの結果) を
まとめた SPEC.md を置いてもらえると助かります。
ベンチマークはタイトル画面で B を 3 秒押すと始まります。

## License

MIT

The sound effects (assets/se/, packed into docs/play/se.bin) are from
[効果音ラボ / Sound Effect Lab](https://soundeffect-lab.info/) and are not covered by
the MIT license: they may be used as part of this game but not redistributed as
material. See assets/se/README.md.
