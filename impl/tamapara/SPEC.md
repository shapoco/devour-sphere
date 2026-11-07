# Devour Sphere (Tamagotchi Paradise 版)

概要・入手方法・操作・ビルドの手順は [README.md](README.md)。この文書は実装の詳細。

ハードウェアは Bandai の Tamagotchi Paradise (2025)。Sonix SNC73410 (Cortex-M3 ×2、96MHz、
FPU なし、ハード除算あり)、128x128 の TFT (SPI1、ST7735 系)、ボタン A / B / C、プッシュスイッチ付き
ロータリーエンコーダ、圧電ブザー、16MB の SPI NOR フラッシュ。ゲームのルールと描画はすべて core/
側にあり、ここには「起動と復帰」「クロックとボタン」「表示への転送」「フレームループ」「音」だけがある。
core は ESPboy 版と同じ**縮小構成**でビルドする (impl/espboy/SPEC.md「縮小構成」。値は devoursphere/build.sh)。

**純正ファームウェアのゲストとして動く。** Paramix Studio (mameise、GPL-3) が DOOM や各種エミュレータを
載せているのと同じ方式で、純正ファームは置き換えず、空いているフラッシュにプログラムを置き、ホーム画面の
ハンドラ 4 語をランチャ (stub) に向ける。ホーム画面で A+C を約 2 秒押すとランチャがプログラムの先頭 64KB を
PRAM にコピーしてソフトリセットし、プログラムが終わればファーム内のルーチンで純正に戻る。

**このディレクトリは GPL-3.0-or-later** (LICENSE、hw/NOTICE.md)。hw/ のレジスタ列は Paramix Studio の
DOOM 移植から取ったもので、Sonix の SDK は使っていない (入手困難。レジスタはアドレス直書き)。
core/ と ShapoGFX は MIT のまま。

## ハードウェアの事実 (出典: Paramix Studio のソース、GMMan の tama-para-research、SNC7340 データシート V1.7)

| 項目 | 内容 |
|---|---|
| CPU | Cortex-M3 Core 0 のみ使う (Core 1 は純正でも周辺担当)。クロックは hw_init 後 96MHz (SysTick の基準) |
| PRAM | 64KB、0x00000000 (リマップ後)。**コード専用**。書き込みは信頼できない (DOOM v0.1.4 の記録) |
| SRAM | 128KB、0x18000000〜0x1801FFFF。データとスタック |
| メールボックス | 4KB、0x20000000。0x20000FFC に "PMXL" を書くとランチャの自動起動が止まる (他のゲストと同じ作法)。0x20000FC0 は時計の受け渡し (後述) |
| XIP 窓 | 1MB、0x10000000〜。16KB の I キャッシュ (64 way、32B ライン) 経由。ベースはレジスタ 0x4002F004 (純正と同じ 0x60011000 のまま使う: 窓はフラッシュ 0x11000〜0x111000 を映す) |
| フラッシュ | 16MB SPI NOR、0x60000000〜に非キャッシュで見える。4KB セクタ。書き込みは SPIFC (0x40022000) の DMA で PRAM から (hw/flash.c) |
| パネル | SPI1 (0x4000E000)、D/C は P0.5、リセット P0.4、CS P1.5 (Low 固定)。RGB565、CASET は +2、RASET は +1 のオフセット |
| ボタン | GPIO0 (0x40018000) アクティブ Low: P0.8 ダイヤル押下、P0.9 A、P0.10 C、P0.11 B |
| エンコーダ | GPIO2 (0x4001A000) の bit0/1 (P2.0 / P2.1)。1ms の SysTick でポーリング |
| ブザー | CT32B7 (0x40007000) の PWM、72MHz 基準、矩形波 1 音 (`hw_tone(hz)`) |
| バックライト | CT32B2 の PWM。`hw_backlight()` |
| WDT | 2 本 (0x40008000 / 0x40009000)。0x5AFA55AA を +0x0C に書いて餌をやる。SysTick で毎 ms |
| UART1 | 0x4000B000。本体上部の接点 (ドングル)。460800 bps。ベンチマークの結果行を出す |
| RTC 秒カウンタ | 0x45000304。乱数シードと純正の時計の補正に使う |

