# Devour Sphere (wave_43 版)

Waveshare [ESP32-P4-WIFI6-Touch-LCD-4.3](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm)
(ESP32-P4 に 4.3 インチ 480x800 の MIPI-DSI パネルと GT911 のタッチ。Kern では "wave_43" と呼ぶボード)
で core/ を動かすためのファームウェア。概要・操作・ビルドの手順は [README.md](README.md)。
この文書は実装の詳細。ゲームのルールと描画はすべて core/ 側にあり、
ここには「実機へのビルド」「表示への転送」「タッチ入力」「音」だけがある。

**M5Tab5 版の移植**である。同じ ESP32-P4 なので、帯の ping-pong、PPA による拡大・回転・
バイト入れ替え、2 コアの分担、仮想パッド、NVS のハイスコアは impl/m5tab5/ のものを
そのまま持ってきている (それぞれの理由は impl/m5tab5/SPEC.md に書いてあり、ここでは繰り返さない)。
違うのはボードの足回りで、M5Unified / M5GFX の代わりに **ESP-IDF 自身のドライバ**を使う。

## 必須要件

- 画面は横長で使う。パネルは **480x800 の縦長**なので、毎フレーム 90 度回す。
- ゲームは **640x384** のランドスケープのフレームに描き、PPA が **5/4 倍**の拡大・90 度回転・
  バイト入れ替えを 1 回の操作でまとめてパネルのフレームバッファ (800x480 を回したもの) へ書く。
- フレームバッファを 1 枚も持たず、帯を 2 枚交互に使って描画・転送する。
- シミュレーションは 60Hz 固定、描画は追いつける範囲で行う (可変フレームレート)。
- 入力は M5Tab5 版と同じ仮想パッド (アナログの方向ディスク + A + B) と、ポーズボタン。
- 効果音は ES8311 コーデック経由のスピーカーで**多重発音**する (M5Tab5 版と同じ鳴り方)。
- ハイスコアは NVS に保存する。
- プラットフォームは **ESP-IDF v6.1** (Kern と同じ)。M5 のライブラリは使わない。

## ファイル構成

```
impl/wave43/
  README.md            概要、操作、ビルド
  SPEC.md              この文書
  devoursphere/        ESP-IDF プロジェクト
    CMakeLists.txt     トップレベル
    sdkconfig.defaults ボード固有の設定 (後述)
    partitions.csv     nvs / phy_init / factory 4MB (M5Tab5 版と同じ)
    build.sh           DS_IDF_PATH を読んで idf.py build
    run.sh             同 build + flash
    monitor.sh         同 monitor
    build_release.sh   bootloader + パーティション + アプリを 1 つの .bin に
    components/
      devoursphere_core/CMakeLists.txt   core/ を IDF コンポーネントとして登録 (M5Tab5 版の複製)
      shapogfx/CMakeLists.txt            submodule/shapo-gfx/ を同上
    main/
      CMakeLists.txt   main コンポーネント
      idf_component.yml  esp_lcd_st7701 / esp_lcd_touch_gt911 の依存
      ds_config.hpp    解像度、拡大率、帯、アリーナ、パッドの配置などの定数
      ds_platform.hpp/.cpp  乱数・NVS・スタック計測・メモリ (M5Tab5 版と同じ)
      board.hpp/.cpp   バックライト、パネルの起動、タッチ (M5Unified の代わり)
      panel_st7701.h/.c  DSI バスと ST7701 の初期化 (C。理由は下記)
      panel_out.hpp/.cpp  帯の ping-pong と PPA 転送
      touch_pad.hpp/.cpp  仮想パッド (判定と描画)
      se_player_wave43.cpp  効果音 (ES8311 + I2S、多重発音のミキサ)
      app_main.cpp     エントリ、静的領域、フレームループ、コア間の受け渡し
```

Xiamocon 版と共有しているもの (`profiler.cpp`、`high_score_store.hpp`、`se_player.hpp`) は
M5Tab5 版と同じ仕組みで include パスに入れている。

## ビルドと書き込み

手順 (`build.sh` / `run.sh` / `monitor.sh`) は README.md。

