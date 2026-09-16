# Devour Sphere (Xiamocon 版)

[Xiamocon](https://github.com/shapoco/xiamocon) (XIAO RP2350 用のゲーム機型マザーボード)
で core/ を動かすためのファームウェア。
ゲームのルールと描画はすべて core/ 側にあり、ここには
「実機へのビルド」「表示への転送」「入力」だけがある。

Xiamocon SDK のグラフィックス API (`xmc::gfx2d` / `gfx3d` / `FrameBuffer`) は使わない。
描画は core/render の `Renderer::renderBand()` + ShapoGFX で行い、
SDK からは表示転送・DMA・入力・電源管理といった足回りだけを借りる。

## 必須要件

- 画面は 240x240 (ST7789)。HUD のレイアウトは core/ 側が解像度から動的に決めるので
  (core/SPEC.md の HUD 参照)、正方形でも破綻しない。
- フレームバッファを 1 枚も持たず、40 行の帯を 2 枚交互に使って描画・転送する。
- シミュレーションは 60Hz 固定、描画は追いつける範囲で行う (可変フレームレート)。
- 音は無し。ハイスコアのフラッシュ保存も無し (電源を切るまでは保持する)。
- ターゲットは RP2350 のみ (`rp2350_pico_sdk`)。

## ファイル構成

```
impl/xiamocon/
  SPEC.md              この文書
  devoursphere/        ファームウェアのプロジェクト (ディレクトリ名 = 生成物の名前)
    CMakeLists.txt     pico-sdk のトップレベル (リポジトリのホストビルドとは別)
    include/
      ds_config.hpp    帯の高さ、アリーナサイズ、tick 周期などの定数
      band_writer.hpp
    src/
      app.cpp          xmcApp* エントリ、静的領域、フレームループ、入力変換
      band_writer.cpp  帯の ping-pong と DMA 転送
```

ディレクトリ名が `devoursphere` でなければならないのは、`xmc run` が
`.cmake/<カレントディレクトリ名>.uf2` を探して書き込むため。

## ビルドと書き込み

```sh
source ~/repo/2026/xiamocon/setup.shrc   # XMC_REPO_PATH をリポジトリ側に向ける
cd impl/xiamocon/devoursphere
xmc build -p rp2350_pico_sdk             # .cmake/devoursphere.uf2 を生成
xmc run   -p rp2350_pico_sdk -d E        # ビルドして書き込み (WSL2、E: はドライブレター)
```

- `-p` は省略できない。省くと `xmc` は ESP32S3 版も試して非ゼロ終了する。
  `XMC_DEFAULT_PLATFORM=rp2350_pico_sdk` を設定しておいてもよい。
- 書き込みは Xiamocon をマスストレージモードにしてから行う
  (Down ボタンを押しながら電源ボタンを 3 秒長押し、その後 Down を離す)。
- 反復ビルドは `.cmake/` で `make -j` を直接叩く方が速い。
  `xmc clean` は `.cmake/` ごと消すので、fetch してビルドした picotool も捨ててしまう。
- SDK は `~/.xmc/` にインストールされたコピーと、開発用のリポジトリ
  (`~/repo/2026/xiamocon/`) の 2 つがある。`~/.xmc/setup.shrc` は前者を指すので、
  リポジトリ側で開発するときは `~/repo/2026/xiamocon/setup.shrc` を読むこと。

## メモリ配分

RP2350A のリンカ領域は `RAM` 512KB (`.data` + `.bss` + ヒープ) と、
`SCRATCH_X` / `SCRATCH_Y` が各 4KB。
**スタックは SCRATCH 領域にあり 512KB とは別枠**だが、
4096 が上限でそれを超えるとリンクエラーになる。
既定の 2KB は `buildSphere()` の 7 段再帰にはやや心もとないので
`PICO_STACK_SIZE` / `PICO_CORE1_STACK_SIZE` を 4096 に上げている。
これは `add_compile_definitions()` でディレクトリスコープに指定する必要がある
(`crt0.S` と `multicore.c` は INTERFACE ライブラリ経由で `xmc_pfm` 側に
コンパイルされるため、`target_compile_definitions()` では効かない)。

`.bss` の実測 (M2 時点):

| 用途 | サイズ |
|---|---|
| `sim::Game` | 136,512 |
| 3D アリーナ | 65,536 |
| 帯バッファ x2 (240x40) | 38,408 |
| `render::Renderer` | 17,024 |
| SDK / pico-sdk / newlib / TinyUSB ほか | 約 11,300 |
| **合計** | **268,804 (262.5KB / 512KB)** |

`.text` は 274,704 バイト (4MB のフラッシュに対して十分小さい)。
動的確保はしない。`sim::Game` だけで 133KB あるのでスタックには絶対に置かない。

`pico_set_binary_type(copy_to_ram)` は使えない: `.text` + `.bss` = 530.8KB で
512KB の RAM に入らない。

## 帯単位の描画と転送

ST7789 のメモリ書き込みはウィンドウの原点から始まるので、**帯ごとに
`setWindow()` が必要**。`writePixelsStart()` は DMA を開始して即座に返り、
SPI ロックを保持したままになる。`writePixelsComplete()` が完了を待ってロックを解放する。

```
beginFrame(game, dt)
for b in 0 .. 5:
    buf = bands[cur]; cur ^= 1
    renderBand(buf, b*40, 40, 0)     # 直前の帯の DMA と重なる
    drain()                          # 直前の帯の DMA を回収 (start() の中で行う)
    setWindow(0, b*40, 240, 40); writePixelsStart(buf, 240*40*2)
endFrame()
# 最後の帯は転送中のまま抜ける (次フレームの tick と beginFrame に重ねる)
```

設計上の要点:

- **転送中に `setWindow()` や `writePixelsStart()` を呼ぶと復帰不能なハングになる。**
  SPI ロックはバイナリセマフォ上のスピンで、エラーを返さない。
  表示 API に触るのは `BandWriter` だけに限り、`start()` の先頭で必ず `drain()` する。
- **最後の帯は `drain()` せずに抜ける。** 末尾の転送 (2.46ms) を次フレームの
  tick と `beginFrame()` に重ねられるので、フレーム時間が
  `max(CPU 合計, DMA 合計)` に近づく。素直に待つと両者が直列になる。
  これが安全なのは、`system::requestShutdown()` が各種 deinit より**先に**
  `xmcAppTerminate()` を呼ぶため。`xmcAppTerminate()` で `drain()` している。
- **帯バッファの添字はフレームを跨いで連続させる** (フレーム先頭で 0 に戻さない)。
  戻すと、帯数が奇数のときに「前フレームの最後の帯が転送中のバッファへ
  次フレームの帯 0 を描く」競合が起きる。
- 表示 SPI は 62.5MHz なので、全画面 115,200 バイトの転送に 14.75ms かかる。
  これがフレームレートの上限 (約 67fps) を決める。
- SDK の `RGB565` はメモリ上でバイトスワップされており、ShapoGFX の
  `RGB565BE` とビット単位で一致する。変換は不要。

## ゲームループ

`xmcAppLoop()` の 1 回の呼び出しが 1 フレームに対応する。

- `getTimeUs()` の差分をアキュムレータに足し、`1000000/60` µs ごとに `game.tick()` を進める。
  1 フレームあたりの追いつきは 4 tick まで。経過時間はアキュムレータに入れる前にも
  後にもクランプするので、長時間停止から復帰してもシミュレーションがバーストしない。
- tick が 1 回以上進んだフレームだけ描画する。描画がシミュレーションより速いときは
  同じ絵を描き直さずに抜ける。
- **tick ごとに `Renderer::pollEffects()` を呼ぶ。** エフェクトのイベントは tick ごとに
  クリアされるので、1 フレームに 2 tick 進むときこれを怠るとフレーム内の最後の tick
  以外の爆発やデブリが出ない。
- `beginFrame()` に渡す `dt` は実経過時間ではなく `ticks / 60` 秒。レンダラはカメラの
  補間・デブリ・スコアのロールアップをこの値で進めるので、実際に進んだ tick と
  揃えないと演出だけがシミュレーションより先に進む。
- **`beginFrame()` から最後の `renderBand()` までの間にシミュレーションを進めてはいけない。**
  `renderBand()` は HUD を描き、HUD は生きた `Game` を読む。
- ボタンの押下エッジは追いつき tick を跨いでも壊れない
  (`Game::tick()` が `pressed = buttons & ~prevButtons_` を計算するので、
  4 tick 押しっぱなしでも押下イベントは 1 回)。

ホストで同じアキュムレータを回した結果 (600 フレーム):

| 描画レート | シミュレーション時間 / 実時間 | tick/フレームの内訳 |
|---|---|---|
| 60fps | +0.2% | 1x599, 2x1 |
| 50fps | +0.1% | 1x479, 2x121 |
| 40fps | +0.1% | 1x299, 2x301 |
| 30fps | +0.1% | 2x599, 3x1 |
| 100fps | +0.3% | 0x239 (描画せず), 1x361 |
| 15fps | -0.0% | 4x600 (追いつき上限ちょうど) |
| 10fps | -33% | 4x600 (上限を超え、スローモーションになる) |

15fps までは実時間を保つ。それより遅いとゲームがゆっくりになるが、
追いつきを増やして取り戻そうとするよりはこちらの方が破綻しない。

## 入力

| 操作 | ボタン |
|---|---|
| 左右旋回 | LEFT / RIGHT |
| ダッシュ | UP |
| ブレーキ | DOWN |
| A (攻撃・決定) | A / B / X / Y のどれでも |

- `input::service()` は自分で呼ばない。`libLoop()` の `system::service()` が
  毎回呼んでおり、二重に呼ぶと `wasPressed` / `wasReleased` のエッジが壊れる。
- 入力はフレームの先頭で `input::getState()` を 1 回だけ読む。
- FUNC は起動時のみ SDK が特別扱いする (押しながら起動すると内蔵の診断アプリになる)。
  実行中は自由に使える。

## 起動時の注意

- `xmcAppGetConfig()` は `display::init()` **より前**に呼ばれるので、そこで表示に触らない。
- 乱数のシードは pico-sdk の `get_rand_32()` を使う。
  `xmc::randomU32()` はシードされていない newlib の `rand()` で、毎回同じ列になる。
