# MitiruEngine — Scope and Identity

> **Canonical statement.** When other docs (README.md, ARCHITECTURE.md,
> GETTING_STARTED.md, etc.) appear to disagree with this file, **this file
> wins.** Other docs are aligned over time.

## TL;DR

**MitiruEngine は「必要なものしか画面に出さない」C++ game framework です。**

Unity / Godot のような全機能 mega editor の対極を志す。1 ツール = 1 関心事 = 1 ウィンドウ。CLI が一級市民。GUI editor は提供しない。

ターゲットは **コードが読める自学者 / 真剣な初心者** — Scratch レベル未経験者向けではない。

---

## Core philosophy

### 1. アトミックツール哲学 (Atomic Tools)

> **必要なものしか画面に出さない。文脈外の機能は視界に存在させない。**

すべての design 決定がここから派生する。具体的には:

- **Mega editor / mega inspector は作らない**: Unity / Godot の 50 panel が同時に見える状態の対極
- **1 ツール = 1 関心事 = 1 ウィンドウ**: AviUtl 流。Renderer / Audio / FSM / Scene Tree など別ウィンドウ
- **デフォルトでは何も開かない**: pushed UI じゃなく pulled UI。開発者が必要に応じて窓を開く
- **CLI が一級市民**: `mitiru new/build/run/debug/inspect` で全機能アクセス可能
- **IDE は optional**: VS / VS Code / Vim / Helix / メモ帳 でも開発できる
- **エンジンも機能別に独立**: Renderer 単独で動く、Audio 単独で動く

### 2. Code-first, inspector for observation only

GUI editor は提供しない。authoring は全て **コード** で行う。

ただし **inspector window 群** は提供する。これは「**書き換える GUI**」ではなく「**観察する GUI**」:

- ✗ Editor (Unity / Godot のインスペクタ) = state を書き換える GUI
- ✓ **Inspector** (Chrome DevTools 系) = state を観察する GUI

inspector は読み取り専用。authoring は code、observation は inspector。

### 3. C++ for gameplay, CEF for UI/inspector overlay

2026-05 に確定:

- **gameplay logic (シーン、ゲームルール、状態機械、シミュレーション、入力解釈、save/load、AI、物理) は全 C++**
- **CEF は UI / HUD / 演出 / inspector window** の表示レイヤー
- **bridge は signal-only** に薄く保つ (JS → C++ は input/UI イベント通知、C++ → JS は state push のみ)

同じく 2026-05 に確定:
- **Lua / NodeGraph scripting は削除済み**

### 4. Host-Game 境界は C-only signal flow

2026-05-20 に確定、v0.2.0 で実装完了:

Game DLL は純関数に近い形で実装される: `(memory, input, dt) → (memory', render, intents)`

- **Game DLL は host (engine) の object pointer を一切持たない** (`Engine*` も `Screen subsystem*` も)
- **Host は毎フレーム POD で必要データを push** する (1 フレーム分の入力をまとめた `InputSnapshot` / dt / Screen 引数)
- **Game の side effect は `FrameIntents` (エンジンへの依頼を書く欄) 経由で「お願い」** する (`requestStop`, `executeJs`, etc.)
- 結果: hot reload が構造的に安全、巻き戻し / リプレイがゲームの全状態 (1 個の struct) の serialize で完結、ABI drift が POD version field で検出可能

これは「gameplay は C++、CEF は表示のみ」の `signal-only` 規約を **DLL 境界にも一般化** したもの。「engine.foo() で何でも済む」誘惑を構造的に消し、host capability の追加を常に明示的にする。

**実装の reference**: `examples/rewind/rewind_dll.cpp` (game side) + `apps/mitiru_host/main.cpp` (host side)。`mitiru_host --watch path/to/game.dll` で **L3 hot reload** (state preserved across code swap) が動く。

**副次的効果**: `InspectableRegistry` (lambda-based) は **non-DLL モード専用** に縮退した。DLL 側は `FrameIntents::exportedInspectables[]` に pre-serialized JSON を push し、engine が SharedSnapshot に write する経路に統一されたため、旧 step 5 (「Inspectable registry を DLL-aware 化」) の課題は構造的に解消された。

