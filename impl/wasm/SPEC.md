# Devour Sphere (WASM 版)

ブラウザ上でコアプログラム (core/) を動かすためのフロントエンド。
ゲームのルールと描画はすべて core/ 側にあり、ここには
「WebAssembly へのビルド」「ブラウザとの結線」「公開用ページ」だけがある。

## 必須要件

- 画面は既定で 480x320px。RGB565_SWAPPED のフレームバッファを ShapoGFX で描画し、キャンバスに表示する。
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
  Makefile         Emscripten ビルド (docs/play/devoursphere.wasm と se.bin を生成)
  CMakeLists.txt   ネイティブビルド (1 フレームを PPM に書き出す)
core/tools/pack_se.py  assets/se/*.wav を docs/play/se.bin に詰める (ffmpeg が必要。PicoSystem 版と共用)
assets/se/      効果音の素材 (wav)。出典は assets/se/README.md
docs/
  style.css        サイト共通スタイル
  index.html       トップページ (play/ へのリンク)
  favicon.ico      favicon (16/32/48px。play/icon-512.png から生成、両ページ共通)
  image/
    ogp_image_v2.png  OGP 画像 (958x538。両ページ共通)
  play/
    index.html     ゲームページ (ページ固有のスタイルを含む)
    play.js        ローダ、ゲームループ、入力処理、仮想パッド、スマホ向けレイアウト
    manifest.json  Web アプリマニフェスト (ホーム画面に追加すると全画面で起動)
    icon-*.png     アイコン
    devoursphere.wasm  ビルド成果物 (静的配信のためコミットする)
    se.bin         効果音のパック (同上)
```

## ビルド

```sh
cd impl/wasm
make            # emcc と ffmpeg が必要。docs/play/devoursphere.wasm と se.bin を生成する
make se         # 効果音のパック (docs/play/se.bin) だけ作り直す
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
描画統計 (線分数、三角形数、アリーナ使用量)、入力列の間に要求された効果音の種類ごとの回数を
表示する。ブラウザなしで見た目と負荷を確認するためのもの。

## C API (WASM エクスポート)

| 関数 | 内容 |
|---|---|
| `ds_init(seed)` | ゲームとレンダラを初期化しタイトル画面にする |
| `ds_set_screen(w, h)` | フレームバッファの大きさを決める。`ds_init()` より前に呼ぶ。受け付けたら 1、範囲外なら 0 を返し、その場合は直前の大きさのままになる |
| `ds_get_width()`, `ds_get_height()` | 現在のフレームバッファの大きさ (既定 480, 320) |
| `ds_get_max_pixels()` | フレームバッファが保持できる最大画素数 (1280x720) |
| `ds_get_fb()` | RGB565_SWAPPED フレームバッファ (width x height x 2 バイト) の先頭アドレス |
| `ds_get_tick_rate()` | 1 秒あたりのシミュレーション tick 数 (60) |
| `ds_tick(buttons, x, y)` | 1 tick 進める。`buttons` は sim::Button のビット (A=16, PAUSE=32, B=64。方向ビット LEFT=1, RIGHT=2, UP=4, DOWN=8 はその軸の最大として扱う)。`x` / `y` はアナログの方向 -127〜127 (`x` 右が正、`y` 下 = ブレーキが正、上 = ダッシュが負。範囲外は丸める)。軸ごとに方向ビットと絶対値の大きい方を使う (core 3.7 の `sim::Input`) |
| `ds_render(dt)` | 現在の状態をフレームバッファに描画する。`dt` は前回描画からの秒数 (カメラの補間のみに使う) |
| `ds_get_state()` | GameState (0 TITLE, 1 WEAPON_SELECT, 2 PLAYING, 3 LAUNCH, 4 DEAD, 5 ARRIVE) |
| `ds_get_sounds()` | 最後の tick が要求した効果音のビットマスク (sim::SoundKind の順)。tick ごとにクリアされるので `ds_tick()` の直後に読む。ミュート中は 0 |
| `ds_set_muted(on)`, `ds_get_muted()` | ミュート (ゲームの設定。タイトル / ポーズ画面の ↓ でも切り替わる)。JS 側が localStorage の `devoursphere.sound` ('0' でオフ) に保持し、毎秒読んで変化を保存する |
| `ds_get_paused()` | ポーズ中なら 1 |
| `ds_get_score()` | 現在のスコア |
| `ds_set_high_score(v, sphere)` | 起動時に保存してあったハイスコアとそのとき到達していたスフィアを渡す (表示用)。JS 側が localStorage の `devoursphere.highscore` に JSON `{major, score, sphere}` で保持する。読み出し時に `major` が `ds_get_version_major()` と違う記録 (バージョンの無い古い数値も) は破棄する (core/SPEC.md の「バージョン」) |
| `ds_keep_high_score()` | ゲームがスコアをハイスコアとして取り込んだら 1 (`Game::keepHighScore()`)。タイトルデモの得点は取り込まれない。JS 側は毎秒呼び、1 のときだけ `ds_get_high_score()` と `ds_get_high_score_sphere()` を localStorage に保存する |
| `ds_get_sphere_level()`, `ds_get_version_major()` | 現在のスフィアのレベルと core のメジャーバージョン (上の記録用) |
| `ds_debug_start(level, weapon)` | デバッグ用: メニューを飛ばして指定レベルのスフィアで開始 |
| `ds_set_debug(on)` | デバッグモード: HUD に「DEBUG MODE」が出続け、`ds_debug_key` が効くようになる |
| `ds_debug_key(n)` | デバッグモードのチート。1 Shield / 2 Overdrive / 3 Thruster / 4 Extra Core を 1 つ取る、5 自機の大きさ 2 倍、6 半分、7 体力 -25%、8 体力 +25% (最大値比) |
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
- 転送は 65536 要素の変換表 (RGB565_SWAPPED 1 画素 → RGBA8888 の 32 ビット語) を使い、
  フレームバッファを `Uint16Array`、`ImageData` を `Uint32Array` として 1 画素 1 回の
  参照と書き込みで埋めて `putImageData()` する。表は WASM のメモリ上のバイト順 (BE) を
  そのまま添字にできるように作ってあるので、画素ごとのバイト入れ替えは要らない。
  高解像度では画素あたりの手数がそのまま効くため、チャンネルごとに展開すると間に合わない。
- 入力はキーボード、ゲームパッド、仮想パッドを合わせて tick ごとに渡す。ボタンは OR、
  方向は軸ごとに 3 つのうち絶対値が最大のもの (キーボードと十字キーは ±127)。
- tick ごとに `ds_get_sounds()` を読み、立っているビットの音を鳴らす (下記)。

## 効果音

core/SPEC.md の「効果音」のとおり、sim は tick ごとに「鳴らす音」のビットを出すだけで、
波形と再生はこちらが持つ。

- 波形は docs/play/se.bin 1 本にまとめる。core/tools/pack_se.py が assets/se/*.wav を
  ffmpeg でモノラル 24 kHz 16 bit (全プラットフォーム共通のレート。ESP32S3 版がこのファイルを
  そのまま使う) に変換し、両端の無音 (-50 dB 以下、末尾は 20 ms 残す) を
  切って連結する。形式は先頭に "DSSE"、サンプルレート、個数、総サンプル数、
  各音の (先頭サンプル, サンプル数) の表、続けて s16le の PCM (すべてリトルエンディアン)。
  並びは `sim::SoundKind` の順 (pack_se.py の `SOUNDS` と play.js の `SE_NAMES`)。
  素材のまま 12 個で 2.5 MB あるものが 340 KB ほどになる。
- play.js の `SoundPlayer` は se.bin を fetch して PCM から直接 `AudioBuffer` を作る
  (デコーダを通さないので Safari でも形式の心配がない)。再生要求ごとに `AudioBufferSourceNode`
  を作るので、同時発音数の制限はなく、バルカンの連射や撃破と破片の取得が重なっても鳴る。
- ブラウザは `AudioContext` をユーザー操作の中でしか開始させない。最初の keydown /
  pointerdown / touchend (window のキャプチャ段階) で作って `resume()` し、それまでの要求は捨てる。
  タイトルで最初に押した A の音は、その keydown で開始した直後の tick で要求されるので鳴る。
  スマホの TAP TO START のタップも同じ経路で開始する。
- 種類ごとのゲイン表 `SE_GAIN` (play.js) で音量を揃える。素材の平均音量は撃破・アップグレード・
  決定の組 (-23 dB 台) と発射・被弾・選択の組 (-29〜-34 dB) で 10 dB 近く違うが、
  まずは素材のまま (すべて 1) で入れ、遊びながらここで詰める。
- ミュートはゲーム側の設定 (`ds_set_muted` / `ds_get_muted`、core/SPEC.md「ポーズとミュート」) で、
  ミュート中は `ds_get_sounds()` が 0 を返す。ツールバーの「サウンド ON / OFF」とタイトル /
  ポーズ画面の ↓ のどちらでも切り替わり、JS は毎秒 `ds_get_muted()` を見て localStorage の
  `devoursphere.sound` ('0' でオフ) に保存し、起動時に戻す。スマホ向けレイアウトでは
  ツールバーが隠れるが、仮想パッドの ↓ で切り替えられる。
- se.bin が読めなくてもゲームは動く (コンソールに警告を出して無音)。

### 素材

効果音の素材は「効果音ラボ」(https://soundeffect-lab.info/) で配布されているものを使っている。
規約上、ゲームに組み込んで配布すること (音源ファイルがむき出しでも、GitHub で公開することも)、
形式変換や切り詰めなどの改変は許可されており、クレジット表記は任意。
禁止されているのは素材 (改変したものを含む) を素材として再配布すること。
「できる限り音源ファイルを隠す措置を」と依頼されているので、公開ページには個々の wav ではなく
パック 1 本だけを置く。詳細は assets/se/README.md。

URL パラメータ (デバッグ用):

| パラメータ | 内容 |
|---|---|
| `?screen=WxH` | フレームバッファの大きさ (既定 480x320) |
| `?level=N&weapon=W` | メニューを飛ばしてレベル N、武器 W (0 バルカン, 1 レーザー, 2 ミサイル) で開始 |
| `?debug` | デバッグモード (下記) |
| `?seed=N` | 乱数シードを固定 |
| `?auto=1` | AI がプレイヤーを操作する (デモ) |

## 入力

| 操作 | キーボード | ゲームパッド | 仮想パッド |
|---|---|---|---|
| 左右旋回 | ← → / A D | 左スティック X (アナログ)、十字キー | 方向ディスクの左右 (アナログ) |
| ダッシュ | ↑ / W | 左スティック上 (アナログ)、十字キー上 | 方向ディスクの上 (アナログ) |
| ブレーキ | ↓ / S | 左スティック下 (アナログ)、十字キー下 | 方向ディスクの下 (アナログ) |
| A (攻撃・決定) | スペース / J L / Enter | ボタン 0, 2, 3, 7 | A ボタン |
| B (緊急回避) | I K / C V B N M (スペースの隣) | ボタン 1, 4, 5 (B とショルダー) | B ボタン |
| ポーズ / 再開 | Esc / P | Start (ボタン 9) | (無し) |
| ミュート切り替え | タイトル / ポーズ画面で ↓ (ツールバーの「サウンド」ボタンでも) | 同左 | 同左 |

- キーは `KeyboardEvent.code` で判定し、ゲームに使うキーは `preventDefault()` する
  (スペースや矢印でページがスクロールしない)。ウィンドウがフォーカスを失ったら全キーを離す。
- 方向の強さ (core 3.7): キーボードと十字キーはデジタル (常に最大)、左スティックと方向ディスクは
  アナログ。不感帯と飽和はこの版が決める (core は -127〜127 をそのまま強さに使う。`roundAxes()`)。
- ゲームパッドは `navigator.getGamepads()` を tick ごとにポーリングする (標準マッピング前提)。
  左スティックと方向ディスクは同じ読み方 (`roundAxes()`) をする。左スティックの強さはベクトルの長さで決める:
  0.1 までが不感帯 (スティックはちょうど 0 には戻らない)、0.9 で最大。向きは丸いゲートから正方形に写す (大きい方の成分を 1 にする) ので、
  **斜めに倒しきると両軸とも最大**になる。クイックターンは最大の旋回と最大のブレーキの組み合わせで、
  旋回速度は両方の強さの積で効く (両軸 90 だと 109 度/秒、127 で 180 度/秒) ため、軸ごとに判定して
  飽和させていた最初の版 (不感帯 0.15、最大 0.7) では斜めいっぱいでも各軸 0.7 前後 (パッドによってはそれ以下)
  にしかならず、ゆっくりしか回らなかった。写した後の各成分には 0.15 の不感帯を付けて残りを伸ばす
  (軸から数度ずれて倒しても、旋回に弱いダッシュやブレーキが混ざらない。10 度ずれで 3/127)。
- 仮想パッドは十字キーではなく「方向ディスク」にしている。ノブ (指の位置を幅の 32% の
  可動域に丸めたもの) の中心からのオフセットから両軸を出すので、斜め入力
  (ダッシュしながら旋回、ブレーキしながらクイックターン) ができる。読み方は左スティックと同じ
  `roundAxes()` で、中心からの距離が幅の 6% までが不感帯、28% で最大 (可動域の端より少し手前なので、
  縁まで届かない指でも最大になる)。斜めに倒しきれば両軸とも最大。
  最初の版は軸ごとに判定していた (各軸 7% / 21%)。Pointer Events を使い、ディスクと
  A / B ボタンでそれぞれ別のポインタを捕捉するのでマルチタッチで同時に操作できる。
  B (緊急回避) は A の左上に小さめの青い丸で置く。
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
- 言語: ブラウザの言語 (`navigator.language`) が `ja` で始まれば日本語、それ以外は英語。ページには両方の
  文言を `lang="ja"` / `lang="en"` 付きで置き、`<head>` の最初のスクリプトが `<html>` に `ja` / `en` クラスと
  `lang` を付け、CSS (`html.ja [lang="en"], html.en [lang="ja"] { display: none }`) で片方だけ見せる
  (docs/index.html も同じ)。play.js が書く文字列 (サウンドボタンのラベル) は同じ判定で切り替える。
  操作説明の段落は README の操作表と同じ内容 (回避、ポーズ、ミュート、ゲームパッドの割り当てを含む) に保つ。
- ページのメタデータ: 両ページの `<head>` に `<meta name="description">`、favicon (`docs/favicon.ico` と
  高解像度用の `play/icon-512.png`)、OGP (`og:title` / `og:description` / `og:url` / `og:image` など) と
  `twitter:card` = `summary_large_image` (X で大きな画像として表示させる) を置く。`og:url` と `og:image` は
  https://shapoco.github.io/devour-sphere/ からの絶対 URL、画像は docs/image/ogp_image_v2.png を両ページで共用する。
  description はメタタグでは言語を切り替えられないので、日本語の後に英語を " / " で続けた 1 本の文にする。

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

## デバッグモード

URL に `?debug` を付けて開くとデバッグモードに入る (`ds_set_debug(1)`)。数字キーがチートになる
(`ds_debug_key`、キーの割り当ては上の表)。HUD の上中央に半透明の「DEBUG MODE」が出続けるので、
チートした状態のスクリーンショットはそれと分かる。LAUNCH の演出などを確認するときは
5 を何度か押して自機を最大まで育て、4 秒待てば飛び立つ。

## ベンチマーク

タイトル画面で **B のキー (I / K / C / V / B / N / M) を 3 秒** 押し続けると始まる (core/SPEC.md「ベンチマーク」)。
実行中 (`ds_bench_running()`) は play.js が 1 フレーム 1 tick で、1 回の `requestAnimationFrame` の中で
約 12 ms 分のフレームを続けて描き、最後の 1 枚だけをキャンバスに出す (画面のリフレッシュレートで
頭打ちにならないように)。結果はキャンバスと、ブラウザのコンソール (`printf`) に出る。`TCK` は `ds_tick()`、
`BGN` / `RAS` は `ds_render()` の中を `emscripten_get_now()` で測る。表示待ちは無い。
