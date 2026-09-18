# Devour Sphere (WASM 版)

ブラウザ上でコアプログラム (core/) を動かすためのフロントエンド。
ゲームのルールと描画はすべて core/ 側にあり、ここには
「WebAssembly へのビルド」「ブラウザとの結線」「公開用ページ」だけがある。

## 必須要件

- 画面は既定で 480x320px。RGB565BE のフレームバッファを ShapoGFX で描画し、キャンバスに表示する。
  URL に `?screen=WxH` を付けると別の解像度で起動する。
  各辺 64～1280px、総画素数 1280x720 まで。HUD のレイアウトは core/ 側が解像度から
  動的に決めるので (core/SPEC.md の HUD 参照)、縦長でも正方形でも破綻しない。
  受け付けられない値のときは既定の解像度で起動し、画面下のステータスに理由を出す。
- スマホ等の幅の狭いデバイスでもアスペクト比が崩れないようにする。
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
    play.js        ローダ、ゲームループ、入力処理、仮想パッド、スマホ向けレイアウト
    manifest.json  Web アプリマニフェスト (ホーム画面に追加すると全画面で起動)
    icon-*.png     アイコン
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
| `ds_set_screen(w, h)` | フレームバッファの大きさを決める。`ds_init()` より前に呼ぶ。受け付けたら 1、範囲外なら 0 を返し、その場合は直前の大きさのままになる |
| `ds_get_width()`, `ds_get_height()` | 現在のフレームバッファの大きさ (既定 480, 320) |
| `ds_get_max_pixels()` | フレームバッファが保持できる最大画素数 (1280x720) |
| `ds_get_fb()` | RGB565BE フレームバッファ (width x height x 2 バイト) の先頭アドレス |
| `ds_get_tick_rate()` | 1 秒あたりのシミュレーション tick 数 (60) |
| `ds_tick(buttons)` | 1 tick 進める。`buttons` は sim::Button のビット (LEFT=1, RIGHT=2, UP=4, DOWN=8, A=16) |
| `ds_render(dt)` | 現在の状態をフレームバッファに描画する。`dt` は前回描画からの秒数 (カメラの補間のみに使う) |
| `ds_get_state()` | GameState (0 TITLE, 1 WEAPON_SELECT, 2 PLAYING, 3 LAUNCH, 4 DEAD, 5 ARRIVE) |
| `ds_get_score()` | 現在のスコア |
| `ds_set_high_score(v)` | ハイスコアを渡す (表示用)。JS 側が localStorage の `devoursphere.highscore` に保持し、毎秒スコアと比べて更新する |
| `ds_debug_start(level, weapon)` | デバッグ用: メニューを飛ばして指定レベルのスフィアで開始 |
| `ds_debug_auto(on)` | デバッグ用: AI にプレイヤーを操作させる |

フレームバッファ (最大画素数分) と 3D レンダラのアリーナ (256KB) は main.cpp の静的配列。
動的確保はしないので、フレームバッファは選べる最大の大きさで常に確保される (約 1.8MB)。
アリーナは解像度によらず 256KB で足りる (解像度に比例するのは 3D 側の行バケットだけで、
720 行でも 3KB 程度)。

## ゲームループ (play.js)

- `requestAnimationFrame` で回し、経過時間をアキュムレータに足して
  1/60 秒ごとに `ds_tick()` を呼ぶ。1 フレームに最大 4 tick まで追いつき、
  それ以上遅れている場合は残りを捨てる (タブが隠れていた後など)。
- tick が 1 回以上進んだフレームだけ `ds_render()` と転送を行う。
  60Hz のディスプレイでは毎フレーム 1 tick 進むので描画も 60fps で、シミュレーションと描画は 1:1。
