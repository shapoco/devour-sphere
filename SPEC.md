# Devour Sphere

## 概要

「Devour Sphere」は、プレイヤーがサイバー空間の中でエンティティとして成長し、
他のエンティティと戦いながらフラグメントを集めて巨大化していくシューティングゲームである。

## プロジェクト構成

### コアプログラム

コアプログラムは、ゲームの基本的なルールやエンティティの挙動、フラグメントの管理などを担当する。
描画や入力処理などのプラットフォーム依存部分は含まれない。
コアプログラムは、異なるプラットフォーム上でも同じ挙動を保証することを目的としている。

コアプログラムは 2 層に分かれる。

- `sim`: ルールと物理。整数演算のみで、どのプラットフォームでも同じ結果になる。
- `render`: ShapoGFX (submodule/shapo-gfx/) で状態を描画する。プラットフォームには依存しないが、
  フレームバッファの用意や表示への転送はプラットフォーム側が行う。帯単位の描画に対応する。

詳細は core/SPEC.md を参照のこと。

### WASM 版

impl/wasm/ 配下に WASM 版の実装が置かれる。
WASM 版は、ブラウザ上でコアプログラムを実行するためのラッパーや入力処理、公開用ページを含む。

詳細は impl/wasm/SPEC.md を参照のこと。

### Xiamocon 版

impl/xiamocon/ 配下に Xiamocon (XIAO RP2350 / ESP32S3 用のゲーム機型マザーボード)
向けの実装が置かれる。240x240 の RGB565_SWAPPED ディスプレイに、フレームバッファを持たず
帯単位で描画・転送する。RP2350 と ESP32S3 の両方をビルドでき、ソースは共通。

詳細は impl/xiamocon/SPEC.md を参照のこと。

### M5Tab5 版

impl/m5tab5/ 配下に M5Stack Tab5 (ESP32-P4 の 5 インチタブレット型開発機) 向けの実装が置かれる。
ESP-IDF のプロジェクト。640x360 のランドスケープのフレームに帯単位で描き、PPA (ESP32-P4 の
2D アクセラレータ) が 2 倍拡大・90 度回転・バイト入れ替えを 1 回でまとめて
720x1280 の MIPI-DSI パネルへ書く。入力は WASM 版の横画面と同じ仮想パッド。

詳細は impl/m5tab5/SPEC.md を参照のこと。

### M5StickS3 版

impl/m5sticks3/ 配下に M5StickS3 (ESP32-S3 のスティック型開発機) 向けの実装が置かれる。
ESP-IDF のプロジェクト。135x240 の ST7789 を横倒しにして 240x135 として使い、
45 行の帯で描く (回転はパネルの MADCTL が行うので、ソフトウェアでの回転もバイト入れ替えも無い)。
方向の入力はデバイスの傾き (BMI270、アナログ)、射撃と緊急回避が 2 つのキー、ポーズは本体を振る。

詳細は impl/m5sticks3/SPEC.md を参照のこと。

### PicoSystem 版

impl/picosystem/ 配下に PicoSystem (Pimoroni の RP2040 携帯ゲーム機) 向けの実装が置かれる。
PicoSystem SDK は使わず (フレームバッファを静的に確保してしまう)、素の pico-sdk プロジェクトとして
ST7789 を 16 ビットモードで帯ごとに駆動する。フレームループと計測オーバーレイは Xiamocon 版と共通。

詳細は impl/picosystem/SPEC.md を参照のこと。

### ESPboy 版

impl/espboy/ 配下に ESPboy (ESP8266 の携帯ゲーム機、128x128 の ST7735) 向けの実装が置かれる。
Arduino ではなく ESP8266_RTOS_SDK v3 のプロジェクト (DRAM 96KB を全部リンカに見せ、WiFi を初期化しない)。
core は縮小構成 (半径 2^16 のスフィアに 48 体、深度なし、フラットシェーディング) でビルドし、
16 行の帯 1 枚を CPU で転送する。音はタイマー割り込みの矩形波。

詳細は impl/espboy/SPEC.md を参照のこと。

### CLI 版

impl/cli/ 配下に Linux の端末で遊ぶ実装が置かれる (半分冗談)。320x240 のフレームバッファに描き、
色付きの ASCII アート (おまけで点字と半ブロック) にして端末に流す。キーは kitty keyboard protocol
(非対応の端末ではキーリピートから推定)、音はベル、ハイスコアはカレントディレクトリのファイル。
依存は C++17 と cmake と POSIX だけ。