- **ESP-IDF v6.1 が必要** (`idf_component.yml` で `idf: ">=6.1"`)。Kern と同じ IDF をそのまま使う。
- M5Tab5 版と違い、スクリプトの `DS_IDF_PATH` の既定は `${HOME}/esp`。
- 初回は `sdkconfig.defaults` の `CONFIG_IDF_TARGET` で esp32p4 が選ばれる。
- `managed_components/`、`sdkconfig`、`build/` は生成物なので git 管理外。
- コンソールは IDF の既定 (UART0)。Kern と同じ。
- Kern から書き換えると NVS パーティションの中身が読めない (オフセットも暗号化も違う)。
  `nvs_flash_init()` が `NO_FREE_PAGES` / `NEW_VERSION_FOUND` を返したら消して作り直す。
  **フラッシュ暗号化を有効にした個体には書けない** (Kern の `feat/flash-encryption-61` の話で、
  master の Kern を書いただけの個体なら関係ない)。

### sdkconfig.defaults

ボード固有の値は Kern が IDF 6.1 でこのボードに使っているもの
(`~/Kern/sdkconfig.defaults` + `sdkconfig.defaults.wave_43`、および `build_wave_43` で解決された値)。

| 設定 | 理由 |
|---|---|
| `ESP32P4_SELECTS_REV_LESS_V3` / `REV_MIN_1` | チップは v1.x。IDF の既定 (v3.1 以上) ではブートローダが起動を拒否する |
| `ESP_DEFAULT_CPU_FREQ_MHZ_360` | v3 未満の上限。M5Tab5 版の 400MHz より 1 割遅い |
| `SPIRAM_MODE_HEX` + `SPIRAM_SPEED_200M` | パネルのフレームバッファが PSRAM にあり、DSI が常時読み続ける |
| `CACHE_L2_CACHE_256KB` / `LINE_128B` | Kern と同じ |
| `COMPILER_OPTIMIZATION_PERF` | M5Tab5 版と同じ理由 |
| `ESP_MAIN_TASK_STACK_SIZE=16384` | 同上 (`buildSphere()` の再帰) |
| `ESP_TASK_WDT_INIT=n` | 同上 (2 つのコアがそれぞれ専有して回る) |
| `ESPTOOLPY_FLASHMODE_QIO` / `FLASHSIZE_16MB` | Kern と同じ |

## ボードの足回り (board.cpp / panel_st7701.c)

M5Unified がしていたことを IDF のドライバで直接やる。ピン・タイミング・初期化列は
すべて Kern の BSP (`~/Kern/components/wave_43/`) から持ってきた。**その BSP をコンポーネントとして
使わない**のは、マニフェストが LVGL と LVGL アダプタを引き込むから (ゲームは使わない)。

| 部品 | 中身 |
|---|---|
| パネル | ST7701、MIPI-DSI 2 レーン 500Mbps、DPI 30MHz。PHY の電源は LDO 3 番 2.5V。RST は GPIO27 |
| 映像タイミング | 480x800、HSYNC 12/42/42、VSYNC 8/2/60 → 30MHz / (576 x 870) = **59.9Hz** |
| フレームバッファ | `num_fbs = 1`、RGB565。`esp_lcd_dpi_panel_get_frame_buffer()` で取る (公開 API。M5Tab5 版のように Panel_DSI の内部に手を入れない) |
| バックライト | GPIO26、LEDC 5kHz 10 ビット、**出力反転** (反転バッファ越しに駆動されている) |
| I2C | I2C1 (SDA 7 / SCL 8) 400kHz。タッチとコーデックで共有し、`board::i2cBus()` で渡す |
| タッチ | GT911。**RST も INT も未配線** |
| 音 | ES8311 (I2C 0x18)、I2S1 (MCLK 13 / BCLK 12 / WS 10 / DOUT 9)、アンプは GPIO53 (High で有効)。se_player_wave43.cpp |

- ST7701 の初期化列は複合リテラル (`(uint8_t[]){...}`) で書かれていて C++ では通らないので、
  パネルの起動だけ C (`panel_st7701.c`) にしてある。Waveshare / Kern のものを文字どおり写すため。
- 起動順は バックライト消灯 → パネル → フレームバッファを黒で埋めてキャッシュを書き戻す →
  I2C バス → タッチ → バックライト点灯 (コーデックはその後 `audio::init()` が起こす)。書き戻しは M5Tab5 版と同じ理由 (PPA はキャッシュを通らずに書くので、
  後から汚れたラインが書き戻されると PPA の結果を上書きする)。
