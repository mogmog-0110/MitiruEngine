# Tool Windows: 独立ウィンドウのデバッグツール

MitiruEngine のデバッグ・観察ツール (inspector / 巻き戻し / scene tree / replay / perf / mixer / input /
scene view / why / frame anatomy / side state / ai / nav / anim) は、ゲーム本体とは別の OS ウィンドウとして立ち上がる。
中身は全て 1 つの汎用ホスト `mitiru_tool --page <name>` が描く RML / RCSS (RmlUi、ADR 0051) で、
`--page <name>` が `apps/mitiru_tool/assets/<name>.rml` に対応する。窓はエンジンの Dx12Device の上に
小さく開き、動作中ゲームの SharedSnapshot を 30Hz で読んで data model へ写す。この文書はどう開くかと、
どう増やすかをまとめる。

## 基本思想: 欲しい窓の行だけ書く

- ツールウィンドウを開く判断は **host を書く人 (`main.cpp`) が C++ で持つ**。「このデバッグ機能を
  使いたいから、この行を書く」 → 書いた窓だけ出る。要らなければ何も書かない = 何も出ない
  (pulled UI)。
- **ゲームのキー入力には割り当てない**。実ゲームは全キーを gameplay に使うので、ツールウィンドウを
  ゲーム内キーで開くとキー設計と競合する。トリガーは常にゲーム入力の外 (host コード / CLI)。
- ゲーム窓にドックする窓は、開いた時もクリックされた時もフォーカスを取らない (WS_EX_NOACTIVATE)。
  キーで操作する rewind だけが、人がバーを掴むか ⏸ を押した時に自分から前面へ出てキーを受け、▶ でゲームの窓へ返す。
- 観測窓は読み取り専用。動作中ゲームが push する観察データ (snapshot) を描くだけで、ゲーム側の state を
  書き換えたり、ゲームを freeze させたりしない。ゲームへ届くのは次の 2 つだけ:
  - rewind の巻き戻しの要求 (ScrubControlChannel。戻すのは host)
  - scene view / why / frame anatomy がゲームの HTTP API (`--http-port`) に頼む問い合わせ。
    scene view の「試す / 残す / 捨てる」(ADR 0035 の分岐エディタ) もこの経路で、窓は自分では書かない

## 開き方

### ゲームコードから

```cpp
hud.open(mitiru::Tool::Perf);   // Game.hpp の update() 内など
```

### host コードから (正面)

```cpp
#include <mitiru/debug/InspectorLauncher.hpp>

// main.cpp、engine.runModule(...) の直前あたり:
mitiru::debug::openTool(mitiru::Tool::Inspector);   // 状態 inspector
mitiru::debug::openTool(mitiru::Tool::Rewind);      // 巻き戻しウィンドウ
// 要らない窓は書かない。
```

`openTool` は spawn に成功すると `true`、ホストが見つからなければ `false` (無害な no-op)。
特定ファイルを渡す窓 (replay) には引数付き版:

```cpp
mitiru::debug::openTool(mitiru::Tool::Replay, "--mtrr run.mtrr");
```

`mitiru_tool.exe` は host と同じフォルダか、開発中のビルド先 (`build/apps/mitiru_tool/`) から探す。
環境変数 `MITIRU_TOOL_EXE` で場所を指定してもよい。古い DLL が `tool_cef` という名前で頼んでも
`mitiru_tool` が開く。

### 参照 host の CLI から

`mitiru_host` は `--inspect <name>` で起動時にツールウィンドウを開ける (複数指定可):

```
mitiru_host game.dll --inspect inspector --inspect perf
```

`name` = `inspector` / `input` / `rewind` / `scene` / `perf` / `mixer` / `scene_view` / `why_view` / `frame_view` /
`side_state` / `ai` / `nav` / `anim` / `story` / `statemachine` / `stage_edit` (引数が要る。[STAGE_EDIT.md](STAGE_EDIT.md)) / `motion_tune` (引数が要る。[MOTION_TUNE.md](MOTION_TUNE.md))。
`scene?tab=memory` のように `?` の後ろを付けると、ページへそのまま渡す (scene は game memory の tab で開く。
ai は `ai?tree=<木の JSON>`、nav は `nav?mesh=<.navmesh か .navcache>` を渡すと、DLL が渡した資産より優先する)。