Core 1 用と思われる 0x20001000〜 の 60KB を DOOM は使っているが、データシートには 4KB のメールボックスしか
無いので使わない。

## 必須要件

- 画面は 128x128。16 行×2 = 32 行の帯 1 枚 (8KB、RGB565_SWAPPED) を描いては `hw_px_raw()` で送る。
  転送は CPU がバイトを FIFO に詰める (DMA は未使用。Paramix の DOOM と同じ経路)。
- シミュレーションは 30Hz 固定、描画は追いつける範囲で (可変フレームレート)。
- 効果音は ESPboy 版と同じ 4 音符までのチューン、1 音、優先度付き。音程の切り替えは SysTick から `hw_tone()`。
- ハイスコアは専用の 4KB セクタ (既定 0x10D000)。書き込みは PRAM のコードから割り込み禁止で。
- 動的確保はしない (`_sbrk` は HUD の snprintf 用に 2KB の静的領域を返すだけ)。`Game`、`Renderer`、
  アリーナ、帯は `.bss` (SRAM)。
- プログラムは**実行アドレス固定** (XIP 部はフラッシュ上で実行)。配置は build.sh の `XIP_FLASH_OFF` /
  `HISCORE_OFF` で決め、イメージの設定ブロックに書いて mkrom.js が ROM と照合する。

## ファイル構成

```
impl/tamapara/
  README.md / SPEC.md / LICENSE (GPL-3)
  hw/                      Paramix Studio 由来のハード層 (NOTICE.md に出典と差分)
    io.h hw.h hw.c         クロック・ピン・表示電源・パネル初期化、ボタン、ブザー、バックライト、純正への復帰
    disp_power.h           表示電源のレジスタ列 (純正ファームのトレース)
    flash.h flash.c        セクタ消去と書き込み (SPIFC の DMA)
    paraclock.h paraback.h 純正の時計を読む / 滞在時間ぶん進める
    stub_tpl.bin           ランチャのテンプレート (512B、プレースホルダ入り)
  hello/                   最小プログラム: ボタン・ダイヤル・ブザー・tick・復帰の確認 (PRAM のみ)
  devoursphere/
    build.sh               arm-none-eabi-g++ で core + ShapoGFX + ここを -O2 でビルド → build/pram.bin, build/xip.bin, build/ds.elf
    link.ld pram_hot.ld    PRAM 64KB + XIP 窓 + SRAM の配置。pram_hot.ld に PRAM へ置くオブジェクトを列挙
    main.cpp               ベクタ以外の起動、SysTick、入力の割り当て、フレームループ、復帰
    plat_c.c               ベクタテーブル、純正の時計の印と補正、フラッシュの読み書き、UART1、フォルト画面、_sbrk
    ds_config.hpp          帯の高さ、アリーナ、tick 周期、スタック、ダイヤルの定数、長押しの時間
    ds_platform.hpp/.cpp   時計、乱数シード、ハイスコアのセクタ、スタックの高水位
    tone_player.hpp/.cpp   ブザーのチューン再生 (audio:: の実装)
  tools/
    mkrom.js               バックアップ ROM にランチャとプログラムを入れる / 取り除く (node)
    sim.py                 Unicorn で PRAM + XIP を走らせる (画面を PNG に、キーとダイヤルの台本、関数別プロファイル)
```

計測オーバーレイ (`profiler.cpp`) とハイスコアをいつ書くか (`high_score_store.hpp`) は Xiamocon 版のものを
そのディレクトリから直接コンパイルして共有する (ESPboy 版と同じ仕組み: `ds_config.hpp` / `ds_platform.hpp` を
同じ名前で持つ。インクルードパスはこのディレクトリを先にして、Xiamocon の同名ファイルを隠す)。

