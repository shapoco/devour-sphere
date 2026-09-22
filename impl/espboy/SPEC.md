# Devour Sphere (ESPboy 版)

[ESPboy](https://www.espboy.com/) (ESP8266 の携帯ゲーム機。WeMos D1 mini、128x128 の ST7735、
MCP23017 経由の 8 ボタン、スピーカー) で core/ を動かすためのファームウェア。
ゲームのルールと描画はすべて core/ 側にあり、ここには「クロックとボタン」「表示への転送」
「フレームループ」「音」だけがある。core は **縮小構成** (後述) でビルドする: このチップの RAM
(DRAM 96KB、外部 RAM なし) と CPU (160MHz のシングルコア、FPU も除算器も無し) には
既定の構成 (エンティティ 224 体、`Game` 84KB) が載らない。

**Arduino も ESPboy のライブラリも使わない。** [ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK)
v3.4 (ESP-IDF 流の SDK) のプロジェクトで、理由は RAM: Arduino core は WiFi を使わなくても
WiFi スタックの静的バッファを持ち、リンカに見せる DRAM も 80KB (上端 0x3FFFC000) で、
起動時の空きヒープは 50KB 前後になる。RTOS SDK は DRAM 96KB 全域 (0x3FFE8000〜0x40000000) を
リンカ領域にし、`esp_wifi_init()` を呼ばない限り WiFi を初期化せず、SDK 自身の静的な DRAM は
8KB (hello_world の実測: `.data` 1,356 + `.bss` 6,464)。IRAM は既定で 48KB (命令キャッシュ 16KB) で、
コードが使った残りは自動的に 32 ビットアクセス専用のヒープになる。

## 必須要件

- 画面は 128x128 (ST7735)。16 ビット (RGB565) モードで、HSPI で帯ごとに書く。**DMA は無い**
  (ESP8266 の SPI に DMA は繋がっていない) ので、転送は CPU が 64 バイトの FIFO に詰めて送る。
- フレームバッファを持たず、16 行の帯を **1 枚** で描画・転送する (転送が CPU なので 2 枚にしても重ならない)。
- シミュレーションは 30Hz 固定、描画は追いつける範囲で行う (可変フレームレート)。
- 効果音はスピーカー (GPIO0) に矩形波 1 音。タイマー割り込みでピンをトグルする (PCM パックは使わない)。
- ハイスコアはフラッシュの専用パーティション (4KB) に保存する。
- 動的確保はしない。`Game`、`Renderer`、アリーナ、帯は `.bss`。

## ファイル構成

```
impl/espboy/
  SPEC.md                    この文書
  devoursphere/
    CMakeLists.txt           RTOS SDK のプロジェクト (project.cmake を include)
    sdkconfig.defaults       160MHz、-Os、IRAM 48KB、メインタスクのスタック 8KB、WDT、UART 115200
    partitions.csv           factory 1MB + hiscore 4KB (type 0x40)
    env.sh                   IDF_PATH (既定 ~/esp/ESP8266_RTOS_SDK)、ツールチェーン、Python 環境を PATH に
    build.sh / flash.sh / monitor.sh / build_release.sh
    merge_bin.py             bootloader + パーティションテーブル + app を 1 つの .bin に (0xFF 詰め)
    components/
      devoursphere_core/     core/ をコンポーネントとしてビルド。縮小構成のマクロはここ
      shapogfx/              ShapoGFX をコンポーネントとしてビルド。フラット・不透明・固定小数点、スパン描画を IRAM に
    main/
      app_main.cpp           フレームループ、ボタンの割り当て、帯バッファ、起動時のメモリ報告
      ds_config.hpp          帯の高さ、アリーナ、tick 周期、ピン、SPI クロック、パネルのオフセット
      ds_platform.hpp/.cpp   時計、乱数シード、ハイスコアのパーティション、スタックの高水位
      display.hpp/.cpp       ST7735 (HSPI のレジスタ直書き)
      input.hpp/.cpp         MCP23017 (ボタン、パネルの CS) と MCP4725 (バックライト)
      tone_player.hpp/.cpp   矩形波の効果音 (se_player.hpp の audio:: を実装)
```