### CLI から

```
mitiru inspect [pid]
```

`--inspectable input|rewind` で開く page を指定、`--all` で全窓を開く。

### 窓を出さずに撮る

```
mitiru_tool --page perf --file snapshot.json --capture perf.png [--frames 60] [--size 400x620] [--dp 1.5]
mitiru_tool --page why_view <pid> --http-port 8090 --capture why.png --capture-input "10:click:150,72;14:type:player.hp;18:key:13"
```

`--capture` は windowless の Dx12Device に描いて PNG を書き、窓は作らない。見た目は Windows のアプリのテーマ (ライト / ダーク) に従い、環境変数 `MITIRU_UI_THEME=light|dark` で決め打てる。`--capture-input` は
本物の窓と同じ口からクリック・ドラッグ・文字・キーを渡す (`フレーム:click:X,Y` / `フレーム:down:X,Y` /
`フレーム:move:X,Y` / `フレーム:up:X,Y` / `フレーム:type:文字` / `フレーム:key:仮想キーコード`)。down から up までは押したまま。

## 用意されている窓

| Tool | page | 見るもの |
|---|---|---|
| `Perf` | `--page perf` | fps / frameMs + 折れ線グラフ (60fps の基準線つき)。3D を描く game はパスごとの GPU 時間 (main / sky / fog / atmosphere / 後処理 / upscale / lights) も |
| `Inspector` | `--page inspect` | ゲームが `hud.watch()` で出した観察データ全部 (HP / score 等)。MITIRU_ENUM / MITIRU_FIELD_RANGE は動かせない select / スライダー、MITIRU_FIELD_GROUP は畳める組 |
| `SceneTree` | `--page scene` | 観察データの階層構造を tree 表示 (開閉) と game memory の tab |
| `Rewind` | `--page rewind` | ゲーム窓の下に付くシークバー。掴むと止まってそのフレームへ戻り、▶ でそこから続ける (窓を閉じても続きから動く)。掴んだ後は ←/→ で 1 フレームずつ、Home/End で端へ動く。節目に重ねると名前が出る。`hud.timelineMarker` (ABI v50) のカットシーンの区間は名前つきの帯で出る |
| `Replay` | `--page replay` | 入力記録ファイル (`.mtrr`) を frame 単位でコマ送り (← / → / Home / End、バーのクリック) |
| `AudioMixer` | `--page mixer` | master volume + 再生中チャンネルの per-channel VU + voice 一覧 (`hud.voice()` の id / 実ファイル名 / gain / pan / 残り秒) |
| `InputMonitor` | `--page input` | 生の入力値 |
| `SceneView` | `--page scene_view` | ゲーム画面 + オブジェクト枠。ドラッグや候補で分岐を試し、残す / 捨てる (ADR 0035、`docs/BRANCH_EDITOR.md`) |
| `WhyView` | `--page why_view` | field を打って、最後に書いた phase と値の推移を見る (ADR 0035 O6) |
| `FrameView` | `--page frame_view` | 1 フレームの解剖図 (入力 → 書かれた field → 描画 → 音、`docs/FRAME_ANATOMY.md`) |
| `SideState` | `--page side_state` | GameMemory の外に持つ状態の窓口 (ADR 0054) ごとの形の番号・bytes・hash、記録のリング。`--replay` / `--replay-test` の照合中は、窓口ごとに食い違ったフレームを帯に印で出す |
| `Ai` | `--page ai` | 敵を選び、ビヘイビアツリーのノードの結果 (running / success / failure)、知覚の記憶、攻撃トークンの持ち主を見る |
| `Nav` | `--page nav` | 真上から見た地図 (右が +x、下が +z) に、ナビメッシュの床、群衆の agent と速度、障害物の開け閉め |
| `Anim` | `--page anim` | モデルを選び、姿勢のレイヤ (クリップ、時刻、重み)、状態機械の今の状態・クロスフェード・param、このフレームに通ったイベント、ルートモーションの差分を見る |
| `Story` | `--page story` | カットシーンの再生位置、会話の位置と台本、旗の値、クエストの進みを名前で見る (`narrative::StoryInspect` を MITIRU_INSPECT_ASSETS で渡す、ABI v50) |
| `StageEdit` | `--page "stage_edit?schema=...&file=..."` | ステージの JSON を上からの図と 3D の図で組む編集の窓。試す / 残す / 捨てる ([STAGE_EDIT.md](STAGE_EDIT.md)、ADR 0082) |
| `MotionTune` | `--page "motion_tune?schema=..."` | 調整値の JSON をスライダーと数で調整し、動いているゲームに入れて確かめ、同じ入力で流しなおした軌跡を元の値と重ねる。試す / 残す / 捨てる ([MOTION_TUNE.md](MOTION_TUNE.md)、ADR 0082) |
| `StateMachine` | `--page statemachine` | ゲームが宣言した状態機械を図にして、今の状態、遷移の回数、直近の経過の帯を見る。宣言に無い遷移は赤 (ADR 0082) |