## イメージの構造と配置

PRAM イメージ (pram.bin、64KB 以下):

| オフセット | 内容 |
|---|---|
| 0x000 | ベクタテーブル 80 語 (スタック先頭 0x18020000、Reset_Handler、フォルト、SysTick) |
| 0x140 | "PMXC" ブロック (ランチャと Studio が見る。中身は使わない) |
| 0x1D8 | "DSPH" ブロック: version、`xip_off` (XIP 部のフラッシュオフセット)、`xip_len`、`hiscore_off`、`detent` |
| 以降 | `.pram_text*` (SysTick、音、入力、hw/、flash、plat_c、core と ShapoGFX のホットコード)、pram_hot.ld のオブジェクト、libgcc (soft-float)、memcpy/memset、`.data` のロードイメージ |

XIP 部 (xip.bin) は残りの `.text` / `.rodata` と `.init_array`。リンク時に `XIP_ORIGIN = 0x10000000 + XIP_FLASH_OFF - 0x11000`。

フラッシュ上の配置 (Glacier / Tropical。純正のコードが 0xB5E00 まであり、Studio が DOOM に使う 0xA0000 は空いていない):

| オフセット | 内容 |
|---|---|
| 0xB6000 | ランチャ (4KB。純正コードの直後の空きページ。Studio の `freeStubOff()` と同じ規則) |
| 0xB7000 | PRAM イメージ (64KB) |
| 0xC7000 | XIP 部 (`XIP_FLASH_OFF`、既定) |
| 0x10D000 | ハイスコアのセクタ (`HISCORE_OFF`、既定) |
| 0x10E000〜 | 触らない (Studio の設定ページ 0x10E000、六フィールドのページ 0x10F000、DPD ファーム 0x110000) |

Land / Water / Sky はランチャ 0xB0000、イメージ 0xA0000 が空いているので mkrom.js はそこを使う。
その場合 XIP 部は 0xB1000 (`XIP_FLASH_OFF=0xB1000 ./build.sh`)。`node tools/mkrom.js --rom dump.bin --place` が
ダンプを見てこの 2 値をシェルの代入文で出すので、make_bin.sh はそれを eval してからビルドする (エディションの
指定は不要)。mkrom.js はイメージの設定ブロックと
ROM の空きを照合し、合わなければ何も書かずに理由を出す。

ランチャは `stub_tpl.bin` のプレースホルダ (`APP_SRC` = イメージの窓アドレス、`ORIG0..3` = ホーム画面の元の
ハンドラ、`AUTO` = 0) を埋めたもの。ホーム画面のシーンテーブル (`PSID_` の名前と 28 バイトのエントリの並び)
を探し、エントリ +12 からの 4 語をランチャの +0x40/+0x46/+0x4C/+0x52 に向ける。ランチャは呼ばれるたびに
A+C が押されているか数え、90 回で `start`: クロックをソース 4 に切り替え、`APP_SRC` から 64KB を PRAM 0 に
コピーし、VTOR を 0 にして SYSRESETREQ。

## 起動と復帰

`Reset_Handler` (PRAM) の順序:

1. `ds_para_mark()`: SRAM を壊す前に純正の時計 (日と秒の基点 + 6 つの表示フィールド) を探し、RTC 秒カウンタと
   一緒にメールボックス 0x20000FC0 ("PCLK") に残す (paraclock.h / paraback.h)。
2. UNALIGN_TRP を切る。`hw_init()`: WDT の設定、クロック (PLL)、周辺のイネーブル、XIP ベース 0x60011000、ピン、
   表示電源、パネル初期化、バックライト。
