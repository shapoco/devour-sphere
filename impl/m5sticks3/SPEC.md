# Devour Sphere (M5StickS3 版)

[M5StickS3](https://docs.m5stack.com/en/core/M5StickS3) (ESP32-S3-PICO-1-N8R8、
1.14 インチ 135x240 の ST7789、BMI270、ES8311 + 1W スピーカー、キー 2 個) で core/ を
動かすためのファームウェア。ゲームのルールと描画はすべて core/ 側にあり、
ここには「実機へのビルド」「表示への転送」「傾きの読み取り」「音」だけがある。

M5Unified / M5GFX は**足回りだけ**を借りる (PMIC とパネルの起動、SPI バス、BMI270、
ES8311、ボタン)。描画は core/render の `Renderer::renderBand()` + ShapoGFX で行い、
M5GFX の描画 API は起動画面でしか使わない。M5Tab5 版と同じ取り決め。

## 必須要件

- 画面は **135x240 の縦長パネルを横倒しにして 240x135** で使う。回転は ST7789 の
  MADCTL (`M5GFX::setRotation()`) が行うので、**ソフトウェアでの回転もバイト入れ替えも無い**。
- フレームバッファを 1 枚も持たず、45 行の帯を 2 枚交互に使って描画・転送する。
- **どちら向きに倒して持つかは起動時に決める**。倒れた向きを重力から判別し、
  静止した姿勢を基準姿勢として core を起動する (「起動時の姿勢決定」)。
- 操作は**デバイスを傾けて方向キー**、KEY1 が射撃、KEY2 が緊急回避、**振るとポーズ**。
- シミュレーションは 60Hz 固定、描画は追いつける範囲で行う (可変フレームレート)。
- 効果音は M5Unified のスピーカーで多重発音する (M5Tab5 版と同じ)。
- ハイスコアは NVS に保存する。
- プラットフォームは **ESP-IDF v5.5.x** (Arduino / PlatformIO ではない)。

## ファイル構成

```
impl/m5sticks3/
  SPEC.md              この文書
  devoursphere/        ESP-IDF プロジェクト
    CMakeLists.txt     トップレベル
    sdkconfig.defaults ボード固有の設定 (後述)
    partitions.csv     nvs / phy_init / factory 4MB
    build.sh           DS_IDF_PATH を読んで idf.py build
    run.sh             同 build + flash
    monitor.sh         同 monitor
    build_release.sh   bootloader + パーティション表 + app をまとめた 1 枚の .bin
    components/
      devoursphere_core/CMakeLists.txt   core/ を IDF コンポーネントとして登録
      shapogfx/CMakeLists.txt            submodule/shapo-gfx/ を同上
    main/
      CMakeLists.txt   main コンポーネント + 効果音パックの埋め込み
      idf_component.yml  m5gfx / m5unified の依存
      ds_config.hpp    解像度、帯、アリーナ、音量などの定数
      ds_platform.hpp  乱数・NVS・スタック計測・メモリの宣言 (他の版と同名)
      ds_platform.cpp  その実装
      panel_out.hpp/.cpp  帯の ping-pong と SPI 転送
      attitude.hpp/.cpp   BMI270 の読み取り、起動時の向き判別、傾き → キー
      se_player_s3.cpp    効果音 (M5Unified のスピーカー、多重発音)
      app_main.cpp     エントリ、静的領域、起動画面、フレームループ、コア間の受け渡し
```

Xiamocon 版と共有しているもの (そちらの `include/` を include パスに入れている):

- `profiler.cpp` / `profiler.hpp` — 計測オーバーレイ。`<ds_config.hpp>` / `<ds_platform.hpp>` を
  山括弧で include するので、こちらのものが拾われる。
- `high_score_store.hpp` — ハイスコアをいつ書くか。
- `se_player.hpp` — `audio::` のインタフェース。実装だけこちらのもの。

## ビルドと書き込み

```sh
cd impl/m5sticks3/devoursphere
./build.sh                       # DS_IDF_PATH 既定 ${HOME}/esp/5.5
./run.sh /dev/ttyACM0            # ビルドして書き込み (ポート省略可)
./monitor.sh                     # シリアルログ
```

- **ESP-IDF v5.5.x**。M5GFX / M5Unified の ESP-IDF ビルドが検証されているのが 5.5 系まで。
- `managed_components/` と `sdkconfig` は生成物なので git 管理外。

### sdkconfig.defaults