計測オーバーレイ (`profiler.cpp`) とハイスコアをいつ書くか (`high_score_store.hpp`) は
Xiamocon 版のものをそのディレクトリから直接コンパイルして共有する (PicoSystem 版と同じ仕組み:
`ds_config.hpp` / `ds_platform.hpp` を同じ名前で持ち、プロファイラは `<...>` でインクルードする)。

## ビルドと書き込み

```sh
cd impl/espboy/devoursphere
./build.sh                    # build/devoursphere.bin (+ bootloader, partition-table)
                              # と 3 つを 1 つにした build/devoursphere.factory.bin
./flash.sh [/dev/ttyUSB0]     # esptool で書き込み (WeMos の CH340 経由)
./monitor.sh [/dev/ttyUSB0]   # 起動ログ (メモリの実数)
```

- `IDF_PATH` は環境変数を優先し、未設定なら `${HOME}/esp/ESP8266_RTOS_SDK` (`env.sh`)。
- ツールチェーンと Python 環境は SDK の `install.sh` が `~/.espressif` に置くもの。
  2026-09-22 の環境では `tools/idf_tools.py install` で `xtensa-lx106-elf` 8.4.0 (esp-2020r3) が入り、
  `install-python-env` は Python 3.12 の "externally managed" 制約で失敗したので、
  `python3 -m venv ~/.espressif/python_env/rtos3.4_py3.12_env` を手で作って `requirements.txt` と
  `setuptools<70` (`pkg_resources` が要る) を入れた。`env.sh` は `python_env/rtos*_env` を探す。
- SDK は C++ を gnu++11 でコンパイルするので、プロジェクトの CMakeLists で `-std=gnu++17` を後ろに足す。
  `-fno-threadsafe-statics` も付ける。SDK の NVS (system API から参照される) が libstdc++ の
  例外オブジェクトを引き込み、その静的ガードが `pthread_cond_*` を要求する。`main` が `pthread`
  コンポーネントを REQUIRES に持つとリンクグループ内で解決する。
- この SDK の `idf_build_set_property(COMPILE_DEFINITIONS ...)` は値をそのまま渡す (`-D` を付けない) ので、
  コンポーネントの CMakeLists は `-DNAME=VALUE` の形で書いている。
- `build.sh` の最後に `merge_bin.py` が bootloader (0x0)、パーティションテーブル (0x8000)、app (0x10000) を
  0xFF で詰めて 1 つにした `build/devoursphere.factory.bin` を作る (SDK 同梱の esptool v2.4 には merge_bin が無い)。
  オフセット 0 に 1 ファイルを書く書き込み器ならこれで済む: リリースの upload.sh、
  そして **WildCardBoy の ESPboy カード** (ホストが TF カードの `.bin` を esp-serial-flasher でオフセット 0 から
  ストリーミングし、ヘッダを書き換えない。`/WCB/Cards/ESPboy/Apps/` に置いて Apps メニューから選ぶ)。
  イメージ先頭のフラッシュモード / サイズ / 周波数のバイトは bootloader.bin のもの (`e9 03 02 40` = DIO、4MB、40MHz)
  で、sdkconfig から elf2image が書いたものがそのまま使われる。
- `make_release.sh` はこのターゲットも `espboy/` に含める (`devour-sphere.factory.bin` と upload.sh)。

### WildCardBoy の ESPboy カードで動かすとき

カード (wildcardboy/cards/ESPboy/SPEC.md) は実機と次の点で違うが、このファームウェアはどれも実機と同じ手順で
触るので、そのまま動く見込み:

- LCD は無く、HSPI の信号を LcdTap が ST7789 系として復号する (`ESPboy` プリセット: 136x136 に
  オフセット (6, 5) で 128x128 を切り出し、回転 2、RB 入れ替え、INVOFF)。CASET / RASET / MADCTL / COLMOD は
  ESPboy ライブラリと同じものを送っているので、実機と同じ絵になるはず。
- MCP23017 はホストが模擬する (0x20。IODIR / GPPU / OLAT の書き込みは受け、GPIOA はボタンを返す)。
  MCP4725 (0x60) は模擬されず NACK になり、`setBacklight()` は失敗を無視する。
- 音は GPIO0 が RC フィルタ経由で LCAUDIO に出る。UART0 (起動ログ) はホストの ISP 線に出る (無害)。

