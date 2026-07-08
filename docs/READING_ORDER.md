# Reading Order

Where to start, depending on who you are and what you're doing.

## Newcomer — human

1. **`README.md`** — タグライン + 特徴 + CLI quickstart
2. **`docs/GETTING_STARTED.md`** — `mitiru` CLI を入れて最初のプロジェクトを作る
3. **`docs/SCOPE.md`** — canonical identity statement、差別化機能の定義、target user
4. **`examples/hello_game/`** — C++ gameplay + HTML/CSS HUD の動く showcase
5. **`docs/ARCHITECTURE.md`** — エンジン全体の設計、deeper dive

Templates (`templates/`) は参照用。日常のプロジェクト作成は `mitiru new` がテンプレートを CLI 内に embed して使う。

## Newcomer — LLM (Claude Code, Copilot CLI, etc.)

1. **`CLAUDE.md`** (project root) — engine 規約、アトミックツール哲学、差別化機能の定義 — **必須**
2. **`docs/SCOPE.md`** — canonical engine identity、out-of-scope の明示
3. **`.claude/rules/`** — 担当エリアの rule ファイル
4. **`docs/API_QUICKREF.md`** — よく使う API のチャート
5. **`docs/api/_index.jsonl`** — class/function/enum を 1 行 1 entry で grep 可能。symbol 検索はまずここ
6. **`docs/api/<module>.md`** — 必要なモジュールだけ load (34 files, ~6 KB to ~200 KB each)。**`docs/API_CATALOG.md` (19k lines) は loading 禁止**

---

## エンジンの差別化機能を理解する

`docs/SCOPE.md` の「5 つの独自軸」の章 (定義の場) を読む。機能別の入口:

| 機能 | 入口の doc |
|---|---|
| HTML/CSS で UI が書ける C++ engine | `examples/hello_game/`、`docs/CEF_STATE_BRIDGE.md` |
| 巻き戻し (タイムトラベル窓) | `docs/TIME_TRAVEL.md`、`docs/REPLAY_DEBUGGER.md` |
| 全 system 単独起動 | `docs/SUBSYSTEMS.md` |
| 録画リプレイ | `docs/REPLAY_DEBUGGER.md`、`docs/recipe-replay-as-test.md` |
| 別窓ツール | `docs/TOOL_WINDOWS.md` |

---

## 新規 feature を追加する

1. **`docs/SCOPE.md`** — out-of-scope に該当しないか、差別化機能のどれを強化するかを確認
2. **`docs/ARCHITECTURE.md`** — 該当レイヤーを特定
3. **`include/mitiru/<module>/`** — 既存ヘッダを読む
4. **`.claude/rules/mitiru-engine.md`** — coding conventions、hot-path discipline、validation tools
5. **`.claude/rules/test-standards.md`** — Catch2 パターン
6. **`tests/mitiru/Test*.cpp`** — 既存テストをコピー出発点に

## HTML/CSS UI を追加する

1. **`examples/hello_game/`** — canonical pattern (C++ StateStore + HTML subscribe)
2. **`docs/CEF_STATE_BRIDGE.md`** — bridge mechanics
3. **`include/mitiru/cef/StateStore.hpp`** — `set` / `emit` / `onAction` API
4. **`web/mitiru_runtime/mitiru_cef_state.js`** — JS 側 subscribe API
5. **`.claude/rules/html-animation.md`** / `local-design-workflow.md` — visual conventions

### bridge 設計の原則 (signal-only)

- **JS → C++**: 入力 / UI イベント通知のみ (button click、menu select 等)
- **C++ → JS**: state push + DOM 更新指示のみ
- **gameplay state を JS に持たせない**

---

## Investigating a bug in the UI layer

1. Inspector window を開いて該当 state を確認 (`mitiru inspect <subject>`)
2. **Real User Path Smoke (RUP-S)** で再現 — 内部 API 呼び出しじゃなく real PointerEvent dispatch
3. 証拠スクショを `specs/.../evidence/` に保存
4. **根本原因を直す**。対症療法禁止 — `.claude/rules/definition-of-done.md`

---

## CLI を使わず CMake から直接消費する

CLI を使わずに既存 CMake プロジェクトに足したい場合は `docs/GETTING_STARTED.md` 末尾の **「CMake から直接消費したい (上級)」** セクションを参照。`FetchContent` / `find_package` レシピがある。

---

## 歴史的文書 (旧 dual-mode 時代、現役じゃない)

以下は 2026-05 の方針転換より前の文書。**新規開発時は読まないでください** — 古い「JS gameplay」前提で書かれている:

- `docs/HYBRID_RUNTIME.md` — HTML UI 構成 (CEF あり) 前提。歴史的経緯のみ
- `docs/NARRATIVE_VMS.md` — JS narrative VM の選定。`vn::ScenarioScript` (C++) のみ使用

新方針の canonical は **`docs/SCOPE.md`** + **`CLAUDE.md`** + **`docs/ARCHITECTURE.md`**。
