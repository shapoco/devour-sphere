# Devour Sphere (WASM 版)

ブラウザ上でコアプログラム (core/) を動かすためのフロントエンド。
ゲームのルールと描画はすべて core/ 側にあり、ここには
「WebAssembly へのビルド」「ブラウザとの結線」「公開用ページ」だけがある。

## 必須要件

- 画面は 480x320px。RGB565BE のフレームバッファを ShapoGFX で描画し、キャンバスに表示する。
- スマホ等の幅の狭いデバイスでもアスペクト比 (3:2) が崩れないようにする。
- スマホでは仮想のゲームパッドを画面に表示する。
- 公開用 HTML は docs/play/ に配置する (docs/ を GitHub Pages 等で静的配信する)。
- サイト全体に共通するスタイルは docs/style.css に記述する。

## ファイル構成

```
impl/wasm/
  SPEC.md          この文書
  main.cpp         C API (WASM エクスポート) とネイティブ確認用の main()
  Makefile         Emscripten ビルド (docs/play/devoursphere.wasm を生成)
  CMakeLists.txt   ネイティブビルド (1 フレームを PPM に書き出す)
docs/
  style.css        サイト共通スタイル
  index.html       トップページ (play/ へのリンク)
  play/
    index.html     ゲームページ (ページ固有のスタイルを含む)
    play.js        ローダ、ゲームループ、入力処理、仮想パッド
    devoursphere.wasm  ビルド成果物 (静的配信のためコミットする)
```

## ビルド

```sh
cd impl/wasm
make            # emcc が必要。docs/play/devoursphere.wasm を生成する
make serve      # docs/ を http://localhost:52980/ で配信 (fetch は file:// では動かない)
```

- Emscripten の STANDALONE_WASM モードでビルドする。JS グルーコードは生成せず、
  wasi_snapshot_preview1 のスタブを渡して `WebAssembly.instantiate()` で直接読み込む。
- ソースは core/src/sim, core/src/render, ShapoGFX の src/gfx2d, src/gfx3d と main.cpp を
  そのままコンパイルする (ライブラリは動的確保をしないので特別なランタイム設定は不要)。
- `-sALLOW_MEMORY_GROWTH=1` を指定しているため、JS 側は毎フレーム
  `ex.memory.buffer` からフレームバッファのビューを作り直す。