## メモリ配分

リンク結果 (2026-09-22、`xtensa-lx106-elf-size`):

| 領域 | サイズ | 内訳 |
|---|---|---|
| `.dram0.bss` + `.data` | **60,176** | `Game` 23,512、アリーナ 16,384、`Renderer` 10,512、帯 (128x16x2) 4,096、SDK と残り 約 5.7KB |
| DRAM の残り (ヒープ) | 約 38KB | FreeRTOS がここからタスクのスタック (メイン 8KB、タイマー 3.5KB、アイドル) を取り、**実機の空きは 20KB 台の見込み** (`app_main` が起動時に出力する) |
| `.iram0.text` + bss | 23,992 / 49,152 | SDK 約 20KB、ShapoGFX のスパン描画 3,935 (`.iram1.gfx3d`)、割り込み。残り 25KB は 32 ビット専用ヒープ |
| フラッシュ | 276,624 (`.bin`) | `.text` 215,706 + `.rodata` 36,244。core 116,773、ShapoGFX 42,479、main 11,671 (ライブラリのサイズ) |

Arduino 上で試算していた 45KB の予算に対して、RTOS SDK では 60KB の静的領域を置いてなお 20KB 以上が余る。
それでも縮小構成が要るのは RAM ではなく CPU の側 (次節)。

## 縮小構成

core は `DEVOURSPHERE_*` のマクロ (core/SPEC.md「縮小構成のためのビルド時定数」) で上限と規則を
変えられる。この版の値と理由 (`components/devoursphere_core/CMakeLists.txt`):

| マクロ | 値 (既定) | 理由 |
|---|---|---|
| `TICK_RATE` | 30 (60) | シングルコアで tick とフレームが直列 |
| `SPHERE_RADIUS_SHIFT` | 16 (17) | 半径 256 FU、表面積 1/4。**絶対値の距離 (視界、リスポーン環、制限時間) はそのまま**なので、同じゲームの短縮版になる |
| `INITIAL_ENTITIES` / `MAX_ENTITIES` | 48 / 64 (200 / 224) | tick の 8 割がエンティティ数に比例し、200 体では 1 tick 約 31ms の見込みで 30Hz が成立しない。48 体は面積 1/4 の球で密度 96% |
| `INITIAL_FOOD_FRAGMENTS` / `FOOD_TARGET` / `MAX_FLOATING_FRAGMENTS` | 80 / 100 / 128 (320 / 400 / 512) | 密度を保つ。上限は目標の 1.28 倍しかないので |
| `FRAGMENT_KEEP_LARGEST` | 1 (0) | 配列が一杯なら最小のフラグメントを置き換え、最小より小さいものは生成しない (既定は最古を置き換える) |
| `MAX_BULLETS` | 32 (128) | 実測ピーク 84 は 200 体のとき |
| `SPHERE_TARGET_PX` / `MAX_WIRE` | 32 / 160 (24 / 1100) | 128px の画面で面 32px は約 16 面、線 75 本前後 |
| `MAX_LINES2D` / `MAX_POINTS2D` | 96 / 48 (512 / 256) | |
| `SUPPRESS_ALPHA` | 1 (0) | PicoSystem 版と同じ。合成は画素ごとにフレームを読み戻す |
| `NO_DEPTH` | 1 (0) | 世界のレイヤーも深度なし。レコードから深度平面 (12B) が消え、スパンの深度比較も無くなる。描画順は render 層が手で決める (core/SPEC.md) |
| `MAX_DEBRIS` / `MAX_DUST` / `MAX_GAUGES` / `MAX_MARKERS` / `MAX_ENEMY_MARKERS` / `MAX_STARS` | 16 / 16 / 16 / 16 / 12 / 48 (64 / 64 / 64 / 40 / 32 / 120) | 64 体・128px に見合う数 |

ShapoGFX 側 (`components/shapogfx/CMakeLists.txt`): `SHAPOGFX3D_TEXTURE=0`、**`GOURAUD=0` (フラットシェーディングのみ)**、
`BLEND=0` (すべて不透明)、`FIXED_POINT=1`、`VCACHE_SIZE=16`、`LAYER_MAX=4`。
GOURAUD=0 でスパンは 68 → 44B (TEXTURE=0 と合わせて 28B)、滑らかに塗る記録は 96 → 48B になる。
本体は元からフラットなので見た目は変わらず、オーラの扇形と体力警告のグラデーションが単色になる。

