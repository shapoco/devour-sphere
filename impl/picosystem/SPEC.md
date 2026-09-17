# Devour Sphere (PicoSystem 版)

[PicoSystem](https://shop.pimoroni.com/products/picosystem) (Pimoroni の RP2040 携帯ゲーム機、
240x240 ST7789、264KB SRAM) で core/ を動かすためのファームウェア。
ゲームのルールと描画はすべて core/ 側にあり、ここには
「クロックとボタン」「表示への転送」「フレームループ」だけがある。
フレームループは Xiamocon 版 (impl/xiamocon/SPEC.md) と同じ構成で、
core1 がシミュレーションを、core0 がシーン構築・帯のラスタライズ・転送を担当する。

**PicoSystem SDK は使わない。** SDK は 240x240x2 = 115,200 バイトのフレームバッファを
静的に確保する (`picosystem.cpp` の `_fb`) が、このゲームはフレームバッファを持たずに
40 行の帯で描くのが前提で、そのバッファがあると RP2040 には載らない。
SDK から必要なのは ST7789 の初期化列、ボタンのピン、250MHz へのオーバークロックだけで、
どれも数十行なので src/ に持っている。ピン番号は pico-sdk 自身のボード定義
(`pimoroni_picosystem.h`) から取る。

## 必須要件

- 画面は 240x240 (ST7789)。16 ビット (RGB565) モードで、ハードウェア SPI0 + DMA で帯ごとに書く。
- フレームバッファを 1 枚も持たず、40 行の帯を 2 枚交互に使って描画・転送する。
- シミュレーションは 30Hz 固定、描画は追いつける範囲で行う (可変フレームレート)。
- 音は無し。ハイスコアのフラッシュ保存も無し (電源を切るまでは保持する)。
- 動的確保はしない。`Game` (95KB) は `.bss` に置く。

## ファイル構成

```
impl/picosystem/
  SPEC.md              この文書
  CMakeLists.txt       pico-sdk プロジェクト (リポジトリのホストビルドとは別のトップレベル)
  pico_sdk_import.cmake  pico-sdk の external/ のコピー
  include/
    ds_config.hpp      帯の高さ、アリーナサイズ、tick 周期、クロックなどの定数
    ds_platform.hpp    乱数シードとスタック計測の宣言
    display.hpp        ST7789 ドライバ
  src/
    main.cpp           クロック、ボタン、フレームループ、帯バッファ、2 コアの分担
    display.cpp        ST7789 の初期化、帯の窓設定、DMA 転送
    platform.cpp       乱数シードとスタック計測
```

計測オーバーレイ (`profiler.hpp` / `profiler.cpp`) は Xiamocon 版のものを
そのディレクトリから直接コンパイルして共有する。そのために両者は
`ds_config.hpp` / `ds_platform.hpp` を同じ名前・同じメンバで持ち、
プロファイラ側はこの 2 つを `<...>` でインクルードしている
(`"..."` だとインクルードパスに関係なく自分の隣のファイルが先に見つかる)。

## ビルドと書き込み

```sh
cd impl/picosystem
cmake -S . -B build -DPICO_SDK_PATH=~/pico/pico-sdk
cmake --build build -j                  # build/devoursphere.uf2
```

- pico-sdk 2.x は picotool を要求する。手元にビルド済みのものがあれば
  `-Dpicotool_DIR=<picotoolConfig.cmake のあるディレクトリ>` で指す
  (無ければ SDK が GitHub から取得してビルドする)。
- ボードは `CMakeLists.txt` が `PICO_BOARD=pimoroni_picosystem` に固定している
  (RP2040、フラッシュ 16MB、フラッシュ分周 2)。
- 書き込みは PicoSystem を BOOTSEL モードにして .uf2 をコピーする
  (X を押しながら電源を入れる)。
- `make_release.sh` はこのターゲットも `picosystem/` に含める。

## メモリ配分

