# Hybrid Runtime — where does game code live?

> **方針。** gameplay は C++、CEF は表示レイヤー。この doc はその境界の分担を記述する。
> 関連 doc:
> - bridge の具体的 API: [`BRIDGE_API_CONTRACT.md`](BRIDGE_API_CONTRACT.md)
> - C++ gameplay の書き方: [`CPP_GAMEPLAY_GUIDE.md`](CPP_GAMEPLAY_GUIDE.md)

MitiruEngine は **C++ ゲームエンジン** である。CEF は綺麗な HTML / CSS UI
を低コストで作れる **見た目のレイヤー** として残す。両者は **薄い signal
層** で繋ぎ、gameplay state はすべて C++ 側に置く。

> Companion docs:
> - [ARCHITECTURE.md](ARCHITECTURE.md) — the C++ layer stack
> - [BRIDGE_API_CONTRACT.md](BRIDGE_API_CONTRACT.md) — bridge の signal-only 契約
> - [CPP_GAMEPLAY_GUIDE.md](CPP_GAMEPLAY_GUIDE.md) — C++ gameplay を書く入口

---

## 1. アーキテクチャ (新方針)

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

> Gameplay は C++、見た目は CEF、繋ぎは薄い signal。

C++ 側が gameplay state を単独所有し、tick もする。CEF は表示と入力受付
のみで、ロジックを持たない。bridge は「event」と「view update」の二種
類のメッセージしか流さない。

---

## 2. Decision matrix — where does this feature go?

| Feature type                                | Home | Notes |
|---------------------------------------------|:-:|---|
| **Scene flow / シーン遷移ロジック**         | **C++** | `SceneTransition` + game scene class。state は C++ にのみ存在。 |
| **Minigame state machine** (cooking, puzzle 等) | **C++** | 状態と遷移は C++。CEF には「今この絵を出して」と指示するだけ。 |
| **Novel / dialogue VM**                     | **C++** | スクリプトは JSON、VM (進行・分岐・条件評価) は C++。 |
| **Dialogue / cutscene 表示**                | CEF | C++ から「この行を typewriter で表示せよ」と指示。 |
| **Drag-and-drop (判定)**                    | **C++** | pointerdown/move/up は JS → bridge → C++ に発火、ヒット判定と結果は C++。 |
| **Drag-and-drop (見た目)**                  | CEF | カーソル追従や snap アニメは CSS / WAAPI で表現。 |
| **Per-frame UI animation** (tween, typewriter) | CEF | `Element.animate()` / CSS transition。完了通知だけ C++ に返す。 |
| **HUD layout / メニュー / ダイアログ枠**    | CEF | HTML + CSS。値は C++ から `setHUD(...)` で push。 |
| **Input routing** (kb / mouse / gamepad)    | **C++** | window 層で集約。CEF にはフォーカス時のみ転送。 |
| **Localization / i18n strings**             | JSON | C++ が読み、必要な行を CEF に push。 |
| **Balance / progression / loot tables**     | JSON | C++ が読み、ゲームロジックで使用。 |
| **Save blob format**                        | JSON | C++ がシリアライズ、`SaveStore` が永続化。 |
| **Audio playback**                          | **C++** | `mitiru::audio` 直接呼び出し。CEF からは event 経由で要求。 |
| **Save file I/O**                           | **C++** | `SaveStore` が atomic write。 |
| **Graphics backend** (DX12, Vulkan, …)      | C++ | Not reachable from JS. |
| **Window / CEF host**                       | C++ | Platform layer. |
| **ECS / physics / spatial query**           | C++ | gameplay と同居。 |
| **Heavy simulation** (AI, pathfinding, particle) | C++ | 元から C++。 |
| **Shader / VFX / post-processing**          | C++ | `RenderPipeline2D/3D`。 |
| **Scene transition compositor**             | C++ (`SceneTransition`) | Frame-accurate blend. |

「迷ったら C++」が原則。CEF に置くのは「画面に映るピクセルとそのアニメ
ーションだけ」と覚える。

---

## 3. Bridge — signal-only contract

新方針の bridge は **二方向の薄い signal 層**。型付きメッセージのスキーマ
と具体的 API は [`BRIDGE_API_CONTRACT.md`](BRIDGE_API_CONTRACT.md) を一次
情報として参照。本 doc では役割のみ示す。

### 3.1 JS → C++ : input / UI event のみ

CEF 内で起きたユーザー入力を C++ に通知するチャネル。**state を渡さない**
— 「何が起きたか」だけを送り、「次に何をするか」は C++ が決める。

許される送信例:
- `ui.button.click` (id=start_game)
- `ui.drag.end`     (source=ingredient_egg, target=slot_pan, position=...)
- `ui.menu.select`  (id=settings.volume.master, value=0.8)
- `ui.dialog.advance` (current_line_id=12)

**禁止例:**
- gameplay state の更新 (HP 計算結果、進行段階、所持アイテム…)
- 「次のシーンに遷移せよ」のような決定 — C++ が決める

### 3.2 C++ → JS : view update / 指示のみ

C++ が gameplay state を更新したあと、CEF に「画面をこう変えろ」と push
するチャネル。