### Meta-rule: 既存資産の都合より哲学を優先

新機能や refactor の design 判断は **常に哲学から始める**。「既存コードに合うから」「これが楽だから」は design 理由にならない。詳細は [`CLAUDE.md`](../CLAUDE.md) の Meta-rule セクション。

---

## Target user

| 想定する | 想定しない |
|---|---|
| コード書ける自学者 (raylib / Love2D / Pyxel ユーザー層) | Scratch レベルプログラミング未経験者 |
| C++ 初心者だが tutorial で追える程度 | Unity / Godot で「クリックで動く」を求める層 |
| 「裏側で何が起きてるか見える」のを価値とする層 | 大規模 AAA チーム開発 |
| 個人開発 / 小規模インディー | console / mobile target を必要とする |

target が違えば philosophy も違うので、外す target には **無理に対応しない** ことを scope 宣言とする。

---

## 5 つの独自軸 (Differentiators)

raylib / Love2D / Pyxel など既存 minimal engine 群との差別化として 5 軸を持つ。実装は段階的:

> **外向けの語り方**: 「5 軸」「軸 N」は内部の設計指針としての呼び名。公開文書・サイト・README
> では軸番号ではなく機能名そのもの (「HTML/CSS UI」「巻き戻し」「単独起動」「録画リプレイ」
> 「別窓ツール」) で語る。この章はその定義の場としてのみ軸番号を使う。

### 軸 1: HTML/CSS で UI が書ける C++ engine

CEF 統合済み。**ゲーム本体は C++、UI / HUD / メニューは HTML/CSS** で書ける。

- raylib / Love2D / Pyxel は独自 UI 描画で Web スキル流用不可
- Unity / Unreal はネイティブ UI で Web スキル無効
- MitiruEngine は Web 開発者の知識がそのまま使える

### 軸 2: タイムトラベル inspector

state を毎フレーム ring buffer に記録。inspector で**過去のフレームに巻き戻して観察**できる。

- 「なぜ HP が 50 になったか」を 30 フレーム前まで戻って原因の 1 行を特定
- 既存 engine に存在しない発想
- Bret Victor "Inventing on Principle" の game engine 化

### 軸 3: 全 system 単独起動

Renderer / Audio / Physics / Input / Scene などが **個別 CLI コマンドで起動可能**:

```
mitiru renderer --scene test.json    # renderer だけ動かす
mitiru audio --play hit.wav          # audio だけ動かす
mitiru physics --world test.json     # physics だけ動かす
```

engine 内部が「機能別に小さく分解されている」ことが学習者から見える。Unix philosophy の engine 内 ver。

### 軸 4: Deterministic + 自動リプレイ

入力 (キー / マウス) + 乱数 seed を毎プレイ自動記録 → **完全再現可能**。

- バグ報告: 再現手順を文字で書く代わりに replay file を送れる
- speedrun: 自動 replay 動画化
- 教育: 上級プレイヤーの replay をそのまま教材化

### 軸 5: Modular sub-window architecture

ゲーム本体は **メインウィンドウ**、debug ツールや inspector は **サブウィンドウ** として OS-level に独立する設計。

- ✓ Main window は gameplay 専用 — screenshot に debug 情報が映り込まない
- ✓ 1 ツール = 1 OS window (Input Monitor / Time-travel Inspector / Scene Tree など)
- ✓ default では何も開かない、必要な debug mode の opt-in でのみ spawn
- ✓ マルチモニタユーザーに革命的 (debug 画面を 2nd モニタへ追い出せる)
- ✓ アトミックツール哲学の OS-window レベル実装

engine 本体に実装済み。ツール窓の一覧と開き方: [`docs/TOOL_WINDOWS.md`](TOOL_WINDOWS.md)。

---

## Module status (current as of 2026-05-20)