RP2040 の SRAM は 264KB だが、リンカ領域の `RAM` は 256KB (`.data` + `.bss` + ヒープ) で、
残りの `SCRATCH_X` / `SCRATCH_Y` (各 4KB) がそれぞれ core1 / core0 のスタック。
**スタックは 4KB 固定で拡張できない**のは RP2350 と同じで、
`PICO_STACK_SIZE` / `PICO_CORE1_STACK_SIZE` を 4096 にしてある。

`.bss` の内訳 (`arm-none-eabi-size`、`.data` は `text` 側に含まれて表示される):

| 用途 | サイズ |
|---|---|
| `sim::Game` | 95,360 |
| 3D アリーナ | 40,960 |
| 帯バッファ x2 (240x40) | 38,400 |
| `render::Renderer` | 23,848 (ワイヤーフレームの線分 11KB を含む) |
| pico-sdk / newlib ほか | 約 3,000 |
| **`.bss` 合計** | **201,568** |
| `.data` (RAM に置くコードを含む。`DS_RAM_CODE=sim` で) | 46,868 |
| **RAM 合計** | **248,436 (242.6KB / 256KB)** |

フラッシュ側は `.text` + `.rodata` + `.data` の初期値で約 236KB (16MB のうち)。RAM の空きは約 13.7KB。

- `Game` は配列の上限を実測ピークに合わせて 136,512 から 95,360 になった (core/SPEC.md の
  メモリの項)。上限を絞る前は Xiamocon 版と同じ構成で 259,888 バイト必要で、
  256KB に載らなかった。
- アリーナは 40KB。ワイヤーフレームが 2D に移る前は三角形バッファ 36.9KB (アリーナ 48KB) が
  間引きなしの最小だったが、線のレコード (52B x 最大 550 本) が要らなくなった。
  実機の `ARN` で使用量を見て、余裕が大きければさらに削れる。
- どのコードを RAM に置くかは `DS_RAM_CODE` で選ぶ (後述)。

## 表示

ST7789 は SPI0 (CS 5, SCK 6, MOSI 7, DC 9, RESET 4, バックライト 12) に繋がっている。
PicoSystem SDK は同じピンを PIO に振り替えて 12 ビットモード (COLMOD 0x03) で
フレーム全体を流すが、ここではハードウェア SPI を使い、
16 ビットモード (COLMOD 0x55) で帯ごとに CASET / RASET / RAMWR を送ってから DMA で画素を流す。
初期化列 (順序、遅延、電圧・ガンマ、8MHz のコマンドクロック、コマンドごとの CS) は
SDK の `hardware.cpp` のものをそのまま使っている。コマンドは常に 8MHz、画素は 62.5MHz (250MHz / 4) で、
帯ごとに `spi_set_baudrate` で切り替える (レジスタ書き込み 2 回なので帯あたりのコストは無視できる)。

- 帯バッファは ShapoGFX の RGB565BE (メモリ上ビッグエンディアン) で、Xiamocon 版と同じ。
  DMA は 16 ビット幅・`bswap` 付きで読むので、SPI は 16 ビットフレームで送れる
  (DREQ のハンドシェイクが画素ごとになる)。全画面の実測 16.63ms は理論値 14.75ms の 88.7% で、
  同じ条件の 8 ビット DMA 18.0ms、CPU 書き込み 18.8ms より速い。
- コマンド (8 ビット、8MHz) と画素 (16 ビット、62.5MHz) で SPI のフレーム幅とクロックを切り替えるので、
  切り替え前に必ず転送完了 (DMA 完了 + SPI の BSY 解除) を待つ。`Display::complete()` がそれ。
- 帯の転送は開始だけして戻り、次の帯のラスタライズと重ねる。最後の帯は次フレームの
  tick と `beginFrame()` に重なる。Xiamocon 版と同じ。
- ST7789 の書き込みは窓の原点から始まるので帯ごとに窓を設定する。

## 入力