| 設定 | 理由 |
|---|---|
| `SPIRAM` + `SPIRAM_MODE_OCT` + `SPIRAM_SPEED_80M` | N8R8 のオクタル PSRAM。`sim::Game` (84KB) をここに置く |
| `COMPILER_OPTIMIZATION_PERF` | フレーム時間は ShapoGFX のスパンループと M5GFX の SPI に消えるので、自分の翻訳単位だけ -O2 にしても効かない |
| `ESP_DEFAULT_CPU_FREQ_MHZ_240` | |
| `ESP_CONSOLE_USB_SERIAL_JTAG` | USB-C しか出ていない |
| `ESP_MAIN_TASK_STACK_SIZE=16384` | `app_main` のタスクがシーン構築とラスタライズをする。`buildSphere()` は 7 段再帰する |
| `ESP_TASK_WDT_INIT=n` | 2 つのコアがそれぞれ専有して回るので、アイドルタスクが回らないコアで発火してしまう |

## 画面: 3 つの移植の中で一番単純な経路

```
ゲームが描くもの  240x135 のランドスケープ、RGB565BE
パネル            135x240 の縦長 ST7789、SPI3 40MHz、オフセット x=52 / y=40
```

`M5GFX::setRotation(1 or 3)` が MADCTL を書き、パネルの論理的な幅と高さを入れ替える。
**以後パネルは 240x135 そのもの**で、帯はランドスケープの行の並びのまま出せる。
さらに ShapoGFX の **RGB565BE はそのまま ST7789 が求めるバイト順**であり、
M5GFX の `swap565_t` と同じ並びなので、`writePixelsDMA(..., swap = false)` が
`Panel_LCD::writePixels()` の `no_convert` 経路 → `Bus_SPI::writeBytes(..., use_dma)` に入る。
**画素をなめ直す処理はどこにも無い** (M5Tab5 版が PPA を要したのは、
縦長の DSI フレームバッファに書き込む必要があったため)。

帯は 45 行 x 3 本。135 = 27x5 なので割り切れるのは 45 か 27 で、
帯 1 枚 21.6KB・2 枚で 43KB を内蔵 DMA 可能ヒープから取る。

転送の開始と終了は `startWrite()` を `PanelOut::init()` で 1 回だけ呼び、**閉じない**。
バスはこのパネル専用 (M5GFX が `bus_shared = false` で構成する) であり、
`endWrite()` は飛行中の転送を待ってしまう ── その転送こそフレームをまたいで
走らせたいものなので、閉じては意味が無い。

## 起動時の姿勢決定

`attitude.cpp` の座標系は**画面基準**で、センサのものではない:

```
x = 画面右、y = 画面下、z = 画面の奥 (x × y = z の右手系)
```

デバイスを縦に持って画面を見ているとき、重力は +y。

1. **向きの判別**。スティックを横倒しにすると、重力は画面の x 軸に乗る。
   時計回りに倒す (天面が右に来る) とデバイスの +x が下を向くので重力は +x、
   反対向きなら −x。`|x| < sin 20°` の間は判別できないので、
   縦のまま「TIP THE STICK ONTO ITS SIDE」を出して待つ。
2. **静止判定**。`||a|| − 1G` が 0.12G 以内の状態が 0.5 秒続いたら確定。
3. その姿勢を**基準姿勢**として保存し、`setRotation()` で横向きにして core を起動する。
   ディスプレイの初期化のやり直しは不要 (MADCTL を書き直すだけ)。

起動画面は**そのときの重力を中央から伸びる線で描く**。これは
`attitude.cpp` の `SRC_* / SIGN_*` (BMI270 の実装軸 → 画面軸の対応) が
正しいかどうかを一目で確かめるためのもので、**この線は常に床を指していなければならない**。
M5Unified はこのボードに対して軸の補正を持っていないので、この対応は実機でしか決まらない。

## 傾きを方向キーにする

基準姿勢の重力 `g0` と現在の重力 `g` の**外積 1 回**で 2 軸が同時に出る。
`c = g0 × g` はデバイスを回した軸を向き、長さは回した角の sin になる
(角度は一度も求めない。閾値の sin と比べるだけ)。

| 成分 | 回した軸 | キー |
|---|---|---|
| `c·z` | 視線軸 (画面の奥) | 左に倒すと LEFT、右に倒すと RIGHT |
| `c·R` | 画面の左右軸 | 奥に倒すと UP (ダッシュ)、手前に倒すと DOWN (ブレーキ) |

`z` は倒した向きによらず同じ物理軸なので、左右の符号は**どちら向きに倒しても同じ**。
一方 `R` (ランドスケープの右方向) は縦画面の ∓y なので、そこだけ `confirm()` が
決めた符号が掛かる。画面の上下軸まわりの回転は重力では観測できない (ヨー) が、
その軸は使わないので問題にならない。

- 閾値は 15 度で入り、11 度で抜ける (ヒステリシス)。
- 加速度は時定数 60ms の 1 次 IIR。係数は dt から毎回求めるので、
  フレームレートが変わっても操作感は変わらない。
