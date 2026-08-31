# Hybrid Runtime: where does game code live?

> **方針。** gameplayはC++、CEFは表示レイヤー。このdocはその境界の分担を記述する。
> 関連doc:
> - bridgeの具体的API: [`BRIDGE_API_CONTRACT.md`](BRIDGE_API_CONTRACT.md)
> - C++ gameplayの書き方: [`CPP_GAMEPLAY_GUIDE.md`](CPP_GAMEPLAY_GUIDE.md)

MitiruEngineはC++ゲームエンジン である。CEFは綺麗なHTML / CSS UI
を低コストで作れる見た目のレイヤーとして残す。両者は薄いsignal
層で繋ぎ、gameplay stateはすべてC++側に置く。

> Companion docs:
> - [ARCHITECTURE.md](ARCHITECTURE.md) — the C++ layer stack
> - [BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md) — bridgeのsignal-only契約
> - [CPP_GAMEPLAY_GUIDE.md](CPP_GAMEPLAY_GUIDE.md) — C++ gameplayを書く入口

---

## 1. アーキテクチャ(新方針)

```
┌──────────────────────────────────────────────────────────────┐
│  GAME LOGIC  (C++)                                            │
│  ─────────────────────────────────────────────────────────── │
│  ▸ scene flow         ▸ minigame state     ▸ novel VM         │
│  ▸ UI state (model)   ▸ dialogue trees     ▸ interaction      │
│  ▸ save / load model  ▸ AI / pathfinding   ▸ physics          │
│  ─────────  すべて C++ で実装。state は C++ が単独所有  ────── │
└──────────────────────────────────────────────────────────────┘
            ▲                                      │
            │   JS → C++ : input / UI events       │
            │   (button click, drop, menu select)  │
            │                                      │
            │              C++ → JS : view updates │
            │      (set HUD value, show dialog,    │
            │       play CSS animation, swap DOM)  │
            ▼                                      ▼
┌──────────────────────────────────────────────────────────────┐
│  VIEW LAYER  (CEF: HTML / CSS / JS)                           │
│  ─────────────────────────────────────────────────────────── │
│  ▸ HUD layout        ▸ menu / dialog        ▸ overlay         │
│  ▸ transition / VFX  ▸ typewriter / tween   ▸ CSS animation   │
│  ──  view-only。gameplay state は持たない。input を発火するだけ  │
└──────────────────────────────────────────────────────────────┘
            ▲
            │
┌──────────────────────────────────────────────────────────────┐
│  NATIVE SERVICES  (C++)                                       │
│  ─────────────────────────────────────────────────────────── │
│  ▸ CEF process host           ▸ audio mixer (miniaudio)       │
│  ▸ graphics backends          ▸ save disk I/O (SaveStore)     │
│    (DX11/12/Vulkan/OpenGL/…)  ▸ window / input                │
│  ▸ scene transition compositor▸ asset loading                 │
│  ▸ ECS / physics / AI         ▸ shader / VFX                  │
└──────────────────────────────────────────────────────────────┘
```

**一行要約:**

> GameplayはC++、見た目はCEF、繋ぎは薄いsignal。

C++側がgameplay stateを単独所有し、tickもする。CEFは表示と入力受付
のみで、ロジックを持たない。bridgeは「event」と「view update」の二種
類のメッセージしか流さない。

---

## 2. Decision matrix: where does this feature go?