ボタンは GPIO 16〜23、内部プルアップでアクティブ Low。フレームの先頭で `gpio_get_all()` を
1 回読む。

| 操作 | ボタン |
|---|---|
| 左右旋回 | LEFT / RIGHT |
| ダッシュ | UP |
| ブレーキ | DOWN |
| A (攻撃・決定) | A / B / X のどれでも |
| 計測オーバーレイの表示切り替え | Y (押した瞬間) |

Xiamocon には FUNC があるが PicoSystem には無いので、Y を計測表示に充てている。

## クロック

PicoSystem SDK と同じく、コア電圧を 1.20V に上げてから 250MHz にする
(`DS_OVERCLOCK=0` で定格 125MHz。すべての時間が 2 倍になる)。
ボード定義のフラッシュ分周 2 で QSPI は 125MHz になるが、SDK が出荷時からこの設定で動いている。

**`clk_peri` は自分で `clk_sys` に戻す。** pico-sdk 2.x の `set_sys_clock_pll()` は
UART のボーレートを保つために `clk_peri` を USB PLL の 48MHz に切り替える
(`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK` を定義しない限り)。
SPI も `clk_peri` から作られるので上限が 24MHz になり、初回の実機では
`P48 SPI24.0`、全画面転送 42.5ms (理論値 14.75ms の 2.9 倍) だった。
`main()` はクロック設定の直後に `clock_configure_undivided(clk_peri, ..., clk_sys)` で
250MHz に戻してからパネルを初期化する (`spi_init()` はその時点の `clk_peri` から分周を決める)。
計測パネルの `P250 SPI62.5` で確認できる。

## 2 つのコアの分担

Xiamocon 版と同じ (impl/xiamocon/SPEC.md「2 つのコアの分担」)。
core0 が tick のバッチを依頼してから前のバッチの状態を描き、その間 core1 が tick を進める。
入力は 1 フレーム遅れて画面に届く。`DS_SIM_ON_CORE1=0` で全部 core0 にできる (遅いが切り分け用)。

`multicore_launch_core1()` で core1 に関数を渡すだけで、FreeRTOS も SDK のタスクも無い。
core1 は依頼が無い間 `tight_loop_contents()` で回る。

## デバッグ表示

シリアルは繋いでいない (`pico_enable_stdio_*` は両方 0)。
Y を押すと Xiamocon 版と同じ計測パネルが左上に出る (行の意味は impl/xiamocon/SPEC.md「デバッグ表示」)。
このターゲットではさらに 4 行、tick・`beginFrame()`・帯の内訳が付く
(core/ の `devoursphere::profileClockUs` に時計を渡すと `Game` と `Renderer` が
フェーズごとの時間を積算する。core/SPEC.md「フェーズ計測」。時計はオーバーレイが出ている間だけ渡す):

```
A1.2 M3.4 L5.6 F0.1    1 tick あたりの内訳 (ms)。A AI の判断  M 移動  L 配置物理と合体  F 発射
B0.5 F0.1 K2.1 X1.2    B 弾丸  F 浮遊フラグメントとアップグレード  K 捕食
                       X 残り (衝突、近傍順序、メニュー、順位、リスポーン)
S19.8 O8.7 H5.1 X0.4   beginFrame 1 回の内訳 (ms)。S 星とワイヤーフレーム  O エンティティ
                       H 残り (浮遊フラグメント、弾丸、エフェクト、オーバーレイ)  X カメラ・エフェクト・深度ソート
D30.2 U4.1 V6.0        帯の合計 (ms)。D 3D ラスタライズ  U 2D (クリア、ゲージ、HUD)  V この計測パネルの描画
```

上 2 行はバッチの合計を tick 数で割った値、下 2 行はフレームあたり。どれも 1 フレーム遅れ。

### 実機の記録

初回 (2026-09-17、`clk_peri` 修正後、gfx3d はフラッシュ):

