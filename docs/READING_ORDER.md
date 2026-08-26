# Reading Order

Where to start, depending on who you are and what you're doing. Every link below
points to a file included in this snapshot or to the public website.

## Newcomer: human

1. **`README.md`** — タグライン + 特徴 + CLI quickstart
2. **`docs/GETTING_STARTED.md`** — `mitiru` CLI を入れて最初のプロジェクトを作る
3. **`docs/SCOPE.md`** — canonical identity statement、特徴、target user
4. **`examples/welcome/`** — 絵・文字・図形と HTML/CSS パネルを 1 画面で見せる最初のサンプル
5. **`docs/ARCHITECTURE.md`** — エンジン全体の設計、deeper dive

同梱サンプルは番号順 (welcome → shapes → text → input → …) に読むと、描画・入力・音・
カメラ・HTML の画面・3D までひととおり触れます。一覧は [`examples/README.md`](../examples/README.md)。
日常のプロジェクト作成は `mitiru new` がテンプレートを内蔵して使います。

## Newcomer: LLM (Claude Code, Codex, etc.)

1. **`docs/SCOPE.md`** — canonical engine identity、out-of-scope の明示
2. **`docs/ARCHITECTURE.md`** — レイヤー構造とモジュール依存
3. **`docs/FLAT_POD.md`** — 状態を 1 個の struct に置く核心設計 (巻き戻し・観察・AI 連携の土台)
4. **`docs/AI_WORKFLOW.md`** — 観測 API・`mitiru verify`・MCP でゲームを観て直すループ
5. **`include/mitiru/`** — header-only 実装。symbol はここを grep する

---

## エンジンの特徴を理解する

機能別の入口:

| 機能 | 入口 |
|---|---|
| HTML/CSS で UI が書ける C++ engine | `examples/html_hud/`、`docs/HYBRID_RUNTIME.md` |
| 巻き戻し | `docs/TIME_TRAVEL.md`、`docs/FLAT_POD.md` |
| 全 system 単独起動 | `docs/SUBSYSTEMS.md` |
| 録画リプレイ | `docs/AI_WORKFLOW.md`、`docs/TOOL_WINDOWS.md` |
| 別窓ツール | `docs/TOOL_WINDOWS.md` |

---

## HTML/CSS UI を書く

1. **`examples/html_hud/`** — C++ の値を HTML に流す canonical pattern (JavaScript ゼロ)
2. **`examples/html_menu/`** — HTML の操作を C++ が受ける逆向き
3. **`docs/BRIDGE_API_CONTRACT.md`** — bridge の signal-only 規約
4. **`web/mitiru_runtime/`** — `data-m-*` binder と JS runtime

### bridge 設計の原則 (signal-only)

- **JS → C++**。入力 / UI イベント通知のみ (button click、menu select 等)
- **C++ → JS**。state push + DOM 更新指示のみ
- **gameplay state を JS に持たせない**

---

## CLI を使わず CMake から直接消費する

CLI を使わずに既存 CMake プロジェクトに足したい場合は `docs/GETTING_STARTED.md` 末尾の
**「CMake から直接消費したい (上級)」** セクションを参照。`FetchContent` / `find_package` レシピがある。
