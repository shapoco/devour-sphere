# Devour Sphere (CLI 版)

Linux の端末で遊ぶ Devour Sphere。半分冗談。フレームを 320x240 のフレームバッファに描き、
それを色付きの文字に変換して端末に流す。コアプログラム (core/SPEC.md) には手を入れていない。

## 方針

- 描画は既定で ASCII アート。文字のグリフの形に最も近いピクセルパターンの文字を置く。
  おまけとして点字 (U+2800〜) と半ブロック (▀) の 2 モード。
- 依存は C++17 と cmake だけ。端末の扱いは POSIX (termios / poll / ioctl) とエスケープシーケンスで、
  ライブラリも別途インストールするパッケージも使わない。Linux、macOS、BSD で同じコード。
- キー割り当ては WASM 版 (impl/wasm/SPEC.md) に合わせる。押下・解放は kitty keyboard protocol で取り、
  対応していない端末ではキーリピートから推定する。
- tick は 60Hz、表示はベストエフォート (既定で上限 30 fps)。
- 音は端末のベル (BEL) だけ。鳴らすのは自機の被弾・死亡、敵の撃破、ミュート解除に絞る。
- ハイスコアはカレントディレクトリの `devoursphere.highscore` (16 バイト)。
- HUD の文字は 320x240 の 8 px フォントが文字 1 個分より小さくなるので読めない。冗談なので構わない。

## ディレクトリ

```
impl/cli/
  SPEC.md          この文書
  Makefile         `make` で build/devoursphere を作る (cmake を呼ぶ)。`make run ARGS=...`、`make clean`
  CMakeLists.txt   独立したトップレベル。core と submodule/shapo-gfx を add_subdirectory する
  src/main.cpp     引数、ループ (tick / フレーム / 待ち)、ベル、ハイスコア、デバッグ、--once
  src/term.*       端末: raw mode、代替スクリーン、kitty protocol の問い合わせと解析、レガシー入力、ウィンドウサイズ
  src/aa.*         フレームバッファ → セル (文字 + 色) → 前フレームとの差分のエスケープシーケンス
```

## ビルドと起動

```sh
cd impl/cli
make                        # build/devoursphere
./build/devoursphere        # そのまま遊ぶ
./build/devoursphere --help
```

Makefile は cmake を `$(shell command -v cmake)` で探す。GNU make 自身の PATH 探索は
`cmake` という名前の**ディレクトリ** (Emscripten SDK の `upstream/emscripten/cmake/`) で止まって
"Permission denied" になるため。

| オプション | 既定 | 内容 |
|---|---|---|
| `--mode=ascii\|braille\|half` | ascii | 文字への変換方法 (下記) |
| `--colors=true\|256` | true | SGR の色指定。truecolor (38;2) か 256 色キューブ (38;5) |
| `--color=common\|bright` | common | セルの色: 最頻の色か、輝度の合計が最大の色 |
| `--threshold=N` | 26 | 輝度 (0〜255) がこれ以上のピクセルを「点灯」とする (約 10%) |
| `--fps=N` | 30 | 表示の上限。tick は常に `sim::TICK_RATE` (60) |
| `--keys=auto\|kitty\|legacy` | auto | 入力方式。auto は端末に問い合わせる |
| `--beep=on\|off` | on | ベル |
| `--stats` | | 1 行目に fps / tps / 落とした tick / 入力方式 / 格子 / 各段の時間 / 1 フレームのバイト数。終了時に stderr にも出す |
| `--debug` | | デバッグモード (WASM 版の `?debug`)。数字キー 1〜8 がチート (impl/wasm/SPEC.md の表) |
| `--level=N` `--auto` `--seed=N` | | スフィア N から開始 / AI が自機を操縦 / シード (既定は時刻 ^ pid) |
| `--once=N` `--size=CxR` | | 端末を触らず、N tick 進めた 1 フレームを CxR (既定 80x30) の行として stdout に出して終了。動作確認用 |

## 入力

| 操作 | キー |
|---|---|
| 左右旋回 | ← → / A D |
| ダッシュ / ブレーキ | ↑ / W、↓ / S |
| A (攻撃・決定) | スペース / J L / Enter |
| B (緊急回避) | I K / C V B N M |
| ポーズ / 再開 | Esc / P (ポーズ画面の ↓ でミュート切り替え) |
| 終了 | Q / Ctrl-C |
| 再描画 | Ctrl-L (端末が乱れたとき) |

WASM 版と同じ割り当てに Q / Ctrl-C と Ctrl-L を足したもの。タイトルの操作案内は既定 (PC 向け) のまま、
ポーズ画面の案内だけ Q: QUIT を含む文字列に差し替える (`Renderer::setControlHints()`)。

### kitty keyboard protocol