ネイティブ版は CMake のトップレベルからビルドされる:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/impl/wasm/devoursphere_native [level] [script] [out.ppm] [auto] [seed]
```

`level` が 0 ならタイトル画面、1 以上ならそのレベルのスフィアで開始する。
`script` は「tick 数 x ボタンビット」をコンマで並べた入力列 (例: `5x0,1x16,300x2`。
数値だけなら入力なしの tick 数)、`auto` を 1 にすると AI がプレイヤーを操作する。
入力列を実行して 1 フレームを PPM に書き出し、tick と描画の所要時間、
描画統計 (線分数、三角形数、アリーナ使用量) を表示する。ブラウザなしで見た目と
負荷を確認するためのもの。

## C API (WASM エクスポート)

| 関数 | 内容 |
|---|---|
| `ds_init(seed)` | ゲームとレンダラを初期化しタイトル画面にする |
| `ds_get_width()`, `ds_get_height()` | 480, 320 |
| `ds_get_fb()` | RGB565BE フレームバッファ (width x height x 2 バイト) の先頭アドレス |
| `ds_get_tick_rate()` | 1 秒あたりのシミュレーション tick 数 (30) |
| `ds_tick(buttons)` | 1 tick 進める。`buttons` は sim::Button のビット (LEFT=1, RIGHT=2, UP=4, DOWN=8, A=16) |
| `ds_render(dt)` | 現在の状態をフレームバッファに描画する。`dt` は前回描画からの秒数 (カメラの補間のみに使う) |
| `ds_get_state()` | GameState (0 TITLE, 1 WEAPON_SELECT, 2 PLAYING, 3 LAUNCH, 4 DEAD) |
| `ds_debug_start(level, weapon)` | デバッグ用: メニューを飛ばして指定レベルのスフィアで開始 |
| `ds_debug_auto(on)` | デバッグ用: AI にプレイヤーを操作させる |

フレームバッファと 3D レンダラのアリーナ (192KB) は main.cpp の静的配列。

## ゲームループ (play.js)

- `requestAnimationFrame` で回し、経過時間をアキュムレータに足して
  1/30 秒ごとに `ds_tick()` を呼ぶ。1 フレームに最大 4 tick まで追いつき、
  それ以上遅れている場合は残りを捨てる (タブが隠れていた後など)。
- tick が 1 回以上進んだフレームだけ `ds_render()` と転送を行う。
  つまり描画も 30fps で、シミュレーションと描画は 1:1。
- 転送は RGB565BE の 2 バイトを 8 ビット RGBA に展開して `putImageData()` する。
  変換テーブル (5 ビット/6 ビット → 8 ビット) を使う。
- 入力はキーボード、ゲームパッド、仮想パッドの OR を tick ごとに渡す。

URL パラメータ (デバッグ用):

| パラメータ | 内容 |
|---|---|
| `?level=N&weapon=W` | メニューを飛ばしてレベル N、武器 W (0 バルカン, 1 レーザー, 2 ミサイル) で開始 |
| `?seed=N` | 乱数シードを固定 |
| `?auto=1` | AI がプレイヤーを操作する (デモ) |

## 入力

| 操作 | キーボード | ゲームパッド | 仮想パッド |
|---|---|---|---|
| 左右旋回 | ← → / A D | 左スティック X、十字キー | 方向ディスクの左右 |
| ダッシュ | ↑ / W | 左スティック上、十字キー上 | 方向ディスクの上 |
| ブレーキ | ↓ / S | 左スティック下、十字キー下 | 方向ディスクの下 |
| A (攻撃・決定) | スペース / I J K L / Enter | ボタン 0～3, 7 | A ボタン |

- キーは `KeyboardEvent.code` で判定し、ゲームに使うキーは `preventDefault()` する
  (スペースや矢印でページがスクロールしない)。ウィンドウがフォーカスを失ったら全キーを離す。
- ゲームパッドは `navigator.getGamepads()` を tick ごとにポーリングする (標準マッピング前提)。
- 仮想パッドは十字キーではなく「方向ディスク」にしている。タッチ位置と中心との
  オフセットから左右と上下を独立に判定するので、斜め入力 (ダッシュしながら旋回、
  ブレーキしながらクイックターン) ができる。Pointer Events を使い、ディスクと
  A ボタンでそれぞれ別のポインタを捕捉するのでマルチタッチで同時に操作できる。
- 仮想パッドは `(pointer: coarse)` または `ontouchstart` があるデバイスで自動表示し、
  「タッチ操作」ボタンで手動でも切り替えられる。

## ページ構成とレスポンシブ対応

- `#game` はキャンバスを包む要素で、`width: min(960px, 100%)` と `aspect-ratio: 480 / 320` を
  指定する。幅の狭い画面では幅いっぱいに縮小され、高さは比率から決まるので
  アスペクト比は崩れない。キャンバス自体は 480x320 の固定解像度で、
  `image-rendering: pixelated` で拡大する。
- `#stage` (画面 + 仮想パッド) を全画面化の対象にする。全画面時はパッドの高さ分を
  引いた高さに収まるように画面幅を計算する。
- サイト共通のスタイル (配色、フォント、リンク、ボタン、ステータス表示) は docs/style.css、
  ゲームページ固有のレイアウト (画面、パッド、全画面) は play/index.html 内の `<style>` に置く。
- `touch-action: none` と `user-select: none` をキャンバスとパッドに指定し、
  スクロールや長押し選択がゲーム操作を妨げないようにする。

## RP2350 版との関係

このフロントエンドは「フレームバッファ 1 枚を確保して、フレーム全体を 1 つの帯として描く」
形になっている。core/render の `Renderer::renderBand()` は任意の行範囲を任意の
サーフェスに描けるので、RP2350 版ではフレームバッファを持たずに
帯単位で描画してディスプレイへ転送する構成に置き換える。
シミュレーションは固定小数点で決定的なので、どちらの版でも同じ入力列から同じ結果になる。