| Feature type                                | Home | Notes |
|---------------------------------------------|:-:|---|
| **Scene flow / シーン遷移ロジック**         | **C++** | `SceneTransition` + game scene class。stateはC++にのみ存在。 |
| **Minigame state machine** (cooking, puzzle等) | **C++** | 状態と遷移はC++。CEFには「今この絵を出して」と指示するだけ。 |
| **Novel / dialogue VM**                     | **C++** | スクリプトはJSON、VM (進行・分岐・条件評価)はC++。 |
| **Dialogue / cutscene表示**                | CEF | C++から「この行をtypewriterで表示せよ」と指示。 |
| **Drag-and-drop (判定)**                    | **C++** | pointerdown/move/upはJS → bridge → C++に発火、ヒット判定と結果はC++。 |
| **Drag-and-drop (見た目)**                  | CEF | カーソル追従やsnapアニメはCSS / WAAPIで表現。 |
| **Per-frame UI animation** (tween, typewriter) | CEF | `Element.animate()` / CSS transition。完了通知だけC++に返す。 |
| **HUD layout / メニュー / ダイアログ枠**    | CEF | HTML + CSS。値はC++から`setHUD(...)`でpush。 |
| **Input routing** (kb / mouse / gamepad)    | **C++** | window層で集約。CEFにはフォーカス時のみ転送。 |
| **Localization / i18n strings**             | JSON | C++が読み、必要な行をCEFにpush。 |
| **Balance / progression / loot tables**     | JSON | C++が読み、ゲームロジックで使用。 |
| **Save blob format**                        | JSON | C++がシリアライズ、`SaveStore`が永続化。 |
| **Audio playback**                          | **C++** | `mitiru::audio`直接呼び出し。CEFからはevent経由で要求。 |
| **Save file I/O**                           | **C++** | `SaveStore`がatomic write。 |
| **Graphics backend** (DX12, Vulkan, …)      | C++ | Not reachable from JS. |
| **Window / CEF host**                       | C++ | Platform layer. |
| **ECS / physics / spatial query**           | C++ | gameplayと同居。 |
| **Heavy simulation** (AI, pathfinding, particle) | C++ | 元からC++。 |
| **Shader / VFX / post-processing**          | C++ | `RenderPipeline2D/3D`。 |
| **Scene transition compositor**             | C++ (`SceneTransition`) | Frame-accurate blend. |

「迷ったらC++」が原則。CEFに置くのは「画面に映るピクセルとそのアニメ
ーションだけ」と覚える。

---

## 3. Bridge: signal-only contract

新方針のbridgeは 二方向の薄いsignal層。型付きメッセージのスキーマ
と具体的APIは [`BRIDGE_API_CONTRACT.md`](BRIDGE_API_CONTRACT.md)を一次
情報として参照。本docでは役割のみ示す。

### 3.1 JS → C++ : input / UI eventのみ

CEF内で起きたユーザー入力をC++に通知するチャネル。**stateを渡さない**。
「何が起きたか」だけを送り、「次に何をするか」はC++が決める。

許される送信例:
- `ui.button.click` (id=start_game)
- `ui.drag.end`     (source=ingredient_egg, target=slot_pan, position=...)
- `ui.menu.select`  (id=settings.volume.master, value=0.8)
- `ui.dialog.advance` (current_line_id=12)

**禁止例:**
- gameplay stateの更新(HP計算結果、進行段階、所持アイテム…)
- 「次のシーンに遷移せよ」のような決定。C++が決める

### 3.2 C++ → JS : view update / 指示のみ

C++がgameplay stateを更新したあと、CEFに「画面をこう変えろ」とpush
するチャネル。

