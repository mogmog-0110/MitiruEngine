# C++ Gameplay API ギャップ調査

> **Status**: 2026-05-14 初版。ADR 0001 (`docs/adr/0001-cpp-gameplay-cef-view-only.md`) に基づき、新方針「gameplay は C++ で書く」を成立させるために engine 側に不足している API を棚卸ししたもの。
>
> **Progress (2026-05-14 18:00 JST)**:
> - §1 Scene flow / §2 StateMachine / §4 Timer/Sequence: ✅ P0 5 件 commit `b820712a` で実装済
> - §3 signal → EventBus glue: ✅ `mitiru::bridge::BridgeEventBusGlue` 実装済 (commit `1102557f`)
> - §6 InputMapper × CEF DOM signal 統合: ✅ `mitiru::input::BridgeInputAdapter` + `InputMapper::triggerActionFromBridge` 実装済
> - §7 Bridge → View 自動 push: ✅ 5 Bridge すべてに `setViewPush` 統合済
> - §5 Save/load schema: ✅ `mitiru::data::JsonBinding` + `SaveSchema<T>` + `MigrationChain<T>` 実装済
> - §9 Data-driven authoring: ✅ `mitiru::data::ContentLoader<T>` 実装済
> - §8 Lua 廃止判断: ✅ ADR 0002 Accepted (Lua / NodeGraph 全削除)
> - §9 拡張: ✅ `ContentLoader<T>::loadFileValidated(path, schema)` で SchemaValidator opt-in を追加
> - §9 拡張 (Schema 自動生成): ✅ SchemaImporter で draft-07 JSON Schema → Schema
> - 残: P2 拡張 (例: save migration の宣言的記述 / SmallFunction Tracy zone 計測の production 検証)
>
> **調査方法**: `include/mitiru/` 以下を Glob / Grep / Read で確認。クラス名・ファイル名は実在を裏取り済み。確認できなかった点は「未確認」と明示する。
>
> **関連 doc**:
> - [ADR 0001](adr/0001-cpp-gameplay-cef-view-only.md) — ピボットの決定そのもの
> - [BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md) — bridge の新責務
> - [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — 新レイヤー分担

## 凡例

- **P0**: これが無いと新方針 (C++ で gameplay を書く) が回らない
- **P1**: 典型的な consumer game (例: 15-state SM + drag-drop + dialogue + 並列 entity) を書くのに必要
- **P2**: Nice to have
- **見積もり**: small (数百行) / medium (~2 週間規模) / large (1 か月以上 / 設計決定が要る)

---

## 1. Scene / scene flow 管理

**現状**:
- `include/mitiru/scene/SceneGraph.hpp` — **階層的トランスフォームグラフ** (NodeId / parent-child / Mat4 計算)。flow 制御ではなく描画用ツリー
- `include/mitiru/scene/SceneTransitionManager.hpp` — シーン遷移マネージャ (詳細未確認)
- `include/mitiru/scene/SceneSerializer2.hpp` — シーンの永続化
- `include/mitiru/scene/SceneGraph.hpp` の `SceneNode` は transform 付きノード、ゲームフロー (Title → Menu → Stage → Result) の制御ではない
- `include/mitiru/cef/SceneTransition.hpp` — CEF view 側の transition

**ギャップ**:
- **シーンスタック / ルータ** が無い: 「タイトル画面 → メニュー → ステージ → リザルト」のような flow を C++ で簡潔に書く API が無い。ゲーム側でアドホックに `enum SceneId` + `switch` を書くしかない
- 各シーンを **ライフサイクル付きの class** (`onEnter` / `onUpdate` / `onExit` / `onPause` / `onResume`) として登録する仕組みが無い
- シーンへの引数渡し / pop 結果の受け渡し規約が無い
- view (CEF) との同期 (シーン変更時に `view.transition.start` を push する) が手動

**優先度**: **P0** (gameplay flow を書く土台。これが無いと毎ゲームで再発明)
**見積もり**: medium

---

## 2. State machine (汎用 FSM)

**現状**:
- `include/mitiru/scene/Node.hpp` に `StateMachine` 風のクラスがある (詳細未確認、Grep ヒットのみ)
- `include/mitiru/audio/AudioEffectChain.hpp` にも State 系のクラスがあるが audio 専用
- `include/mitiru/scripting/NodeGraph.hpp` + `NodeExecutor.hpp` — ノードベース実行グラフ (visual scripting 寄り)。FSM の代替にはなる可能性

**ギャップ**:
- **汎用の `StateMachine<T>` テンプレート** が無い (型安全 transition、guard、entry/exit action、history、nested state machine 等)
- 15 状態級の minigame state machine を C++ で書こうとすると、enum + switch + bool フラグの組み合わせになり保守性が低い
- NodeGraph はあるが gameplay state machine よりは behavior tree / visual script 寄り、軽量 FSM 用途にはオーバースペック

**優先度**: **P0** (cooking / battle / dialogue / mini-game 全てに必要な原始的 building block)
**見積もり**: small (テンプレ FSM ヘッダ 1 つ書けば足りる)

---

## 3. イベント / signal dispatch

**現状**:
- `include/mitiru/core/EventBus.hpp` — **型安全 Pub/Sub**。`WindowResizeEvent` / `SceneChangeEvent` / `InputEvent` / `AudioEvent` / `GameStateEvent` 等の組み込みイベント型。`publish` (同期) と `publishDeferred` (遅延) 両対応、スレッドセーフ option
- `include/mitiru/core/Signal.hpp` — シンプル signal 実装
- `include/mitiru/cef/StateStore.hpp` (BRIDGE_API_CONTRACT.md で参照) — JS↔C++ の dispatch
- `include/mitiru/bridge/EventBridge.hpp` — bridge 経由イベント

**ギャップ**:
- 基本は **既に揃っている**。EventBus は型安全で良好
- 不足: **bridge から来た UI signal (例: `ui.button|order-card-click`) を gameplay handler にルーティングする規約** が未整備。BRIDGE_API_CONTRACT で命名規則は決まったが、それを C++ 側で受け取る dispatch table がまだ慣習化されていない
- 「bridge signal → EventBus event」の自動変換 (例: `ui.button|X` → `UiButtonEvent{name:"X"}`) を engine が提供すると、ゲーム側のボイラーが減る

**優先度**: **P1** (基盤はあるので bridge-to-gameplay glue を engine 側で標準化する)
**見積もり**: small

---

## 4. Timer / scheduler

**現状**:
- `include/mitiru/core/FrameTimer.hpp` — フレーム単位の時間管理 (詳細未確認)
- `include/mitiru/core/JobSystem.hpp` — ジョブシステム (詳細未確認、並列処理寄り)
- `include/mitiru/ecs/SystemScheduler.hpp` — ECS の System 実行スケジューラ

**ギャップ**:
- **ゲームロジック用の Timer / Cooldown / Delayed action** が見当たらない:
  - `scheduleAfter(seconds, lambda)`
  - `Cooldown(2.0f).tick(dt).ready()`
  - `Sequence().wait(1.0f).action(...).wait(0.5f).action(...)` のような chainable timeline
- 焼き時間カウントダウン、ダイアログ自動進行、tutorial 段階待ちなど、ゲーム作りで頻出する用途が手薄
- `JobSystem` は並列処理用途で、用途が違う

**優先度**: **P0** (cooking 焼き時間や演出 wait に必須)
**見積もり**: small (Timer / Cooldown / Sequence 三点セット)

---

## 5. Save / load

**現状**:
- `include/mitiru/cef/SaveStore.hpp` — **C++ 側で完結する save-slot I/O**:
  - スロット N 毎に `slot_<N>.json` (本体) + `slot_<N>.meta.json` (一覧用) + `slot_<N>.staging.json` (atomic write 用) のクラッシュリカバリ付き
  - `save.write` / `save.read` / `save.list` / `save.delete` の StateStore handler を提供
  - 内部 mutex で read/write race を防ぐ
- `include/mitiru/bridge/SaveBridge.hpp` — bridge wrapper
- `include/mitiru/scene/EntityPersistence.hpp` — entity 単位の永続化
- `include/mitiru/observe/StructuredDiff.hpp` — JSON diff (save 比較用?)

**ギャップ**:
- ファイル I/O は揃っているが、**「C++ struct ↔ JSON」serialize の規約**が明示されていない:
  - 現状は手書きの `nlohmann::json` 構築 / parse が前提に見える
  - reflection ベース or マクロベースの自動 serialize が無い
  - 結果: gameplay 開発者が毎回 schema を手書きする
- versioning / migration の仕組みは SaveStore レベルでは無さそう (version フィールドはあるが、古い save の自動変換は手動)

**優先度**: **P1** (動くが、ゲーム数が増えると手書き schema が辛い)
**見積もり**: medium (serialize macro / reflection は設計が要る)

---

## 6. 入力マッピング (bridge UI event → gameplay action)

**現状**:
- `include/mitiru/input/InputMapper.hpp` — **アクションマッピングレイヤー**:
  - `BindingType`: Key / MouseButton / GamepadButton / GamepadAxis
  - `GamepadButtonId` (XInput ビット互換、win32 非依存)
  - 文字列アクション名 ⇔ 物理入力の双方向マッピング
  - 実行時リバインド対応
- `include/mitiru/input/Win32Input.hpp` / `GamepadInput.hpp` — OS 入力ソース
- `include/mitiru/input/InputInjector.hpp` / `InputRecorder.hpp` / `InputReplayer.hpp` — record/replay (テスト用)
- `include/mitiru/input/KeyCode.hpp`

**ギャップ**:
- **CEF (DOM) からの UI signal を InputMapper の "action" 体系に流す glue が無い**:
  - 例: `ui.button|fire` → `Action::Fire` 発火
  - 例: `ui.pointer|drag-end` → `Action::DragRelease(x, y)`
- 現状は ゲーム側で `BridgeQueryHandler` を書いて自前ルーティングする想定
- ネイティブ入力と DOM 入力を **同じ action にマージできない** (gameplay handler を 2 系統書くハメに)

**優先度**: **P0** (CEF UI 一本化の前提が崩れる)
**見積もり**: small (Mapper 拡張で済む)

---

## 7. 演出 trigger (C++ → JS view)

**現状**:
- `include/mitiru/bridge/DialogueBridge.hpp` — **sgc DialogueGraph を C++ で走査** (start / advance / currentText / バックログ)。**逆方向 (JS view へ "現在のセリフを表示しろ" を push する仕組み) は別途必要**
- `include/mitiru/bridge/TransitionBridge.hpp` — フェードイン/アウト管理 (alpha 値を毎フレーム計算)。これも view 側に alpha を反映させるルートは別途
- `include/mitiru/bridge/AnimationBridge.hpp` — アニメーション統合 (詳細未確認)
- `include/mitiru/bridge/ParticleBridge.hpp` / `EventBridge.hpp` / `VNBridge.hpp` — それぞれ専用 bridge
- `include/mitiru/cef/StateStore.hpp` — `set()` / `emit()` で view に push する基盤

**ギャップ**:
- BRIDGE_API_CONTRACT.md で `view.<サブシステム>.<キー>` 規約は決まったが、**各 Bridge が新規約に乗っているかは未確認**。DialogueBridge / TransitionBridge は sgc を直接ラップしていて、StateStore への push を engine 側で自動で行う設計にはなっていなさそう
- ゲーム側で `dialogue.advance()` を呼んだ後、view への push を **手動で書く必要がある**。これは新方針の「JS は表示するだけ」を実現するためにも engine 側で自動化すべき
- 「C++ で `play("cut_in", entity)` と書くだけで view に effect 表示が指示される」レベルの API が無い

**優先度**: **P0** (ADR で「JS は表示するだけ」を宣言したので、push 側の自動化は不可欠)
**見積もり**: medium (各 Bridge を StateStore 自動 push に統合する)

---

## 8. Hot reload / iteration speed

**現状**: **想像以上に充実**
- `include/mitiru/resource/HotReloadManager.hpp` / `HotReloadWatcher.hpp` — 汎用 hot reload
- `include/mitiru/asset/FileWatcher.hpp` / `AssetHotReloader.hpp` / `HotReloader.hpp` — asset 系
- `include/mitiru/scripting/LuaHotReload.hpp` — **Lua スクリプトの hot reload**
- `include/mitiru/vn/HotReloadScenario.hpp` — VN シナリオ hot reload
- `include/mitiru/render/ShaderLoader.hpp` — シェーダリロード
- `include/mitiru/core/EngineCommands.hpp` — runtime コマンド

**Lua scripting の扱い — 廃止決定 (2026-05-14)**:
- Siv3D ロールモデル採用に伴い **案 (a) Lua 完全廃止** を選択。ADR 0002 (`docs/adr/0002-remove-lua-scripting.md`) で固定
- `include/mitiru/scripting/` 配下の Lua* / NodeGraph* / NodeExecutor / DirectiveNodes は engine 負債として **削除予定**
- iteration speed は「scripting 言語を入れる」ではなく「C++ incremental compile + header-only + small API surface」で解決する (Siv3D 流)
- 過去案 (b) Lua を declarative content 専用に残す / (c) gameplay scripting として残す はいずれも不採用 (二言語依存を避ける建前と矛盾)

**ギャップ**:
- 上記の Lua 位置付けが決まっていないこと自体がギャップ
- C++ 純粋ホットリロード (DLL swap / live++) は未確認 — おそらく無い

**優先度**: **P1** (iteration speed は dev velocity に直結。Lua 位置付けは ADR で決めるべき)
**見積もり**: small (ADR 1 本) ~ large (DLL hot swap を入れるなら)

---

## 9. Authoring / content データ駆動読み込み

**現状**:
- `include/mitiru/MitiruData.hpp` — トップレベルのデータ読み込み (詳細未確認)
- `include/mitiru/data/` — データ定義 (詳細未確認、Glob で複数ファイル確認)
- `include/mitiru/asset/AssetRegistry.hpp` / `AssetManager.hpp` — アセット管理
- `include/mitiru/i18n/` — 国際化
- `include/mitiru/resource/IAssetLoader.hpp` — アセットローダ抽象
- `include/mitiru/bridge/I18nBridge.hpp`

**ギャップ**:
- 個別 loader はあるが、**「JSON 定義 → C++ struct 自動 binding」の汎用基盤** が未確認
  - 例: `balance.json` → `BalanceTable` struct を 1 行で読みたい
  - reflection or codegen が無いと毎回手書き parse
- content authoring tool (エディタ) は engine 側には無い (これは consumer 側の責務とも言える)
- データのスキーマ検証 (`balance.schema.json` 等の JSON Schema 連動) は確認できず

**優先度**: **P1**
**見積もり**: medium (汎用 binder を書くか、外部ライブラリで済ますか設計判断要)

---

## サマリ表

| カテゴリ | P0 | P1 | P2 | 主な見積もり |
|---|---|---|---|---|
| 1. Scene flow 管理 | 1 | 0 | 0 | medium |
| 2. State machine | 1 | 0 | 0 | small |
| 3. Signal dispatch | 0 | 1 | 0 | small |
| 4. Timer / scheduler | 1 | 0 | 0 | small |
| 5. Save / load | 0 | 1 | 0 | medium |
| 6. 入力マッピング | 1 | 0 | 0 | small |
| 7. 演出 trigger | 1 | 0 | 0 | medium |
| 8. Hot reload (Lua 位置付け) | 0 | 1 | 0 | small ~ large |
| 9. Authoring / data 駆動 | 0 | 1 | 0 | medium |
| **合計** | **5** | **4** | **0** | — |

---

## P0 ハイライト (新方針が回るために最優先)

1. **Scene flow ルータ / シーンライフサイクル** — gameplay flow を書く土台
2. **汎用 `StateMachine<T>` テンプレート** — cooking / battle / dialogue 全てに必要
3. **Timer / Cooldown / Sequence** — 焼き時間 / 演出 wait の最頻出パターン
4. **InputMapper への CEF (DOM) signal 統合** — UI イベントを action 体系に流す
5. **Bridge の StateStore 自動 push 統合** — 「C++ で計算 → view に反映」を engine が肩代わり

これら 5 つが揃わないと、consumer 側で毎ゲーム同じ glue を再発明することになる。

---

## 未調査事項 (今後の deeper dive 候補)

- `core/JobSystem.hpp` / `ecs/SystemScheduler.hpp` の詳細 — Timer/scheduler ギャップを部分的に埋める可能性
- `MitiruData.hpp` / `data/` ディレクトリ — データ駆動の現状確認
- `scripting/NodeGraph.hpp` — FSM の代替になり得るか
- `bridge/AnimationBridge.hpp` / `ParticleBridge.hpp` — 演出 trigger の現状確認
- 各 Bridge が StateStore 規約に乗っているかの一斉確認