端末が押下・リピート・解放を区別して送ってくる。起動時に raw mode にしてから `CSI ? u` (フラグの問い合わせ) と
`CSI c` (DA1。どの端末も答える) を続けて送り、`CSI ? ... u` の返事が DA1 の返事より先に来れば対応と判断する
(どちらも来なければ 1 秒で諦めてレガシー)。対応なら `CSI > 11 u` でフラグ 1 (曖昧さの除去)、
2 (イベント種別の報告)、8 (すべてのキーをエスケープシーケンスで) を push し、終了時に `CSI < u` で pop する。
8 が無いと文字キーは素のバイトで届いて解放が分からない。

キーは `CSI code ; mods : event u` (event 1 押下、2 リピート、3 解放) と、矢印の `CSI 1 ; mods : event A〜D`
で届く。押されているキーは「解放がまだ来ていないキー」。フォーカスイベント (`CSI ? 1004 h`) も有効にして、
フォーカスを失った (`CSI O`) ら全キーを離す (WASM 版の blur と同じ)。

対応端末 (2026-02 時点の調べ): kitty、WezTerm、foot、Ghostty、Alacritty、iTerm2、Windows Terminal 1.25 Preview 以降、
VS Code 1.109 以降。tmux は未対応 (PR あり)、GNOME Terminal (VTE) はパッチのレビュー中。

### レガシー (キーリピートからの推定)

端末はキーが押されている間しかバイトを送らず、解放は分からない。そこで

- 最初のバイトから 600 ms (`HOLD_FIRST_MS`) は押されている扱い (自動リピートの開始遅延が 0.5 秒程度なので、
  その間に離されても分からない)、
- その後はリピートのバイトが来るたびに 120 ms (`HOLD_REPEAT_MS`) 延長する。

旋回の解放が最大 0.6 秒遅れるので操作感は鈍い。Esc 単独は 50 ms (`ESC_ALONE_MS`) 続きが来なければ Esc キー。
`ESC [ A` と `ESC O A` (application cursor keys) の両方を矢印と見る。

### 本物の仮想コンソール

Linux の VT では `KDSKBMODE` の `K_MEDIUMRAW` で真の押下・解放が取れるが、今は入れていない
(WSL や ssh では使えず、root か VT の所有が要る)。第 3 のバックエンドとして足せる場所は `Term::open()`。

## 描画

1. `Renderer` が 320x240 の RGB565_SWAPPED に 1 帯で描く (`renderBand(dst, 0, 240)`)。WASM 版と同じ経路。
2. 端末の幅 C・高さ R (毎フレーム `TIOCGWINSZ`) から格子を決める。セルは横 1 : 縦 2 とみなし、
   `gw = C`、`gh = gw * 240 / 320 / 2 = gw * 3 / 8`。`gh > R` なら `gh = R`、`gw = gh * 8 / 3`。
   格子は画面の中央に置き、余りは黒のまま。120x45 の端末で 120x45 (セル 1 つが 2.67x5.33 px)、
   80x24 では 64x24 (5x10 px)。サイズが変わったら全面を描き直す。
3. 各セルに対応するフレームバッファの矩形をサブピクセル格子 (ascii 3x6、braille 2x4、half 1x2) に切り、
   **各サブボックスの中に 1 つでも点灯ピクセルがあればそのサブピクセルを点灯**とする。平均ではなく「あれば」なのは、
   ワイヤーフレームの線が 1 px 幅で、粗い格子で平均すると消えるため。点灯は輝度 (77R + 151G + 28B) / 256 が
   `--threshold` 以上。
4. セルの色は矩形内の点灯ピクセルを RGB222 (64 段) の箱に入れ、最も数の多い箱 (`--color=bright` なら
   輝度の合計が最大の箱) に入ったピクセルの平均色。暗いワイヤーフレームがセルの多数を占めて明るい機体が少数だと、
   既定では線の色になる。
5. モードごとの文字:
   - **ascii**: 18 bit のパターンで 2^18 エントリの表を引く (下記)。
   - **braille**: 2x4 のドットは U+2800 + ビット (左列 1,2,4,64、右列 8,16,32,128) と 1 対 1 なので表は要らない。
   - **half**: 上半分・下半分それぞれの色を別に求め、上が点灯なら `▀` (U+2580) を fg = 上、bg = 下で、
     上が消灯なら空白を bg = 下で出す。1 セル 2 色。
6. 背景色は端末の既定を使わず黒を明示する (`SGR 48;2;0;0;0`、256 色では `48;5;16`)。明るいテーマの端末でも
   同じ絵になる。全面を描き直すとき (起動時とサイズ変更時) は格子の外の余白も含めて画面全体を黒背景の空白で
   埋める (`CSI 2 J` は現在の背景色で埋めない端末があるので使わない)。
7. 前フレームと違うセルだけを書く。カーソル移動 (`CSI r ; c H`) は直前に書いたセルの隣でないときだけ、
   SGR は色が変わったときだけ (空白は fg を見ない)。1 フレームは `CSI ? 2026 h` 〜 `CSI ? 2026 l`
   (synchronized output) で囲み、対応端末ではティアリングが出ない。自動折り返しは `CSI ? 7 l` で切る
   (右下のセルを書いてもスクロールしない)。