許される送信例:
- `view.hud.set`      (hp=80, gold=120)
- `view.dialog.show`  (speaker=Maria, line="...", typewriter=true)
- `view.animation.play` (target=#hero, name=flip, duration=400ms)
- `view.dom.swap`     (region=order_panel, html_template=order_card, data=...)
- `view.scene.switch` (id=kitchen, transition=fade, duration=600ms)

**禁止例:**
- gameplay stateをJS側にキャッシュさせる(HPの整数値をJSが保持して
  計算する等)
- ビジネスロジックを呼び出すマクロ("complete_recipe" のような副作用
  を伴うコマンド)。C++内部の関数呼び出しで完結させる

### 3.3設計原則

- **stateの単独所有**。gameplay stateはC++にのみ存在する。JS側に
  holdするのは「今映っている見た目の表現」だけ。
- **冪等なview update**。同じ`view.hud.set(hp=80)`を二回送っても結果
  が変わらないように作る。再接続 / hot reloadを容易にする。
- **schemaを切る**。全event / view updateは型付きmessageとし、
  schemaは`BRIDGE_API_CONTRACT.md`に集約。野良文字列dispatchは
  追加しない。
- **fallbackは不要**。旧docにあった「C++ handlerが無ければJS実装
  で動かす」前提は廃止。C++が無い状況はそもそもview単体のdev preview
  のみで、その場合はmock eventをJSから流す。

---

## 4. なぜ「人間にもAIにも素直」なのか

- **責務が一直線**。gameplayはC++、viewはCEF、繋ぎはsignal。新規
  featureを書くとき迷う軸が減る。
- **再現性 / determinism**。stateがC++単独所有なので、replay / save
  state / 自動テストが(V8 GCやDOMタイミングに依存せず)素直に書ける。
- **AIへのラベリングは控えめに**。LLMはC++もJSも書ける。
  MitiruEngineの強みは「LLMが書きやすい言語を選んだ」ことではなく、
  「責務分割が明確で誤った場所に書くと弾けること」。AIフレンドリーで
  あることは結果であって、看板ではない。

---

## 5. Rule of thumb: JSON vs C++ vs CEF

| Kind of thing | Shape | Example |
|---|---|---|
| **Pure data** (read-only at play time)      | **JSON** | Recipes, dialogue lines, UI strings, balance tables |
| **Scenario / scripted content**              | **JSON + C++ interpreter** | Novel scriptはJSON、VMはC++。 |
| **Interactive gameplay** (per-frame logic, state machine) | **C++** | Cooking scene、drag-and-drop判定、menu state、AI。 |
| **Visual presentation** (layout, animation, transition) | **CEF (HTML/CSS/JS)** | HUD、ダイアログ枠、CSS animation、tween。 |
| **Hot / deterministic simulation**           | **C++** | Physics, pathfinding, heavy AI, particle. |
| **Platform services**                        | **C++** | Audio mixer, window, graphics backend, disk I/O. |

**Failure modes to avoid:**

- **JSにgameplay stateを持たせる**。「JS側でちょっと計算してからC++に
  投げる」を許すと、二箇所に真実が出来て同期バグになる。常に「eventを
  C++に発火して結果を待つ」形にする。
- **C++にDOMレイアウトを書く**。CSSで済むレイアウト計算をC++から
  座標で指定するのは無駄。CEFに任せる。
- **bridgeを太らせる**。「complete_recipe」のような副作用付き高レベル
  コマンドをbridgeに乗せると、ロジックがどっちにあるか曖昧になる。
  eventは「何が起きたか」、view updateは「何を映すか」に限定する。
- **Over-JSONifying**。状態機械をJSON ruleで宣言するのはROIが合わ
  ない。C++で書く。
- **Over-C++-ifying view**。ダイアログのフェードインtweenをC++で書く
  のは過剰。CSS / WAAPIで十分。

---

## 6. ポジショニング

MitiruEngineは:

- C++ゲームエンジン。Siv3D / Unreal-Nativeと同じカテゴリ。gameplay
  はC++で書く。
- **+ CEF UIレイヤー** — HUD / メニュー / 演出をHTML+CSSで素早く美しく
  作るための表示エンジン。差別化要素。
- **薄いsignal bridge** で繋ぎ、gameplay stateはC++に集約する。

向いている案件:

- **見た目の凝った2D / 2.5Dゲーム** — narrative / management /
  simulationで、HUDやダイアログの作り込みが効くもの。
- **C++チームがUI表現で消耗したくない** — CEFが肩代わりする。
- **将来的にCEFを外す可能性がある** — gameplayがC++に閉じているので、
  view層を差し替えるだけで非CEF環境(mobile / console / native overlay)
  に向ける道が残る。

向いていない案件:

- **CEFが使えない初期ターゲット** — モバイル / コンソールfirstの場合
  はCEF viewを最初から使わず、native overlayで代替する設計を取る。
- **60 fps action / 競技FPS** — CEF compositorのlatencyは許容できない
  ジャンルがある。その場合はviewもC++で組む。
- **ビジュアルエディタ前提のワークフロー** — 本エンジンに内蔵エディタは
  ない。