- GT911 のアドレスは INT のレベルで 0x5D か 0x14 に決まるが、INT が未配線なので**選べず、探すしかない**。
  両方を probe する (Kern と同じ)。
- `ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` は部分的な指示付き初期化子で、C++ の `-Wextra` が
  エラーにするので、同じ値をフィールドごとに書いている。
- タッチが見つからなくてもゲームは起動する (タイトルのデモが流れ続ける)。ログにそう出る。
- ESP32-C6 (Wi-Fi) には触らない。Kern はリセットに保持するが、ゲームには関係ない。

### タッチはタスクで読む

INT が無いのでポーリングになる。GT911 の読み出しは 400kHz で 1ms 程度の I2C 転送で、
これをフレームループの中でやると core0 がそのぶん止まる。そこで **core0 に優先度 2 のタスク**
(`ds_touch`、フレームループの app_main は 1) を置き、**100Hz** で読んで最新の点をスピンロック越しに渡す。
I2C の転送中はタスクがバスを待って眠るので、描画から取られるのは起床と後始末の数マイクロ秒だけ。
`TouchPad::poll()` はその写しを読むだけで、I2C を待たない。

読み出しに失敗したときは前回の点を残す (バスの 1 回の不調で「ディスクから指を離した」ことにしない)。

## 画面: 座標系と PPA

```
ゲームが描くもの  640x384 のランドスケープ、RGB565 ビッグエンディアン
PPA の拡大後      800x480 のランドスケープ (5/4 倍)
パネル            480x800 の縦長ラスタ、RGB565 リトルエンディアン、DSI が常時走査
```

`PANEL_ROTATION` (ds_config.hpp) は M5Tab5 版と同じ番号の付け方で 1 か 3。

- 1: 拡大後の (u, v) → パネルの (479 - v, u)。ランドスケープを時計回りに 90 度。PPA は `ANGLE_270`
- 3: 拡大後の (u, v) → パネルの (v, 799 - u)。反時計回り。PPA は `ANGLE_90`

**PPA の角度・帯のオフセット・タッチの逆変換がすべてこの 1 つの値から決まる**ので、
上下が逆さまに出たらここだけ直せば絵とパッドが同時に追従する。
**実機では既定の 1 で正しく出ている** (2026-09-23)。

### 640x384 と 5/4 倍にした根拠

- パネルの 800x480 をそのまま描くと 640x360 (M5Tab5 版) の 1.67 倍の画素になる。
  M5Tab5 版で分かっているとおり、増えるのは帯の作業 (ラスタライズ) で、それがフレーム時間を決める。
- 640x384 は M5Tab5 版の 640x360 の 1.07 倍で、あちらの実測 (40〜50fps、400MHz) にほぼ乗る。
  こちらは CPU が 360MHz なので、その 1 割減くらいを見込む。
- 800 と 480 を**両方とも整数で割り切り**、かつ PPA の拡大率の刻み (1/16) に乗る比率は
  2 倍 (400x240) と 5/4 倍 (640x384) だけ。400x240 は速いが粗すぎる。
- 5/4 = 20/16 なので PPA の `scale_x_frag` にちょうど乗り、丸めは起きない。PPA が内部で使う
  マクロブロック (16 または 32 画素) も 5/4 倍で 20 / 40 画素と整数になる。
- 拡大はバイリニア (PPA の SRM)。2 倍でないぶん M5Tab5 版よりにじむ。気になるようなら
  400x240 の 2 倍にするしかない (最近傍の拡大は PPA に無い)。

HUD の大きさ: `UiMetrics` の `scale8` は 640x384 でも 640x360 と同じ 9 (`fontMult` 1) なので、
HUD のレイアウトは M5Tab5 版と同じ画素数になる。物理的にはパネルの画素ピッチ (約 0.117mm) x 1.25
= 約 0.146mm がフレームの 1 画素で、M5Tab5 版 (約 0.17mm) より 15% ほど小さく見える。

### 帯

帯の高さは**実行時に決める**。`BAND_H_CHOICES = {48, 32, 24, 16}` を背の高い順に試し、
内部 SRAM に 2 枚分の連続ブロックが取れた最初のものを使う (`PanelOut::init()`、
シリアルに `band rows` と出る)。取れなかった高さについては「合計」と「最大の連続ブロック」の
両方を出す (M5Tab5 版で 60 行が取れなかったのと同じ現象を見分けるため)。

