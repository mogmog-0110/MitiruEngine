# MitiruEngine: Scope and Identity

> **Canonical statement.** When other docs (README.md, ARCHITECTURE.md,
> GETTING_STARTED.md, etc.) appear to disagree with this file, **this file
> wins.** Other docs are aligned over time.

## TL;DR

**MitiruEngineは「必要なものしか画面に出さない」C++ game frameworkです。**

Unity / Godotのような全機能mega editorの対極を志す。1ツール = 1関心事 = 1ウィンドウ。CLIが一級市民。GUI editorは提供しない。

ターゲットは**コードが読める自学者**。Scratchレベルの未経験者向けではない。

---

## Core philosophy

### 1. アトミックツール哲学(Atomic Tools)

> **必要なものしか画面に出さない。文脈外の機能は視界に存在させない。**

すべてのdesign決定がここから派生する。具体的にはこうなる。

- **Mega editor / mega inspectorは作らない**。Unity / Godotの50 panelが同時に見える状態の対極
- **1ツール = 1関心事 = 1ウィンドウ**。AviUtl流。Renderer / Audio / FSM / Scene Treeなど別ウィンドウ
- **デフォルトでは何も開かない**。pushed UIじゃなくpulled UI。開発者が必要に応じて窓を開く
- **CLIが一級市民**。`mitiru new/build/run/debug/inspect`で全機能アクセス可能
- **IDEはoptional**。VS / VS Code / Vim / Helix / メモ帳 でも開発できる
- **エンジンも機能別に独立**。Renderer単独で動く、Audio単独で動く

### 2. Code-first, inspector for observation only

GUI editorは提供しない。authoringは全てコードで行う。

ただし **inspector window群** は提供する。書き換えるGUIではなく、観察するGUIとして。

- Editor (Unity / Godotのインスペクタ)はstateを書き換えるGUI。これは作らない
- **Inspector** (Chrome DevTools系)はstateを観察するGUI。こちらを作る

inspectorは読み取り専用。authoringはcode、observationはinspector。

### 3. C++ for gameplay, CEF for UI/inspector overlay

2026-05に確定した。

- **gameplay logic (シーン、ゲームルール、状態機械、シミュレーション、入力解釈、save/load、AI、物理)は全C++**
- **CEFはUI / HUD / 演出 / inspector window** の表示レイヤー
- **bridgeはsignal-only** に薄く保つ(JS → C++はinput/UIイベント通知、C++ → JSはstate pushのみ)

同じく2026-05に確定した。
- **Lua / NodeGraph scriptingは削除済み**

### 4. Host-Game境界はC-only signal flow

2026-05-20に確定し、v0.2.0で実装まで終えた。

Game DLLは純関数に近い形で実装される: `(memory, input, dt) → (memory', render, intents)`

- **Game DLLはhost (engine)のobject pointerを一切持たない** (`Engine*`も`Screen subsystem*`も)
- **Hostは毎フレームPODで必要データをpush** する(1フレーム分の入力をまとめた`InputSnapshot` / dt / Screen引数)
- **Gameのside effectは`FrameIntents` (エンジンへの依頼を書く欄)経由で「お願い」** する(`requestStop`, `executeJs`, etc.)
- 結果: hot reloadが構造的に安全、巻き戻し / リプレイがゲームの全状態(1個のstruct)のserializeで完結、ABI driftがPOD version fieldで検出可能

これは「gameplayはC++、CEFは表示のみ」の`signal-only`規約をDLL境界にも一般化 したもの。「engine.foo()で何でも済む」誘惑を構造的に消し、host capabilityの追加を常に明示的にする。

**実装のreference**: `examples/rewind/rewind_dll.cpp` (game side) + `apps/mitiru_host/main.cpp` (host side)。`mitiru_host --watch path/to/game.dll`でL3 hot reload (state preserved across code swap)が動く。

**副次的効果**: `InspectableRegistry` (lambda-based)はnon-DLLモード専用 に縮退した。DLL側は`FrameIntents::exportedInspectables[]`にpre-serialized JSONをpushし、engineがSharedSnapshotにwriteする経路に統一されたため、旧step 5 (「Inspectable registryをDLL-aware化」)の課題は構造的に解消された。

### Meta-rule: 既存資産の都合より哲学を優先

新機能やrefactorのdesign判断は **常に哲学から始める**。「既存コードに合うから」「これが楽だから」はdesign理由にならない。詳細は [`CLAUDE.md`](../CLAUDE.md)のMeta-ruleセクション。

---

## Target user

| 想定する | 想定しない |
|---|---|
| コード書ける自学者(raylib / Love2D / Pyxelユーザー層) | Scratchレベルプログラミング未経験者 |
| C++初心者だがtutorialで追える程度 | Unity / Godotで「クリックで動く」を求める層 |
| 「裏側で何が起きてるか見える」のを価値とする層 | 大規模AAAチーム開発 |
| 個人開発 / 小規模インディー | console / mobile targetを必要とする |

targetが違えばphilosophyも違うので、外すtargetには無理に対応しないことをscope宣言とする。

---

## 5つの独自軸(Differentiators)

raylib / Love2D / Pyxelなど既存minimal engine群との差別化として5軸を持つ。実装は段階的に進めている。