### host での難易度曲線

`core/tools/bench_levels.sh` 相当 (レベル 1〜8 × シード 7/11/23 × 180 秒、AI 自機、`sim_bench` を
この構成のマクロでビルド) を既定の構成 (30Hz) と並べた平均:

| lv | 生存秒 (既定 / この版) | 死亡 | 被ダメ/s (%) | ハンター | 撃破数 |
|---|---|---|---|---|---|
| 1 | 180 / 180 | 0.0 / 0.0 | 0.1 / 0.1 | 0.44 / 0.31 | 84 / 18 |
| 3 | 179 / 177 | 0.3 / 1.0 | 0.5 / 0.9 | 0.40 / 0.35 | 264 / 46 |
| 5 | 121 / 77 | 1.7 / 2.0 | 2.7 / 3.9 | 0.64 / 1.30 | 259 / 31 |
| 7 | 58 / 130 | 2.0 / 2.0 | 4.5 / 3.0 | 0.75 / 0.65 | 157 / 90 |
| 8 | 72 / 89 | 2.0 / 2.3 | 3.3 / 3.3 | 0.86 / 0.72 | 199 / 78 |

被ダメージと死亡数は同じ程度、撃破数は敵の数なりに 1/3〜1/5。AI 自機は競合が減った分だけ大きく育つ
(sizeMax がレベル 7 以降で桁違いに大きい)。**面白さの判断は実機でのプレイ待ち**で、
半径を 2^17 に戻して密度を落とす選択肢 (エンティティ数はそのまま) も残っている。

## CPU の見込み

PicoSystem (M0+ 250MHz、2 コア) からの換算で、クロック比 0.64 とキャッシュ実行の分でおよそ 2 倍遅く、
tick と描画が 1 コアに乗る。48 体の tick 約 12ms (30Hz で 36%)、`beginFrame` 約 16ms、帯 約 7ms、
転送 10ms で **1 フレーム 70〜100ms (10〜15fps)** の見込み。PicoSystem の事前見積りはフレームで
4 倍外れたので、実機の計測パネルで読み直す。

## 表示

ST7735 は HSPI (SCK GPIO14、MOSI GPIO13) に繋がり、D/C は GPIO16、リセット線は無い (SWRESET で代用)。
**チップセレクトは ESP のピンではなく MCP23017 の GPB0** で、ESPboy のライブラリと同じく起動時に
Low に固定する (`input::init()`)。以後の SPI はすべて D/C だけで区切る。

- 初期化列は TFT_eSPI の ST7735 "green tab" (`Rcmd1` → `Rcmd2green` → `Rcmd3`、ESPboy の
  `User_Setup.h` は `ST7735_GREENTAB3` + `TFT_BGR`)。回転 0 の MADCTL は 0xC8 (MX | MY | BGR)、
  ガラスのオフセットは列 2・行 3 (`PANEL_COL_START` / `PANEL_ROW_START`)。COLMOD は 0x05 (16 ビット)。
- SDK の `spi_init()` でピンとモード (0)、ビット順 (MSB first)、バイト順 (メモリ順) を設定し、
  帯の転送は `SPI1` のレジスタに直接書く: 64 バイトずつ `data_buf[]` に詰めて `cmd.usr` を立て、
  完了を待つ (SDK の `spi_trans()` は 1 回ごとに設定を書き直すので使わない)。帯 4KB は 64 回。
- SPI クロックは 80MHz / `SPI_CLK_DIV` (3 = 26.7MHz。TFT_eSPI が「ST7735 は 27MHz 超で化けることがある」と
  している値)。全画面 32KB で 9.8ms の CPU 時間。2 (40MHz) は実機で試す価値がある。
- 帯は 1 枚 (`g_band`、128x16 RGB565BE)。描いて送って次の帯、の直列。
- `Display::init()` の最後に黒で塗ってからバックライト (MCP4725、4095) を点ける。

## 入力

