# Tool Windows: 独立ウィンドウのデバッグツール

MitiruEngineのデバッグ・観察ツール(inspector / 巻き戻し / scene tree / replay /
perf / mixer / input)は、ゲーム本体とは 別のOSウィンドウ として立ち上がります。
中身は全て1つの汎用CEFホスト(`mitiru_tool_cef --page <name>`)が描くHTML/CSSで、
`--page <name>`が`assets/<name>.html`に対応します。動作中ゲームのSharedSnapshotが
毎フレームpushされます。この文書はどう開くかと、どう増やすかをまとめます。

## 基本思想: 欲しい窓の行だけ書く

- ツールウィンドウを開く判断は **hostを書く人(`main.cpp`)がC++で持つ**。「このデバッグ機能を
  使いたいから、この行を書く」 → 書いた窓だけ出る。要らなければ何も書かない = 何も出ない
  (pulled UI)。
- **ゲームのキー入力には割り当てない**。実ゲームは全キーをgameplayに使うので、ツールウィンドウを
  ゲーム内キーで開くとキー設計と競合する。トリガーは常にゲーム入力の外(hostコード / CLI)。
- ツールウィンドウは読み取り専用。動作中ゲームがpushする観察データ(snapshot)を描くだけで、
  ゲーム側のstateを書き換えたり、ゲームをfreezeさせたりしない。

## 開き方

### ゲームコードから

```cpp
hud.open(mitiru::Tool::Perf);   // Game.hpp の update() 内など
```

### hostコードから(正面)

```cpp
#include <mitiru/debug/InspectorLauncher.hpp>

// main.cpp、engine.runModule(...) の直前あたり:
mitiru::debug::openTool(mitiru::Tool::Inspector);   // 状態 inspector
mitiru::debug::openTool(mitiru::Tool::TimeTravel);  // 巻き戻しウィンドウ
// 要らない窓は書かない。
```

`openTool`はspawnに成功すると`true`、ホストが見つからなければ`false` (無害なno-op)。
特定ファイルを渡す窓(replay)には引数付き版:

```cpp
mitiru::debug::openTool(mitiru::Tool::Replay, "run.mtrr");
```

### 参照hostのCLIから

`mitiru_host`は`--inspect <name>`で起動時にツールウィンドウを開けます(複数指定可):

```
mitiru_host game.dll --inspect inspector --inspect perf
```

`name` = `inspector` / `input` / `timetravel` / `scene` / `perf` / `mixer`。

### CLIから

```
mitiru inspect [pid]
```

`--inspectable input|timetravel`で開くpageを指定、`--all`で全窓を開きます。

## 用意されている窓

どの窓も`mitiru_tool_cef --page <name>` 1本が描きます(中身は`assets/<name>.html`)。

| Tool | page | 見るもの |
|---|---|---|
| `Perf` | `--page perf` | fps / frameMs +折れ線グラフ |
| `Inspector` | `--page inspect` | ゲームが`hud.watch()`で出した観察データ全部(HP / score等) |
| `SceneTree` | `--page scene` | 観察データの階層構造をtree表示(開閉) |
| `TimeTravel` | `--page timetravel` | 直近フレームを巻き戻す(履歴グラフ) |
| `Replay` | `--page replay` | 入力記録ファイル(`.mtrr`)の録画をframe単位でコマ送りで行き来 |
| `AudioMixer` | `--page mixer` | master volume +再生中チャンネルのper-channel VU |
| `InputMonitor` | `--page input` | 生の入力値 |

## 増やし方

開ける窓の単一の真実は`include/mitiru/debug/ToolRegistry.hpp`の`Tool` enum + `kToolTable`
です。増設は1パターンだけ:

1. `Tool` enumに値を1つ足す。
2. `kToolTable`に1行足す: `{ Tool::MyTool, "tool_cef", "--page mytool" }`。
3. `apps/mitiru_tool_cef/assets/mytool.html`を作る(snapshotを読んで描くHTML/CSS)。

新しいexeは不要です。全窓が`tool_cef`のpageなので、HTMLを1枚足せば窓が増えます。

## ツールウィンドウ ≠ subsystem単独起動

ツールウィンドウ(上記)は「動作中ゲームを観察する別窓」です。これと別に、
subsystem単独起動(1サブシステムだけを単体で走らせる)があります。これはデバッグウィンドウ
ではなく、`mitiru_subsys_*`を`mitiru audio | input | renderer | scene` (+決定的な
record/playbackの`mitiru replay`)で起動するものです。両者は混同しないでください。
