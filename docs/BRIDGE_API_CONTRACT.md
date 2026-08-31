# Bridge API Contract: MitiruEngine CEF Bridge責務定義

> **関連**: [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — C++ gameplay + CEFはView専用

---

## 1. 責務定義

アーキテクチャ方針の転換(2026-05-14、gameplayはC++・CEFはview専用)により、bridgeは 薄いsignal層 として再定義された。

### 1.1 JS → C++で許可される用途

| 用途 | 説明 | 例 |
|---|---|---|
| **入力signalの発火** | ポインタ / キー操作をC++入力系に転送する | ドラッグ操作、タップ、キー押下 |
| **UIイベントの発火** | ボタンクリック、メニュー選択などUI上の操作を通知する | スタートボタン押下、メニュー項目選択 |

**JS → C++で禁止される用途** (§4アンチパターン参照):

- ゲームプレイ状態の問い合わせ・書き換え
- シーン分岐の指示
- 計算の委託と結果受け取り
- gameplay関数の直接RPC呼び出し

### 1.2 C++ → JSで許可される用途

| 用途 | 説明 | 例 |
|---|---|---|
| **状態通知** | C++が持つgameplay stateの変化をJSに伝える | HP変化、スコア更新、フラグ変化 |
| **DOM更新指示** | 表示すべき内容をJSに伝え、JSがDOMを操作する | ダイアログ表示、HUD更新、エフェクト発火 |

**C++ → JSで禁止される用途**:

- gameplay判定の依頼
- 「次に何をすべきか」の問い合わせ
- JS側での状態計算を期待する呼び出し

---

## 2. 現状のAPI surface

> **注**: 以下は2026-05-14時点の実装に基づく要約。実装ファイルを正として参照すること。

### 2.1低レベルtransport (`include/mitiru/cef/MitiruCefBridge.hpp`)

```
request 形式: "handlerName|payload"
             or "handlerName"  (payload なし)
```

`MitiruCefBridge::registerHandler(name, fn)`で任意のhandler名を登録できる。
JS側からは`window.cefQuery({ request: "handler|payload", onSuccess, onFailure })`で呼ぶ。

C++ → JS pushは`MitiruCefBridge::executeJavaScript(browser, code)`で任意JSを実行する。

### 2.2型付きstate/event層(`include/mitiru/cef/StateStore.hpp`)

`StateStore`は低レベルtransportの上に構築されたtyped API:

| C++メソッド | 方向 | 用途 |
|---|---|---|
| `store.set(key, value)` | C++ → JS | 状態値をbroadcast。JSの`window.mitiru.onStateChange(key, fn)`が受け取る |
| `store.emit(name, payload)` | C++ → JS | one-shotイベント発火。JSの`window.mitiru.on(name, fn)`が受け取る |
| `store.onAction(action, fn)` | JS → C++ | `window.mitiru.dispatch(action, payload)`を受け取るhandlerを登録する |

**現状のhandler名(StateStore経由)**:

- `state.dispatch` — JS `window.mitiru.dispatch()`のエントリポイント(StateStore内部)

### 2.3 Audio bridge (`include/mitiru/cef/AudioBridge.hpp`)

登録されるhandler名(`window.cefQuery({ request: "..." })`形式):

| Handler | payload形式 | 内容 |
|---|---|---|
| `audio.playBgm` | `"BGM_KEY"` | BGM再生(同キーならno-op) |
| `audio.stopBgm` | (なし) | BGM停止 |
| `audio.crossFadeBgm` | `"BGM_KEY\|duration_ms"` | BGMクロスフェード |
| `audio.playSe` | `"SE_KEY"` | SE再生 |
| `audio.setCategoryVolume` | `"bgm\|0.8"` | カテゴリ別volume |
| `audio.setMasterVolume` | `"0.8"` | マスターvolume |
| `audio.currentBgm` | (なし) | 現在BGM keyを返す(C++ → JS response) |

### 2.4シーン遷移bridge (`include/mitiru/cef/SceneTransition.hpp`)

| Handler | payload | 内容 |
|---|---|---|
| `__mitiru_scene_next__` | (内部用) | フェードアウト完了後にJS timerが呼ぶ。C++が`loadUrl()`を実行する |

C++ → JS pushは`executeJavaScript`経由でオーバーレイ`<div>`のCSSを直接操作する。

### 2.5 save bridgeのhandler

正規実装は`web/mitiru_runtime/`のJSと`include/mitiru/cef/SaveStore.hpp` (C++側)。

save bridgeのC++ dispatch handler:

| Handler (推定) | 内容 |
|---|---|
| `save.write` | スロットへのセーブ |
| `save.read` | スロットからのロード |
| `save.list` | スロット一覧取得 |
| `save.delete` | スロット削除 |

### 2.6 `include/mitiru/bridge/`ディレクトリ(sgc統合bridge群)

`UiBridge`, `DialogueBridge`, `AnimationBridge`, `PhysicsBridge`等はC++内部bridgeであり、CEF transportとは独立している。これらはsgcライブラリとMitiru engineをつなぐadapterであって、JS ↔ C++通信の経路には含まれない。

> **未確認**: `include/mitiru/bridge/`内のbridgeがCEF handlerを直接登録するかどうかは、各ファイルの実装を個別確認すること。本doc執筆時点ではC++内部APIとして扱う。

---

## 3. 新責務でのAPI surface

この方針に従い、bridgeは以下の2カテゴリのみを持つ。

### 3.1 JS → C++ request handler命名規則

**形式**: `<カテゴリ>.<アクション>`

**許可カテゴリ**:

| カテゴリ | 用途 | 例 |
|---|---|---|
| `input` | ポインタ / キー / タッチ操作の転送 | `input.pointer`, `input.key` |
| `ui` | UIウィジェットの操作通知 | `ui.button`, `ui.menu.select` |

**カテゴリ禁止事項**:

- `command.*` — 禁止。gameplayへの命令形RPC
- `state.*` — 禁止。gameplay stateの読み書き(§4アンチパターン参照)
- `scene.*` — 禁止。シーン分岐の指示
- `game.*` — 禁止。gameplay関数の直接呼び出し

**標準handler一覧(目標)**:

```
input.pointer         // ポインタ操作 (pointerdown/move/up)
input.key             // キーボード入力
ui.button             // ボタンクリック通知
ui.menu.select        // メニュー項目選択通知
ui.slider.change      // スライダー値変更通知
ui.dialog.close       // ダイアログ閉じる操作通知
ui.option.select      // 選択肢 (ノベル等) 選択通知
```

**payloadスキーマ**: JSON。`|`セパレータは低レベルtransportの実装詳細であり、
新規handlerはJSON payloadを使うこと。

```jsonc
// input.pointer 例
{
  "type": "pointerdown" | "pointermove" | "pointerup",
  "x": 320,
  "y": 240,
  "pointerId": 0
}

// ui.button 例
{
  "id": "start-button"
}

// ui.menu.select 例
{
  "menuId": "main-menu",
  "itemId": "new-game"
}
```

### 3.2 C++ → JS push channel命名規則

**形式**: `view.<サブシステム>.<キー>`

`StateStore::set()` / `StateStore::emit()`を使う。`executeJavaScript`直接呼び出しは内部実装詳細にとどめ、公開APIとしては使わない。

**キー体系**:

| prefix | 用途 | 例 |
|---|---|---|
| `view.hud.*` | HUDの表示値更新 | `view.hud.hp`, `view.hud.score`, `view.hud.ammo` |
| `view.dialog.*` | ダイアログ / ノベル表示制御 | `view.dialog.show`, `view.dialog.speaker`, `view.dialog.text` |
| `view.transition.*` | シーン遷移制御 | `view.transition.begin`, `view.transition.end` |
| `view.effect.*` | 演出トリガー | `view.effect.flash`, `view.effect.shake` |
| `view.menu.*` | メニュー表示制御 | `view.menu.open`, `view.menu.items` |
| `view.status.*` | プレイヤー / 敵ステータス表示 | `view.status.player`, `view.status.enemy` |

**`set()` vs `emit()`の使い分け**:

- `set(key, value)` — 遅れて購読したJSも最新値を受け取れる。HUD値のような 保持が必要な状態 に使う
- `emit(name, payload)` — one-shot。アニメーション発火など 保持不要のイベント に使う

**payloadスキーマ例**:

```jsonc
// StateStore::set("view.hud.hp", 85)
// JS: window.mitiru.onStateChange('view.hud.hp', v => hud.setHp(v))

// StateStore::emit("view.effect.flash", {color: "#ff0000", duration: 200})
// JS: window.mitiru.on('view.effect.flash', p => playFlash(p.color, p.duration))

// StateStore::set("view.dialog.show", {
//   speaker: "マリア",
//   text: "こんにちは！",
//   choices: []
// })
```

---

## 4. アンチパターン

以下の5パターンは新設計で **明示的に禁止** する。

### AP-1: JSがstate machineを持つ

**禁止例**:

```js
// NG: JS 側でクッキング状態を管理する
let cookingState = 'idle';
window.mitiru.onStateChange('cooking.ingredient_dropped', ({ingredientId}) => {
    if (cookingState === 'idle') {
        cookingState = 'mixing';
        // ... 状態遷移ロジックが JS に増殖する
    }
});
```

**正しい実装**: クッキング状態機械はC++に置く。JSは`view.cooking.state`の変化通知を受けてDOMを更新するだけ。

```cpp
// C++: 状態遷移はここで完結する
void CookingScene::onIngredientDropped(IngredientId id, SlotId slot) {
    m_stateMachine.transition(Event::IngredientDropped{id, slot});
    m_stateStore.set("view.cooking.state", m_stateMachine.currentStateName());
}
```

### AP-2: JSがtutorial完了 / シーン分岐を判定する

**禁止例**:

```js
// NG: JS がシーン遷移を決める
window.mitiru.onStateChange('tutorial.step', (step) => {
    if (step >= 5) {
        // tutorial 完了と判断して次のシーンへ
        window.cefQuery({ request: 'scene.load|game_main.html' });
    }
});
```

**正しい実装**: 分岐判定はC++。JSは`view.transition.begin`を受けて画面を切り替えるだけ。

```cpp
// C++: tutorial 完了条件の判定と遷移指示はここで行う
void TutorialScene::onStepComplete(int step) {
    if (step >= TUTORIAL_COMPLETE_STEP) {
        m_stateStore.emit("view.transition.begin", {{"url", "game_main.html"}});
    }
}
```

### AP-3: C++が計算をJSに投げて結果を受け取る

**禁止例**:

```cpp
// NG: C++ が JS に計算させてコールバックで受け取る
bridge.executeJavaScript("window.__result = computeDamage(" + params + ")");
// ... 後で __result を読み出す
```

**正しい実装**: 計算はC++で行う。JSは表示専用。

### AP-4: JSが「次のシーンはどこか」を決める

**禁止例**:

```js
// NG: JS がゲームフローを制御する
function onButtonClick(id) {
    if (id === 'new-game') {
        cefQuery('scene.load|prologue.html');
    } else if (id === 'load-game') {
        cefQuery('scene.load|save_select.html');
    }
    // ← ゲームフローのルーティング知識が JS に漏れている
}
```

**正しい実装**: JSは`ui.button` signalを発火するだけ。どのシーンへ遷移するかはC++が決める。

```js
// OK: JS はイベントを転送するだけ
button.addEventListener('click', () => {
    window.cefQuery({ request: 'ui.button|' + JSON.stringify({ id: 'new-game' }) });
});
```

```cpp
// C++: ルーティングはここで行う
bridge.registerHandler("ui.button", [this](std::string_view payload) -> std::string {
    const auto data = json::parse(payload);
    const auto id = data.at("id").get<std::string>();
    if (id == "new-game") m_router.transitionTo(SceneId::Prologue);
    else if (id == "load-game") m_router.transitionTo(SceneId::SaveSelect);
    return "{}";
});
```

### AP-5: JSがC++のgameplay functionを名前で直接RPC呼び出しする

**禁止例**:

```js
// NG: C++ の内部関数を名前で直接叩く
window.cefQuery({ request: 'command.CookingActions.pourBatter|{}' });
window.cefQuery({ request: 'command.PlayerCharacter.takeDamage|{"amount":10}' });
```

これはJSとC++の実装を密結合させ、C++側のリファクタリングを阻害する。
`command.*`カテゴリは全面禁止。`ui.*` / `input.*`のsignalのみ許可する。

---

## 5. JSに残してよい責務

JS (CEF view layer)が担ってよい処理を明示する。

### 5.1 DOM描画

HTML / CSS / JavaScriptによるビジュアル表現全般。

- `filter: blur()`, `backdrop-filter`, `mix-blend-mode`, `conic-gradient`
- WAAPI (Web Animations API)によるイージング
- SVG `<filter>`, webフォント描画
- CSS transition / animation

### 5.2入力signalの発火

ポインタ・キーボード・タッチ操作を受け取り、`window.cefQuery`でC++に転送する。

```js
// OK: イベントを受け取って転送するだけ
canvas.addEventListener('pointerdown', (e) => {
    window.cefQuery({
        request: 'input.pointer|' + JSON.stringify({
            type: 'pointerdown', x: e.clientX, y: e.clientY, pointerId: e.pointerId
        }),
        onSuccess: () => {}, onFailure: () => {}
    });
});
```

### 5.3 DOM更新指示の実行

`window.mitiru.onStateChange()` / `window.mitiru.on()`で受け取った通知に従い、DOMを更新する。

```js
// OK: C++ からの通知を受けて DOM を更新するだけ
window.mitiru.onStateChange('view.hud.hp', (hp) => {
    document.getElementById('hp-bar').style.width = hp + '%';
    document.getElementById('hp-value').textContent = hp;
});

window.mitiru.on('view.effect.flash', ({ color, duration }) => {
    playFlashEffect(color, duration);
});
```

### 5.4表示専用local state

DOM描画のためだけに必要な一時的な表示状態はJSに持ってよい。

**許可されるlocal stateの条件**:

- C++のgameplay stateと同期が不要(あるいは常にC++からの通知で上書きされる)
- 画面表示のためだけに使われる(アニメーション進行度、hover状態、スクロール位置など)
- C++が「知る必要がない」状態である

**禁止されるlocal state**:

- gameplayの進行度・フラグ・カウンタ
- 「次に何をすべきか」の判断に使われる状態
- C++側でも追跡が必要な状態(それはC++に持ち、`view.*` pushでJSに通知すべき)

---

## 6. 移行ガイドライン

既存コードを新責務に適合させる際の手順:

1. JSの`window.mitiru.dispatch(action, ...)`の`action`名を確認する
2. actionが`ui.*` / `input.*`のsignal転送であれば 適合済み(§3.1参照)
3. actionがgameplay stateの読み書きを行っていればAP-1〜AP-5該当 → C++に移す
4. C++ → JS pushで`executeJavaScript`を直接呼んでいる箇所は、`StateStore::set()` / `StateStore::emit()`に置き換え、keyを`view.*`体系に沿って命名する
5. `command.*`系のhandler名は全て`ui.*` / `input.*`に改名するか削除する

---

## 7. 参照

- [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — アーキテクチャ方針(C++ gameplay + CEFはview専用)
- `include/mitiru/cef/MitiruCefBridge.hpp` — 低レベルtransport実装
- `include/mitiru/cef/StateStore.hpp` — typed state/event層
- `include/mitiru/cef/AudioBridge.hpp` — audio handler実装例
- `include/mitiru/cef/SceneTransition.hpp` — シーン遷移実装例
- `include/mitiru/cef/SaveStore.hpp` — saveスロットI/O
- `web/mitiru_runtime/` — JS side runtime実装