- **基準姿勢はゆっくり追従する** (方向キーが 1 つも出ていない間だけ、毎秒 1 度)。
  意図した傾けはキーを出すので追従は止まり、持ち方が変わった分だけが吸収される。

## 振ってポーズ

`| ||a|| − 1G |` (同じ IIR) が 0.70G を超えたら「振っている」、0.30G を下回る状態が
0.25 秒続いたら終わり。開始のエッジで `Button::PAUSE` を 1 フレームだけ立てる
(core はエッジで受けるので押しっぱなしにしない)。

振っている間と、その後 0.30 秒は**方向キーを全て落とす**。
そうしないと、振り終わってスティックが落ち着いた姿勢がそのまま旋回入力になる。

## メニューでの入力の抑止

core はタイトル画面の `DOWN` をミュート切替に使っている。傾けて操作する以上、
少し傾けて持っているだけでこれが働いてしまうので、**タイトル / 武器選択 / ゲームオーバー /
ポーズ中は上下の傾きを捨てる** (`app_main.cpp` の `readInput()`)。左右は武器の選択に要るので残す。

計測オーバーレイは他の版では `UP` だが、ここでは上下が傾きなので
**KEY1 + KEY2 同時押し**にしてある (タイトルかポーズ中のみ)。

## core への変更

この移植のために core を 2 つ変えた。どちらも既存のターゲットの絵は 1 画素も変えない
(240x240 / 320x240 / 480x320 / 640x360 で確認済み)。

- **エンティティの詳細度の閾値を画面の高さでスケール** (`Renderer::lod()`、core/SPEC.md)。
  投影半径は画面の高さに比例するので、240x135 では 240x240 の 0.5625 倍になり、
  目の前の敵まで簡略表示 (菱形の輪郭) に落ちていた。
- **`DEVOURSPHERE_CAMERA_ROLL=0`**。デバイスを傾けて操作する以上、画面は既に
  手の中で傾いている。そこにカメラのロールを足すと傾きが二重になる。
  機体の `bank` は sim のものなので、傾き方は他の版と同じ。

加えて `DEVOURSPHERE_MAX_WIRE=440`。135 行では `UiMetrics::wireLines` が最大 412 本しか
要求しないので、既定の 1100 本 (1 本 10 バイト) は Renderer の中で 11KB を遊ばせることになる。

## メモリ配分

| 用途 | 置き場所 |
|---|---|
| `sim::Game` (84KB) | **PSRAM** (`MALLOC_CAP_SPIRAM`、`allocGame()` が placement new) |
| 3D アリーナ (64KB) | `.bss` |
| `render::Renderer` (約 30KB) | `.bss` |
| 帯バッファ x2 (240x45、43KB) | **内蔵 DMA ヒープ** (`MALLOC_CAP_DMA \| MALLOC_CAP_INTERNAL`) |

`idf.py size` で静的 DIRAM 175KB / ヒープに残り 167KB。帯 43KB を引いても 120KB 以上残る。
PSRAM が 8MB あるので、Xiamocon の ESP32S3 版のように内蔵 RAM を取り合う必要は無い。

## 未確定 (実機で決めること)

実機に触れていない段階で書いたので、次の 4 つは**最初の 1 回で確かめる**。
どれも直す場所は 1 箇所しかない。

1. **BMI270 の軸の対応** (`attitude.cpp` の `SRC_* / SIGN_*`)。起動画面の線が床を指すか。
2. **`ROTATION_TOP_RIGHT`** (`ds_config.hpp`、既定 1)。倒したときに絵が上下逆なら 3 にする。
   もう一方の向きも同時に入れ替わる。
3. **KEY2 のピン**。M5Unified 0.2.18 は BtnA=GPIO11 / **BtnB=GPIO12** として読む。
   製品資料は KEY2 を GPIO42 としているので、`M5.BtnB` が反応しなければ GPIO42 を直読みする。
4. **`TICK_RATE`**。60 で始めている。240x135 は Xiamocon の ESP32S3 版 (240x240 で 30Hz、
   core0 が 28.5ms) の 56% の画素数なので届くはずだが、オーバーレイの CPU 行が
   16.6ms を超えるようなら 30 に落とす。

その他、実機で見て決めるもの: `SE_MASTER_VOLUME` (既定 64。M5Unified は音量を 2 乗する)、
傾きの閾値 15 度、振りの閾値 0.70G、帯 45 行 x 3 (オーバーレイの CMD 行が高ければ 27 行 x 5)。
SPI は M5GFX が 40MHz で構成する (全画面 13ms)。足りなければバスを再構成して 80MHz を試す余地がある。
