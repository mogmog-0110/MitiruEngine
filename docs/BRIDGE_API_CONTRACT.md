# Bridge API Contract — MitiruEngine CEF Bridge 責務定義

> **関連**: [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — C++ gameplay + CEF は View 専用

---

## 1. 責務定義

アーキテクチャ方針の転換 (2026-05-14、gameplay は C++・CEF は view 専用) により、bridge は **薄い signal 層** として再定義された。

### 1.1 JS → C++ で許可される用途

| 用途 | 説明 | 例 |
|---|---|---|
| **入力 signal の発火** | ポインタ / キー操作を C++ 入力系に転送する | ドラッグ操作、タップ、キー押下 |
| **UI イベントの発火** | ボタンクリック、メニュー選択など UI 上の操作を通知する | スタートボタン押下、メニュー項目選択 |

**JS → C++ で禁止される用途** (§4 アンチパターン参照):

- ゲームプレイ状態の問い合わせ・書き換え
- シーン分岐の指示
- 計算の委託と結果受け取り
- gameplay 関数の直接 RPC 呼び出し

### 1.2 C++ → JS で許可される用途

| 用途 | 説明 | 例 |
|---|---|---|
| **状態通知** | C++ が持つ gameplay state の変化を JS に伝える | HP 変化、スコア更新、フラグ変化 |
| **DOM 更新指示** | 表示すべき内容を JS に伝え、JS が DOM を操作する | ダイアログ表示、HUD 更新、エフェクト発火 |

**C++ → JS で禁止される用途**:

- gameplay 判定の依頼
- 「次に何をすべきか」の問い合わせ
- JS 側での状態計算を期待する呼び出し

---

## 2. 現状の API surface

> **注**: 以下は 2026-05-14 時点の実装に基づく要約。実装ファイルを正として参照すること。

### 2.1 低レベル transport (`include/mitiru/cef/MitiruCefBridge.hpp`)

```
request 形式: "handlerName|payload"
             or "handlerName"  (payload なし)
```

`MitiruCefBridge::registerHandler(name, fn)` で任意の handler 名を登録できる。
JS 側からは `window.cefQuery({ request: "handler|payload", onSuccess, onFailure })` で呼ぶ。

C++ → JS push は `MitiruCefBridge::executeJavaScript(browser, code)` で任意 JS を実行する。

### 2.2 型付き state/event 層 (`include/mitiru/cef/StateStore.hpp`)

`StateStore` は低レベル transport の上に構築された typed API:

| C++ メソッド | 方向 | 用途 |
|---|---|---|
| `store.set(key, value)` | C++ → JS | 状態値を broadcast。JS の `window.mitiru.onStateChange(key, fn)` が受け取る |
| `store.emit(name, payload)` | C++ → JS | one-shot イベント発火。JS の `window.mitiru.on(name, fn)` が受け取る |
| `store.onAction(action, fn)` | JS → C++ | `window.mitiru.dispatch(action, payload)` を受け取る handler を登録する |

**現状の handler 名 (StateStore 経由)**:

- `state.dispatch` — JS `window.mitiru.dispatch()` のエントリポイント (StateStore 内部)

### 2.3 Audio bridge (`include/mitiru/cef/AudioBridge.hpp`)

登録される handler 名 (`window.cefQuery({ request: "..." })` 形式):

| Handler | payload 形式 | 内容 |
|---|---|---|
| `audio.playBgm` | `"BGM_KEY"` | BGM 再生 (同キーなら no-op) |
| `audio.stopBgm` | (なし) | BGM 停止 |
| `audio.crossFadeBgm` | `"BGM_KEY\|duration_ms"` | BGM クロスフェード |
| `audio.playSe` | `"SE_KEY"` | SE 再生 |
| `audio.setCategoryVolume` | `"bgm\|0.8"` | カテゴリ別 volume |
| `audio.setMasterVolume` | `"0.8"` | マスター volume |
| `audio.currentBgm` | (なし) | 現在 BGM key を返す (C++ → JS response) |

### 2.4 シーン遷移 bridge (`include/mitiru/cef/SceneTransition.hpp`)

| Handler | payload | 内容 |
|---|---|---|
| `__mitiru_scene_next__` | (内部用) | フェードアウト完了後に JS timer が呼ぶ。C++ が `loadUrl()` を実行する |

C++ → JS push は `executeJavaScript` 経由でオーバーレイ `<div>` の CSS を直接操作する。

### 2.5 save bridge の handler

正規実装は `web/mitiru_runtime/` の JS と `include/mitiru/cef/SaveStore.hpp` (C++ 側)。

save bridge の C++ dispatch handler:

| Handler (推定) | 内容 |
|---|---|
| `save.write` | スロットへのセーブ |
| `save.read` | スロットからのロード |
| `save.list` | スロット一覧取得 |
| `save.delete` | スロット削除 |

### 2.6 `include/mitiru/bridge/` ディレクトリ (sgc 統合 bridge 群)

`UiBridge`, `DialogueBridge`, `AnimationBridge`, `PhysicsBridge` 等は **C++ 内部 bridge** であり、CEF transport とは独立している。これらは sgc ライブラリと Mitiru engine をつなぐ adapter であって、JS ↔ C++ 通信の経路には含まれない。

> **未確認**: `include/mitiru/bridge/` 内の bridge が CEF handler を直接登録するかどうかは、各ファイルの実装を個別確認すること。本 doc 執筆時点では C++ 内部 API として扱う。

---

## 3. 新責務での API surface

この方針に従い、bridge は以下の 2 カテゴリのみを持つ。

### 3.1 JS → C++ request handler 命名規則

**形式**: `<カテゴリ>.<アクション>`

**許可カテゴリ**:

| カテゴリ | 用途 | 例 |
|---|---|---|
| `input` | ポインタ / キー / タッチ操作の転送 | `input.pointer`, `input.key` |
| `ui` | UI ウィジェットの操作通知 | `ui.button`, `ui.menu.select` |

**カテゴリ禁止事項**:

- `command.*` — 禁止。gameplay への命令形 RPC
- `state.*` — 禁止。gameplay state の読み書き (§4 アンチパターン参照)
- `scene.*` — 禁止。シーン分岐の指示
- `game.*` — 禁止。gameplay 関数の直接呼び出し

**標準 handler 一覧 (目標)**:

```
input.pointer         // ポインタ操作 (pointerdown/move/up)
input.key             // キーボード入力
ui.button             // ボタンクリック通知
ui.menu.select        // メニュー項目選択通知
ui.slider.change      // スライダー値変更通知
ui.dialog.close       // ダイアログ閉じる操作通知
ui.option.select      // 選択肢 (ノベル等) 選択通知
```

**payload スキーマ**: JSON。 `|` セパレータは低レベル transport の実装詳細であり、
新規 handler は JSON payload を使うこと。

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

### 3.2 C++ → JS push channel 命名規則

**形式**: `view.<サブシステム>.<キー>`

`StateStore::set()` / `StateStore::emit()` を使う。`executeJavaScript` 直接呼び出しは内部実装詳細にとどめ、公開 API としては使わない。

**キー体系**:

| prefix | 用途 | 例 |
|---|---|---|
| `view.hud.*` | HUD の表示値更新 | `view.hud.hp`, `view.hud.score`, `view.hud.ammo` |
| `view.dialog.*` | ダイアログ / ノベル表示制御 | `view.dialog.show`, `view.dialog.speaker`, `view.dialog.text` |
| `view.transition.*` | シーン遷移制御 | `view.transition.begin`, `view.transition.end` |
| `view.effect.*` | 演出トリガー | `view.effect.flash`, `view.effect.shake` |
| `view.menu.*` | メニュー表示制御 | `view.menu.open`, `view.menu.items` |
| `view.status.*` | プレイヤー / 敵ステータス表示 | `view.status.player`, `view.status.enemy` |

**`set()` vs `emit()` の使い分け**:

- `set(key, value)` — 遅れて購読した JS も最新値を受け取れる。HUD 値のような **保持が必要な状態** に使う
- `emit(name, payload)` — one-shot。アニメーション発火など **保持不要のイベント** に使う

**payload スキーマ例**:

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

以下の 5 パターンは新設計で **明示的に禁止** する。

### AP-1: JS が state machine を持つ

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

**正しい実装**: クッキング状態機械は C++ に置く。JS は `view.cooking.state` の変化通知を受けて DOM を更新するだけ。

```cpp
// C++: 状態遷移はここで完結する
void CookingScene::onIngredientDropped(IngredientId id, SlotId slot) {
    m_stateMachine.transition(Event::IngredientDropped{id, slot});
    m_stateStore.set("view.cooking.state", m_stateMachine.currentStateName());
}
```

### AP-2: JS が tutorial 完了 / シーン分岐を判定する

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

**正しい実装**: 分岐判定は C++。JS は `view.transition.begin` を受けて画面を切り替えるだけ。

```cpp
// C++: tutorial 完了条件の判定と遷移指示はここで行う
void TutorialScene::onStepComplete(int step) {
    if (step >= TUTORIAL_COMPLETE_STEP) {
        m_stateStore.emit("view.transition.begin", {{"url", "game_main.html"}});
    }
}
```

### AP-3: C++ が計算を JS に投げて結果を受け取る

**禁止例**:

```cpp
// NG: C++ が JS に計算させてコールバックで受け取る
bridge.executeJavaScript("window.__result = computeDamage(" + params + ")");
// ... 後で __result を読み出す
```

**正しい実装**: 計算は C++ で行う。JS は表示専用。

### AP-4: JS が「次のシーンはどこか」を決める

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

**正しい実装**: JS は `ui.button` signal を発火するだけ。どのシーンへ遷移するかは C++ が決める。

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

### AP-5: JS が C++ の gameplay function を名前で直接 RPC 呼び出しする

**禁止例**:

```js
// NG: C++ の内部関数を名前で直接叩く
window.cefQuery({ request: 'command.CookingActions.pourBatter|{}' });
window.cefQuery({ request: 'command.PlayerCharacter.takeDamage|{"amount":10}' });
```

これは JS と C++ の実装を密結合させ、C++ 側のリファクタリングを阻害する。
`command.*` カテゴリは全面禁止。`ui.*` / `input.*` の signal のみ許可する。

---

## 5. JS に残してよい責務

JS (CEF view layer) が担ってよい処理を明示する。

### 5.1 DOM 描画

HTML / CSS / JavaScript によるビジュアル表現全般。

- `filter: blur()`, `backdrop-filter`, `mix-blend-mode`, `conic-gradient`
- WAAPI (Web Animations API) によるイージング
- SVG `<filter>`, web フォント描画
- CSS transition / animation

### 5.2 入力 signal の発火

ポインタ・キーボード・タッチ操作を受け取り、`window.cefQuery` で C++ に転送する。

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

### 5.3 DOM 更新指示の実行

`window.mitiru.onStateChange()` / `window.mitiru.on()` で受け取った通知に従い、DOM を更新する。

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

### 5.4 表示専用 local state

DOM 描画のためだけに必要な一時的な表示状態は JS に持ってよい。

**許可される local state の条件**:

- C++ の gameplay state と同期が不要 (あるいは常に C++ からの通知で上書きされる)
- 画面表示のためだけに使われる (アニメーション進行度、hover 状態、スクロール位置など)
- C++ が「知る必要がない」状態である

**禁止される local state**:

- gameplay の進行度・フラグ・カウンタ
- 「次に何をすべきか」の判断に使われる状態
- C++ 側でも追跡が必要な状態 (それは C++ に持ち、`view.*` push で JS に通知すべき)

---

## 6. 移行ガイドライン

既存コードを新責務に適合させる際の手順:

1. JS の `window.mitiru.dispatch(action, ...)` の `action` 名を確認する
2. action が `ui.*` / `input.*` の signal 転送であれば **適合済み** (§3.1 参照)
3. action が gameplay state の読み書きを行っていれば **AP-1〜AP-5 該当** → C++ に移す
4. C++ → JS push で `executeJavaScript` を直接呼んでいる箇所は、`StateStore::set()` / `StateStore::emit()` に置き換え、key を `view.*` 体系に沿って命名する
5. `command.*` 系の handler 名は全て `ui.*` / `input.*` に改名するか削除する

---

## 7. 参照

- [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md) — アーキテクチャ方針 (C++ gameplay + CEF は view 専用)
- `include/mitiru/cef/MitiruCefBridge.hpp` — 低レベル transport 実装
- `include/mitiru/cef/StateStore.hpp` — typed state/event 層
- `include/mitiru/cef/AudioBridge.hpp` — audio handler 実装例
- `include/mitiru/cef/SceneTransition.hpp` — シーン遷移実装例
- `include/mitiru/cef/SaveStore.hpp` — save スロット I/O
- `web/mitiru_runtime/` — JS side runtime 実装