帯の高さの条件 (static_assert で検査):

- **4 の倍数**。5/4 倍した後の帯が整数行になり、どの帯も 5/4 の周期の頭から始まる。
  PPA の補間は帯ごとに始め直すので、周期の途中から始まる帯があると継ぎ目が見える。
- 384 を割り切る。

48 行なら 8 帯、1 枚 61,440 バイト。ビルド時の静的使用量 (`idf.py size`) は DIRAM 249,234 バイトで
残り 192,062 バイト (2026-09-29、アリーナ 32KB・効果音込み。アリーナ 48KB・効果音なしの最初の版は
残り 180,036、M5Tab5 版は M5 のライブラリ込みで残り 167,698)。

アリーナは M5Tab5 版に合わせて **32KB** (ShapoGFX d538138 以降、640x360 での変曲点が 24KB に下がった。
測定は impl/m5tab5/SPEC.md)。フレームが 24 行増えても使う量はプリミティブの数で決まるので変わらない。
帯バッファの添字をフレームを跨いで連続させること、内部 SRAM に置くこと、
投入前に `esp_cache_msync()` することは M5Tab5 版と同じ。

帯 b は拡大後の行 `[v0, v0 + 5/4 * H)` (`v0 = 5/4 * b * H`) で、回転 1 では
パネルの縦帯 `X ∈ [480 - v0 - 5/4 * H, 480 - v0)`、回転 3 では `X ∈ [v0, v0 + 5/4 * H)`、
`Y` は 800 行すべて。

### ティアリング

`num_fbs = 1` なのでティアリングはある (M5Tab5 版と同じ)。DSI の走査はパネルの行 (ランドスケープの列)
に沿い、帯はランドスケープの行を更新するので、境目は縦に走る。
消すなら `num_fbs = 2` にして、PPA は裏のバッファに書き、フレームの終わりに
`esp_lcd_panel_draw_bitmap()` に裏のバッファを渡して垂直帰線で切り替える (DPI ドライバは
自分のフレームバッファを渡されるとコピーせずに切り替える)。PSRAM を 768KB 余分に使う。未実装。

PSRAM の帯域は DSI の走査が 480 x 800 x 2 x 59.9 = 約 46MB/s で、M5Tab5 版 (約 118MB/s) より
ずっと軽い。

## 入力

M5Tab5 版と同じ (ディスク、A、B、右上のポーズ、タイトル / ポーズ画面での上下)。割り当ては README.md。
ディスクは core 3.7 の `sim::Input` を返す**アナログ**で、読み方 (`discAxes()`、不感帯と飽和、
計測オーバーレイの「上」を `sim::directionBits()` で読むこと) は M5Tab5 版の `touch_pad.cpp` と
同じコードなので、理由は impl/m5tab5/SPEC.md の「入力」を見ること。
配置の定数だけ、ブラウザ版の横画面の比率をフレームの高さ 384 に合わせて置き直した:
ディスク半径 80、A 51、B 38、ポーズ 17 (フレームの画素)。ディスクは直径 23mm ほど。

タッチ座標の変換 (`touch_pad.cpp`): GT911 はパネルの縦長の画素 (x ∈ [0, 480)、y ∈ [0, 800)) を返すので、
上の回転の逆で拡大後のランドスケープ (u, v) に戻し、4/5 倍してフレームの画素にする。
- 回転 1: u = y、v = 479 - x
- 回転 3: u = 799 - y、v = x

GT911 の座標系がパネルの画素とそろっている (反転・入れ替えが無い) ことは Kern の BSP の設定
(`swap_xy = mirror_x = mirror_y = 0`) に拠っている。

## ベンチマーク

M5Tab5 版と同じ (タイトル画面で B の円を 3 秒、A で次のページ、B で閉じる。結果はシリアルにも出る)。
プラットフォーム名は `WAVE43`。結果はまだ取っていない。

## 効果音

M5Tab5 版と同じく**多重発音** (最大 `SE_VOICES` = 8 音)。ハンドヘルドの 1 音 + 優先度ではなく、
ブラウザ版と同じ鳴り方。

### 足回り

