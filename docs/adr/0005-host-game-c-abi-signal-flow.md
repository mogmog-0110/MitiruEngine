# ADR 0005 — Host-Game 境界は C-only signal flow

- **Status:** Accepted (2026-05-20). 実装 ✅ (commits `47e0a43a` / `204e5671` / `e768e419`)
- **Context:** v0.2.0 step 2 で game を DLL 化する loader (`Engine::loadModule`)
  を実装。続く step 3 (hello_game を DLL 移植) で「**DLL から engine の機能
  (CEF / requestStop / capture / inspectable) にどうアクセスさせるか**」が
  設計判断として浮上した。
- **Supersedes / extends:** ADR 0001 (CEF を UI overlay 化) で `signal-only`
  bridge 原則を確立済。本 ADR はこれを **DLL boundary にも一般化** する。
- **Obsoletes:** 旧 v0.2.0 step 5 (「Inspectable registry DLL-aware 化」)。
  本 ADR の構造で `InspectableRegistry` lambda は **DLL boundary を跨がなくなった** ので、
  unload 時の stale function pointer 問題が **構造的に発生しなくなった** 。

## 決定

**Game DLL は host (engine) の object pointer を一切持たない。DLL は純関数に近い形で実装される:**

```
(memory_in, input_snapshot, dt) → (memory_out, render_commands, intents)
```

### 3 つの不変条件

1. **Game は host の object pointer を保持しない**
   - `Engine*`、`Screen*` (per-frame 引数を除く)、`CefContext*`、`InputState*`、その他 host 所有の class への pointer は持たない
   - DLL の GameMemory が host の object を指す pointer を含むのは禁止
2. **Host が必要データを毎フレーム POD で push する**
   - `InputSnapshot` (POD struct: key 配列 / マウス座標 / ボタン状態) を on_update 引数で渡す
   - `Screen*` は on_draw の引数で per-frame 渡す (DLL は保持しない)
   - `dt` は float
   - STL 型 (`std::string` / `std::vector` / `std::function`) は **境界跨ぎ禁止**
3. **DLL が host に何かさせたい時は intent field に書く**
   - `FrameIntents` POD struct を on_update が in/out で受け取る
   - 「quit したい」「screenshot 撮りたい」「CEF JS 実行したい」等は intent flag / message として書く
   - Host が次フレーム頭で intents を読んで実行する

## なぜ — 失敗モード分析

DLL boundary で発生し得る失敗を列挙し、各 design 案がそれぞれにどう対処するかを表で示す:

| # | 失敗モード | 「Engine* を渡す」案 | 本 ADR (C-only signal) |
|---|---|---|---|
| F1 | host と DLL の `Engine` 定義が version mismatch (header-only inline がそれぞれの TU で展開) | 黙って memory corruption。検出不能 | POD のみ → `version` field で実行時検出 |
| F2 | DLL が host の C++ class pointer を保持 → reload 後に layout 仮定がズレる | クラッシュ or silent breakage | pointer を持たないので発生し得ない |
| F3 | DLL の lambda (e.g. Inspectable registry) が host から呼ばれる; DLL unload 後に死ぬ | step 5 の追加コストで対処を試みる | 「callback は ModuleApi の 4 つだけ」を構造保証 — 追加対処不要 |
| F4 | `std::string` / `std::function` の allocator / vtable 不一致 | DLL と host が同 compiler なら動く (= 暗黙の前提) | POD のみ境界なので発生不可能 |
| F5 | hot reload 直後、stale function pointer 経由で死ぬ | ModuleApi callback は null 化対処したが、他経路は未対処 | callback の所在が ModuleApi の 4 つに **構造的に** 限定 |
| F6 | snapshot / replay / time-travel が「engine 内部にも state がある」せいで再現不能 | engine state を別途記録する仕組みが必要 | **GameMemory だけが state** → memcpy で完全再現 |

## 既存哲学との関係

これは [アトミックツール哲学](../SCOPE.md) の **DLL 境界レベルへの自然な拡張**:

- 「**必要なものしか画面に出さない**」 → DLL から見える host 機能を **enumerated** にする。「engine.foo() で何でも呼べる」誘惑が構造的に消える
- 「**1 ツール = 1 関心事**」 → Game DLL は 1 関心事 (gameplay) のみ。platform service (audio mixer / file I/O / window) は host の関心事として完全分離
- 「**bridge は signal-only**」 (ADR 0001) → CEF JS との signal-only 規約を **DLL 境界にも** 適用しただけ

