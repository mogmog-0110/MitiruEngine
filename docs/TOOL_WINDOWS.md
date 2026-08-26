# Tool Windows: 独立ウィンドウのデバッグツール

MitiruEngine のデバッグ・観察ツール (inspector / 巻き戻し / scene tree / replay /
perf / mixer / input) は、ゲーム本体とは 別の OS ウィンドウ として立ち上がります。
中身は全て 1 つの汎用 CEF ホスト (`mitiru_tool_cef --page <name>`) が描く HTML/CSS で、
`--page <name>` が `assets/<name>.html` に対応します。動作中ゲームの SharedSnapshot が
毎フレーム push されます。この文書はどう開くかと、どう増やすかをまとめます。

## 基本思想: 欲しい窓の行だけ書く

- ツールウィンドウを開く判断は **host を書く人 (`main.cpp`) が C++ で持つ**。「このデバッグ機能を
  使いたいから、この行を書く」 → 書いた窓だけ出る。要らなければ何も書かない = 何も出ない
  (pulled UI)。
- **ゲームのキー入力には割り当てない**。実ゲームは全キーを gameplay に使うので、ツールウィンドウを
  ゲーム内キーで開くとキー設計と競合する。トリガーは常にゲーム入力の外 (host コード / CLI)。
- ツールウィンドウは読み取り専用。動作中ゲームが push する観察データ (snapshot) を描くだけで、
  ゲーム側の state を書き換えたり、ゲームを freeze させたりしない。

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
mitiru::debug::openTool(mitiru::Tool::TimeTravel);  // 巻き戻しウィンドウ
// 要らない窓は書かない。
```

`openTool` は spawn に成功すると `true`、ホストが見つからなければ `false` (無害な no-op)。
特定ファイルを渡す窓 (replay) には引数付き版:

```cpp
mitiru::debug::openTool(mitiru::Tool::Replay, "run.mtrr");
```

### 参照 host の CLI から

`mitiru_host` は `--inspect <name>` で起動時にツールウィンドウを開けます (複数指定可):

```
mitiru_host game.dll --inspect inspector --inspect perf
```

`name` = `inspector` / `input` / `timetravel` / `scene` / `perf` / `mixer`。

### CLI から

```
mitiru inspect [pid]
```

`--inspectable input|timetravel` で開く page を指定、`--all` で全窓を開きます。

## 用意されている窓

どの窓も `mitiru_tool_cef --page <name>` 1 本が描きます (中身は `assets/<name>.html`)。

| Tool | page | 見るもの |
|---|---|---|
| `Perf` | `--page perf` | fps / frameMs + 折れ線グラフ |
| `Inspector` | `--page inspect` | ゲームが `hud.watch()` で出した観察データ全部 (HP / score 等) |
| `SceneTree` | `--page scene` | 観察データの階層構造を tree 表示 (開閉) |
| `TimeTravel` | `--page timetravel` | 直近フレームを巻き戻す (履歴グラフ) |
| `Replay` | `--page replay` | 入力記録ファイル (`.mtrr`) の録画を frame 単位でコマ送りで行き来 |
| `AudioMixer` | `--page mixer` | master volume + 再生中チャンネルの per-channel VU |
| `InputMonitor` | `--page input` | 生の入力値 |

## 増やし方

開ける窓の単一の真実は `include/mitiru/debug/ToolRegistry.hpp` の `Tool` enum + `kToolTable`
です。増設は 1 パターンだけ:

1. `Tool` enum に値を 1 つ足す。
2. `kToolTable` に 1 行足す: `{ Tool::MyTool, "tool_cef", "--page mytool" }`。
3. `apps/mitiru_tool_cef/assets/mytool.html` を作る (snapshot を読んで描く HTML/CSS)。

新しい exe は不要です。全窓が `tool_cef` の page なので、HTML を 1 枚足せば窓が増えます。

## ツールウィンドウ ≠ subsystem 単独起動

ツールウィンドウ (上記) は「動作中ゲームを観察する別窓」です。これと別に、
subsystem 単独起動 (1 サブシステムだけを単体で走らせる) があります。これはデバッグウィンドウ
ではなく、`mitiru_subsys_*` を `mitiru audio | input | renderer | scene` (+ 決定的な
record/playback の `mitiru replay`) で起動するものです。両者は混同しないでください。
