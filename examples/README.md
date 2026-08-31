# Examples

MitiruEngineの機能を **1章 = 1機能** で見せる教材カリキュラム(規格:
[`docs/EXAMPLE_STANDARD.md`](../docs/EXAMPLE_STANDARD.md))。ゲームは全てDLL形式:
`MITIRU_GAME(T)`で入口を1行宣言したSHARED libraryを`mitiru_host.exe`がloadして
main loopを駆動する。

```bash
cmake --preset default
cmake --build build --config Debug --target welcome   # mitiru_host も依存で一緒に build される
build/apps/mitiru_host/mitiru_host.exe build/apps/mitiru_host/welcome/welcome.dll
```

各DLLは`build/apps/mitiru_host/<name>/<name>.dll`にdeployされる(assets/ も同じdirへ)。
CEF runtime (`libcef.dll` + `icudtl.dat` + `MitiruCefHelper.exe` + locales等)はPOST_BUILDで
自動配置される。手で何かをコピーする必要はない。`--watch`を付けるとhot reload
(state保持)、hostの全オプションは`mitiru_host.exe --help`。

## 章(基礎)：この表の並び = 学習順

| 章 | 何を見せるか |
|---|---|
| [`welcome`](welcome/welcome_dll.cpp) | ようこそ画面：額装した浮世絵(写楽)を主役に、大きな題字 / 案内文 / 朱印を1画面に。画像(drawSprite) / 見栄えする文字(drawTextWithShadow・drawTextBold) / 図形(角丸の額縁) / 動き(絵の上下ゆれ・朱印の回転)をまとめて見せる第一章 |
| [`shapes`](shapes/shapes_dll.cpp) | 図形カタログ：rect / circle / line / gradient / dashedLine / 三角形(drawTriangle) / 多角形(drawPolygonで五角形・六角形)を4列x 2段のグリッドに |
| [`text`](text/text_dll.cpp) | テキスト描画：枠内の整列(drawTextInRect) / 文字サイズ / 色 / 折り返し(drawTextWrapped) / はみ出しの省略(drawTextClipped)を、余白をとった区画で1つずつ |
| [`input`](input/input_dll.cpp) | 入力を図で見せる：キーボード図の押されたキーが光る / マウス軌跡 / パッド配置図。押す・動かすと視覚で分かる（InputSnapshot経由、キー名の文字列は出さない） |
| [`motion`](motion/motion_dll.cpp) | イージング(動きの緩急)の格子：列にLinear / EaseInOut / EaseIn / EaseOut、行に 位置 / 大きさ / 回転 / 透明度。1つのイージング曲線が各プロパティをどう動かすかを一望する。位置の目盛りが速さの変化を形で見せる。dt (前フレームからの経過秒)の使い方も |
| [`sprites`](sprites/sprites_dll.cpp) | スプライト(画像)を描く：赤べこ(会津の郷土玩具)の 大きさ / 回転 / 左右反転 / 半透明 の見本と、矢印キーで歩く赤べこ(進む向きで反転・脚が交互に動く歩行アニメ) |
| [`camera`](camera/camera_dll.cpp) | 追従カメラ(FollowCam)：赤べこがマウスの方へ画面より広い牧場を歩き、視点がdeadzone +先読み + world clampで滑らかに追ってスクロールする。木と赤べこは接地yで前後が入れ替わる |
| [`audio`](audio/audio_dll.cpp) | 音を視覚化：鳴らすと弾ける（SEの音程を色つきリング / BGMは回るディスクで再生・一時停止・フェードを表現）。状態テキストなし、`hud.play()` / `hud.music()`でエンジンに再生を依頼 |

## 章(看板)

