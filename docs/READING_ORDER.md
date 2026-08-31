# Reading Order

Where to start, depending on who you are and what you're doing. Every link below
points to a file included in this snapshot or to the public website.

## Newcomer: human

1. **`README.md`** — タグライン +特徴 + CLI quickstart
2. **`docs/GETTING_STARTED.md`** — `mitiru` CLIを入れて最初のプロジェクトを作る
3. **`docs/SCOPE.md`** — canonical identity statement、特徴、target user
4. **`examples/welcome/`** — 絵・文字・図形とHTML/CSSパネルを1画面で見せる最初のサンプル
5. **`docs/ARCHITECTURE.md`** — エンジン全体の設計、deeper dive

同梱サンプルは番号順(welcome → shapes → text → input → …)に読むと、描画・入力・音・
カメラ・HTMLの画面・3Dまでひととおり触れます。一覧は [`examples/README.md`](../examples/README.md)。
日常のプロジェクト作成は`mitiru new`がテンプレートを内蔵して使います。

## Newcomer: LLM (Claude Code, Codex, etc.)

1. **`docs/SCOPE.md`** — canonical engine identity、out-of-scopeの明示
2. **`docs/ARCHITECTURE.md`** — レイヤー構造とモジュール依存
3. **`docs/FLAT_POD.md`** — 状態を1個のstructに置く核心設計(巻き戻し・観察・AI連携の土台)
4. **`docs/AI_WORKFLOW.md`** — 観測API・`mitiru verify`・MCPでゲームを観て直すループ
5. **`include/mitiru/`** — header-only実装。symbolはここをgrepする

---

## エンジンの特徴を理解する

機能別の入口:

| 機能 | 入口 |
|---|---|
| HTML/CSSでUIが書けるC++ engine | `examples/html_hud/`、`docs/HYBRID_RUNTIME.md` |
| 巻き戻し | `docs/REWIND.md`、`docs/FLAT_POD.md` |
| 全system単独起動 | `docs/SUBSYSTEMS.md` |
| 録画リプレイ | `docs/AI_WORKFLOW.md`、`docs/TOOL_WINDOWS.md` |
| 別窓ツール | `docs/TOOL_WINDOWS.md` |

---

## HTML/CSS UIを書く

1. **`examples/html_hud/`** — C++の値をHTMLに流すcanonical pattern (JavaScriptゼロ)
2. **`examples/html_menu/`** — HTMLの操作をC++が受ける逆向き
3. **`docs/BRIDGE_API_CONTRACT.md`** — bridgeのsignal-only規約
4. **`web/mitiru_runtime/`** — `data-m-*` binderとJS runtime

### bridge設計の原則(signal-only)

- **JS → C++**。入力 / UIイベント通知のみ(button click、menu select等)
- **C++ → JS**。state push + DOM更新指示のみ
- **gameplay stateをJSに持たせない**

---

## CLIを使わずCMakeから直接消費する

CLIを使わずに既存CMakeプロジェクトに足したい場合は`docs/GETTING_STARTED.md`末尾の
**「CMakeから直接消費したい(上級)」** セクションを参照。`FetchContent` / `find_package`レシピがある。