## 5 軸との関係

本 ADR は新規軸ではなく、**既存の 2 軸を構造的に強化**する:

### 軸 2 (time-travel inspector) との関係

GameMemory が **唯一の state** であることを構造保証する → ring buffer に GameMemory の snapshot を memcpy するだけで time-travel が完成。「engine 内部に隠れた state があって rewind しても再現しない」問題が発生し得ない。

### 軸 4 (deterministic + replay) との関係

同上。InputSnapshot を frame ごとに記録 + GameMemory の初期 state を記録 → replay は「同じ InputSnapshot を順次 push する」だけで bit-exact 再現。host 側の state 漏れの懸念ゼロ。

## トレードオフ

### コスト

- **新 host capability を game が使いたい時、intent struct への明示的拡張が必要**
- 例: 「ゲームから OS の通知を出したい」→ intent enum に `kShowOsNotification` を追加 + host の handler 実装
- 「engine.foo() を呼ぶだけ」のショートカットが存在しないので、host capability の追加は常に「これは本当に必要か」を問わされる

これは **コストではなく feature**: 5 軸のいずれかを実際に強化する機能しか追加されなくなる構造を作る。

### CEF state push の特殊事情

hello_game は CEF StateStore に `view.hud.hp = 80` のような細かい push を多数行う。これを intent 経由で書き換える方法:

- **Option A**: GameMemory に `HudState` POD struct を持たせる。Host が毎フレーム sync。差分しか送らない optimization は host 側で
- **Option B**: intent struct に bounded message queue を持たせ、key/value pair を enqueue。Host が drain して StateStore に push

step 3 では Option A を採用 (game 側コード変更が最小、HUD は数十 field なので POD に乗る)。Option B は inspectable registry redesign で必要になる (動的な item 数)。

## 実装 path (step 3-4 で完了)

1. ✅ `include/mitiru/module/ModuleApi.hpp` を v1 → v2 bump
   - `InputSnapshot` POD 定義 (keysDown/justPressed/justReleased + mouse + actionEvents)
   - `FrameIntents` POD 定義 (requestStop / requestScreenshot / paletteToggle / statePushes[] / exportedInspectables[] / jsToExecute)
   - on_update signature 変更: `(memory, dt, const InputSnapshot*, FrameIntents*)`
2. ✅ hello_game を `HelloGameMemory` struct + 自由関数群に refactor (commit `47e0a43a`)
3. ✅ Engine 側で per-frame に InputSnapshot を組み立て、FrameIntents を drain (`Engine_Module.hpp` の `buildModuleInputSnapshot()` / `drainModuleFrameIntents()`)
4. ✅ CEF state push は `FrameIntents::statePushes[]` 経由で engine が `m_moduleStateStore->set(key, value)` を呼ぶ。`StateStore::onActionFallback` が DLL の任意 action を `actionEvents[]` に queue する
5. ✅ Inspectable は GameMemory 内で DLL が JSON を `intent.exportedInspectables[]` に書く / host が SharedSnapshot に write — **旧 step 5 はこれで完全 obsolete**。`InspectableRegistry` (lambda ベース) は **non-DLL モード専用** であり、DLL boundary を跨がないため hot reload 時の stale pointer 問題が発生しない
6. ✅ DLL hot reload は `mitiru_host --watch` が mtime polling で `engine.reloadModule()` を呼ぶ。`HelloGameMemory` pointer が host 所有で reload 跨ぎで生存 (commit `e768e419`)

## 既存 consumer への影響

- **v0.2.0 リリース前** に決定したので、外部 consumer (KaeruCrape / hato / Mathlands / pandd-dodo) は migration 一回のみで済む
- **v0.2.0 から後** に決めると、既存 DLL が ABI v1 を持っている前提で v2 への upgrade path が必要になり、本 ADR の安全性が損なわれる

タイミング的に**今**決定する以外の選択肢はない。

## 参考

- Casey Muratori "Handmade Hero" — Day 022 (Win32 DLL hot reload pattern の原典)
- Unreal Engine 4 Live Coding (同様の C-API isolation pattern)
- 本 engine の ADR 0001 (CEF signal-only bridge) — 同原理の前身