スピーカーは ES8311 コーデックの先にある。レジスタは I2C (タッチと同じバス)、サンプルは I2S、
パワーアンプは GPIO で入れる。配線とコーデックの設定は **Waveshare のこのボード用 BSP**
([ESP32-P4-WIFI6-Touch-LCD-4.3](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3) の
`esp32_p4_wifi6_touch_lcd_4_3` 1.0.1、`bsp_audio_init()` と `bsp_audio_codec_speaker_init()`) と、
それを使う `06_I2SCodec` の例から持ってきた。同じ `esp_codec_dev` (~1.5) を使う。
Arduino の `09_Audio_Playback` の例ともピンが一致する。Kern の BSP は音声を使わない
(`BSP_CAPS_AUDIO 0`) ので、そちらには情報が無い。

- I2S は**出力だけ**作る。GPIO11 (データ入力) とマイク用の ES7210 はマイクのもので、触らない。
- ES8311 は I2S スレーブ、外部 MCLK (256 x fs)。コーデックを open / close するとアンプの
  GPIO も一緒に上げ下げされる (`pa_pin`)。`hw_gain` は BSP と同じ (アンプ 5V、DAC 3.3V)。
- I2S のポートは 1 (BSP の既定 `CONFIG_BSP_I2S_NUM`)。
- **パックのレート (24kHz) のまま**鳴らすので再サンプリングは無い。モノラル 1 チャンネルで open
  すると esp_codec_dev が I2S をモノラルのスロットに設定し、同じサンプルが両方のスロットに出る。
- IDF 6.1 では `i2s_port_t` が無くなって `int`、`auto_clear` は `auto_clear_after_cb` になっている。

### ミキサ

core1 のタスク (`ds_audio`、優先度 6。sim タスクは 5) が `SE_CHUNK_FRAMES` = 144 フレーム (6ms) ずつ
混ぜて `esp_codec_dev_write()` に渡す。書き込みが I2S の DMA の空きを待ってブロックするので、
それがそのままループの歩調になる。1 チャンクの仕事は数マイクロ秒で、sim から取る時間は無視できる。

- DMA バッファは `SE_DMA_BUFFERS` = 4 枚 x 144 フレーム。音が鳴り始めるまでの遅れは最大で
  約 24ms (ドライバの既定の 6 x 240 だと 60ms)。
- **鳴っていなくても無音を書き続ける**。コーデックとアンプから見て 1 本の途切れない流れになり、
  音の出だしでプツッといわない。遅れたときは `auto_clear_after_cb` で無音が出る (前のバッファを繰り返さない)。
- 足し合わせは 32 ビットで、16 ビットに**飽和**させる (巻き込まない)。8 音同時でもピークが潰れるだけ。
- 音量はコーデックの出力音量 `SE_VOLUME` (0〜100、`esp_codec_dev_set_out_vol()`)。
  ミックス自体は等倍。既定は 75 (Waveshare の例は音楽を 60 で鳴らす)。
- ボイスの表は `request()` / `setMuted()` (sim タスク)、ミキサ、`playing()` / `stop()`
  (core0 のハイスコア保存) が触るので、スピンロックで守る。どれも数マイクロ秒で抜ける。
- 8 音とも塞がっているときの要求は捨てる (M5Tab5 版と同じ。鳴っている 8 音の中では聞こえない)。
- ミュートは鳴っている音を全部止めて、以後の要求を捨てる。

パックは `docs/play/se.bin` (ブラウザ版と同じ。24kHz モノラル s16le、コミット済み)。
`main/CMakeLists.txt` の `target_add_binary_data()` でフラッシュに置き、ミキサがその場で読む。
素材を変えたら `make -C impl/wasm se` で作り直す。

コーデックが起きなかったとき (バスが無い、ES8311 が応答しない、open に失敗) は
「running without sound」とログに出して無音で動く。`request()` は何もしない。

## ハイスコア

M5Tab5 版と同じ (NVS、名前空間 `devoursphere`、キー `highscore`)。

## 状態

- **表示とタッチは実機で動作** (2026-09-23)。`PANEL_ROTATION` は既定の 1 で正しい。
  `band rows`・`panel write us`・フレームレートの実測値はまだ記録していない。
- **効果音も実機で鳴る** (2026-09-29)。音量 `SE_VOLUME` は既定の 75 のまま。
- リリース (`make_release.sh`) と README にはまだ入れていない。