MCP23017 (I2C 0x20、SDA GPIO4 / SCL GPIO5) のポート A に 8 ボタン (内部プルアップ、押すと 0)。
SDK の I2C はビットバンギングで、1 回のレジスタ読み (GPIOA) は 0.3ms 程度、フレームに 1 回読む。

| 操作 | ボタン |
|---|---|
| 左右旋回 | LEFT / RIGHT |
| ダッシュ | UP |
| ブレーキ | DOWN |
| A (攻撃・決定) | A (ACT) |
| B (緊急回避) | B (ESC) |
| ポーズ / 再開 | 左肩 (LFT) |
| ミュート切り替え | タイトル / ポーズ画面で DOWN (core が処理する) |
| 計測オーバーレイの切り替え (非表示 → FPS のみ → 全部) | タイトル / ポーズ画面で右肩 (RGT、押した瞬間) |

## 効果音

スピーカーは GPIO0 に繋がり、ESPboy のライブラリは `tone()` で鳴らす。ここも同じで、
FRC1 タイマーの割り込み (`tone_player.cpp`、IRAM) がピンをトグルする。他の版の PCM パックは
DAC か DMA 駆動の PWM が要るので使わない。

- 音ごとに最大 4 音符の短い旋律 (`TUNES[]`、周波数と長さ)。割り込みは今の音符のトグル残数を数え、
  尽きたら次の音符の半周期を `frc1.load.data` に書く (リロード付きなので次の周期から効く)。
  旋律が終わったらピンを Low にして `active` を落とし、メインループの `audio::poll()` がタイマーを止める。
- 1 音、優先度付き。優先度と `HOLD_US` (400ms) は RP2 版と同じ (impl/picosystem/SPEC.md「効果音」)。
- 割り込みはフラッシュのコードに触れないので、ハイスコアの書き込み (キャッシュ停止) 中も鳴り続ける。

## ハイスコアの保存

レコード (16 バイト) と「いつ書くか」は Xiamocon 版と共通 (`high_score_store.hpp`)。
ここでは `partitions.csv` の `hiscore` パーティション (type 0x40、subtype 0、0x110000、4KB) に
`esp_partition_erase_range()` / `esp_partition_write()` で書き、`esp_partition_read()` で読む。
未書き込みの 0xFF は CRC で弾かれる。

## フレームループ

`app_main()` のタスク (優先度 1、スタック 8KB) が全部やる: ボタンを読む → 溜まった tick を回す
(`MAX_CATCHUP` = 3) → `beginFrame()` → 帯ごとに `renderBand()` と転送 → 先頭に戻る。
PicoSystem 版の `DS_SIM_ON_CORE1=0` の経路と同じ構成で、ループは一度もブロックしないので
アイドルタスクが動かず、**タスク WDT はフレームごとに `esp_task_wdt_reset()` で自分で餌をやる**。

## デバッグ表示

シリアル (UART0、115200) には起動時に DRAM / IRAM の空き、`Game` 等のサイズ、全画面転送の実測を出す。
それ以外は画面の計測パネル (右肩で切り替え) で、行の意味は impl/xiamocon/SPEC.md「デバッグ表示」と
impl/picosystem/SPEC.md の 5 行の内訳。違いは `DMA` 行が転送の CPU 時間になること、最後の内訳行が
`D U V T` (T は転送) になること、`STK1` が常に 0 なこと。21 桁 × 6px = 126px なので幅は収まるが、
全部表示の 13 行は下端で切れる。

## 実機の記録

まだ無い (2026-09-22: ビルドが通った段階。実機に書いていない)。最初に見るもの:
起動ログの空きヒープ、`XFR` (全画面転送)、`FPS` / `TCK` / `BGN` / `RAS`、`STK0` (8KB に対して)、`ARN`。

### 未確認の事項

- 実機で動くこと自体 (パネルの初期化列、MADCTL とオフセット、I2C のアドレス、GPIO16 の D/C、GPIO0 の音)。
- `uxTaskGetStackHighWaterMark()` の単位 (word として計算している。`STK0` が 4 倍おかしければここ)。
- SPI 40MHz、フラッシュ 80MHz (`CONFIG_ESPTOOLPY_FLASHFREQ_80M`)、`SOC_FULL_ICACHE` (キャッシュ 32KB と IRAM 25KB のどちらが効くか)。
- 半径 2^16 のゲームとしての感触。