3. `.data` を PRAM から SRAM にコピー、`.bss` をゼロに。NVIC の割り込みを全部止め、GPIO2 を入力、SysTick 1ms。
4. A+C が離されるまで待つ (最大 3 秒)。
5. `gameMain()` (XIP): `.init_array` (静的オブジェクトのコンストラクタ)、UART1、音、`Game::reset(seed)`、
   ハイスコアの読み込み、`Renderer::init()`、転送時間の計測、フレームループ。

復帰 (`leave()`、PRAM): 音を止め、保留中のハイスコアを書き (`HighScoreStore::flush`)、SysTick を止め、
0x20000FFC に "PMXL"、`ds_para_back_fix()` で純正の「生きていた時刻」(設定ページ 0xEFE000 / 0xEFF000 の
+0x1C〜、CRC 付き) を滞在時間ぶん進め (進めないと復帰時にその時間を一気に生きて空腹や死亡になる。
Paramix 1.6.8 の PARA_BACK と同じ)、バックライトを消し、ボタンが離されるのを待って
`hw_back_to_paradise()` (ファームのルーチン 0x100FF569: 暗号化された純正 PRAM イメージを読み直して再起動)。

フォルト: 専用スタックに切り替えて `ds_fault_c()` (PRAM): 音を止め、画面を赤く塗り、ダイヤル 2 秒長押しで
純正へ戻る。sim.py は `ds_fault_c` をシンボルで捕まえて PC / LR / CFSR を表示する。

## 入力の割り当て

`readInput()` (main.cpp) が生のボタンとダイヤルの検出数から `sim::Input` を作る。

| 操作 | core への入力 | 備考 |
|---|---|---|
| ダイヤル回転 (プレイ中) | レバー: `x` = クリック数 × 127 / `LEVER_MAX`。反時計回りで左、時計回りで右、±`LEVER_MAX` (5) クリックで打ち止め | 回し戻すまで保持。PLAYING / LAUNCH / ARRIVE 以外では中立に戻す (エンコーダは相対値なので、打ち止めがあるから中立がずれない) |
| ダイヤル回転 (プレイ中以外) | 1 クリック = 左 / 右を 1 回 (`x` = ∓127 を 1 tick、次の tick で離す) | クリックは tick に渡すまで溜めておく (`tickInput()`)。core はメニューで軸をパッドとして読み、押された瞬間だけ見る |
| B / C | `y` = -127 (ダッシュ) / +127 (ブレーキ) | プレイ中のみ。両方押しでペアでなければ打ち消して 0 |
| A | `Button::A` (攻撃・決定) | |
| ダイヤル押下 | `Button::B` (緊急回避) | |
| B+C 同時 (押し下げの時間差 `PAUSE_PAIR_MS` = 200ms 以内) | `Button::PAUSE` (押した瞬間に core が切り替える) | ペアはどちらかを離すまで続く。長く押している B に C が加わるのはブレーキであってポーズではない |
| C 短押し (タイトル / ポーズ、`MUTE_PRESS_MS` 未満、B が加わらない) | 離したときに `y` = +127 を 1 tick (core の DOWN = ミュート切り替え) | B+C でポーズ / 再開しても切り替わらないよう、押した瞬間ではなく離したときに判定 |
| B+C のペアを 3 秒 (タイトル) | `Button::B` を保持 → core のベンチマーク開始 (B 3 秒) | ダイヤル長押しは復帰に使うため |
| ダイヤルを押しながら A (タイトル / ポーズ) | 計測オーバーレイの切り替え。その A は core に渡さない | |
| ダイヤル 2 秒長押し (タイトル / ポーズ) | 純正へ戻る (`leave()`) | ベンチマーク表示中は `Button::B` を出さない (閉じるのは短押し) |

エンコーダは SysTick で読み、グレイコードの遷移表で ±1 し、`DETENT_STEPS` ごとに 1 クリック。
実機 (2026-11-07) では 1 クリック = 1 ステップだったので `DETENT_STEPS` = 1。遷移表は時計回りが正になる向きで
持つ (hello の棒も同じ符号: 時計回りで伸びる)。

## 表示

