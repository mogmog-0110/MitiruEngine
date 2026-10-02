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
手で何かをコピーする必要はない。`--watch`を付けるとhot reload
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
| [`audio`](audio/audio_dll.cpp) | 音を視覚化：鳴らすと弾ける（SEの音程を色つきリング / BGMは回るディスクで再生・一時停止・フェードを表現 / 台詞は吹き出し）。状態テキストなし、`hud.play()` / `hud.music()` / `hud.voice()`でエンジンに再生を依頼 |

## 章(看板)

| 章 | 何を見せるか |
|---|---|
| [`html_hud`](html_hud/html_hud_dll.cpp) | ひとつのC++の値をUIの複数表示に同時束縛。動かすと全部連動。下はC++がネイティブ描画するスライダー（操作源）、上は同じ値を映す6つのRML/RCSS表示（大きな数字・円形ゲージ・横バー・色面・セグメントpips・折れ線）。スライダーを動かすと6つが一斉に同じ水準へ動く。値は`hud.set`でpushし、`assets/ui/main.rml`の`{{ }}`・`data-class`・`data-style`・`data-for`・`<spark>`が反映する。スクリプトは書かない |
| [`html_menu`](html_menu/html_menu_dll.cpp) | UIの操作 → C++が反応（html_hudの逆向き）。UIパネルを触るとC++が描く図形が変わる（色 / 形 / 数 / 縁取り・拡大トグル / リセットconfirm）。UI→C++は`dispatch(...)`の信号のみ、状態はC++所有、スクリプトは書かない |
| [`observe`](observe/observe_dll.cpp) | MITIRU_REFLECT + watch：状態を外から観測 |
| [`rewind`](rewind/rewind_dll.cpp) | 巻き戻し：状態を1つのstructに置き、見たい値を申告するだけで過去へ戻せる。`--inspect rewind`付きで起動 |
| [`restart_save`](restart_save/restart_save_dll.cpp) | `hud.requestRestart()` +セーブ / ロード(`.mslot`ファイル) |
| [`objects_kitchen`](objects_kitchen/objects_kitchen_dll.cpp) | クラスとコンポーネントで書く:`MITIRU_GAME_OBJECTS`(進行データだけ POD、場面は普通の C++。[解説](../docs/OBJECT_STYLE_GAMES.md)) |
| [`physics_rewind`](physics_rewind/physics_rewind_dll.cpp) | 物理エンジンの世界ごと巻き戻す：積んだ箱に人形(ragdoll)が落ちて崩れる。Jolt の世界は状態の struct の外に置き、`MITIRU_SIDE_STATE`で保存と復元の窓口として預けるので、`--inspect rewind`で崩れる前へ戻すと箱も人形も戻る。Spaceで人形を蹴る([解説](../docs/SIDE_STATE.md)) |
| [`scene3d`](scene3d/scene3d_dll.cpp) | GPU 3Dシーン：平行光の影 + WBOIT半透明 + skybox |
| [`model3d`](model3d/model3d_dll.cpp) | 大きな3Dモデル：26万ポリゴンの宮殿(Crytek Sponza、glTF)を`drawModel` 1行でそのまま置き、一人称で歩き回る。.gltf/.glb/.obj/.fbxは初回だけ隣に変換キャッシュを作って読む。マウス視線(`hud.lockMouse`)とWASD移動、詳細度(LOD)は距離から自動 |
| [`anim3d`](anim3d/anim3d_dll.cpp) | キャラクターを歩かせる：リグ付きglTF (Khronos Fox)の姿勢を「どのクリップを何秒で、どれだけ混ぜるか」の数字 (`AnimPoseParams`) で`drawModelPose`に渡すと骨格アニメが動く。WASDで歩かせると待機と歩きがなめらかに混ざり、頭はまわりを飛ぶ光の玉を目で追う (`AnimIkRequest`)。姿勢も IK も DLL が当たり判定に使うのと同じ計算で描かれ、時間は自分の状態で`t += dt`するだけなので巻き戻しにもそのまま乗る |
| [`lights3d`](lights3d/lights3d_dll.cpp) | 夜の広場に灯りを置く：100本の柱を`drawMeshInstanced` 1回で描き、色の違う6つの`pointLight3D`が柱の間を回る。上からの`spotLight3D`は首を振りながら柱の影を落とす。Spaceで灯りを止める |
| [`navmesh`](navmesh/navmesh_dll.cpp) | 壁を回り込んで歩く(経路探索)：レベルの`.obj`をビルドの一段で`mitiru_navbake`がナビメッシュに焼き、DLLが`nav::NavMesh`で読んで`findPath`する。床をクリックするとそこへ、放っておくと4隅を巡回。描画と焼きが同じ`.obj`を読むので、見える壁と避ける壁はずれない([解説](../docs/NAVMESH.md)) |
| [`enemy_ai`](enemy_ai/enemy_ai_dll.cpp) | 敵の AI：見つけた敵がプレイヤーを囲み、同時に攻めるのは 2 体まで(攻撃トークン)。赤く光って構えてから突く。壁の向こうの敵にはプレイヤーが見えず、手を叩く(Space)と物音を聞いて、最後に知った位置を探しに来る。行動はJSONのビヘイビアツリー(`assets/enemy.json`)、経路は箱の表からDLLの中で焼いたナビメッシュ、敵どうしはぶつからない速度(ORCA)で歩く。敵の状態もすべて数値でゲームの全状態に入るので、巻き戻しと録画リプレイにそのまま乗る([解説](../docs/GAME_AI.md)) |
| [`crowd`](crowd/crowd_dll.cpp) | 大勢の敵を群衆で歩かせる：120 体が 3 本の筋に分かれて広場を回り続け、互いに避けて歩く(DetourCrowd)。真ん中の扉は 6 秒ごとに開け閉めし、閉まるとナビメッシュのその周りだけを作り直して、右へ向かう流れが手前の脇道へ回る。Spaceで扉を手で開け閉めする。群衆と扉は`MITIRU_SIDE_STATE`の窓口で預けるので、`--inspect rewind`で扉が閉まる前へ戻せる([解説](../docs/GAME_AI.md#群衆-navcrowd)) |
| [`rollback_duel`](rollback_duel/rollback_duel_dll.cpp) | 1台のキーボードを2人で分けるテニス(1P = W/S、2P = ↑/↓)。状態を全部GameMemoryに置き入力だけで決まるので、回線を知らないままロールバックのオンライン対戦に載る。`mitiru_rollback`(opt-in)が2つ読んで遅延つきloopbackで対戦させ、一致を確かめる([解説](../docs/ROLLBACK_NETCODE.md)) |
| [`effekseer_fx`](effekseer_fx/effekseer_fx_dll.cpp) | Effekseerのエフェクト(`.efkefc`)を、出してからの経過秒を`drawModel`に渡して描く。いつ・どこで出したかはGameMemoryにあるので、巻き戻すと過去の時刻の姿が出る。Spaceで撃つ、放っておくと1秒ごとに撃つ(opt-in `-DMITIRU_WITH_EFFEKSEER=ON`、[解説](../docs/EFFEKSEER.md)) |

## Subsystem単独起動デモ：[`subsys/`](subsys/)

各subsystemをengine全体なしで立ち上げる単体exe ([`docs/SUBSYSTEMS.md`](../docs/SUBSYSTEMS.md))。
`mitiru renderer` / `audio` / `input` / `scene` CLIコマンドがlaunchするのはこれら。
exeは従来どおり`build/examples/mitiru_subsys_<name>/`に出る。

| Example | 何を見せるか |
|---|---|
| `subsys/mitiru_subsys_renderer` | rendererのみ(UI / audio 無し、cold-start < 1s)。grid +往復rectの視覚smoke |
| `subsys/mitiru_subsys_audio` | audioのみ。440Hz sine +実出力RMSのレベルメーター(5秒で自動終了) |
| `subsys/mitiru_subsys_input` | inputのみ。256 VKのlive grid + mouse状態 + pressログ |
| `subsys/mitiru_subsys_scene` | scene loopのみ。12 entityの積分 +縁反射 |

## デバッグ再現プローブ

カリキュラムの章ではなく、特定 bug の再現・修正の効き目を数値で確認するための最小デモ。

| Example | 何を見せるか |
|---|---|
| [`plane_probe`](plane_probe/plane_probe_dll.cpp) | #56 (「plane が描画されない」)の再現プローブ。固定カメラで cube / plane / 薄い cube を並べて1枚撮り、`--capture-dir`のPNGでどの形が画素になったかを数えで判定する |

## インフラ(host / tool)：`apps/`所在

製品・開発基盤は [`apps/`](../apps/)にある(examplesは教材とデモ専用)。host本体
`mitiru_host`のほか`mitiru_launcher` / `mitiru_dev_companion` / `mitiru_start` /
`mitiru_tool`。詳細は各dirと`docs/`。

## 最小の書き方

`MITIRU_GAME(T)`のTにはゲームの全状態を入れ、まるごとコピーできる形(flat POD、
trivially copyable)に保つ。`std::vector` / `std::string`の代わりに`mitiru::FixedVec` /
`mitiru::FixedString`を使う。hostが状態をbytesのまま記録し、巻き戻しと録画リプレイに
そのまま使うためだ。詳細は [`docs/GETTING_STARTED.md`](../docs/GETTING_STARTED.md)
と`include/mitiru/module/Game.hpp`冒頭のコード例。
