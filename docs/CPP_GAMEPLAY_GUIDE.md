# C++ Gameplay Guide — MitiruEngine

> **対象読者**: MitiruEngine 上で **初めて C++ gameplay を書く** 開発者。
> consumer 側ゲームの作者が、エンジンが提供する基本プリミティブを
> 組み合わせてシーン遷移・状態管理・タイマー・UI signal 配線を行うための入口ガイド。

---

## 1. Overview

MitiruEngine は 2026-05-14 に **Siv3D ロールモデル** に寄せた C++ engine 路線へピボットした。
gameplay の決定権は **すべて C++** にあり、CEF (HTML/CSS/JS) は **View 専用** に格下げされた。

3 行で整理すると:

1. **Gameplay は C++**: state machine / シーン遷移 / タイマー / 判定はすべて C++ に置く。
2. **CEF は UI/HUD/演出**: 描画と「ユーザ操作の検出」だけを担当。JS で条件分岐や状態保持はしない。
3. **Bridge は signal-only**: JS→C++ は `ui.button.start` のような「何が起きたか」だけ。
   C++→JS は `view.<sub>.<key>` 形式の「何を表示すべきか」だけ。
   gameplay 関数の RPC は禁止。

関連 doc:

- [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — レイヤー分担の歴史的経緯
- [BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md) — bridge 責務定義 (signal-only)
- [cpp-gameplay-api-gaps.md](cpp-gameplay-api-gaps.md) — gameplay プリミティブのロードマップ
- [examples/cpp_gameplay_minimal/](../examples/cpp_gameplay_minimal/) — 動く最小例

---

## 2. 最小例

`examples/cpp_gameplay_minimal/main.cpp` が **ウィンドウを出さずに (headless) 動く最小ループ** を実装している。
ビルド:

```bash
cmake --preset default
cmake --build build --config Debug --target mitiru_cpp_gameplay_minimal
```

main loop のスケルトンはこれだけ:

```cpp
mitiru::scene::SceneRouter        router;
mitiru::input::BridgeActionRouter actions;
mitiru::bridge::BridgeViewPush    view("cooking", setSink, emitSink);

router.push(std::make_unique<TitleScene>(router, actions, view));

while (!router.empty()) {
    router.update(dt);   // 内部で top scene の onUpdate(dt) を呼ぶ
    // 実機では描画 / 入力ポーリング / bridge ポンプはここに挟む
}
```

各 Scene は `mitiru::scene::IScene` を継承し、`onEnter / onUpdate / onExit` を実装する。

---

## 3. Primitives 早見表

| やりたいこと | プリミティブ | ヘッダ | 中心 API |
|---|---|---|---|
| 画面遷移 (Title → Stage → Result) | `SceneRouter` + `IScene` | `mitiru/scene/SceneRouter.hpp` | `push` / `pop` / `replace` / `update` |
| シーン内の状態 (Idle → Cook → Done) | `StateMachine<T>` | `mitiru/fsm/StateMachine.hpp` | `transition` / `setGuard` / `setOnTransition` |
| 焼き時間 / カウントダウン | `Timer` | `mitiru/time/Timer.hpp` | `tick` / `expired` / `progress` / `reset` |
| スキル再使用待ち / レート制限 | `Cooldown` | `mitiru/time/Cooldown.hpp` | `tick` / `ready` / `trigger` |
| 演出 (wait → action → wait …) | `Sequence` | `mitiru/time/Sequence.hpp` | `wait` / `action` / `tick` / `done` |
| UI signal → gameplay action | `BridgeActionRouter` | `mitiru/input/BridgeActionRouter.hpp` | `registerHandler` / `unregisterHandler` / `dispatch` |
| HUD / 演出を view に push | `BridgeViewPush` | `mitiru/bridge/BridgeViewPush.hpp` | `set` / `emit` |
| UI signal を InputMapper Action にマージ | `BridgeInputAdapter` | `mitiru/input/BridgeInputAdapter.hpp` | `mapSignalToAction` / `unmapSignal` |
| UI signal を型付き EventBus event に変換 | `BridgeEventBusGlue` | `mitiru/bridge/BridgeEventBusGlue.hpp` | `mapSignal` / `mapSignalToTrivial` / `unmap` |
| C++ struct ↔ JSON シリアライズ | `JsonBinding` | `mitiru/data/JsonBinding.hpp` | `toJson` / `fromJson` / `fromJsonVersioned` / `MigrationChain<T>` |
| Save スロットの型付き読み書き | `SaveSchema<T>` | `mitiru/data/SaveSchema.hpp` | `toJsonString` / `fromJsonString` / `migrations()` |
| Balance / dialogue 等の JSON content 読込 | `ContentLoader<T>` | `mitiru/data/ContentLoader.hpp` | `loadFile` / `loadString` / `loadJson` |

すべて header-only。include して使うだけで OK。

---

## 4. Composition Patterns

ここがメイン。プリミティブは単体ではなく **組み合わせ** で gameplay を表現する。

### Pattern A — Scene + StateMachine の二層構造

**意図**: 「画面の切り替え」と「画面内の状態」を **別レイヤー** として扱う。

```cpp
enum class CookState { Idle, Cooking, Done };

class CookingScene final : public mitiru::scene::IScene {
public:
    CookingScene() : m_fsm(CookState::Idle) {
        m_fsm.setOnTransition([this](CookState from, CookState to) {
            // 状態が変わるたびに view へ push 等
        });
    }
    void onUpdate(float dt) override {
        // m_fsm.state() を見て分岐
    }
private:
    mitiru::fsm::StateMachine<CookState> m_fsm;
};
```

- `SceneRouter` は「Title → Cooking → Result」のような **画面単位** の遷移を持つ。
- `StateMachine<T>` は **1 シーン内** の局所状態を持つ。
- **ハマる点**: 状態が deeply nested (5 段以上) になってきたら、それは「別 scene に切り出すべきサイン」。
  StateMachine を多段化するより `router.push(...)` した方が読みやすい。

### Pattern B — Timer + StateMachine で時間駆動の遷移

**意図**: 「N 秒経ったら次の状態へ」を素直に表現する。
`Timer.expired()` と `StateMachine.transition()` を組み合わせる。

```cpp
void onUpdate(float dt) override {
    if (m_fsm.state() == CookState::Cooking) {
        m_bakeTimer.tick(dt);
        if (m_bakeTimer.expired()) {
            m_fsm.transition(CookState::Done);
        }
    }
}
```

- 状態に入った瞬間に `m_bakeTimer.reset()` を呼ぶ習慣をつける (前回の残り時間を引きずらない)。
- **ハマる点**: `expired()` を毎フレーム見て副作用を発火し続けないこと。
  `transition()` した直後は `state()` が変わっているので二重発火しないが、
  「expired のとき何かする」ロジックは必ず状態遷移にくくる。

### Pattern C — Sequence で演出スクリプト

**意図**: 章間カットイン / Title intro / 一連の演出のような **スクリプテッド時系列** を書く。

```cpp
m_intro
    .wait(0.5f)
    .action([this]{ m_view.emit("intro.start", "{}"); })
    .wait(0.3f)
    .action([this]{ m_startReady = true; });

// update 内で:
m_intro.tick(dt);
```

- `wait` と `action` を自由に交互に並べられる。`action` は即時実行で時間を消費しない。
- `done()` で終了判定。終わったあとの `tick` は no-op。
- **ハマる点**: `action` のラムダ内で **その Sequence 自身を mutate しない** (例: 同じ `m_intro` に `wait(...).action(...)` を追加する)。cursor 不整合の原因になる。
  追加演出を流したいなら **別の Sequence インスタンス** を持つこと。

### Pattern D — BridgeActionRouter + Scene のディスパッチ

**意図**: CEF (UI) から飛んでくる signal を **gameplay の意思決定に変換** する境界を作る。
JS には「ボタンが押された」だけ言わせて、**どの scene に遷移するか / どんな状態変化を起こすかは C++ が決める**。

```cpp
m_actions.registerHandler("ui.button.start",
    [this](std::string_view /*payload*/) {
        if (!m_startReady) { return; }
        m_router.push(std::make_unique<CookingScene>(m_router, m_actions, m_view));
    });

// 後始末: scene が抜けるときに必ず解除
void onExit() override {
    m_actions.unregisterHandler("ui.button.start");
}
```

- handler 登録は **scene 単位で対称に**: `onEnter` / ctor で登録、`onExit` で `unregisterHandler`。
- 同一 signal 名を二重登録すると **last-write-wins** で上書きされる。複数 scene が同じ signal を奪い合わないように責任分界点を決める。
- **ハマる点**: handler 内で重い処理をしない。`dispatch()` は呼び出しスレッドで同期実行されるため、長時間ブロックすると bridge ポンプ全体が詰まる。重い処理は flag を立てて次の `onUpdate` で消化する。

### Pattern E — BridgeViewPush で state を view に流す

**意図**: gameplay 側 (C++) で計算した結果を、JS は **描画するだけ** に徹させる。

```cpp
m_view.set("hp", "80");                            // → "view.cooking.hp" = "80"
m_view.set("state", "Cooking");                    // 状態ラベル
m_view.emit("damage", "{\"amount\":12,\"crit\":true}");  // one-shot エフェクト
```

- **`set` は retained**: 最新値が保持され、後から接続した view も読み取れる (HP / score 等)。
- **`emit` は one-shot**: その瞬間のイベント。聴いていない view には届かない (ヒットエフェクト / SE トリガ等)。
- key は **`view.<subsystem>.<key>`** 形式に統一される (ctor の `subsystem` で prefix を固定)。
  詳細命名規約は [BRIDGE_API_CONTRACT.md §3](BRIDGE_API_CONTRACT.md) を参照。
- **ハマる点**:
  - JS で「HP が 50 以下なら赤くする」のような判定を書きたくなったら **負け**。C++ 側で `view.cooking.hpLow = "true"` を別途 push し、JS は class を toggle するだけにする。
  - 値はあらかじめ JSON 文字列に整形して渡す (数値は `std::to_string`、文字列は `"\"..."\""` で quote)。

---

### Pattern F — BridgeInputAdapter で UI ボタンとキー入力を同じ Action にする

**意図**: 「キーボードでも UI ボタンでも同じ Action として gameplay は受け取る」を仕組みで保証する。

```cpp
mitiru::InputMapper                   mapper;
mitiru::input::BridgeActionRouter     router;
mitiru::input::BridgeInputAdapter     adapter(router, mapper);

// 物理入力
mapper.bindKey("Fire", mitiru::KeyCode::Space);

// UI 入力 (CEF DOM)
adapter.mapSignalToAction("ui.button.fire", "Fire");

// gameplay は分岐なしで同じ Action を見る
if (mapper.isActionPressed("Fire")) { fire(); }
```

- bridge-triggered action は **one-shot**: `isActionPressed` だけが true を返し、`isActionDown` は通常 binding と同じ意味のまま (continuous press の概念は bridge にない)。
- フレーム末で `mapper.endFrame()` を **必ず**呼ぶ。さもないと一度発火した action が永遠に "pressed" 状態のまま居座る。
- **ハマる点**: `mapSignalToAction` を二重登録すると `BridgeActionRouter` の last-write-wins で **上書き** される。複数 signal を 1 action にしたい場合は signal 名側を増やす (例: `ui.button.fire` と `ui.gesture.tap-fire` 両方を `Fire` action にマップ → これは別 signal 名なので両方有効)。

### Pattern G — BridgeEventBusGlue で UI signal を型付き event に変換

**意図**: 「ボタンが押された」だけでなく「どのスロットか」など payload を **型** で保持して gameplay に届ける。

```cpp
struct UiFireEvent { std::string slot; };

mitiru::EventBus                       bus;
mitiru::input::BridgeActionRouter      router;
mitiru::bridge::BridgeEventBusGlue     glue(router, bus);

glue.mapSignal<UiFireEvent>("ui.button.fire",
    [](std::string_view payload) {
        return UiFireEvent{ std::string(payload) };
    });

bus.subscribe<UiFireEvent>([](const UiFireEvent& e) {
    handleFire(e.slot);   // gameplay は型安全に受け取る
});
```

- payload を捨てて event を default 構築だけしたい場合は `mapSignalToTrivial<E>(signal)`。
- `BridgeInputAdapter` との使い分け: 「Action として **押された/離された** だけ知りたい」なら Adapter、「payload を **構造化** したい」なら Glue。両方を併用しても OK (同じ signal 名は last-write-wins なので片方だけ)。

### Pattern H — SaveSchema<T> + MigrationChain で save の互換性管理

**意図**: 「v1 で書いた save を v3 の build で読めるようにする」を 1 か所で宣言する。

```cpp
struct PlayerSave { int level; std::string name; };
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlayerSave, level, name)

mitiru::data::SaveSchema<PlayerSave> schema(/*currentVersion=*/3);
schema.migrations().addStep(1, 2, [](mitiru::data::Json data) {
    data["name"] = "unnamed";          // v1 には name が無かった
    return data;
});
schema.migrations().addStep(2, 3, [](mitiru::data::Json data) {
    if (!data.contains("level")) data["level"] = 1;  // v2 で level 追加
    return data;
});

// 書き込み (常に最新 version で書く):
std::string blob = schema.toJsonString(PlayerSave{ 5, "Alice" });
saveStore.write(slot, blob);

// 読み出し (古い version は自動で migrate される):
auto raw = saveStore.read(slot);
auto result = schema.fromJsonString(raw);
if (result.ok()) { auto& save = *result.value; }
```

- envelope は `{ "version": N, "data": ... }` 形式。`toJsonString` が自動で付ける。
- **ハマる点**: migration step は **append-only**。一度 production で書いた step を後から削ったり順序入れ替えたりすると古い save を壊す。
- `JsonBinding` を直接使うこともできるが、save 用途は **SaveSchema を経由するのが推奨**。「version が混在しうるデータ」を 1 か所で表現できる。

#### Migration ヘルパー (`mitiru::data::Migration`)

手書きの lambda で `data["x"] = ...` を書き続けると bug の温床になる。よくある操作 (backfill / rename / remove / set / transform) は `Migration::*` 経由で **宣言的に** 書ける。`compose` で 1 step に複数操作をまとめることも可能。

```cpp
using mitiru::data::Migration;
using mitiru::data::Json;

schema.migrations()
    .addStep(1, 2,
        Migration::backfillField("unlockedRecipes", Json::array({ "Cookie" })))
    .addStep(2, 3,
        Migration::renameField("score", "totalScore"))
    .addStep(3, 4, Migration::compose({
        Migration::removeField("legacyDebug"),
        Migration::transformField("level", [](Json v) {
            return Json(v.get<int>() + 10);
        }),
    }));
```

- すべて `std::function<Json(Json)>` を返すので `addStep` にそのまま渡せる。
- `addStep` は `MigrationChain&` を返すので上のように **fluent chain** で書ける。 1 step ずつ `schema.migrations().addStep(...)` と書いても等価。
- 引数は **by value capture** されるので、factory のスコープを抜けても安全。
- ヘッダー: `include/mitiru/data/Migration.hpp`。

### Pattern I — ContentLoader<T> で balance/dialogue を 1 行ロード

**意図**: 開発者が JSON で書いた content を **型** として受け取り、validate/parse を毎回手書きしない。

```cpp
struct BalanceRow { std::string name; int cost; float winRate; };
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BalanceRow, name, cost, winRate)

auto result = mitiru::data::ContentLoader<std::vector<BalanceRow>>::loadFile(
    "data/balance.json");
if (result.ok()) {
    for (const auto& row : *result.value) {
        applyBalance(row);
    }
} else {
    log_error(result.error);   // パス不正 / JSON 不正 / 型 mismatch 等
}
```

- 失敗時の `result.error` は人間可読 (nlohmann::json の例外メッセージ or 自前メッセージ)。
- content は **不変** が前提。書き戻したい場合は SaveSchema の方を使う。
- runtime schema 検証は別物 (`include/mitiru/data/SchemaValidator.hpp`)。テンプレートでは型一致が静的に強制されるため、AI 生成コンテンツ等で **structural** な検証が要る場合のみ追加で使う。

---

## 5. アンチパターン

BRIDGE_API_CONTRACT.md と整合する 5 つの NG パターン。

- **JS で state machine を持つな**
  「料理の状態は cooking.js が管理する」は禁止。`StateMachine<CookState>` を C++ に置く。

- **JS が条件分岐 (tutorial 完了判定 / 解放フラグ等) を持つな**
  「tutorial done なら START を有効化」を JS で書かない。C++ が `view.title.startEnabled` を push し、JS は class を付け外しするだけにする。

- **JS が「次のシーンはどこ」を判定するな**
  `ui.button.start` を発火するのは JS の責務だが、「次は CookingScene」と決めるのは **C++ の Scene/Router** の責務。
  `signal: "scene.goto.cooking"` のような **transition を JS に書かせる signal 名** を作らないこと。

- **bridge を太らせて gameplay 関数の RPC にするな**
  `signal: "game.canOpenDoor?"` のような問い合わせ / 計算依頼は NG。
  gameplay の判定は C++ 内で完結させ、結果だけ `view.*` に push する。

- **逆方向に: C++ が JS に「次に何を考えるか」を聞くな**
  C++ → JS は **常に決定済みの表示指示**。`emit("ask.player.choice", ...)` で答えを待つような片務 RPC は禁止。
  選択肢の提示は `set` で出し、選択結果は `BridgeActionRouter` 経由で signal として受け取る。

**まとめ**: JS は「描画」と「何が起きたか発火」だけ。C++ が「判断」と「何を表示するか決定」を持つ。

---

## 6. References

- [docs/HYBRID_RUNTIME.md](HYBRID_RUNTIME.md)
- [docs/BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md)
- [docs/cpp-gameplay-api-gaps.md](cpp-gameplay-api-gaps.md)
- [examples/cpp_gameplay_minimal/main.cpp](../examples/cpp_gameplay_minimal/main.cpp)

ヘッダ:

- `include/mitiru/scene/IScene.hpp`
- `include/mitiru/scene/SceneRouter.hpp`
- `include/mitiru/fsm/StateMachine.hpp`
- `include/mitiru/time/Timer.hpp`
- `include/mitiru/time/Cooldown.hpp`
- `include/mitiru/time/Sequence.hpp`
- `include/mitiru/input/BridgeActionRouter.hpp`
- `include/mitiru/input/BridgeInputAdapter.hpp`
- `include/mitiru/bridge/BridgeViewPush.hpp`
- `include/mitiru/bridge/BridgeEventBusGlue.hpp`
- `include/mitiru/data/JsonBinding.hpp`
- `include/mitiru/data/SaveSchema.hpp`
- `include/mitiru/data/Migration.hpp`
- `include/mitiru/data/ContentLoader.hpp`