`hw_px_begin(0, y, 128, y+32)` → `hw_px_raw(帯, 8192)` → `hw_px_end()`。帯は RGB565_SWAPPED (メモリ上で
ビッグエンディアン) なので、そのままのバイト順がパネルの順になる。全画面 32KB の所要時間は起動時に
`measureTransfer()` で測り、オーバーレイの `XFR` に出る。

- `LCD_FASTCLK` (build.sh): SPI1 の分周を 1 から 0 に (倍速)。Paramix の DOOM とエミュレータ群はすべてこの設定。
- `LCD_STREAM` (build.sh): `hw_px_raw()` が帯 8KB を一続きで送る (FIFO フルのビットだけ見る)。無しだと Paramix
  DOOM v0.8.17 の方式で、16 バイトごとにバスが空になるのを待って FIFO をクリアする。
- 実機の XFR: 分周 1 + 16 バイト方式で 11.74ms (2026-11-07)。倍速 + 一続きは未計測。

## 音

`tone_player.cpp`: 1 音、`TUNES[]` は ESPboy 版と同じ。`audio::request()` が優先度と `HOLD_MS` (400ms) で選び、
`start()` が最初の音を `hw_tone()` で鳴らし、SysTick の `audio::tick1ms()` が音符の残り ms を数えて次の音程に
切り替える (すべて PRAM)。`hw_tone()` は CT32B7 の周期を変えるだけで、周期が走っているカウンタより短ければ
カウンタをリセットする (Paramix の TONE_SAFE)。PCM の経路は無い (この個体の SNC73410 にはオーディオコーデックが
無く、ブザーはタイマー直結)。

## ハイスコア

レコード (16 バイト) と「いつ書くか」は Xiamocon 版と共通。読み出しは 0x60000000 + `hiscore_off` から。
書き込みは帯バッファ (8KB) を 0xFF で埋めてレコードを置き、`ds_flash_write_sector()` (PRAM、割り込み禁止) で
`flash_prog4k()` (消去 → DMA で書き込み → WIP 待ち)。書いている間 XIP 窓は読めないので、呼び出し側まで PRAM。
書き戻して照合する。ブザーはハードウェアのタイマーなので鳴り続ける。

## フレームループ

ESPboy 版と同じ: ボタンを読む → 溜まった tick を回す (`MAX_CATCHUP` 3) → `beginFrame()` → 帯ごとに
`renderBand()`、オーバーレイ、転送 → `ds_frame_mark()` (sim.py がフレームを数える印)。時計は SysTick の ms と
カウンタ値 (`nowUs()`)。

## PRAM の使い方

64KB のうち、`.pram_text*` (hw、flash、plat_c、SysTick、音、入力、core の `DEVOURSPHERE_HOT_ATTR`、ShapoGFX の
`SHAPOGFX3D_HOT_ATTR` + `HOT_INSTANTIATE`) に加え、pram_hot.ld で `gfx3d.o`、`sim/fixed.o`、`render/sphere.o` を
丸ごと置く。libgcc の soft-float と memcpy/memset も PRAM。2026-11-07 のビルド: `.pram` 56,328 + `.data` 2,628 =
58,956 バイト (残り約 6.5KB)。XIP 部 127,368 バイト。`.bss` 62,496 バイト (`Game` 23.5KB、アリーナ 16KB、
`Renderer` 10.6KB、帯 8KB)。スタックは SRAM 上端の 16KB (`STACK_BYTES`、高水位はパターン埋めで測る)。

何を PRAM に置くかは `tools/sim.py --prof` (関数別の PC サンプル。アドレスが 0x10000 未満なら PRAM) で決める。
実機では 16KB キャッシュの効きが違うので、オーバーレイの `BGN` / `RAS` で読み直す。

## シミュレータ (tools/sim.py)

