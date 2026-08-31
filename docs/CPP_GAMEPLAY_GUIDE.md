# C++ Gameplay Guide: MitiruEngine

> **対象読者**: MitiruEngine上で 初めてC++ gameplayを書く 開発者。
> consumer側ゲームの作者が、エンジンが提供する基本プリミティブを
> 組み合わせてシーン遷移・状態管理・タイマー・UI signal配線を行うための入口ガイド。

---

## 1. Overview

MitiruEngineは2026-05-14にSiv3DをロールモデルとするC++ engine路線へピボットした。
gameplayの決定権は すべてC++にあり、CEF (HTML/CSS/JS)はView専用に格下げされた。

3行で整理すると:

1. **GameplayはC++**。state machine / シーン遷移 / タイマー / 判定はすべてC++に置く。
2. **CEFはUI/HUD/演出**。描画と「ユーザ操作の検出」だけを担当。JSで条件分岐や状態保持はしない。
3. **Bridgeはsignal-only**。JS→C++は`ui.button.start`のような「何が起きたか」だけ。
   C++→JSは`view.<sub>.<key>`形式の「何を表示すべきか」だけ。
   gameplay関数のRPCは禁止。

関連doc:

- [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — レイヤー分担(C++ gameplay + CEF view)
- [BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md) — bridge責務定義(signal-only)
- [examples/html_menu/](../examples/html_menu/) — HTMLの操作をC++が受ける動くサンプル

---

## 2. 最小例

gameplay側のmain loopは、これだけのスケルトンで回る。プリミティブを継ぐだけで、
ウィンドウを出さない(headless)ループとしても動く。

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

各Sceneは`mitiru::scene::IScene`を継承し、`onEnter / onUpdate / onExit`を実装する。

---

## 3. Primitives早見表

| やりたいこと | プリミティブ | ヘッダ | 中心API |
|---|---|---|---|
| 画面遷移(Title → Stage → Result) | `SceneRouter` + `IScene` | `mitiru/scene/SceneRouter.hpp` | `push` / `pop` / `replace` / `update` |
| シーン内の状態(Idle → Cook → Done) | `StateMachine<T>` | `mitiru/fsm/StateMachine.hpp` | `transition` / `setGuard` / `setOnTransition` |
| 焼き時間 / カウントダウン | `Timer` | `mitiru/time/Timer.hpp` | `tick` / `expired` / `progress` / `reset` |
| スキル再使用待ち / レート制限 | `Cooldown` | `mitiru/time/Cooldown.hpp` | `tick` / `ready` / `trigger` |
| 演出(wait → action → wait …) | `Sequence` | `mitiru/time/Sequence.hpp` | `wait` / `action` / `tick` / `done` |
| UI signal → gameplay action | `BridgeActionRouter` | `mitiru/input/BridgeActionRouter.hpp` | `registerHandler` / `unregisterHandler` / `dispatch` |
| HUD / 演出をviewにpush | `BridgeViewPush` | `mitiru/bridge/BridgeViewPush.hpp` | `set` / `emit` |
| UI signalをInputMapper Actionにマージ | `BridgeInputAdapter` | `mitiru/input/BridgeInputAdapter.hpp` | `mapSignalToAction` / `unmapSignal` |
| UI signalを型付きEventBus eventに変換 | `BridgeEventBusGlue` | `mitiru/bridge/BridgeEventBusGlue.hpp` | `mapSignal` / `mapSignalToTrivial` / `unmap` |
| C++ struct ↔ JSONシリアライズ | `JsonBinding` | `mitiru/data/JsonBinding.hpp` | `toJson` / `fromJson` / `fromJsonVersioned` / `MigrationChain<T>` |
| Saveスロットの型付き読み書き | `SaveSchema<T>` | `mitiru/data/SaveSchema.hpp` | `toJsonString` / `fromJsonString` / `migrations()` |
| Balance / dialogue等のJSON content読込 | `ContentLoader<T>` | `mitiru/data/ContentLoader.hpp` | `loadFile` / `loadString` / `loadJson` |

すべてheader-only。includeして使うだけでOK。

---

## 4. Composition Patterns

ここがメイン。プリミティブは単体ではなく 組み合わせ でgameplayを表現する。

### Pattern A: Scene + StateMachineの二層構造

**意図**: 「画面の切り替え」と「画面内の状態」を 別レイヤー として扱う。

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

- `SceneRouter`は「Title → Cooking → Result」のような 画面単位 の遷移を持つ。
- `StateMachine<T>`は1シーン内 の局所状態を持つ。
- **ハマる点**。状態がdeeply nested (5段以上)になってきたら、それは「別sceneに切り出すべきサイン」。
  StateMachineを多段化するより`router.push(...)`した方が読みやすい。

### Pattern B: Timer + StateMachineで時間駆動の遷移

**意図**: 「N秒経ったら次の状態へ」を素直に表現する。
`Timer.expired()`と`StateMachine.transition()`を組み合わせる。

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

- 状態に入った瞬間に`m_bakeTimer.reset()`を呼ぶ習慣をつける(前回の残り時間を引きずらない)。
- **ハマる点**。`expired()`を毎フレーム見て副作用を発火し続けないこと。
  `transition()`した直後は`state()`が変わっているので二重発火しないが、
  「expiredのとき何かする」ロジックは必ず状態遷移にくくる。

### Pattern C: Sequenceで演出スクリプト

**意図**: 章間カットイン / Title intro / 一連の演出のような スクリプテッド時系列 を書く。

```cpp
m_intro
    .wait(0.5f)
    .action([this]{ m_view.emit("intro.start", "{}"); })
    .wait(0.3f)
    .action([this]{ m_startReady = true; });

// update 内で:
m_intro.tick(dt);
```

- `wait`と`action`を自由に交互に並べられる。`action`は即時実行で時間を消費しない。
- `done()`で終了判定。終わったあとの`tick`はno-op。
- **ハマる点**。`action`のラムダ内でそのSequence自身をmutateしない(例: 同じ`m_intro`に`wait(...).action(...)`を追加する)。cursor不整合の原因になる。
  追加演出を流したいなら 別のSequenceインスタンスを用意する。

### Pattern D: BridgeActionRouter + Sceneのディスパッチ

**意図**: CEF (UI)から飛んでくるsignalをgameplayの意思決定へ変換する境界を作る。
JSには「ボタンが押された」だけ言わせて、どのsceneに遷移するか / どんな状態変化を起こすかはC++が決める。

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

- handler登録は **scene単位で対称に**: `onEnter` / ctorで登録、`onExit`で`unregisterHandler`。
- 同一signal名を二重登録すると **last-write-wins** で上書きされる。複数sceneが同じsignalを奪い合わないように責任分界点を決める。
- **ハマる点**。handler内で重い処理をしない。`dispatch()`は呼び出しスレッドで同期実行されるため、長時間ブロックするとbridgeポンプ全体が詰まる。重い処理はflagを立てて次の`onUpdate`で消化する。

### Pattern E: BridgeViewPushでstateをviewに流す

**意図**: gameplay側(C++)で計算した結果を、JSは 描画するだけ に徹させる。

```cpp
m_view.set("hp", "80");                            // → "view.cooking.hp" = "80"
m_view.set("state", "Cooking");                    // 状態ラベル
m_view.emit("damage", "{\"amount\":12,\"crit\":true}");  // one-shot エフェクト
```

- **`set`はretained**。最新値が保持され、後から接続したviewも読み取れる(HP / score等)。
- **`emit`はone-shot**。その瞬間のイベント。聴いていないviewには届かない(ヒットエフェクト / SEトリガ等)。
- keyは `view.<subsystem>.<key>` 形式に統一される(ctorの`subsystem`でprefixを固定)。
  詳細命名規約は [BRIDGE_API_CONTRACT.md §3](BRIDGE_API_CONTRACT.md)を参照。
- **ハマる点**。
  - JSで「HPが50以下なら赤くする」のような判定を書きたくなったら 負け。C++側で`view.cooking.hpLow = "true"`を別途pushし、JSはclassをtoggleするだけにする。
  - 値はあらかじめJSON文字列に整形して渡す(数値は`std::to_string`、文字列は`"\"..."\""`でquote)。

---

### Pattern F: BridgeInputAdapterでUIボタンとキー入力を同じActionにする

**意図**: 「キーボードでもUIボタンでも同じActionとしてgameplayは受け取る」を仕組みで保証する。

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

- bridge-triggered actionは **one-shot**: `isActionPressed`だけがtrueを返し、`isActionDown`は通常bindingと同じ意味のまま(continuous pressの概念はbridgeにない)。
- フレーム末で`mapper.endFrame()`を 必ず呼ぶ。さもないと一度発火したactionが永遠に "pressed" 状態のまま居座る。
- **ハマる点**。`mapSignalToAction`を二重登録すると`BridgeActionRouter`のlast-write-winsで上書きされる。複数signalを1 actionにしたい場合はsignal名側を増やす(例: `ui.button.fire`と`ui.gesture.tap-fire`両方を`Fire` actionにマップ → これは別signal名なので両方有効)。

### Pattern G: BridgeEventBusGlueでUI signalを型付きeventに変換

**意図**: 「ボタンが押された」だけでなく「どのスロットか」などpayloadを型で保持してgameplayに届ける。

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

- payloadを捨ててeventをdefault構築だけしたい場合は`mapSignalToTrivial<E>(signal)`。
- `BridgeInputAdapter`との使い分け: 「Actionとして押された / 離された だけ知りたい」ならAdapter、「payloadを構造化したい」ならGlue。両方を併用してもOK (同じsignal名はlast-write-winsなので片方だけ)。

### Pattern H: SaveSchema<T> + MigrationChainでsaveの互換性管理

**意図**: 「v1で書いたsaveをv3のbuildで読めるようにする」を1か所で宣言する。

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

- envelopeは`{ "version": N, "data": ... }`形式。`toJsonString`が自動で付ける。
- ハマる点。migration stepは **append-only**。一度productionで書いたstepを後から削ったり順序入れ替えたりすると古いsaveを壊す。
- `JsonBinding`を直接使うこともできるが、save用途はSaveSchemaを経由するのが推奨。「versionが混在しうるデータ」を1か所で表現できる。

#### Migrationヘルパー(`mitiru::data::Migration`)

手書きのlambdaで`data["x"] = ...`を書き続けるとbugの温床になる。よくある操作(backfill / rename / remove / set / transform)は`Migration::*`経由で 宣言的に 書ける。`compose`で1 stepに複数操作をまとめることも可能。

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

- すべて`std::function<Json(Json)>`を返すので`addStep`にそのまま渡せる。
- `addStep`は`MigrationChain&`を返すので上のようにfluent chainで書ける。1 stepずつ`schema.migrations().addStep(...)`と書いても等価。
- 引数はby value captureされるので、factoryのスコープを抜けても安全。
- ヘッダー: `include/mitiru/data/Migration.hpp`。

### Pattern I: ContentLoader<T>でbalance/dialogueを1行ロード

**意図**: 開発者がJSONで書いたcontentを型として受け取り、validate/parseを毎回手書きしない。

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

- 失敗時の`result.error`は人間可読(nlohmann::jsonの例外メッセージor自前メッセージ)。
- contentは 不変 が前提。書き戻したい場合はSaveSchemaの方を使う。
- runtime schema検証は別物(`include/mitiru/data/SchemaValidator.hpp`)。テンプレートでは型一致が静的に強制されるため、AI生成コンテンツ等でstructuralな検証が要る場合のみ追加で使う。

---

## 5. アンチパターン

BRIDGE_API_CONTRACT.mdと整合する5つのNGパターン。

- **JSでstate machineを持つな**
  「料理の状態はcooking.jsが管理する」は禁止。`StateMachine<CookState>`をC++に置く。

- **JSが条件分岐(tutorial完了判定 / 解放フラグ等)を持つな**
  「tutorial doneならSTARTを有効化」をJSで書かない。C++が`view.title.startEnabled`をpushし、JSはclassを付け外しするだけにする。

- **JSが「次のシーンはどこ」を判定するな**
  `ui.button.start`を発火するのはJSの責務だが、「次はCookingScene」と決めるのは **C++のScene/Router** の責務。
  `signal: "scene.goto.cooking"`のような transitionをJSに書かせるsignal名 を作らないこと。

- **bridgeを太らせてgameplay関数のRPCにするな**
  `signal: "game.canOpenDoor?"`のような問い合わせ / 計算依頼はNG。
  gameplayの判定はC++内で完結させ、結果だけ`view.*`にpushする。

- **逆方向に: C++がJSに「次に何を考えるか」を聞くな**
  C++ → JSは 常に決定済みの表示指示。`emit("ask.player.choice", ...)`で答えを待つような片務RPCは禁止。
  選択肢の提示は`set`で出し、選択結果は`BridgeActionRouter`経由でsignalとして受け取る。

**まとめ**: JSは「描画」と「何が起きたか発火」だけ。C++が「判断」と「何を表示するか決定」を持つ。

---

## 6. References

- [docs/HYBRID_RUNTIME.md](HYBRID_RUNTIME.md)
- [docs/BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md)
- [examples/html_menu/](../examples/html_menu/) — HTML操作 → C++反応の動くサンプル

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