8. `--stats` の行はセルに上書きするので差分に乗る。

### ASCII のグリフ表

起動時に作る。表示可能な ASCII 95 文字を ShapoGFX 同梱の `ShapoSansMono_s08c07` (8 px 等幅) で
16x16 の Surface に描き、そのグリフ箱 (`charAdvance('M')` x `textHeight()`) を 3x6 に縮めて
(サブボックスに 1 px でも白があれば点灯) 18 bit のパターンにする。空白はパターン 0 に固定。
残りの 2^18 - 95 エントリは「パターンにあってグリフに無いサブピクセル x 2 + グリフにあってパターンに無い
サブピクセル x 1」が最小の文字で埋める (同点は文字コードの小さい方。空白は候補に入れない)。
総当たり 262,144 x 94 で 30 ms 程度、表は 256 KB。

最初はハミング距離で埋めていたが、点 1 つのパターンでは空白 (距離 1) と 1 ドットのグリフ (距離 0〜1) が同点になり、
文字コードの小さい空白が勝つ位置があった。星がセルを横切るたびに出たり消えたりしてチラついたのはこれ。
取りこぼしを余分より重くし、空白を 0 パターン専用にしてからは、点灯したサブピクセルは必ず何かの文字で表される
(その分、全体はやや太くなる)。
グリフの形は端末のフォントで違うので、見た目の一致はそもそも近似。

## 時間

- 単調時計 (`CLOCK_MONOTONIC`) の経過をアキュムレータに足し、`TICK_US` (1/60 秒) ごとに `Game::tick()`。
  tick ごとに `Renderer::pollEffects()` と `Game::sounds()` を読む (core/SPEC.md「時間と入力」)。
  1 ループで進めるのは最大 8 tick (`MAX_CATCHUP`)。それでも余る時間は捨てる (端末が遅すぎるとゲームが遅くなる。
  `--stats` の drop に出る)。
- フレームは前のフレームから `1 / fps` 秒経っていれば描く。`beginFrame()` の dt はその実時間。
- 待ちは `poll(stdin)` のタイムアウトで、次の tick と次のフレームの近い方まで。入力が来れば起きる。
- pty 上の計測 (120x45、レベル 4、AI 操縦、3 秒): 3 モードとも 30 fps / 60 tps / drop 0。
  render 0.1〜0.2 ms、変換 0.7〜1.1 ms、書き込み 0.0〜0.4 ms、1 フレーム 7 KB (ascii)、10 KB (braille)、
  16 KB (half)。実端末では書き込みが端末の描画で待たされることがある。

## サウンド

`Game::sounds()` のうち `BEEP_MASK` の種類 (HIT_PLAYER、PLAYER_KILLED、ENEMY_KILLED_SMALL / BIG) が
立っていれば BEL を 1 バイト書く。ミュート解除でも 1 回鳴らす: sim はそこで MENU_SELECT を要求するが、
MENU_SELECT はメニューのカーソル移動でも要求されるので、tick の前後で `Game::muted()` が true → false に
変わったことを見る。100 ms (`BEEP_GAP_US`) に 1 回まで。それ以外の音 (撃つ、当てる、餌、メニュー、演出、
警報、回避) は鳴らさない。音の高さも長さも端末任せで、無音やビジュアルベルの端末も多い。
ミュート中は `sounds()` が 0 になるのでそのまま効く。永続化はしない。

## ハイスコア

カレントディレクトリの `devoursphere.highscore`。中身はハンドヘルドがフラッシュに置くのと同じ 16 バイト
(`sim/high_score_record.hpp`: magic、メジャーバージョン、到達スフィア、スコア、CRC-32) なので、
壊れたファイルと旧メジャーの記録は読み込み時に捨てられる。起動時に読んで `Game::setHighScore()`、
毎秒 `keepHighScore()` を呼んでファイルより大きければ書く (WASM 版と同じ規則)。`.tmp` に書いて rename。
終了時にも書く。`.gitignore` に入れてある。

## 端末の後始末

終了 (Q、Ctrl-C は raw mode で ISIG が切れているので 3 のバイトとして届く)、SIGTERM、SIGHUP、SIGINT で
kitty フラグの pop、SGR リセット、フォーカスイベント解除、自動折り返しとカーソルの復帰、代替スクリーンの解除、
termios の復元を行う。シグナルハンドラは `write()` と `tcsetattr()` しか呼ばない。

## 確認の方法

- `./build/devoursphere --mode=ascii --level=3 --auto --seed=7 --once=600 --size=100x40` で
  端末を触らずに 1 フレームを見られる (3 モードとも)。
- 疑似端末で回す: python の `pty.fork()` で起動し、`TIOCSWINSZ` でサイズを与え、kitty の問い合わせに
  `CSI ? 0 u` `CSI ? 62 c` で答えてから `CSI 32 u` (スペース押下) などを送り、`q` で終了させて
  出力に代替スクリーンの出入りと `CSI > 11 u` / `CSI < u` が対になっているかと、
  `--stats` の最後の行 (stderr) を見る。
