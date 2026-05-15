# MitiruEngine Release Notes — 2026-05-15

**Date:** 2026-05-15
**Commits:** ~37 across Rounds 1 through 5 (`735edc7b..0e050fc2` on `main`) + Round 8 follow-up (`77b88f0c`, `b9e1828c`)
**Test coverage:** ctest 2110 → 2254 (+144 tests; Rounds 1–5 contributed +128, Round 8 contributed +16 round-trip)
**Working tree:** clean

> Engine-side notes. Consumer migration playbooks (KaeruCrape / hato / pandd-dodo /
> Mathlands) belong in the consumer's own repository per
> `[[no-consumer-scope-in-engine]]`. This document describes only the engine
> public API surface, internal refactors, and tooling shipped on `main`.

---

## TL;DR

- 新 `mitiru::data::` レイヤーが揃った: `JsonBinding` / `SaveSchema<T>` / `MigrationChain<T>` / `Migration` ops / `ContentLoader<T>` / `SchemaImporter`. §5 save と §9 content authoring の "C++ struct ↔ JSON" の正解パスが engine 内で完結する。ADR 0003 で binding 戦略を Accepted 化。
- Bridge 側は `BridgeInputAdapter` (signal → `InputMapper` action) と `BridgeEventBusGlue` (signal → 型付き `EventBus` event) を追加し、両者とも destructor で auto-unregister するライフタイム安全に。`AnimationBridge` / `ParticleBridge` / `VNBridge` / `DialogueBridge` / `TransitionBridge` には `BridgeViewPush` 統合と差分短絡 (VN) が入った。
- レンダ側は `mitiru::render::BackendInit::createPipeline2DFor` / `createRenderer3DFor` で `Engine.hpp` から `dynamic_cast<Dx11Device>` 系を撤去。DX12 では `PixelArtFilter` 列挙 + `Screen::drawPixelGrid` オーバーロードを追加し、point-filter PSO は `createFromDx12` 時に eager 構築 (first-frame hitch なし)。
- `Engine.hpp` を `detail/Engine_{Run,Frame,Snapshot,Window,Accessors,...}.hpp` に分割 (1008 → 424 行)。`Sequence` の per-action callable は `mitiru::time::detail::SmallFunction` (48 byte SBO, Tracy zone 付き) に置換。
- ドキュメント: `docs/PROFILING_GUIDE.md` / `docs/CEF_SUBPROCESS_DIAGNOSTIC.md` / `docs/CEF_CRT_EXPERIMENT.md` を追加。ADR 0003 を Accepted。
- **Round 8 追補**: `detail/Engine_Init.hpp` (398 行) を 4 role file に再分割。`SceneDocument` を nlohmann::json バックエンドへ移行し 1382 → 923 行 (−459, ~33%) に縮小、副次的に `AudioTrait::fromJson` が `audioPath` を取りこぼしていた既存 bug も解消。`Engine_Frame.hpp` に Tracy zone 9 本 (outer `Engine::Frame` + 8 phase helpers) を追加。

---

## New APIs

### Save / load

| API | Header | Role |
|-----|--------|------|
| `data::toJson<T>` / `data::fromJson<T>` | `mitiru/data/JsonBinding.hpp` | nlohmann::adl_serializer を経由した薄い wrapper。失敗時は `std::nullopt`。 |
| `data::FromJsonResult<T>` | 同上 | `value` + `error` + `ok()` を持つ詳細結果型。 |
| `data::Versioned<T>` | 同上 | `{ "version": N, "data": <T> }` レイアウトの serialize/deserialize ヘルパ。 |
| `data::MigrationChain<T>` | 同上 | 旧バージョン blob を current version まで走らせる chain。 |
| `data::SaveSchema<T>` | `mitiru/data/SaveSchema.hpp` | `currentVersion` + `MigrationChain<T>` を 1 つにまとめ、`toJsonString` / `fromJsonString` を提供。 |
| `data::Migration` (静的ファクトリ) | `mitiru/data/Migration.hpp` | `backfillField` / `renameField` / `removeField` / `setField` / `transformField` / `compose`. すべて `std::function<Json(Json)>` を返す。 |