## ゲームが見せる型 (ai / anim / nav)

ai / anim / nav の窓は、GameMemory に置いたエンジンの型を読む。game は `mitiru/module/ReflectEngineTypes.hpp` を
include し、その型の field を `MITIRU_REFLECT` (または `MITIRU_REFLECT_STRUCT` の要素) にそのまま並べる。host は
記述子の elemType (`mitiru.BtState` など) と大きさが合うときだけ値を解き、gameMemory の JSON に `"$type"` 付きで出す。
窓は木のどこに置かれていても型で見つける。`/api/ai/state` も同じ形を返す。ABI は変わらない (古い host は解けない値を飛ばす)。

| 型 | 窓 | 同じ持ち主として読むもの |
|---|---|---|
| `gameai::BtState` | ai (敵 1 体ごと) | `PerceptionMemory` |
| `gameai::AttackTokenPool<H, R>` | ai | |
| `animation::AnimPoseParams` | anim (モデル 1 つごと) | `FixedVec<AnimEventHit, N>` (このフレームのイベント)、`YawXform` (ルートモーションの差分) |
| `animation::AnimGraphState` | anim (状態機械 1 つごと) | 同じ持ち主の `AnimPoseParams` も並べて出す。名前は `"animgraph"` の資産 ([ANIM_GRAPH.md](ANIM_GRAPH.md)) |
| `nav::CrowdAgentView`、`nav::NavObstacle` | nav | |
| `sgc::Vec3f` | (どの窓でも `[x, y, z]`) | |

持ち主は field の名前の最後の `.` より前で決まる。`e[0].bt` と `e[0].mind`、`hero.pose` と `hero.events` は同じ持ち主になる。

```cpp
#include <mitiru/module/ReflectEngineTypes.hpp>
MITIRU_REFLECT(EnemyAi, tokens, e[0].bt, e[0].mind, e[1].bt, e[1].mind);   // examples/enemy_ai
MITIRU_REFLECT(CrowdDoor, agents, door);                                   // examples/crowd
```

木の形 (`BtTree`) とナビメッシュは GameMemory に無い (DLL の static に置く)。DLL が `MITIRU_INSPECT_ASSETS` (ABI v49) で
木の JSON (`"bt_tree"`) と焼いたナビメッシュ (`"navmesh"`) を渡すと、host が読み込みの時に 1 度だけ snapshot の隣のフォルダへ写し、
ai の窓が木の種類と葉の名前、nav の窓が床の形を出す。木が複数あれば、敵の結果が収まる木のうちノードの最も少ない木を使う。
木の JSON は `BtTree` と同じ前順なので、ノード番号で結果と突き合わせる。

```cpp
std::int32_t inspectAssets(mitiru::module::InspectAsset* out, std::int32_t cap)   // examples/enemy_ai
{
	const mitiru::module::InspectAsset all[2] = {{"bt_tree", "enemy", treeJson.data(), treeJson.size()},
	                                             {"navmesh", "garden", navBlob.data(), navBlob.size()}};
	const std::int32_t n = std::min<std::int32_t>(cap, 2);
	std::copy(all, all + n, out);
	return n;
}
MITIRU_INSPECT_ASSETS(inspectAssets);
```

