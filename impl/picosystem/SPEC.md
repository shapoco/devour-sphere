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
| 3D アリーナ | 49,152 |
| 帯バッファ x2 (240x40) | 38,400 |
| `render::Renderer` | 11,680 |
| pico-sdk / newlib ほか | 約 7,200 |
| **合計** | **201,768 (197.0KB / 256KB)** |

フラッシュ側は `.text` + `.rodata` + `.data` で 225,852 バイト (16MB のうち)。

- `Game` は配列の上限を実測ピークに合わせて 136,512 から 95,360 になった (core/SPEC.md の
  メモリの項)。上限を絞る前は Xiamocon 版と同じ構成で 259,888 バイト必要で、
  256KB に載らなかった。
- アリーナは 48KB。三角形バッファが 36.9KB を下回るとシーンが間引かれ始め、
  固定部と `SPAN_CAPACITY` 分のスパンが 10.8KB なので、48KB が間引きなしの最小。
  Xiamocon 版の 64KB との差は絵に出ない (ホストの 12,600 フレームのハッシュで確認済みの閾値)。
- 約 54KB の空きがある。RP2040 の XIP キャッシュは 16KB しかなくフラッシュ実行の
  不利が RP2350 より大きいので、この空きは gfx3d のスパンループなどのホットコードを
  RAM に置く余地として取ってある (未着手)。

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
  (DREQ のハンドシェイクが画素ごとになり、バイトごとの Xiamocon SDK より転送効率が良いはず。
  実測は `XFR` 行で見る。理論値は 240x240x2 バイトで 14.75ms)。
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

## デバッグ

シリアルは繋いでいない (`pico_enable_stdio_*` は両方 0) ので、手掛かりは LED と画面だけ。

**RGB LED (GPIO 13〜15、アクティブ High)** が起動の段階を示す:

| 色 | 段階 |
|---|---|
| 赤 | `main()` に入り、クロック設定を終えた。パネルの初期化へ |
| 黄 | パネルの初期化列を送り終えた (SPI の書き込みが詰まっていない) |
| 青 | core1 を起動してフレームループに入った |
| 緑の点滅 (16 フレームごと) | フレームを描いて転送している (ハートビート) |

黄の直後に**パネルの自己診断**として赤・緑・青の横帯 (各 80 行) を 1 秒間、
または A / B / X を押している間表示する。ゲームと同じ帯の経路 (窓コマンド + 16 ビット DMA) を
使うので、これが見えれば SPI・DMA のバイトスワップ・16 ビットモードは正しく、
その後も黒ならレンダラ側。見えなければパネルの立ち上げ側。

**起動時に押しておくボタン** (電源を入れる瞬間に押しておく):

| ボタン | 効果 |
|---|---|
| DOWN | オーバークロックしない (定格 125MHz)。不安定なオーバークロックの切り分け |
| UP | 画素データも 8MHz で送る (1 フレーム 115ms)。配線が 62.5MHz に耐えないかの切り分け |

パネルの初期化列は PicoSystem SDK の `_init_hardware()` と同じ順序・同じ遅延・同じ
8MHz のコマンドクロック・コマンドごとの CS で送っている。違いは COLMOD (0x55 = 16 ビット) だけ。
帯ごとの CASET / RASET / RAMWR も同じ形で送り、画素だけ 62.5MHz の 16 ビットフレームで流す。

Y を押すと Xiamocon 版と同じ計測パネルが左上に出る (行の意味は impl/xiamocon/SPEC.md「デバッグ表示」)。
このターゲットではさらに 2 行が付く:

```
C250 P250 SPI62.5           clk_sys / clk_peri (MHz) と SPI の実クロック (MHz)
16:42.5 8:14.9 C:15.2       全画面転送の所要時間 (ms): DMA 16 ビット / DMA 8 ビット / CPU 書き込み
```

起動時に 3 方式すべてを計測し、**最速のものをゲームでも使う** (`XFR` はその値)。
初回の実機で DMA 16 ビットが 42.5ms (理論値の 2.9 倍) だったため、原因がクロックか
DMA のペーシングか SPI かをこの 2 行で切り分ける。

**実機の数字はまだ無い。** RP2040 (Cortex-M0+) は RP2350 (M33) と違って
64 ビット乗算・除算・浮動小数点がすべてソフトウェアなので、
見積りは tick 15〜25ms、フレーム 25〜30ms (30fps 前後、悪くて 20fps) だが ±2 倍の幅がある。
最初に実機で `TCK` / `BGN` / `RAS` / `XFR` / `STK0` / `STK1` を読んで、
core/SPEC.md の「除算の排除」以降の何を進めるか決める。
