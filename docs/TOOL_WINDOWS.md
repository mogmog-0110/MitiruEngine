# Tool Windows — 独立ウィンドウのデバッグツール

MitiruEngine のデバッグ・観察ツール (inspector / time-travel / scene tree / replay /
perf / mixer) は、ゲーム本体とは **別の OS ウィンドウ** として立ち上がる独立プロセスです。
この文書は **どう開くか** と **どう増やすか** をまとめます。

## 基本思想: 欲しい窓の行だけ書く

- ツール窓を開く判断は **host を書く人 (`main.cpp`) が C++ で持つ**。「このデバッグ機能を
  使いたいから、この行を書く」 → 書いた窓だけ出る。要らなければ何も書かない = 何も出ない
  (pulled UI)。
- **ゲームのキー入力には割り当てない**。実ゲームは全キーを gameplay に使うので、ツール窓を
  ゲーム内キーで開くとキー設計と競合する。トリガーは常にゲーム入力の外 (host コード / CLI)。
- ツール窓は読み取り専用。動作中ゲームが書き出す観察データ (snapshot) を polling して描く。
  ゲーム側の state を書き換えたり、ゲームを freeze させたりしない。

## 開き方

### host コードから (正面)

```cpp
#include <mitiru/debug/InspectorLauncher.hpp>

// main.cpp、engine.runModule(...) の直前あたり:
mitiru::debug::openTool(mitiru::Tool::Inspector);   // 状態 inspector
mitiru::debug::openTool(mitiru::Tool::TimeTravel);  // タイムトラベル scrubber
// 要らない窓は書かない。
```

`openTool` は spawn に成功すると `true`、対応 exe が見つからなければ `false` (無害な no-op)。
特定ファイルを渡す窓 (replay) には引数付き版:

```cpp
mitiru::debug::openTool(mitiru::Tool::Replay, "run.mtrr");
```

### 参照 host の CLI から

`mitiru_host` は `--inspect <name>` で起動時にツール窓を開けます (複数指定可):

```
mitiru_host game.dll --inspect inspector --inspect perf
```

`name` = `inspector` / `input` / `timetravel` / `scene` / `perf` / `mixer`。

### CLI スタンドアロン

各ツールは `mitiru_<tool>.exe <pid>` で単独起動もできます (例 `mitiru_inspector 12345`)。
replay は pid でなくファイル: `mitiru_replay run.mtrr`。

## 用意されている窓

| Tool | exe | 見るもの |
|---|---|---|
| `Inspector` | `mitiru_inspector` | ゲームが `hud.watch()` で出した観察データ全部 |
| `InputMonitor` | `mitiru_inspector --inspectable input` | 入力にフォーカスした inspector |
| `TimeTravel` | `mitiru_inspector --inspectable timetravel` | scrub bar + HP グラフ (観察窓内ローカル scrub) |
| `SceneTree` | `mitiru_scene_tree` | 観察データの階層構造を tree 表示 |
| `Replay` | `mitiru_replay` | `.mtrr` 録画を frame 単位で scrub (入力 / rngSeed) |
| `Perf` | `mitiru_perf` | fps / frameMs + 折れ線 (host が併記) |
| `AudioMixer` | `mitiru_mixer` | master volume + 再生中チャンネルの per-channel VU (host が併記) |

## 増やし方

開ける窓の単一の真実は `include/mitiru/debug/ToolRegistry.hpp` の `kToolTable` です。
増設には 2 つの経路があります。

### 経路 A: inspector に観察項目を足す (安い・観察系)

ゲームが `hud.watch("myview", "My View", json)` でデータを出し、`kToolTable` に
`{ Tool::MyView, "inspector", "--inspectable myview" }` を 1 行足すだけ。inspector が
generic な key/value tree として描きます。新しい exe は不要です。

### 経路 B: 新しいツール exe を作る (別の関心事)

inspector の汎用ビューアに収まらない、別の見せ方・別のデータ源を持つツールは独立 exe にします。
観察系 (動作中ゲームの snapshot を読む) なら `ToolWindowApp` 基底に乗せれば本体は最小です:

```cpp
#include <mitiru/debug/ToolWindowApp.hpp>

class MyTool final : public mitiru::debug::ToolWindowApp {
public:
    using ToolWindowApp::ToolWindowApp;
    const char* windowTitle() const noexcept override { return "my tool"; }
    void drawBody(mitiru::Screen& s, const nlohmann::json& snapshot) override {
        // snapshot["mysection"]["state"] を読んで描く
    }
};

int main(int argc, char** argv) {
    auto a = mitiru::debug::parseToolArgs(argc, argv);   // <pid> | --file <path>
    if (!a.ok) { return 2; }
    MyTool tool(a.pid, a.file);
    return mitiru::debug::runToolWindow(tool, "MitiruEngine — my tool");
}
```

そして `examples/mitiru_my_tool/CMakeLists.txt` を足し、`examples/CMakeLists.txt` に
`add_subdirectory(mitiru_my_tool)`、`kToolTable` に `{ Tool::MyTool, "my_tool", "" }` を 1 行。
host は `mitiru_my_tool.exe` を自動で探して spawn します。

ファイル駆動など snapshot を読まないツール (replay scrubber 等) は `ToolWindowApp` に
無理に乗せず、`mitiru::Game` を直接継承して `runToolGame()` で起動します
(配色・ウィンドウ設定だけ共有)。

## 描画の注意

ツール窓のフォントは Latin atlas です。**描画する文字列は ASCII に限ってください**
(日本語は tofu □ になります)。ソースコードのコメントは日本語で構いません。