`MigrationChain<T>::addStep` は **`MigrationChain&` を返す**ようになり、fluent
chaining が可能 (`schema.migrations().addStep(...).addStep(...)`).
ユーザ型側の opt-in は nlohmann の `NLOHMANN_DEFINE_TYPE_INTRUSIVE` / `_NON_INTRUSIVE`
マクロをそのまま使う。`NON_INTRUSIVE` は型と同じ namespace 内で宣言すること
(`[[nolhmann-non-intrusive-namespace]]`)。

Hot-path 規律: `toJson` / `fromJson` は nlohmann/json 経由で割り当てが起きるため、
**per-frame コードから呼ばない**こと。save point / boot-time content load / editor
ツールが想定。

### Data-driven content

| API | Header | Role |
|-----|--------|------|
| `data::ContentLoader<T>::loadFile(path)` | `mitiru/data/ContentLoader.hpp` | UTF-8 JSON file → typed C++ struct (`FromJsonResult<T>` 返却)。 |
| `ContentLoader<T>::loadString(s)` | 同上 | 文字列 → typed struct。 |
| `ContentLoader<T>::loadJson(json)` | 同上 | `Json` 値 → typed struct。 |
| `ContentLoader<T>::loadFileValidated(path, validator, schemaName)` | 同上 | 上記に加えて `SchemaValidator` チェックを bind 前に実行。 |
| `ContentLoader<T>::loadStringValidated(...)` / `loadJsonValidated(...)` | 同上 | 同。 |
| `data::SchemaImporter::fromJsonSchema` / `fromJsonSchemaFile` | `mitiru/data/SchemaImporter.hpp` | JSON Schema draft-07 (subset) → `mitiru::data::Schema`。 |
| `data::SchemaImportResult` | 同上 | `schema` / `error` / **`warnings`** を保持。`warnings` には未対応 draft-07 feature が `"<scope>: <feature> <reason>"` 形式で列挙される (silent drop を防ぐ)。 |

`ContentLoader<T>` は完全に stateless (全 static method)。`std::vector<T>`、ネスト struct、`Versioned<T>` ラップなど任意の `T` を受ける。

`SchemaImporter` の対応範囲は header 冒頭に明示してあり、`$ref` / `oneOf` /
`allOf` / `anyOf` / `enum` / `pattern` / `additionalProperties` / `definitions` /
`format` / `default` / `items` schema / `$schema` URI などは **warning として捕捉** (エラーにはしない)。
許容される draft-07 構造から逸脱した場合 (top-level が `object` でない等) は `error` に
詳細メッセージを入れて返す。

例: `examples/cpp_data_driven_minimal/` に Stage 5 (SchemaValidator) / Stage 6 (SchemaImporter file) のデモを収録。

### Bridge / view integration

| API | Header | Role |
|-----|--------|------|
| `input::BridgeInputAdapter` | `mitiru/input/BridgeInputAdapter.hpp` | `BridgeActionRouter` signal → `InputMapper::triggerActionFromBridge`. `mapSignalToAction(signal, action)` で 1:1 mapping、`unmapSignal` で取り消し。 |
| `bridge::BridgeEventBusGlue` | `mitiru/bridge/BridgeEventBusGlue.hpp` | `BridgeActionRouter` signal → 型付き `EventBus` event。`mapSignal<Event>(name, builder)` / `mapSignalToTrivial<Event>(name)`. |
| `bridge::BridgeViewPush` integration | `mitiru/bridge/BridgeViewPush.hpp` を消費する各 bridge | `AnimationBridge` / `ParticleBridge` / `VNBridge` / `DialogueBridge` / `TransitionBridge` が現在の view state を CEF へ push。 |

