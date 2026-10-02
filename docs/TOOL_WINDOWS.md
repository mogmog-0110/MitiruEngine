# Tool Windows: 独立ウィンドウのデバッグツール

MitiruEngine のデバッグ・観察ツール (inspector / 巻き戻し / scene tree / replay / perf / mixer / input /
scene view / why / frame anatomy) は、ゲーム本体とは別の OS ウィンドウとして立ち上がる。
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

`name` = `inspector` / `input` / `rewind` / `scene` / `perf` / `mixer` / `scene_view` / `why_view` / `frame_view`。
`scene?tab=memory` のように `?` の後ろを付けると、ページへそのまま渡す (scene は game memory の tab で開く)。

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

`--capture` は windowless の Dx12Device に描いて PNG を書き、窓は作らない。`--capture-input` は
本物の窓と同じ口からクリック・文字・キーを渡す (`フレーム:click:X,Y` / `フレーム:type:文字` / `フレーム:key:仮想キーコード`)。

## 用意されている窓

| Tool | page | 見るもの |
|---|---|---|
| `Perf` | `--page perf` | fps / frameMs + 折れ線グラフ (60fps の基準線つき) |
| `Inspector` | `--page inspect` | ゲームが `hud.watch()` で出した観察データ全部 (HP / score 等)。MITIRU_ENUM / MITIRU_FIELD_RANGE は動かせない select / スライダー、MITIRU_FIELD_GROUP は畳める組 |
| `SceneTree` | `--page scene` | 観察データの階層構造を tree 表示 (開閉) と game memory の tab |
| `Rewind` | `--page rewind` | ゲーム窓の下に付くシークバー。掴むと止まってそのフレームへ戻る。節目に重ねると名前が出る |
| `Replay` | `--page replay` | 入力記録ファイル (`.mtrr`) を frame 単位でコマ送り (← / → / Home / End、バーのクリック) |
| `AudioMixer` | `--page mixer` | master volume + 再生中チャンネルの per-channel VU + voice 一覧 (`hud.voice()` の id / 実ファイル名 / gain / pan / 残り秒) |
| `InputMonitor` | `--page input` | 生の入力値 |
| `SceneView` | `--page scene_view` | ゲーム画面 + オブジェクト枠。ドラッグや候補で分岐を試し、残す / 捨てる (ADR 0035、`docs/BRANCH_EDITOR.md`) |
| `WhyView` | `--page why_view` | field を打って、最後に書いた phase と値の推移を見る (ADR 0035 O6) |
| `FrameView` | `--page frame_view` | 1 フレームの解剖図 (入力 → 書かれた field → 描画 → 音、`docs/FRAME_ANATOMY.md`) |

## 増やし方

開ける窓の単一の真実は `include/mitiru/debug/ToolRegistry.hpp` の `Tool` enum + `kToolTable`。
増設は 1 パターンだけ:

1. `Tool` enum に値を 1 つ足す。
2. `kToolTable` に 1 行足す: `{ Tool::MyTool, "tool", "--page mytool" }`。
3. `apps/mitiru_tool/assets/mytool.rml` を書く (共通の見た目は `tool.rcss`)。
4. ページの振る舞いを `apps/mitiru_tool/ui/pages/` に 1 つ書き、`PageFactory.cpp` の表に足す。
   snapshot を RML が並べられる値 (文字・割合・配列) に直して `ToolView::set` で写すだけにする。

新しい exe は不要。RML は `data-model="tool"` の値を `{{ }}` / `data-for` / `data-if` で引き、操作は
`data-event-click="dispatch('名前', 'キー', 値)"` でページの `onAction` に届く。canvas の代わりが要る
部品 (折れ線・ゲーム画面の絵) は C++ の要素で描く (`<spark>`、`<liveimage>`)。

ゲームが自分のページを持つときは、game DLL の `assets/<name>.rml` に置いて `hud.open("tool", "--page <name>")`
で開く。host が `MITIRU_ASSET_ROOT` を渡すので `mitiru_tool` がそこを探し、snapshot をそのまま
`{{ snap.<節>.state.<値> }}` で見せる。

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