Unicorn に PRAM イメージと XIP 部 (と任意で実機のダンプ) を載せ、hw_* / flash_* の関数をシンボルで捕まえて
ホスト側で応える (Paramix の devemu.py の方式)。SysTick は `--rt` 命令 (既定 64,000 ≒ 96MHz の 1ms) ごとに
`systick_c()` を呼ぶ (XPSR を含むレジスタを保存・復元)。`--keys "秒:マスク,..."` (1 A、2 B、4 C、8 ダイヤル)、
`--wheel "秒:ステップ数"`、`--shots "秒,..."` で PNG、`--prof` で関数別の割合、`--watch sym` で変数の値。
出力の fps は「命令数で律速したときの値」で、実機のキャッシュミスやフラッシュ待ちは含まない
(過去の経験では unicorn の命令数は実機の改善を過大に見せる)。

2026-11-07、hello と本体はこのシミュレータで動作確認。**実機は未確認** (次節)。

## 書き込みの手順と注意

1. Paramix Studio で ROM を読んでバックアップを取る (16MB)。
2. バックアップを `roms/golden.bin` に置いて `./make_bin.sh` (hello と本体をビルドし、`roms/hello.bin` と
   `roms/devoursphere.bin` を作る)。手でやるなら `hello/build.sh` →
   `node tools/mkrom.js --rom roms/golden.bin --pram hello/build/hello.bin --out roms/hello.bin`。
3. Studio で roms/hello.bin を開き、Device → ダウンロード画面 (Planet, A, Connect, Download, Download) → Connect →
   "Write new ROM to the device" ("Only write changes" にチェック。差分 18 セクタ)。
4. 本体のホーム画面で A+C を約 2 秒。4 つの四角 (A/B/C/ダイヤル)、ダイヤルの棒、点滅する点が出れば起動成功。
   B でビープ。ダイヤルを時計回りに 1 クリック回すと棒が 1 ピクセル伸びる (= `DETENT_STEPS`)。
   ダイヤル 2 秒長押しで純正に戻る。
5. 本体用: `roms/devoursphere.bin` を同じ手順で書く (差分約 50 セクタ)。手でやるなら `devoursphere/build.sh` →
   mkrom.js (`--xip` 付き)。
6. 元に戻す: バックアップを同じ手順で書く (差分だけ書かれる)。または `mkrom.js --remove`。

注意:
- 書き込み中は電池を抜かない、ケーブルを外さない。電池は新しいものを。
- Web Serial は Windows 側の Chrome / Edge で。WSL からドングルは見えない。
- 先頭 0x11000 (ヘッダと暗号化された純正 PRAM イメージ)、ID ページ (0xD29000 / 0xD49000)、セーブ (0xEFE000〜) には
  触らない (mkrom.js は 0x10F000 より上に何も書かない)。
- 画面が黒いまま / 固まった: ダイヤル 2 秒長押し (フォルト画面でも同じ)。それでも駄目なら電池を抜く。純正に戻れば
  Studio でバックアップを書く。Studio の救済 (電源投入時に C 押しでフラッシュアプリ) は Studio が
  書き込み時に 0xFF0000 に入れていれば使える。
- まだ実機で確かめていないこと: hw_init の列が「純正ファーム → ランチャ → ソフトリセット」の文脈で DOOM と同じに
  通るか (同じ文脈なので通る見込み)、エンコーダのステップ数と向き、転送時間、フレームレート、XIP 窓から C++ の
  コードが問題なく走るか (DOOM の実績あり)、UART1 の出力。

## 既知の制限

- 既定構成 (224 体) は RAM にも CPU にも載らない。縮小構成のみ。
- 効果音はビープ音のチューン。
- Paramix Studio のメニューや他のゲストとは共存を考えていない (ランチャの位置は同じ規則なので、Studio で
  DOOM やエミュレータを入れた ROM には mkrom.js は「使用中」と言って書かない)。
- 純正ファームのホーム画面のハンドラを差し替えるので、Studio が「プログラムが入っている」と認識する可能性がある。
  戻すときはバックアップを書けばよい。