```
FPS 6.3   TCK 114.11x4   BGN 43.37  RAS 75.00  DMA 0.02  CPU 118.50
W 36.04   XFR 16.63      STK1 868   STK0 2772
```

- `XFR` 16.63ms は理論値 14.75ms の 88.7% (Xiamocon SDK の 8 ビット DMA は 84%)。
  同じ条件で 8 ビット DMA は 18.0ms、CPU 書き込みは 18.8ms だったので 16 ビット DMA を採用した。
- tick 28.5ms、`BGN` 43ms、`RAS` 75ms。RP2350 (250MHz、M33) の 7.4 / 2.6 / 8.4ms に対して
  4 倍 / 17 倍 / 9 倍。`BGN` はほぼ float (M0+ ではソフト float)、`RAS` は整数だが
  深度解決に 64 ビット乗算があり、48KB のコードを 16KB の XIP キャッシュ越しに実行している。
  追いつき上限の 4 tick に張り付いているので、ゲームはスローモーションで進む。

2 回目 (gfx3d を RAM に、`addForce` を 32 ビットに。タイトル / プレイ中):

```
FPS 7.6 / 9.9   TCK 21.7 / 21.9 (1 tick)   BGN 43.7 / 33.3   RAS 54.5 / 44.3
tick: E17.6 (エンティティ) B0.6 F0.1 O0.1 K1.5 C1.4
beginFrame: S32.9 / 19.8 (ワイヤーフレーム) O6.5 / 8.7 H3.6 / 5.1
```

- tick −24% (`addForce`)、`RAS` −27% (RAM 配置)、`BGN` 変わらず。
- tick の 8 割がエンティティのループ、`beginFrame` の 6〜7 割がワイヤーフレーム。
  捕食・衝突はホストのプロファイルでは 4 割を占めていたが、実機では 3ms。
- これを受けてワイヤーフレームの面ごとの `acos` を cos の閾値表に置き換えた
  (core/SPEC.md)。固定条件 21 本のうち 2 本でフレームハッシュが変わる (閾値ぎりぎりの面の
  分割段数が丸めで変わる) が、絵としては同じ。

3 回目 (acos なし、内訳を細分。タイトル / プレイ中):

```
FPS 9.0 / 10.3   TCK 20.7 / 23.0 (1 tick)   BGN 41.0 / 30.2   RAS 53.6 / 34.2
tick: A2.1 M9.7 L5.0 F0.0  B0.3 F0.1 K1.8 X1.3
beginFrame: S32.0 / 19.8  O5.8 / 5.3  H2.5 / 4.5  X0.5
bands: D41.0 / 33.0  U5.2 / 4.3  V5.7 / 5.2
```

- `acos` は効かなかった (S 32.9 → 32.0)。レコード数の差 (189 対 139) と S の差 (32 対 20ms) から
  **線 1 本あたり約 240µs**: 面の走査ではなく、ShapoGFX の float の頂点変換とプリミティブの
  セットアップが支配的。`D` (3D ラスタライズ) も線が細切れのスパンになる固定費が大半。
- そこで星とワイヤーフレームを 3D パイプラインから外し、投影した 2D 線分として帯に直接描く
  ようにした (core/SPEC.md「星とワイヤーフレームは 2D で描く」)。
- tick は移動 `M` が 10ms で最大。正規化と回転の 64 ビット演算 (ライブラリ呼び出し) が主。
- 計測パネル自体の描画 `V` が 5〜6ms あり、`RAS` にはそれが含まれる。

4 回目 (星とワイヤーフレームを 2D に、正規化を逆平方根に。タイトル / プレイ中):

```
FPS 11.2 / 13.8   TCK 18.9 / 21.7 (1 tick)   BGN 36.4 / 25.8   RAS 19.4 / 17.8   W 34.1 / 21.4
tick: A2.4 M7.7 L6.0 / A3.3 M7.7 L4.6 K3.8
beginFrame: (S の計測点が抜けていたので O に含まれる) O32.9 / 19.1  H2.8 / 6.0
bands: D2.4 / 4.7  U10.8 / 6.7  V6.1 / 6.2
```