anim の窓は、DLL が `AnimGraphFile::description()` (状態・param・イベントの名前の表) を `"animgraph"` として渡すと、
状態機械の番号を名前で出す。表はグラフの名前の鍵 (`AnimGraphState::graphKey`) で状態と組にする (見本は `examples/anim3d`)。

ファイルを窓に直接渡すこともできる (`ai?tree=<JSON>`、`nav?mesh=<焼いたファイル>`、`anim?graph=<x.animgraph.json>`)。渡せばそちらを使う。

## 増やし方

開ける窓の単一の真実は `include/mitiru/debug/ToolRegistry.hpp` の `Tool` enum + `kToolTable`。
増設は 1 パターンだけ:

1. `Tool` enum に値を 1 つ足す。
2. `kToolTable` に 1 行足す: `{ Tool::MyTool, "tool", "--page mytool" }`。
3. `apps/mitiru_tool/assets/mytool.rml` を書く (共通の見た目は `assets/ui/tool_look.rcss`。部品の一覧と使い方は [TOOL_LOOK.md](TOOL_LOOK.md))。
4. ページの振る舞いを `apps/mitiru_tool/ui/pages/` に 1 つ書き、`PageFactory.cpp` の表に足す。
   snapshot を RML が並べられる値 (文字・割合・配列) に直して `ToolView::set` で写すだけにする。

新しい exe は不要。RML は `data-model="tool"` の値を `{{ }}` / `data-for` / `data-if` で引き、操作は
`data-event-click="dispatch('名前', 'キー', 値)"` でページの `onAction` に届く。canvas の代わりが要る
部品 (折れ線・ゲーム画面の絵) は C++ の要素で描く (`<spark>`、`<liveimage>`)。

ゲームが自分のページを持つときは、game DLL の `assets/<name>.rml` に置いて `hud.open("tool", "--page <name>")`
で開く。host が `MITIRU_ASSET_ROOT` を渡すので `mitiru_tool` がそこを探し、snapshot をそのまま
`{{ snap.<節>.state.<値> }}` で見せる。

## ツールウィンドウ ≠ デバッグメニュー

デバッグメニュー ([DEBUG_MENU.md](DEBUG_MENU.md)、ADR 0085) は、ゲームの窓に重なる「項目を選ぶ」道具 (F1)。ゲームが申告した項目 (ステージを選ぶ、値を動かす) と、
エンジンの項目 (止める・コマ送り・速さ・画面の保存) が並び、上の窓のうち引数の要らない物を開く入口も持つ。窓は見る・編集するための別の process、
メニューはゲームへ頼むだけの画面で、どちらも既定では何も開かない。

## ツールウィンドウ ≠ subsystem 単独起動

ツールウィンドウ (上記) は「動作中ゲームを観察する別窓」。これと別に、subsystem 単独起動
(1 サブシステムだけを単体で走らせる) がある。これはデバッグウィンドウではなく、`mitiru_subsys_*` を
`mitiru audio | input | renderer | scene` (+ 決定的な record/playback の `mitiru replay`) で起動するもの。
両者は混同しない。

## ツールウィンドウ ≠ --ghost / --replay (mitiru_host 自身の GUI 機能)

これも別窓 (`mitiru_tool`) ではなく、**同じゲームウィンドウの中**に既存の録画を重ねる `mitiru_host` 本体の
CLI フラグ。

- `--ghost <f.mtrr>` — 同じ game DLL をもう 1 本ロードし、`.mtrr` の入力を 1 フレームずつ流し込んで
  live の直前に描く (10-1)。ghost 用 GameMemory は live と独立 (hot reload / rewind ring / HTTP は無し)。
- `--replay <f.mtrr>` — `--replay-test` と同じ決定的再生パスを GUI 窓で流す。EOF で停止して最後の
  フレームのまま静止する (9-3b)。

両方とも `--ghost` / `--replay` 単体で `mitiru_host game.dll --ghost run.mtrr` のように使う。

## 状態機械の窓 (statemachine)