詳細は impl/cli/SPEC.md を参照のこと。

## ディレクトリ構成

```
SPEC.md              この文書
README.md            紹介 (英語)
README.ja.md         紹介 (日本語)
CMakeLists.txt       ネイティブビルド (コア、テスト、確認用フロントエンド)
core/                コアプログラム (core/SPEC.md)
impl/wasm/           WASM 版 (impl/wasm/README.md、SPEC.md)
impl/xiamocon/       Xiamocon 版 (impl/xiamocon/README.md、SPEC.md)
impl/picosystem/     PicoSystem 版 (impl/picosystem/README.md、SPEC.md)
impl/m5tab5/         M5Tab5 版 (impl/m5tab5/README.md、SPEC.md)
impl/m5sticks3/      M5StickS3 版 (impl/m5sticks3/README.md、SPEC.md)
impl/espboy/         ESPboy 版 (impl/espboy/README.md、SPEC.md)
impl/cli/            CLI 版 (impl/cli/README.md、SPEC.md)
docs/                公開用の静的サイト (docs/play/ がゲーム)
assets/se/           効果音の素材 (効果音ラボ、assets/se/README.md)
assets/release/      リリース zip にそのまま入れるファイル (README.txt、upload.sh)
submodule/shapo-gfx/ ShapoGFX (git submodule)
launch_web_server.sh docs/ をローカルで配信する
make_release.sh      リリース用のバイナリと zip を作る
releases/            その出力 (git 管理外)
```

## ビルドとテスト

```sh
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure      # コアのテスト (60Hz と 30Hz)
./build/impl/wasm/devoursphere_native 1 300 out.ppm   # 1 フレームを PPM に書き出す
make -C impl/wasm                                # WASM 版 (emcc が必要)
./launch_web_server.sh                           # http://localhost:52980/play/
```

WASM 版以外の各版は、それぞれのディレクトリの独立したトップレベルでビルドする。
手順は各版の README.md (impl/<版>/README.md)、注意点は同じ場所の SPEC.md。

C/C++ のコードは .clang-format (ShapoGFX と同じ設定) で整形する。

## リリース

GitHub の releases に置くファイルは `./make_release.sh` が作る (Xiamocon SDK が必要。
`XMC_REPO_PATH` に SDK のリポジトリを指しておく)。

```sh
./make_release.sh                  # releases/devour-sphere-YYYYMMDD.zip
```

- impl/ の各ターゲットをビルドする。WASM 版は含めない
  (ダウンロードさせるものではなく docs/play/ で公開するため)。
- ビルドは各ターゲットのディレクトリにあるスクリプトを呼ぶ
  (impl/xiamocon/devoursphere/build_esp32s3.sh、同 build_rp2350.sh、impl/picosystem/build.sh)。
  make_release.sh に全部のコマンドを並べると、SDK の setup.shrc が書き換える環境変数が
  次のターゲットに漏れてうまくいかなかった。スクリプトは子プロセスなので漏れない。
- `releases/devour-sphere-YYYYMMDD/<ターゲット>/` に置いて zip にまとめる。
  ターゲットは `xiamocon-rp2350` (devour-sphere.uf2)、
  `xiamocon-esp32s3` (devour-sphere.factory.bin と upload.sh)、`picosystem` (devour-sphere.uf2)。
- ESP32S3 の factory イメージは bootloader・パーティションテーブル・boot_app0・
  アプリをオフセット通りに連結したものなので、0x0 に 1 回書けば済む。
  同梱の `upload.sh` (assets/release/xiamocon-esp32s3/) は esptool を探して
  (`esptool` / `esptool.py` / `python3 -m esptool`、5.x でのサブコマンド名の変更にも対応) それを書き込む。
- 書き込み方と操作方法を書いた README.txt を zip の直下に入れる。元は assets/release/README.txt で、
  `@DATE@` と `@COMMIT@` を sed で日付とビルド元のコミット (作業ツリーに変更があれば
  "(with local changes)" 付き) に置き換える。
- `set -eux` で、途中で失敗したらそこで止まる。zip は最後に作るので
  中途半端な zip はできない。実行のたびに出力先と zip を消してから作る
  (`zip -r` は既存の zip に追記するので、消さないと前回のファイルが残る)。