- 転送は 65536 要素の変換表 (RGB565BE 1 画素 → RGBA8888 の 32 ビット語) を使い、
  フレームバッファを `Uint16Array`、`ImageData` を `Uint32Array` として 1 画素 1 回の
  参照と書き込みで埋めて `putImageData()` する。表は WASM のメモリ上のバイト順 (BE) を
  そのまま添字にできるように作ってあるので、画素ごとのバイト入れ替えは要らない。
  高解像度では画素あたりの手数がそのまま効くため、チャンネルごとに展開すると間に合わない。
- 入力はキーボード、ゲームパッド、仮想パッドの OR を tick ごとに渡す。

URL パラメータ (デバッグ用):

| パラメータ | 内容 |
|---|---|
| `?screen=WxH` | フレームバッファの大きさ (既定 480x320) |
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

- `#game` はキャンバスを包む要素で、`width: min(var(--game-max), 100%)` と
  `aspect-ratio: var(--aspect)` を指定する。`--aspect` (比)、`--aspect-num` (数値、
  全画面時の計算用)、`--game-max` (実解像度の 2 倍か 960px の大きい方) は play.js が
  起動時に実際のフレームバッファの大きさから書き込む。幅の狭い画面では幅いっぱいに
  縮小され、高さは比率から決まるのでアスペクト比は崩れない。キャンバス自体は
  フレームバッファと同じ解像度で、`image-rendering: pixelated` で拡大する。
- `#stage` (画面 + 仮想パッド) を全画面化の対象にする。全画面時はパッドの高さ分を
  引いた高さに収まるように画面幅を計算する。
- サイト共通のスタイル (配色、フォント、リンク、ボタン、ステータス表示) は docs/style.css、
  ゲームページ固有のレイアウト (画面、パッド、全画面) は play/index.html 内の `<style>` に置く。
- `touch-action: none` と `user-select: none` をキャンバスとパッドに指定し、
  スクロールや長押し選択がゲーム操作を妨げないようにする。

## スマホ向けレイアウト

`(pointer: coarse)` かつ短辺が 700px 未満のデバイスでは、play.js が `body.mobile` を付けてページ全体を
ゲーム機の筐体のように使う。

- 縦画面: 画面上部にキャンバス (幅いっぱい、3:2)、その下に仮想パッド。
- 横画面 (`@media (orientation: landscape)`): キャンバスを表示領域の高さ (または幅) いっぱいに広げ、
  方向ディスクと A ボタンを半透明 (不透明度 0.55) でキャンバスの左下・右下に重ねる。
  パッド以外の領域は `pointer-events: none` なのでキャンバスへのタッチは無視される。
- 向きの切り替えは CSS のメディアクエリで追従する。高さは `100dvh` でアドレスバーを除いた
  表示領域に合わせ、`env(safe-area-inset-*)` でノッチを避ける。ページのスクロールは止める。
- ブラウザは操作なしの全画面化を許可しないので、起動時に「TAP TO START」のオーバーレイを出し、
  最初のタップで `#stage` を全画面にする (Android)。iPhone の Safari は要素の全画面 API を持たないため
  表示領域いっぱいのレイアウトのままになる。`manifest.json` (`display: fullscreen`) と
  `apple-mobile-web-app-capable` により、ホーム画面に追加すると本当の全画面で起動でき、
  その場合はオーバーレイを出さない。アイコンは icon-192.png / icon-512.png。
- 見出し、ツールバー、説明、フッターはスマホ向けレイアウトでは隠す。

## Xiamocon 版との関係

このフロントエンドは「フレームバッファ 1 枚を確保して、フレーム全体を 1 つの帯として描く」
形になっている。HUD 以外の描画はすべて ShapoGFX の 3D シーン (線・点を含む) なので、
core/render の `Renderer::renderBand()` は任意の行範囲を任意のサーフェスに描ける。
Xiamocon 版 (impl/xiamocon/) ではフレームバッファを持たずに 2 本の帯バッファを交互に使い、
帯単位で描画してディスプレイへ DMA 転送する構成に置き換えている。
シミュレーションは固定小数点で決定的なので、どちらの版でも同じ入力列から同じ結果になる。