- 3D ラスタライズ `D` は 41 → 2.4ms、`RAS` は 54 → 19ms (うち `V` 6ms は計測パネル自体)。
- `BGN` は 41 → 36ms しか減らず、ワイヤーフレームの走査 (float の正規化と内積) が約 27ms 残る。
  「線 1 本 240µs」はプリミティブのセットアップではなく走査の面数に比例していた。
- 正規化の書き換えで `M` は 9.7 → 7.7ms。
- `W` (core0 が core1 を待つ時間) が 21〜34ms: フレームが速くなったぶん、
  1 tick 20ms x 2 のバッチがフレームより長くなり、**sim が律速**になった。

5 回目 (sim を RAM に、gfx3d をフラッシュに。タイトル / プレイ中):

```
FPS 15.1 / 15.8   TCK 15.2 / 15.2 (1 tick)   BGN 36.6 / 33.2   RAS 13.7 / 11.5   W 13.2 / 13.8
tick: A1.7 M4.5 L5.7 / A1.6 M4.3 L6.2   K1.6 / 1.4
beginFrame: S22.1 / 21.3  O11.0 / 9.5  H2.7 / 1.6
bands: D3.8 / 1.8  U6.1 / 6.0  V3.6
```

- **XIP の取り合いが確定**: sim を RAM に置いただけで tick 21.7 → 15.2ms (移動 7.7 → 4.5、AI 2.4 → 1.7)。
  配置物理 `L` だけは 6ms のまま (演算そのものの重さ)。3D ラスタライズは RAM から外しても 2〜4ms。
- 残りはフレーム側: ワイヤーフレーム `S` 22ms、エンティティ `O` 10ms (どちらも float)。
  これを受けてワイヤーフレームの走査と投影を整数化した (core/SPEC.md)。

### コードを RAM に置く

`DS_RAM_CODE` (CMake、既定 `sim`) で、どのコードを RAM に置くかを選ぶ。
`memmap_picosystem.ld.in` は pico-sdk の `memmap_default.ld` (rp2040) に `.text` の除外指定を
1 つ足したテンプレートで、除外されたオブジェクトは SDK の memcpy や float ルーチンと同じく
`.data` に落ちてフラッシュから RAM にコピーされる。

| `DS_RAM_CODE` | RAM に置くもの | `.data` |
|---|---|---|
| `sim` (既定) | シミュレーション全体 (`libdevoursphere_sim.a`、約 40KB) | 46,868 |
| `gfx3d` | ShapoGFX の 3D ラスタライザ (`gfx3d.cpp`、約 48KB) | 54,868 |
| `none` | SDK の既定配置 | 約 7,000 |

RP2040 は 16KB の XIP キャッシュ 1 つを 2 コアで共有しており、両コアが同時に走らせる
コードが互いを追い出す。最初は `gfx3d` を置いて `RAS` が 75 → 54ms になったが、
ワイヤーフレームが 2D に移って 3D ラスタライズが 2〜5ms まで落ちた後は、
tick の各フェーズが演算数からの見積りより一様に数倍遅い (core0 が描画コードを
フラッシュから流し続けるため sim のコードが残らない疑い) ので、sim を置く側に切り替えた。
効果は実機で決める。

### これから

RP2040 (Cortex-M0+) は 64 ビット乗算・除算・浮動小数点がすべてソフトウェア。
着手前の見積り (tick 15〜25ms、フレーム 25〜30ms) は tick で 1.2 倍、フレームで 4 倍外れた。
内訳の 4 行が出たら、値の大きいフェーズから順に:
`BGN` の float (頂点変換の固定小数点化)、`RAS` の 64 ビット深度解決、
tick の 64 ビット乗算 (Q15 化) を検討する。