両 adapter とも **destructor で auto-unregister** する (HIGH-4 fix)。adapter が
router より先に破棄されても、router 側で dangling `this` を invoke しない。
非コピー・非ムーブ (参照保持)。

`VNBridge` の view-push は **diff-based short-circuit** が入っており、前回 push
した payload と一致するときは送信を抑止する。テストは
`tests/mitiru/TestBridgeViewPush.cpp` が押し付けカウントで検証する形に更新済み。

### Render

| API | Header | Role |
|-----|--------|------|
| `render::createPipeline2DFor(device, w, h)` | `mitiru/render/BackendInit.hpp` | `device->backend()` で 2D pipeline を dispatch。DX11 は `PostProcessManager` も同時に組み立て、DX12 は専用 `createFromDx12` パス、その他は `nullopt`。 |
| `render::createRenderer3DFor(device, cfg, w, h)` | 同上 | 3D renderer も同様に backend-aware に。 |
| `render::PixelArtFilter` enum | `mitiru/render/RenderPipeline2D.hpp` | `Linear` (default) / `Point` の 2 値。 |
| `Screen::drawPixelGrid(..., PixelArtFilter filter)` オーバーロード | `mitiru/render/Screen.hpp` | DX12 で point-filter PSO を選択するための公開エントリ。 |

`Engine.hpp` 内の `dynamic_cast<Dx11Device*>` / `dynamic_cast<Dx12Device*>` は
削除され、`BackendInit` 側で `device->backend()` enum + `static_cast` に統一。
"engine-internal code には backend 型を leak させない" 規律を回復。

DX12 の point-filter PSO は `RenderPipeline2D::createFromDx12` で **eager 構築**
される (Round 5 perf fix)。従来は first-frame で lazy 構築されており、初回
`drawPixelGrid` 呼び出し時に hitch が出ていた。

### Time / scheduling

`mitiru::time::detail::SmallFunction` を導入し、`mitiru::time::Sequence` の
per-action callable を `std::function` から差し替えた。

- 48 byte の inline SBO バッファ。`sizeof(F) <= 48` かつ `alignof(F) <= alignof(std::max_align_t)` のとき heap allocation なし。
- 超過時は透過的に heap fallback。
- Move-only、`operator bool()`、`operator()()` のみ。`MITIRU_ZONE_NAMED("SmallFunction::invoke")` の Tracy zone を含む。
- Bench は `tests/mitiru/TestSmallFunctionBench.cpp` で SBO / heap 双方を計測。

`mitiru::time::Sequence` も `Sequence::action` で Tracy zone を発行するよう
instrument 済み。recipe は `docs/PROFILING_GUIDE.md` 参照。

---

## Behavior changes & fixes