| 章 | 何を見せるか |
|---|---|
| [`html_hud`](html_hud/html_hud_dll.cpp) | ひとつのC++の値をHTMLの複数表示に同時束縛（手書きJSゼロ）。動かすと全部連動。下はC++がネイティブ描画するスライダー（操作源）、上は同じ値を映す6つのHTML/CSS表示（大きな数字・円形ゲージ・横バー・色面・セグメントpips・折れ線）。スライダーを動かすと6つが一斉に同じ水準へ動く。値は`hud.set`でpushし、`data-m-text/style/class/repeat/spark-push`が反映する。JavaScriptを1行も書かない |
| [`html_menu`](html_menu/html_menu_dll.cpp) | HTMLの操作 → C++が反応（html_hudの逆向き）。HTMLパネルを触るとC++が描く図形が変わる（色 / 形 / 数 / 縁取り・拡大トグル / リセットconfirm）。HTML→C++は`data-m-action`の信号のみ、状態はC++所有、JavaScriptを1行も書かない |
| [`observe`](observe/observe_dll.cpp) | MITIRU_REFLECT + watch：状態を外から観測 |
| [`rewind`](rewind/rewind_dll.cpp) | 巻き戻し：状態を1つのstructに置き、見たい値を申告するだけで過去へ戻せる。`--inspect rewind`付きで起動 |
| [`restart_save`](restart_save/restart_save_dll.cpp) | `hud.requestRestart()` +セーブ / ロード(`.msav`ファイル) |
| [`scene3d`](scene3d/scene3d_dll.cpp) | GPU 3Dシーン：平行光の影 + WBOIT半透明 + skybox |
| [`model3d`](model3d/model3d_dll.cpp) | 大きな3Dモデル：26万ポリゴンの宮殿(Crytek Sponza、glTF)を`drawModel` 1行でそのまま置き、一人称で歩き回る。.gltf/.glb/.objは初回だけ隣に変換キャッシュを作って読む。マウス視線(`hud.lockMouse`)とWASD移動、詳細度(LOD)は距離から自動 |
| [`anim3d`](anim3d/anim3d_dll.cpp) | キャラクターを歩かせる：リグ付きglTF (Khronos Fox)のクリップ名と時間を`drawModelBlend`に渡すだけで骨格アニメが動く。WASDで歩かせると待機と歩きがなめらかに混ざる。時間は自分の状態で`t += dt`するだけなので巻き戻しにもそのまま乗る |

## Subsystem単独起動デモ：[`subsys/`](subsys/)

各subsystemをengine全体なしで立ち上げる単体exe ([`docs/SUBSYSTEMS.md`](../docs/SUBSYSTEMS.md))。
`mitiru renderer` / `audio` / `input` / `scene` CLIコマンドがlaunchするのはこれら。
exeは従来どおり`build/examples/mitiru_subsys_<name>/`に出る。

| Example | 何を見せるか |
|---|---|
| `subsys/mitiru_subsys_renderer` | rendererのみ(CEF / audio無し、cold-start < 1s)。grid +往復rectの視覚smoke |
| `subsys/mitiru_subsys_audio` | audioのみ。440Hz sine +実出力RMSのレベルメーター(5秒で自動終了) |
| `subsys/mitiru_subsys_input` | inputのみ。256 VKのlive grid + mouse状態 + pressログ |
| `subsys/mitiru_subsys_scene` | scene loopのみ。12 entityの積分 +縁反射 |

## インフラ(host / tool)：`apps/`所在

製品・開発基盤は [`apps/`](../apps/)にある(examplesは教材とデモ専用)。host本体
`mitiru_host`のほか`mitiru_launcher` / `mitiru_dev_companion` / `mitiru_start` /
`mitiru_tool_cef`。詳細は各dirと`docs/`。

## 最小の書き方

`MITIRU_GAME(T)`のTにはゲームの全状態を入れ、まるごとコピーできる形(flat POD、
trivially copyable)に保つ。`std::vector` / `std::string`の代わりに`mitiru::FixedVec` /
`mitiru::FixedString`を使う。hostが状態をbytesのまま記録し、巻き戻しと録画リプレイに
そのまま使うためだ。詳細は [`docs/GETTING_STARTED.md`](../docs/GETTING_STARTED.md)
と`include/mitiru/module/Game.hpp`冒頭のコード例。
