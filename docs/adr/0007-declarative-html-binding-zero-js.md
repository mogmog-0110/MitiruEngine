# ADR 0007 — 宣言的 HTML data-binding で UI を zero-JS にする

- **Status:** Accepted (2026-05-24). 実装 🚧 (`web/mitiru_runtime/mitiru_bind.js` 出荷済 / `StateWriter.hpp` は ergonomics として後追い)
- **Context:** engine の最大の差別化要素は「**HTML/CSS で UI が書ける C++ engine**」 (5 軸 #1)。
  だが現状、各 game は scene.html に **per-game の JS glue** を手書きしている
  (`window.mitiru.onStateChange` を購読し、受け取った値を手で DOM に流し込み、
  座標を手で計算して要素を動かす)。これは「HTML/CSS だけ書けばいい」という
  価値提案に反し、**JS の学習コストを再導入** している。開発者は結局
  「C++ + HTML/CSS + JS」の 3 言語を要求される。
- **Extends:** ADR 0001 (CEF を signal-only UI overlay 化) / ADR 0005
  (Host-Game 境界は C-only signal flow)。本 ADR はその signal-only bridge の
  **JS 受信側を汎用化** する。game state は依然 C++ の GameMemory が唯一所有し、
  JS は state も logic も持たない。

## 決定

**engine は汎用の宣言的 data-binding runtime `web/mitiru_runtime/mitiru_bind.js` を出荷する。
game 開発者は JavaScript を 1 行も書かずに HTML/CSS だけで UI を組む。**

開発者が書くのは:

- **C++** — logic と state (GameMemory)。state を bridge 経由で push する。
- **HTML/CSS** — 見た目。`data-m-*` 属性で「どの state を、どこに、どう映すか」を宣言する。

binder は **純粋な presentation layer** である。game state を一切持たず、logic も持たない。
C++ が push した state を DOM に reflect するだけ。ADR 0005 の不変条件
(state は C++ GameMemory が所有 / bridge は signal-only / JS は gameplay state を持たない)
を完全に保つ。

## state schema — 2 階建て

bridge は値を **文字列** として `window.mitiru.onStateChange(key, value)` で届ける (ADR 0001/0005)。
binder はこの文字列を 2 通りに解釈する:

1. **scalar / 小さな object → JSON 文字列**
   - 例: `view.boss = '{"active":true,"pct":62}'`
   - 読みやすく、HUD の数十 field 程度ならこれで十分。
2. **hot / 大きな list → compact delimited 文字列**
   - 例: `view.scene = "2,80,120,1;2,300,60,0"` (行を `;`、列を `,` で区切る)
   - 列名は HTML 側で `data-m-fields="type,x,y,dir"` として宣言する。

### なぜ 2 階建てか — buffer 制約からの帰結

bridge の per-push buffer (`FrameIntents` の `StatePushItem::strVal`) は **3968 byte** 固定。
60fps の弾幕 shmup は 1 frame に ~150 entity を push する。これを JSON
(`[{"type":2,"x":80,"y":120,"dir":1}, …]`) で表現すると buffer を **溢れる**。
compact delimited 形式なら同じ 150 entity でも十分収まる。

→ **clean で小さいものは JSON、hot path の大きな list は compact delimited**。
この使い分けは好みではなく **3968 byte の物理制約からの帰結** である。

## 宣言的 HTML 語彙 (data-m-*)

すべて出荷済 binder が解決する。**user JS はゼロ**。

| 属性 | 役割 |
|---|---|
| `data-m-text="path"` (+ `data-m-format="int\|kmb\|pct\|time\|comma"`) | path の値を textContent に。format で整形 |
| `data-m-tpl="… {path} … {path:fmt} …"` | template 文字列に複数 path を埋め込む |
| `data-m-show` / `data-m-hide="path \| == v \| != v \| < n \| > n \| !path"` | 条件表示 / 非表示 (不等号は数値比較) |
| `data-m-attr="src: {path}; …"` | 任意属性に値をバインド (画像 src / title / aria 等) |
| `data-m-class="cls: path; …"` | path が truthy なら class を付与 |
| `data-m-style="prop: {path}unit; …"` | path を CSS property にバインド |
| `data-m-action="name"` (+ `data-m-arg="path"`) | クリック/入力で `dispatch(name, arg)` (HTML → C++ 入力)。フォーム要素は現在値を自動で arg に。動的リストの項目クリックや設定スライダーもこれで zero-JS |
| `data-m-flash="field"` | 値が変わった瞬間に `m-flash` クラスを一瞬付与 (CSS keyframe 発火。例: 合体ポップ) |
| `data-m-repeat="listPath"` + 子 `<template>` | list を DOM 要素群に展開 (後述) |

### list 展開 (`data-m-repeat`) は pooling する

`data-m-repeat` は子 `<template>` を list の各 item に対して複製するが、
**innerHTML を毎フレーム破棄 / 再生成しない**。DOM 要素を **pool して再利用** する。
60fps で 150 要素を毎 frame innerHTML で thrash すれば GC と layout で破綻するため、
pooling を **engine 側に集約** して best-practice を 1 箇所に閉じ込める。

`<template>` 内で使える item-scope の属性:

- `data-m-case` / `data-m-type` — item の種類で表示を分岐
- `data-m-fields` — compact delimited 行の列名宣言
- `data-m-key="field"` — item を key で対応付け、要素を保持 → 座標更新が CSS transition でスライドになる
- `data-m-pos="xField,yField"` / `data-m-rot` — item の座標 / 回転を field から直接
- 生成された要素には一瞬 `m-enter` クラスが付く (CSS 生成アニメ用、任意)

## C++ 側 ergonomics (optional)

`include/mitiru/bridge/StateWriter.hpp` は schema 文字列 (JSON / compact delimited) を
手で文字列連結せずに emit するための helper。**これは C++ であり JS ではない** ので、
zero-JS の目標には影響しない。binder は StateWriter を前提とせず、生の文字列 push でも動く。

## 既存哲学との関係

[アトミックツール哲学](../SCOPE.md) と整合する:

- 「**HTML/CSS で UI が書ける C++ engine**」 (5 軸 #1) → JS 学習コストを **完全に消す**ことで
  この軸を初めて言葉どおりに成立させる。
- 「**bridge は signal-only / state は C++ 所有**」 (ADR 0001 / 0005) → binder は受信側を
  汎用化しただけで、不変条件を 1 つも崩さない。JS は state を持たず、logic も持たない。
- 「**必要なものしか画面に出さない**」 → `data-m-show`/`data-m-hide` で「文脈外の要素を
  視界から消す」を宣言的に書ける。

## 5 軸との関係

新規軸ではなく、**軸 #1 を構造的に成立させる**:

- これまで「HTML/CSS で書ける」と謳いつつ実態は per-game JS が必須だった。本 ADR で
  tutorial が「**JavaScript は不要**」を文字どおり宣言できる。
- 軸 #2 (time-travel) / 軸 #4 (replay) は ADR 0005 のとおり GameMemory が唯一の state
  であることに依存する。binder が state を持たないので、この保証を一切損なわない。

## トレードオフ — 失敗モード分析

| # | 失敗モード | リスク | 緩和策 |
|---|---|---|---|
| F1 | C++ が push する schema と HTML の `data-m-*` path が **silent に不一致** (typo / 列順ズレ) | 値が黙って表示されない。原因追跡が困難 | dev warning mode: `data-m-debug` 属性 / `?mdebug=1` で未解決 path を console 警告 |
| F2 | 開発者が「いつ JSON、いつ compact」か迷う | 学習 nuance が残る | rule を doc 化し、**3968 byte 上限**に紐付けて説明 (clean→JSON / hot list→compact) |
| F3 | `data-m-show` 式に任意 JS を書きたくなる | eval 導入で安全性 / 哲学が崩れる | 式 sublanguage を **意図的に極小**に保つ (`==` / `!=` / `!` のみ)。eval は不採用。**power より safety** |
| F4 | HTML を使わない (純 C++ 描画) game に強制がかかる | Mode A 利用者の妨げ | native C++ 描画 (Mode A) は **引き続きサポート**。本 ADR は HTML UI game 向けの追加路線 |

## 検討した代替案

- **per-game JS glue を手書きし続ける** — 却下。JS 学習コストを再導入し、軸 #1 を空文句にする。
- **hot list を含め全部 JSON** — 却下。高 entity 数で 3968 byte buffer を溢れる (本 ADR の 2 階建ての動機そのもの)。
- **重量級 client framework (CEF 内に React / Vue)** — 却下。アトミックツール / minimal 哲学に反し、
  大きな依存を持ち込む。binder は依存ゼロの薄い presentation layer で足りる。

## 参考

- 本 engine の ADR 0001 (CEF signal-only bridge) — 受信側を汎用化する前提
- 本 engine の ADR 0005 (Host-Game C-only signal flow) — state は C++ GameMemory が唯一所有
- Vue.js / Alpine.js の declarative directive (`v-*` / `x-*`) — `data-m-*` 語彙の発想の参照元
  (ただし framework は持ち込まず、必要最小の subset のみ engine 内に実装)
