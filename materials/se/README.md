# 効果音の素材

このディレクトリの wav は「効果音ラボ」(https://soundeffect-lab.info/) で配布されている素材です。
著作権は効果音ラボにあり、このリポジトリの MIT ライセンスの対象外です。

- 効果音ラボの規約では、ゲームに組み込んで配布すること (音源ファイルがむき出しでも、
  GitHub で公開することも) と、形式変換・切り詰めなどの改変が許可されています。クレジット表記は任意です。
- 禁止されているのは、素材そのもの (改変したものを含む) を素材として再配布することです。
  このディレクトリのファイルや docs/play/se.bin を素材として取り出して使わないでください。
  利用したい場合は効果音ラボから直接入手してください。
- 規約は予告なく変わることがあります。最新の規約は https://soundeffect-lab.info/agreement/ を参照。

## ファイル

ファイル名は core の `sim::SoundKind` (core/include/devoursphere/sim/game.hpp) に対応します。
impl/wasm/pack_se.py がこれらを 1 本のパック (docs/play/se.bin) に変換します。

| ファイル | 鳴るとき |
|---|---|
| shot_vulcan.wav | プレイヤーがバルカンを撃った |
| shot_laser.wav | プレイヤーがレーザーを撃った |
| shot_missile.wav | プレイヤーがミサイルを撃った |
| hit_enemy.wav | 敵に自弾が当たった |
| hit_player.wav | プレイヤーに敵弾が当たった |
| enemy_killed_small.wav | 自分より小さい (同じ) 敵を倒した |
| enemy_killed_big.wav | 自分より大きい敵を倒した |
| player_killed.wav | プレイヤーが死んだ |
| get_fragment.wav | プレイヤーが浮遊フラグメントを取った |
| get_upgrade.wav | プレイヤーがアップグレードを取った |
| menu_select.wav | メニューで選択項目を切り替えた |
| menu_start.wav | メニューで決定した |
| launch.wav | LAUNCH 演出の開始 |
| arrive.wav | ARRIVE 演出の開始 |

shot_missile と shot_vulcan、get_upgrade と menu_start は今は同じ素材 (後で差し替えるかもしれない)。