| Module group | Status | Notes |
|---|---|---|
| `core`, `gfx`, `platform`, `scene`, `ecs`, `audio`, `input`, `asset`, `resource`, `data`, `control`, `util`, `math`, `i18n`, `observe`, `debug`, `validate` | Stable | |
| `render` — 2D pipeline + 3D Phong/Toon | Stable | |
| `render` — DX12 HDR / MSAA / FXAA / Shadow | Stable | 2026-05 polished |
| `physics` — Box2D, Jolt | Stable | |
| `vn` (native) | Stable | |
| `network` — TCP, lobby, state sync | Stable | |
| `network` — `ReliableUDP`, GameNetworkingSockets | Incomplete | TODO callbacks |
| `cef`, CEF-side bridges | Stable | role shifted to UI overlay + inspector |
| `bridge` (signal-only) | Stable | view-push pattern unified |
| Gameplay primitives (FSM, Timer, SceneRouter, BridgeViewPush, JsonBinding, SaveSchema, ContentLoader, SchemaValidator, etc.) | Stable | 2026-05 added |

### Removed / deprecated

| | Reason |
|---|---|
| Lua scripting | 2026-05 の方針確定で削除 |
| NodeGraph scripting | 2026-05 の方針確定で削除 |
| JS gameplay path (旧「JS-first」路線) | 2026-05 の方針転換 — CEF now UI overlay only |
| `mitiru.novel` JS VM | native vn modules used instead |

---

## Out of scope (explicit non-goals)

- **GUI Visual editor.** Atomic-tools 哲学に反する。Unity / Godot を使うべき
- **Block-based scripting (Scratch / Blueprint 系).** GUI authoring と同じ理由
- **Scratch レベル未経験者の取り込み.** Target 違い (上記 Target user 参照)
- **Console / mobile target.** Windows-first scope。CEF が desktop-only な以上両立困難
- **Vulkan / Metal backend.** 当面なし。DX12 本命 + DX11 明示 fallback で十分
- **JSON で gameplay logic を宣言する DSL.** 純データ (novel script / i18n / balance / save) のみ JSON、interaction は C++
- **AI が JS gameplay を生成する元路線.** 2026-05 に廃止済み
- **Heavy-handed scope cuts to existing modules.** 削除済み (Lua/NodeGraph/JS gameplay) 以外は維持

---

## Tooling philosophy (CLI-first)

`mitiru` CLI が engine のエントリポイント:

| コマンド | 用途 | 導入の節目 |
|---|---|---|
| `mitiru new <name>` | 新規プロジェクト雛形 | 既存 (CLI 統合で polish) |
| `mitiru build` | ビルド (CMake 隠蔽) | 既存 (CLI 統合で polish) |
| `mitiru run` | 実行 | 既存 (CLI 統合で polish) |
| `mitiru debug` | デバッグ + inspector 起動 | **CLI 統合で新規** |
| `mitiru inspect <subject>` | 個別 inspector window | **巻き戻し inspector と同時に実装** |
| `mitiru renderer/audio/physics/input/...` | subsystem 単独起動 | **単独起動の節目で実装** |
| `mitiru replay <file>` | replay 再生 | **録画リプレイの節目で実装** |

**「CLI で全機能アクセス可能」を engine の保証条件にする。** IDE は optional。

---

## Roadmap (5 つの節目, ~11-17 months)

| 節目 | Duration | Goal |
|---|---|---|
| **docs / 哲学** | 1〜2 weeks | docs / philosophy commit (this file is part of it) |
| **HTML/CSS UI** | 1〜2 months | CLI integration + HTML UI sample polish |
| **巻き戻し** | 2〜3 months | 巻き戻し inspector implementation |
| **単独起動** | 2〜3 months | Per-system isolation refactor |
| **録画リプレイ** | 3〜4 months | Deterministic + auto-replay |
| **仕上げ** | 1〜2 months | Submission polish (portfolio package) |

節目の境界 = 自然な pivot point。各節目の終了時点で portfolio として提出可能な状態を保つ。

---

## Reading next

- 新規開発者 → `docs/READING_ORDER.md`
- engine 全体の設計 → `docs/ARCHITECTURE.md`、ツール窓 → `docs/TOOL_WINDOWS.md`
- CLI 使い方 → `docs/GETTING_STARTED.md`
- LLM agent → `CLAUDE.md` + このファイル