ゲームが持つ状態機械を名前つきの図で見る、読み取り専用の窓。ABI は変えず、`MITIRU_INSPECT_ASSETS` の kind `"statemachine"` で宣言を渡す。
名前と遷移はデータのファイル、GameMemory の中の位置と型は C++ に置く。

データのファイル (素の JSON、コメントは書かない):

| 鍵 | 意味 |
|---|---|
| `machines[].id` | 機械の名前。C++ の置き場 (`MachineBinding::id`) と同じ文字列で組にする |
| `machines[].label` | 窓に出す名前 |
| `machines[].states[]` | `{"v": 番号, "name": 名前}`。番号は列挙型の値で、抜けていてよい |
| `machines[].edges[]` | `{"from": 番号, "to": 番号}`。通れる遷移。空なら起きた遷移だけを図に足す |

C++ の置き場:

```cpp
#include <mitiru/debug/StateMachineInspect.hpp>

std::vector<mitiru::debug::MachineBinding> bindings = {
    { "move", offsetof(Game, mode), mitiru::debug::StateValueType::U8 },
};
mitiru::debug::StateMachineInspect sm;
std::string error;
sm.load(jsonText, bindings, error);   // 置き場の無い機械と、JSON に無い置き場は error に書いて飛ばす
// sm.text() を InspectAsset の data に、sm.asset() を out[0] に書く
```

型は `U8` `U16` `U32` `I32` `F32`。`F32` は列挙型の番号を float で持つ計器 (Probe) の値を四捨五入して読む。
host は更新のたびに置き場を見て、機械ごとに遷移 192 件と、(前, 後) の組 128 までを覚える。窓は図、起きた遷移の表、
選んだ時間 (1 / 5 / 15 / 60 秒) の経過の帯を出し、宣言に無い遷移は赤の線と数で示す。

## ステージ制作の窓 (stage_edit)

読む窓ではなく、編集する窓 (ADR 0082)。ゲームが渡した編集スキーマ (JSON) に従って、ステージの JSON を図の上で組み、「試す / 残す / 捨てる」でゲームとファイルへ渡す。
ゲームの形式はエンジンに入らない。開き方、スキーマの書き方、台本は [STAGE_EDIT.md](STAGE_EDIT.md)。この窓だけは、ゲームの窓の脇に吸着せず、ゲームの pid が無くても開ける (`pageStandsAlone`)。
窓の再選択: 閉じた窓は、F8 のメニューなどでもう一度頼めば開き直す。毎フレーム頼み続けるゲームでは、閉じた窓をすぐには戻さない (`ToolSpawnLedger`)。
`--no-activate` (環境変数 `MITIRU_TOOL_NO_ACTIVATE=1`) を付けると、窓は前面に出ずフォーカスも取らない (窓を人の画面に出さずに確かめるとき。`--window-pos` で仮想ディスプレイに置く)。

## 調整値の窓 (motion_tune)

編集する窓の 2 つ目 (ADR 0082)。ゲームが渡すスキーマ (JSON) と、GameMemory に載せた調整値の写し (quiet の欄) だけで動く。
スライダーと数で値を直し、同じ入力の記録から流しなおした軌跡を元の値と重ねて見せ、試す (動いているゲームの GameMemory に入れる)、
残す (JSON に書く)、捨てる (ゲームも元の値に戻す) を選ぶ。開き方、HTTP の口、スキーマ、台本は [MOTION_TUNE.md](MOTION_TUNE.md)。
ゲームの HTTP API に話しかける。ゲームから頼まれた (F8 のメニューなど) ときは、API が閉じていれば host が `127.0.0.1` の空いている port で開き、その port を窓へ `--http-port` で渡す。
窓が全部閉じたら、host が開いた API だけを閉じる。`--http-port` や `MITIRU_AI=1` で利用者が開いた API には触らず、`mitiru_tool.exe` が見つからない出荷版では開かない。
API を要る窓は、`kToolTable` の `needsHttp` で宣言する (`debug/ToolHttpDemand.hpp`)。手で `mitiru_tool --page motion_tune` を開くときだけ、ゲームを `--http-port` か `MITIRU_AI=1` で起動しておく。
`mitiru_host --headless` は、`--pace-fps` を明示すると実時間で進むので、窓から触る確かめに使える。