> **外向けの語り方**: 「5軸」「軸N」は内部の設計指針としての呼び名。公開文書・サイト・README
> では軸番号ではなく機能名そのもの(「HTML/CSS UI」「巻き戻し」「単独起動」「録画リプレイ」
> 「別窓ツール」)で語る。この章はその定義の場としてのみ軸番号を使う。

### 軸1: HTML/CSSでUIが書けるC++ engine

CEF統合済み。ゲーム本体はC++、UI / HUD / メニューはHTML/CSSで書ける。

- raylib / Love2D / Pyxelは独自UI描画でWebスキル流用不可
- Unity / UnrealはネイティブUIでWebスキル無効
- MitiruEngineはWeb開発者の知識がそのまま使える

### 軸2: 巻き戻しウィンドウ

stateを毎フレームring bufferに記録。inspectorで過去のフレームに巻き戻して観察できる。

- 「なぜHPが50になったか」を30フレーム前まで戻って原因の1行を特定
- 既存engineに存在しない発想
- Bret Victor "Inventing on Principle" のgame engine化

### 軸3: 全system単独起動

Renderer / Audio / Input / Sceneが 個別CLIコマンドで起動可能:

```
mitiru renderer      # renderer だけ動かす
mitiru audio hit.wav # audio だけ動かす
mitiru input         # input だけ動かす
mitiru scene         # scene だけ動かす
```

engine内部が「機能別に小さく分解されている」ことが学習者から見える。Unix philosophyのengine内ver。

### 軸4: Deterministic +自動リプレイ

入力(キー / マウス) +乱数seedを毎プレイ自動記録 → **完全再現可能**。

- バグ報告: 再現手順を文字で書く代わりにreplay fileを送れる
- speedrun: 自動replay動画化
- 教育: 上級プレイヤーのreplayをそのまま教材化

### 軸5: Modular sub-window architecture

ゲーム本体はメインウィンドウ、debugツールやinspectorはサブウィンドウとしてOS-levelに独立する設計。

- Main windowはgameplay専用。screenshotにdebug情報が映り込まない
- 1ツール = 1 OS window (Input Monitor / Time-travel Inspector / Scene Treeなど)
- defaultでは何も開かない。必要なdebug modeをopt-inしたときだけspawnする
- debug画面を2ndモニタへ追い出せる
- アトミックツール哲学のOS-windowレベル実装

engine本体に実装済み。ツールウィンドウの一覧と開き方: [`docs/TOOL_WINDOWS.md`](TOOL_WINDOWS.md)。

---

## Module status

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
| Lua scripting | 2026-05の方針確定で削除 |
| NodeGraph scripting | 2026-05の方針確定で削除 |
| JS gameplay path (旧「JS-first」路線) | 2026-05の方針転換。CEF now UI overlay only |
| `mitiru.novel` JS VM | native vn modules used instead |

---

## Out of scope (explicit non-goals)

- **GUI Visual editor.** Atomic-tools哲学に反する。Unity / Godotを使うべき
- **Block-based scripting (Scratch / Blueprint系).** GUI authoringと同じ理由
- **Scratchレベル未経験者の取り込み.** Target違い(上記Target user参照)
- **Console / mobile target.** Windows-first scope。CEFがdesktop-onlyな以上両立困難
- **Vulkan / Metal backend.** 当面なし。DX12本命 + DX11明示fallbackで十分
- **JSONでgameplay logicを宣言するDSL.** 純データ(novel script / i18n / balance / save)のみJSON、interactionはC++
- **AIがJS gameplayを生成する元路線.** 2026-05に廃止済み
- **Heavy-handed scope cuts to existing modules.** 削除済み(Lua/NodeGraph/JS gameplay)以外は維持

---

## Tooling philosophy (CLI-first)

`mitiru` CLIがengineのエントリポイントになっている。

| コマンド | 用途 | 導入の節目 |
|---|---|---|
| `mitiru new <name>` | 新規プロジェクト雛形 | 既存(CLI統合でpolish) |
| `mitiru build` | ビルド(CMake隠蔽) | 既存(CLI統合でpolish) |
| `mitiru run` | 実行 | 既存(CLI統合でpolish) |
| `mitiru debug` | デバッグ + inspector起動 | **CLI統合で新規** |
| `mitiru inspect <subject>` | 個別inspector window | **巻き戻しinspectorと同時に実装** |
| `mitiru renderer/audio/physics/input/...` | subsystem単独起動 | **単独起動の節目で実装** |
| `mitiru replay <file>` | replay再生 | **録画リプレイの節目で実装** |

**「CLIで全機能アクセス可能」をengineの保証条件にする。** IDEはoptional。

---

## 開発の節目(milestones)

以下の節目を順に実装してきた。現行リリースで一通り揃っている。

- **docs / 哲学** — docs / philosophy commit (this file is part of it)
- **HTML/CSS UI** — CLI integration + HTML UI samples
- **巻き戻し** — 巻き戻しinspector
- **単独起動** — per-system isolation
- **録画リプレイ** — deterministic + auto-replay

各節目の終了時点でportfolioとして提出可能な状態を保つ方針。

---

## Reading next

- 新規開発者 → `docs/READING_ORDER.md`
- engine全体の設計 → `docs/ARCHITECTURE.md`、ツールウィンドウ → `docs/TOOL_WINDOWS.md`
- CLI使い方 → `docs/GETTING_STARTED.md`
- LLM agent → `CLAUDE.md` +このファイル