許される送信例:
- `view.hud.set`      (hp=80, gold=120)
- `view.dialog.show`  (speaker=Maria, line="...", typewriter=true)
- `view.animation.play` (target=#hero, name=flip, duration=400ms)
- `view.dom.swap`     (region=order_panel, html_template=order_card, data=...)
- `view.scene.switch` (id=kitchen, transition=fade, duration=600ms)

**禁止例:**
- gameplay state を JS 側にキャッシュさせる (HP の整数値を JS が保持して
  計算する等)
- ビジネスロジックを呼び出すマクロ ("complete_recipe" のような副作用
  を伴うコマンド) — C++ 内部の関数呼び出しで完結させる

### 3.3 設計原則

- **state の単独所有**: gameplay state は C++ にのみ存在する。JS 側に
  hold するのは「今映っている見た目の表現」だけ。
- **冪等な view update**: 同じ `view.hud.set(hp=80)` を二回送っても結果
  が変わらないように作る。再接続 / hot reload を容易にする。
- **schema を切る**: 全 event / view update は型付き message とし、
  schema は `BRIDGE_API_CONTRACT.md` に集約。野良文字列 dispatch は
  追加しない。
- **fallback は不要**: 旧 doc にあった「C++ handler が無ければ JS 実装
  で動かす」前提は廃止。C++ が無い状況はそもそも view 単体の dev preview
  のみで、その場合は mock event を JS から流す。

---

## 4. なぜ「人間にも AI にも素直」なのか

- **責務が一直線**: gameplay は C++、view は CEF、繋ぎは signal。新規
  feature を書くとき迷う軸が減る。
- **再現性 / determinism**: state が C++ 単独所有なので、replay / save
  state / 自動テストが (V8 GC や DOM タイミングに依存せず) 素直に書ける。
- **AI へのラベリングは控えめに**: LLM は C++ も JS も書ける。
  MitiruEngine の強みは「LLM が書きやすい言語を選んだ」ことではなく、
  「責務分割が明確で誤った場所に書くと弾けること」。AI フレンドリーで
  あることは結果であって、看板ではない。

---

## 5. Rule of thumb: JSON vs C++ vs CEF

| Kind of thing | Shape | Example |
|---|---|---|
| **Pure data** (read-only at play time)      | **JSON** | Recipes, dialogue lines, UI strings, balance tables |
| **Scenario / scripted content**              | **JSON + C++ interpreter** | Novel script は JSON、VM は C++。 |
| **Interactive gameplay** (per-frame logic, state machine) | **C++** | Cooking scene、drag-and-drop 判定、menu state、AI。 |
| **Visual presentation** (layout, animation, transition) | **CEF (HTML/CSS/JS)** | HUD、ダイアログ枠、CSS animation、tween。 |
| **Hot / deterministic simulation**           | **C++** | Physics, pathfinding, heavy AI, particle. |
| **Platform services**                        | **C++** | Audio mixer, window, graphics backend, disk I/O. |

**Failure modes to avoid:**

- **JS に gameplay state を持たせる**: 「JS 側でちょっと計算してから C++ に
  投げる」を許すと、二箇所に真実が出来て同期バグになる。常に「event を
  C++ に発火して結果を待つ」形にする。
- **C++ に DOM レイアウトを書く**: CSS で済むレイアウト計算を C++ から
  座標で指定するのは無駄。CEF に任せる。
- **bridge を太らせる**: 「complete_recipe」のような副作用付き高レベル
  コマンドを bridge に乗せると、ロジックがどっちにあるか曖昧になる。
  event は「何が起きたか」、view update は「何を映すか」に限定する。
- **Over-JSONifying**: 状態機械を JSON rule で宣言するのは ROI が合わ
  ない。C++ で書く。
- **Over-C++-ifying view**: ダイアログのフェードイン tween を C++ で書く
  のは過剰。CSS / WAAPI で十分。

---

## 6. ポジショニング

MitiruEngine は:

- **C++ ゲームエンジン** — Siv3D / Unreal-Native と同じカテゴリ。gameplay
  は C++ で書く。
- **+ CEF UI レイヤー** — HUD / メニュー / 演出を HTML+CSS で素早く美しく
  作るための表示エンジン。差別化要素。
- **薄い signal bridge** で繋ぎ、gameplay state は C++ に集約する。

向いている案件:

- **見た目の凝った 2D / 2.5D ゲーム** — narrative / management /
  simulation で、HUD やダイアログの作り込みが効くもの。
- **C++ チームが UI 表現で消耗したくない** — CEF が肩代わりする。
- **将来的に CEF を外す可能性がある** — gameplay が C++ に閉じているので、
  view 層を差し替えるだけで非 CEF 環境 (mobile / console / native overlay)
  に向ける道が残る。

向いていない案件:

- **CEF が使えない初期ターゲット** — モバイル / コンソール first の場合
  は CEF view を最初から使わず、native overlay で代替する設計を取る。
- **60 fps action / 競技 FPS** — CEF compositor の latency は許容できない
  ジャンルがある。その場合は view も C++ で組む。
- **ビジュアルエディタ前提のワークフロー** — 本エンジンに内蔵エディタは
  ない。