- **JsonEscape** が `\n` / `\r` / `\t` / `\b` / `\f` の short-form と他の制御バイト (`< 0x20`) を `\u00XX` 形式で正しくエスケープするよう修正 (HIGH-2 fix)。`include/mitiru/bridge/detail/JsonEscape.hpp` を共有実装にし、`AnimationBridge` / `ParticleBridge` 等が同じパスを使う形に揃えた。
- **`SceneDocument`** の reader / writer が対称になった。`writeStr` が escape したものを `readStr` が unescape せず生で取り出していた HIGH-5 を修正。`tests/mitiru/TestSceneDocumentVariables.cpp` で round-trip を担保。
- **`SceneDocument::deserialize`** で variables (KV) が復元されない問題を修正 (Task #20)。
- **Platform cursor capture**: SDL2 / GLFW backend の `setCursorCaptured` が no-op stub だったのを実機にワイヤ。SDL2 は `SDL_SetRelativeMouseMode`、GLFW は `glfwSetInputMode(..., GLFW_CURSOR, GLFW_CURSOR_DISABLED)` を呼ぶ。
- **Bridge view-push の per-frame allocation** を排除 (HIGH-1 perf)。push key を cached `std::string` に変えてホットパスから割り当てを除去。
- **`BridgeInputAdapter` / `BridgeEventBusGlue`** の destructor で auto-unregister (HIGH-4 lifetime fix)。詳細は New APIs セクション参照。
- **`MigrationChain<T>::addStep`** が `MigrationChain&` を返すようになった (fluent)。既存の値返却なし呼び出しは互換。

---

## Refactors

- **`Engine.hpp` split** — 1008 行のヘッダを `include/mitiru/core/detail/Engine_{Run,Frame,Snapshot,Window,Accessors,Init,Audio,AutoTest,Cef,Http,Settings}.hpp` に分割し、`Engine.hpp` 本体は 424 行まで縮小。`include/mitiru/` の "ヘッダ ≤ 800 行" 規律を回復。
- **Backend type leakage removal** — `Engine` のメンバから `Dx11Device` / `Dx12Device` の `dynamic_cast` 経路が完全に消え、`render::BackendInit` の `device->backend()` enum dispatch に統一。Null backend や非対応 backend では `std::optional<...>` の空値を返す。
- **`Sequence` SBO 化** — `std::function` 依存 (常に heap allocation 可能性あり) を `SmallFunction` に置換。callable が小さい限り heap を踏まない。
- **`JsonEscape` 抽出** — bridge 別々に持っていた quoting 実装を `include/mitiru/bridge/detail/JsonEscape.hpp` に共通化。
- **ADR 0003 Accepted** — `docs/adr/0003-json-binding-strategy.md` を Accepted 化。nlohmann macros (`INTRUSIVE` / `NON_INTRUSIVE`) + `Versioned<T>` envelope を engine 標準として確定。

---

## New docs / tools

| Artifact | Purpose |
|----------|---------|
| `docs/PROFILING_GUIDE.md` | Tracy integration (zone macros / capture recipe / SmallFunction SBO vs heap の見方)。`include/mitiru/debug/TracyZones.hpp` の使い方と、Sequence / SmallFunction を含むホットパスのプロファイル手順。 |
| `docs/CEF_SUBPROCESS_DIAGNOSTIC.md` | CEF subprocess の `error_code=63` を切り分けるための H1〜H7 仮説ツリー + ProcMon recipe + preflight 手順。 |
| `docs/CEF_CRT_EXPERIMENT.md` | "libcef Release `/MD` vs Debug `/MDd`" CRT mismatch 仮説を実地検証する 6 phase の実験 protocol。 |
| `docs/adr/0003-json-binding-strategy.md` | JSON binding 戦略 ADR (Accepted)。`JsonBinding` / `SaveSchema` / `ContentLoader` の決定理由をまとめる。 |
| `tools/diagnose_cef_subprocess.py` | CEF subprocess の preflight チェック自動化スクリプト。 |
| `tools/cef_crt_experiment.py` | 上記 6 phase 実験ドライバ。 |
| `examples/cpp_data_driven_minimal/` | Stage 5 (`SchemaValidator`) + Stage 6 (`SchemaImporter` from file) の最小デモ。 |

クロスリファレンス:
- ADR 0001 (`docs/adr/0001-cpp-gameplay-cef-view-only.md`) — C++ engine ピボット。Bridge は signal-only の前提。
- ADR 0002 (`docs/adr/0002-remove-lua-scripting.md`) — Lua 廃止。
- `docs/CPP_GAMEPLAY_GUIDE.md` — `JsonBinding` / `SaveSchema` / `ContentLoader` / `BridgeInputAdapter` を含む primitive 一覧。
- `docs/BRIDGE_API_CONTRACT.md` — bridge view-push / signal 規約。
- `docs/HYBRID_RUNTIME.md` — JS ↔ C++ レイヤー分担。

---

## Test suite

ctest: **2110 → 2238 (+128 tests)** on `main`. Highlights:

- `TestJsonBinding` / `TestSaveSchema` / `TestContentLoader` — typed JSON round-trip と migration chain の境界ケース。
- `TestSchemaImporter` — draft-07 supported subset + warning パス (length-key 警告 / required-non-array 警告 / unsupported keyword 警告)。
- `TestMigration` — `backfillField` / `renameField` / `removeField` / `setField` / `transformField` (absent-field no-op を含む) / `compose` の単体ケース。
- `TestBridgeViewPush` — `AnimationBridge` / `ParticleBridge` / `VNBridge` の view-push assertion。`VNBridge` は diff-based short-circuit を考慮した push-count 検証に更新。
- `TestBridgeInputAdapter` / `TestBridgeEventBusGlue` — destructor による auto-unregister で dangling 呼び出しが起きないことの regression test。
- `TestSmallFunctionBench` — SBO / heap 双方の構築・呼び出しコストを bench。
- `TestSmallFunctionTracy` — Tracy zone が SmallFunction::invoke を覆っていることの structural test。
- `TestSceneDocumentVariables` — variables KV の deserialize round-trip。
- `TestJsonEscape` — `\n` / `\r` / `\t` / `\b` / `\f` + 制御バイトの escape regression。
- `TestContentLoaderFileNotFound` — `loadFileValidated` の存在しないファイルパス。

すべての new test は header-only / `NullDevice` で実行可能で、CI 上の Linux / Windows / macOS いずれでも green。

---

## Round 8 追補 (2026-05-15 続)

Rounds 1–5 の merge (`0e050fc2`) 後に走らせた Round 8 (`77b88f0c` + doc follow-up `b9e1828c`) で、Known limitations に残っていた 1 件と、`SceneDocument` 周りの長期負債、Tracy instrumentation の engine-side カバレッジを片付けた。

### 1. `Engine_Init.hpp` を role 別に再分割 (`90ee5db6`)

旧 `include/mitiru/core/detail/Engine_Init.hpp` (398 行) を削除し、4 つの role file に分割した。`Engine_Init_Lifecycle.hpp` (145 行, `initialize` 本体) / `Engine_Init_Pipeline.hpp` (81 行, pipeline + viewport setup) / `Engine_Init_Font.hpp` (159 行, font loading) / `Engine_Init_Input.hpp` (34 行, `applyInjectedInput`) という分割。`include/mitiru/core/Engine.hpp:444-460` で 4 ファイルを alphabetical に `#include` し、STATIC build path 用の `src/core/Engine.cpp` も同じ並びで揃えた。これにより Rounds 1–5 時点で残っていた "Engine.hpp 分割は機械的 split" 由来の最後の大ヘッダが解消した。

### 2. `SceneDocument` を nlohmann::json バックエンドに移行 (`47afb4e2` + テスト `0104118b`)

`include/mitiru/core/SceneDocument.hpp` を 1382 → 923 行 (−459 行, 約 33% 削減) に縮小。Scene 本体 + 7 個の trait class が個別に持っていたハンドロール JSON helper (~25 個) を削除し、nlohmann::json の `to_json` / `from_json` に統一した。ADR 0002 (Lua 廃止) で確定した「.lua のシナリオ / セーブはすべて .json」方針と整合する形で、scene save 経路の JSON 取扱いが engine 全体で同じ nlohmann 経由に揃った。

意図的な wire format 変更が 2 点あり、release note 化しておく:

- **key ordering が alphabetical になる**: nlohmann のオブジェクトは `std::map` バックなので、書き出し時に key が昇順に並ぶ。`load → toJson → load → toJson` は **byte-identical** であることを `TestSceneDocumentRoundtrip` (test #2251) が enforce する。
- **trait-object の whitespace が compact になる**: 旧ハンドロール writer は indent / 改行を独自に挿入していたが、nlohmann::json の compact serializer に統一した。

新規テスト `tests/mitiru/TestSceneDocumentRoundtrip.cpp` (378 行) は 16 ケースで unicode / emoji / JSON metachar / 空 scene / 複数 node / byte-identical-stability / garbage input / legacy format / `CustomTrait` を網羅する。

副次効果として、旧 `AudioTrait::fromJson` が writer 側で出力していた `audioPath` を silently drop していた既存 bug が露見し、移行と同時に解消した。

### 3. Tracy zone を engine hot path に展開 (`f08fed08`)

`include/mitiru/core/detail/Engine_Frame.hpp` に `MITIRU_ZONE_NAMED` を 9 本入れた。outer の `Engine::Frame` 1 本に加えて `Engine::Input` / `Engine::MouseScaling` / `Engine::FixedUpdate` / `Engine::Render` / `Engine::Present` / `Engine::CefComposite` / `Engine::AutoCapture` / `Engine::HttpPoll` の 8 phase。`MITIRU_HAS_TRACY` が off のときは `((void)0)` に展開されてゼロオーバーヘッド。Rounds 1–5 で既に入っていた `SmallFunction::invoke` / `Sequence::action` の zone はそのまま保持 (二重定義はしていない)。

### 4. テスト総数 2238 → 2254 (+16)

増分はすべて `TestSceneDocumentRoundtrip` の round-trip ケース。`NullDevice` で走り、Linux / Windows / macOS 共通で green。

---

## Known limitations / future work (engine-side only)

- **CEF `--single-process` workaround が依然残る**。`MitiruCefApp.hpp:94-107` で hard-coded。`docs/CEF_CRT_EXPERIMENT.md` の 6 phase protocol を未実施 (実機 build を要する)。CRT mismatch 仮説の確定 / 反証はそのプロトコル完了後。
- **Tracy production measurement 未着手**。`PROFILING_GUIDE.md` のレシピは整備済みだが、実プロジェクトでの SBO vs heap 比率や Sequence hot path の measured 値はまだ collect していない。
- **`SchemaImporter` は draft-07 の subset**。`$ref` / `oneOf` / `allOf` / `anyOf` / `enum` / `pattern` / `additionalProperties` / `definitions` / `format` / `default` / `items` schema / `$schema` などは `SchemaImportResult::warnings` に列挙されるのみで `Schema` には反映されない。consumer が warning を inspect することで fidelity loss を検知可能。本格対応は `SchemaField` が enum / pattern / length 等のフィールドを獲得した後。
- **DX12 path** は `createFromDx12` 専用ルートを使う。`RenderPipeline2D::createFromDevice` (汎用) は DX12 backend では PSO / root signature が bind されず silent no-op になるため、現状 `BackendInit::createPipeline2DFor` は DX12 で専用パスを明示的に呼ぶ。汎用パスへの一本化は今後の課題。

> Round 5 時点で挙げていた "`detail/Engine_Init.hpp` (398 行) など機械的 split が粗い" は Round 8 で解消した (`Engine.hpp:444-460` で 4 role file を `#include`)。詳細は上の Round 8 追補 §1 を参照。

---

## How to consume (engine-side)

すべて header-only な追加なので、`#include <mitiru/...>` のパスを足すだけで使える
(該当ヘッダ参照)。`Engine.hpp` 分割は internal なリファクタで、`mitiru::Engine`
の public API は不変 (ABI 互換)。`MigrationChain::addStep` の戻り値変更は
**source-compatible** (戻り値を捨てている既存コードは何も変えなくて良い)。

`drawPixelGrid` の filter オーバーロードはデフォルト引数 `PixelArtFilter::Linear`
を持つので、既存 caller は無変更で従来挙動を保つ。

`BridgeInputAdapter` / `BridgeEventBusGlue` を採用するときは、両者とも
non-copyable / non-movable で参照を保持するため、**`BridgeActionRouter` /
`InputMapper` / `EventBus` より長生きしない**ことを構造的に保証すること
(local 変数で同 scope に並べる、または同じ owner オブジェクトに member として
持たせる)。
