# UI を RML / RCSS で書く (RmlUi)

main window の HUD・メニュー・ダイアログは RmlUi で描く (ADR 0051)。RML / RCSS は HTML / CSS の方言で、
エンジンは game を描いた同じフレームの上に UI を重ねる。スクリプトは使わない。C++ が値を送り、UI は操作を送り返すだけ。

## 置き場所

game DLL の隣に `assets/ui/main.rml` を置く。mitiru_host はこのファイルがあれば RmlUi で描き、無ければ UI を出さない。

```
my_game/
  my_game.dll
  assets/ui/main.rml      ← 文書
  assets/ui/*.rcss        ← <link type="text/rcss" href="hud.rcss"/> で読む
  assets/ui/fonts/*.ttf   ← 書体 (assets/fonts/ でもよい)
```

`.rml` と `.rcss` を保存すると、エンジンが文書を読み直す。C++ が送った値は残るので、すぐ今の値で出る。

例は `examples/html_hud` (値を見せる)、`examples/html_menu` (操作を送る、パッドで選ぶ)、`examples/welcome_start` と
`examples/hello_preview` (スライダー・チェック・ボタン)、`examples/beko_run` (ゲームの HUD)。

## 値を見せる (C++ → UI)

C++ の `hud.set("view.level", 42)` は、`data-model="view"` の要素の中で `level` という名前で引ける。
キーは `view.` で始める。文字列が `[` か `{` で始まると JSON として読むので、配列とオブジェクトも送れる。

```html
<body data-model="view">
  <div>{{ level }}</div>
  <div class="bar" data-style-width="level + '%'" data-class-low="level &lt; 33"></div>
  <div class="pip" data-for="p : pips" data-class-lit="p.on"></div>
  <div>{{ time | time }}</div>
</body>
```

| 書き方 | 働き |
|---|---|
| `{{ x }}` | 値を文字にする |
| `{{ x \| int }}` | 書式 (`int` `time` `comma` `f2` `pct` `kmb`) |
| `data-if="x"` / `data-visible="x"` | 表示を切り替える |
| `data-class-名前="式"` | 式が真の間だけ class を付ける |
| `data-style-プロパティ="式"` | RCSS の値を式で作る。式の変数は、最初に `hud.set` するまで 0 として計算する (`level + '%'` は `0%`) |
| `data-attr-属性="式"` | 属性を式で作る |
| `data-for="v : 配列"` | 配列の数だけ要素を複製する |
| `<spark data-attr-value="x" cap="90">` | 値が変わるたびに 1 点足す折れ線 |
| `<tween data-attr-value="x" format="comma" ms="300">` | 値が変わると ms かけて数え上げて出す。最初の値と数でない値はそのまま出す |
| `<flash data-attr-watch="x" ms="400">…</flash>` | 値が変わった瞬間から ms の間、自分に class `m-flash` を付ける (最初の値では付けない) |
| `<toast data-attr-value="t" ms="4000">` | `{"kind":"warn","message":"…"}` が変わると文を出し、`kind-warn` と `is-visible` を付け、ms 後に `is-visible` を外す |
| `<include src="mitiru:talk.rml"/>` | 別ファイルの RML の断片をその場所に置く。`mitiru:` はエンジン同梱の UI 資産 (会話の窓 `talk.rml` と `talk.rcss`)、ほかは文書からの相対 |

式の中の `<` と `>` は `&lt;` `&gt;` と書く。UI から値を書き戻す経路は無い (`data-value` を書いても C++ の値は変わらない)。
`<tween>` `<flash>` `<toast>` の時間は UI の時計 (下の「時計」) で数えるので、replay で途中の絵まで同じになる。

## 操作を送る (UI → C++)

```html
<button data-event-click="dispatch('pick.color', 'id', c.id)">…</button>
<input type="range" data-attr-value="speed" data-event-change="dispatch('speed', ev.value)"/>
<button data-event-click="confirm('もどしますか?', 'かくにん', 'reset')">リセット</button>
```

`dispatch(名前, キー, 値, …)` は game の `in.action("pick.color")` に届き、`in.actionPayload("pick.color")` が
`{"id":2}` を返す。名前の後ろに値を 1 つだけ書くと、payload はその値そのもの (`"240"` `"true"` `"\"hard\""`) になる。
整数は `2.0` ではなく `2` と書かれる。

チェックボックスのように「今どちらか」を持つ部品は、状態を C++ から受けて表示し、押されたら反対の値を頼む
(`data-class-on="locked"` と `dispatch('lock', !locked)`。welcome_start と hello_preview がこの形)。

| 書き方 | 働き |
|---|---|
| `confirm(本文, 見出し, 名前)` | 確認ダイアログを開き、`confirm_ok()` を押した時だけ `名前` を送る。ダイアログは `data-if="ui_confirm_open"` で出し、`{{ ui_confirm_title }}` `{{ ui_confirm_text }}` を表示する |
| `prompt(見出し, 本文, 名前, 初期値)` | 文字入力のダイアログを開く。`data-if="ui_prompt_open"` で出し、入力欄に `data-attr-value="ui_prompt_value"` `data-event-change="prompt_input()"` と `autofocus=""` を付ける。`prompt_ok()` か Enter で `名前` を `{"value":"…"}` 付きで送り、`prompt_cancel()` は何も送らない |
| `commit_input('profile.name')` | 入力欄の `data-event-change` と `data-event-blur` に付ける。Enter か欄を離れた時に `input:profile.name` を `{"value":"…"}` 付きで送る |
| `live_input('search.q')` | 入力欄の `data-event-change` に付ける。1 文字ごとに同じ形で送る |
| `drop(名前, to)` | 落とし先の `data-event-dragdrop` に付ける。掴んだ要素の `drag-value` 属性を from にして `{"from":…,"to":…}` を送る |

ダイアログが開いているかどうかは UI の中だけの状態で、game の状態ではない (`examples/html_menu/assets/ui/main.rml` の最後を参照)。
IME で変換中の文字は入力ではないので、変換中は `dispatch` も入力欄の送信も止まる。

`drag-value` を持つ要素は `mitiru:base.rcss` が `drag: drag-drop` にするので、そのまま掴める。掴んでいる要素には
`m-dragging`、落とし先の上にいる間はその要素に `m-drop-hover` が付く。

## エンジンが用意するもの

```html
<link type="text/rcss" href="mitiru:base.rcss"/>
<link type="text/rcss" href="mitiru:tokens.rcss"/>
<link type="text/rcss" href="mitiru:components.rcss"/>
```

- `base.rcss`: よく使う要素の display と既定の書体。RmlUi にはブラウザの既定スタイルが無く、`div` も inline、文字色の既定は白になる。
  RmlUi は空白でしか行を折らないので、`body` に `word-break: break-word` を置いて空白の無い日本語の文も枠の幅で折る。
  `overflow: auto` の送りの棒 (`scrollbarvertical` など) の幅と色もここで決める
- `tokens.rcss`: 色・文字の大きさ・影の変数 (`--mitiru-accent` `--mitiru-text-md` `--mitiru-shadow-2` など)。
  `var(--mitiru-accent)` で引く。別の配色にするときは後から読む RCSS の `body` で同じ名前を書き直す
  (`python tools/design_md.py export --format mitiru-tokens DESIGN.md -o tokens.rcss` が DESIGN.md からその RCSS を書き出す)
- `components.rcss`: ゲームの画面で使う部品。`m-btn` `m-bar` `m-list` `m-overlay` + `m-dialog`
  `m-menu` `m-toast` `m-tabs` `m-nav` `mitiru-modal` (confirm と prompt のダイアログ) `mitiru-preloader` `m-compact`。
  `m-dialog` は窓より高くなると窓の中に収まり、中身を枠の中で送る

`tokens.rcss` と `components.rcss` の長さは `dp` で書いてあり、設定の UI の倍率 (`accessibility.uiScale`) で大きくなる。
倍率 1 では `px` と同じ大きさになる。

書体は `"M PLUS Rounded 1c"` (Regular 400 / Bold 700 / Black 900) を同梱し、仮名と漢字の代替書体にも使う。
game の `assets/ui/fonts/` と `assets/fonts/` にある `.ttf` / `.otf` も読む。名前はファイルの中の書体名になる。
画像は PNG と JPEG を読める。

## 文言を訳す

RML の文の中の `[key]` を、文書の隣の `strings.json` の訳にする。`[key:N]` は N を数として言語ごとの複数形
(`key_one` / `key_few` など) を選び、`{0}` に N を入れる。数を data binding で書けば (`[coins:{{ coins }}]`)、
値が変わるたびに訳し直す。表に無いキーとキーの形でない `[...]` (`[A] で決定` など) はそのまま出る。

```json
{ "languages": [ {"code": "en"}, {"code": "ja"} ], "fallback": "en",
  "strings": { "menu.start": {"en": "Start", "ja": "はじめる"},
               "coins": {"en": "{0} coins", "ja": "{0} 枚"}, "coins_one": {"en": "{0} coin"} } }
```

表の形は `LocalizationManager` (core/Localization.hpp) と同じで、訳が無ければ言語の基本部分 (`pt-BR` なら `pt`)、
予備の言語 (`fallback`、既定 `en`)、キーの順に探す。engine の画面の文言 (`assets/ui/mitiru_strings.json`) の上に重ねて読む。
言語は game の `in.language()` と同じ値 (設定の `language`) で、変わると文書を読み直して訳し直す (入力欄の中身や
スクロールの位置は戻る)。`strings.json` を保存しても読み直す。

## ボタンの絵柄

`<img src="glyph:jump"/>` は、操作 `jump` の割り当て (`input_actions.json` と利用者の設定) のうち、今使っている機器
(最後に触ったのがキーボードとマウスかパッドか) の最初の入力の絵柄を出す。パッドは機種の書き方になる
(A ビットは Xbox で A、PlayStation で ×、Nintendo で B)。`glyph:pad:A` や `glyph:key:Space` のように入力の名前を
直に書くと、割り当てを通さずにその入力の絵柄になる。機器・パッドの機種・割り当てが変わると host が絵を引き直す。

絵は engine の `assets/glyphs/<絵柄>.png` (64x64、CC0、`tools/gen_input_glyphs.py` が描く)。game の
`assets/ui/glyphs/<絵柄>.png` があればそちらを使う。絵柄の名前は `input::inputGlyph` (input/PadGlyphs.hpp) が返すもので、
今の機器に割り当てが無い操作は何も出さない。UI は絵の名前を受けるだけで、状態は host が持つ (`view3d:N` と同じ形)。

ゲームが自分で描く HUD (Screen に描く 3D の吹き出しなど) は、`in.inputDevice()` と `in.padFamily()` (ABI v50) を
`input::glyphFor(名前, 表, {}, 機器, 書き方)` に渡して同じ絵柄の名前を引く。利用者の割り当ては host の設定にあって DLL からは見えないので、
DLL は表 (MITIRU_ACTIONS) の既定で引く。オンラインでは機器が PC ごとに違うので、0 (キーボード、Xbox) が届く。

## host が重ねる文書

host は自分の確認画面 (前の実行のクラッシュ報告、`assets/ui/crash_report.rml`) を game の文書の上に重ねて開く
(`RmlUiHost::openOverlay`)。data model は game の文書と同じ `view` を使い、操作の名前は `crash.*` で、host が受けて
game へは渡さない。game が UI を持たない時は、その画面だけを UI の文書として開く。

## 時計

UI の時計は、決定論で動かしているとき (headless・replay) はフレーム数から作る。遷移やアニメーションの途中も
replay で同じ絵になる。RmlUi は 1 回の更新で進める時間を 0.1 秒までに抑える。

## 入力

マウス・ホイール・パッド・IME の変換中の文字は、game に渡すのと同じ InputSnapshot から UI へ渡す。`--input-script` の
クリックとホイール (`wheel`)・変換中の文字 (`ime`) が UI にも届き、`--replay-test` / `--replay` でも UI が同じ操作を受ける。
UI の操作は次の固定ステップで game に届く。UI とゲームは同じマウスを受けるので、UI の部品を押したクリックもゲームに届く。

パッドは十字キーと左スティックの倒し始めで上下左右、A で Enter を UI へ渡す。選べるのは RCSS で `tab-index: auto; nav: auto;`
を付けた要素だけで、何も選んでいなければ最初の押下で先頭の要素を選ぶ。`components.rcss` では `m-nav` の中のボタンと入力欄が
この形になる。選んだボタンは Space と Enter でも押せるので、Space で遊ぶゲームの HUD には付けない。

キーと文字は窓が受けた WM_KEYDOWN / WM_CHAR の列から渡し、IME の確定文字も届く。入力欄を選んでいる間は、game が
文字を求めていなくても host が IME を有効にし、変換窓をその欄の位置へ置く。欄を離れると、game が求めていなければ IME を切る。
変換中の文字は欄の中のキャレットの位置に下線つきで出る。キーの列は録画しないので、入力欄に打った文字は replay では
再現されない (変換中の表示は再現される)。その文字で送った action は録画に入るので、game の状態は再現される。

## 撮影と解像度

`--headless-3d` で撮ると UI ごと写る。`--headless` だけ (CPU の描画) では UI は出ない。

文脈 (座標) は game の論理解像度で、描画はバックバッファの実画素で行う。窓を論理解像度より大きくすると、
図形の縁はそのまま滑らかで、文字は論理解像度で作った字形を拡大して描く。

## HTML の HUD から移すとき

C++ 側は `hud.set` で送り `in.action` で受けるまま変えない (変わるのは下のキーの名前だけ)。`assets/scene.html` を `assets/ui/main.rml` に書き直し、CSS は下の表のとおり RCSS に写す。
`data-m-text` は `{{ }}` に、`data-m-action` は `data-event-click="dispatch(...)"` にする。
`view.hud.score` のように `view.` の後ろにも点があるキーは、model の中で `hud.score` という点を含む名前の変数になり、
RML の式からは引けない。キーは `view.score` のように `view.` の直下に平らに送る。

## CSS から RCSS へ写すときに変わること

| CSS の書き方 | RCSS での書き方 |
|---|---|
| `rgba(r, g, b, 0.5)` | `#rrggbbaa` (RCSS の `rgba()` のアルファは 0〜255) |
| `box-shadow: 0 10px 24px rgba(…)` | `box-shadow: #rrggbbaa 0px 10px 24px`。`inset` は後ろに書く |
| `background: linear-gradient(…)` | `decorator: linear-gradient(…)` (`background` は色だけ) |
| `background-clip: text` (文字をグラデーションで塗る) | 無い。グラデーションの中ほどの 1 色で塗る (hello_preview) |
| `font-family: "A", "B", sans-serif` | 1 つだけ書く。足りない字は代替書体で出る |
| `--font: "A"` を `font-family: var(--font)` で引く | 変数の値は引用符で囲まない (`--font: M PLUS Rounded 1c;`)。囲むと引用符ごと書体名になる |
| `font-weight: 800` / `600` | 読んである太さ (`bold` か `700`、`900`) を書く |
| `text-shadow: 0 4px 20px …` | `font-effect: shadow(0px 4px #00000080)`。ぼかしは別の `blur` |
| `transition: color .25s ease` | `transition: color 0.25s cubic-out` (時間は秒、緩急は名前。`steps()` は無い) |
| `border: 1px solid #ccc` | `border: 1px #cccccc` |
| `display: inline-flex` のボタンに裸の文字 | flex の直下の裸の文字は描かれない。`inline-block` + `text-align: center` にするか `<span>` で包む |
| `width: max-content` | 無い。絶対配置の要素は幅を書かなければ中身に合わせて縮む |
| `inset: 0` | `top: 0px; right: 0px; bottom: 0px; left: 0px;` |
| `<input type=range>` の `accent-color` | `slidertrack` (溝) `sliderprogress` (つまみの左の塗り) `sliderbar` (つまみ) を RCSS で描く。溝は押せる高さのまま `decorator: linear-gradient(to bottom, …)` で細い線だけを描く (welcome_start) |
| `<input type=checkbox>` | 状態を C++ が持つボタンにする (`data-class-on` + `dispatch('x', !x)`)。チェックの印は文字か要素で描く |
| CSS の三角形 (`border` を透明にする) | 左右 2 枚の要素に `decorator: linear-gradient(to top left, 色 50%, 透明 50%)` |
| `display: grid` (pane 群など) | 無い。flex を入れ子にして書き直す |
| `::before` / `::after` | 無い。要素を足して書き直す |
| `:has()` | 無い。親に付ける class を C++ から送る (`data-class-*`) |
| `color-mix(in srgb, A 10%, transparent)` | 無い。混ぜた色を変数にしておく (`tokens.rcss` の `--mitiru-accent-tint`) |
| `calc()` / `min()` / `max()` | 無い。データ式 (`data-style-width="(x - 20) / 280 * 100 + '%'"`) で作るか、値を固定する |
| `clip-path` | 無い。`mask-image` で代わりが利く形もある |
| `@media (prefers-reduced-motion)` | 無い (RCSS の `@media` は幅・高さ・向き・テーマだけ)。動きを止める設定は C++ から class で渡す |
| `position: sticky` / `mix-blend-mode` / `aspect-ratio` | 無い |
| `:root { --x: … }` | `body { --x: … }` (`:root` が無い) |
| `outline` (フォーカスの枠) | 無い。`:focus-visible` に `box-shadow` を付ける |

ほかの差 (角丸の親の `overflow: hidden` で子のグラデーションを切る、など) と比較画像は `docs/RMLUI_EVALUATION.md` にある。

## ツール窓

inspector / perf / rewind などのツール窓も RmlUi で描く。`mitiru_tool --page <name>` が
`apps/mitiru_tool/assets/<name>.rml` を小さな窓に 1 枚だけ出し、model 名は `tool`。値の写し方と操作の
送り方は main window と同じで、ページごとの振る舞いは `apps/mitiru_tool/ui/pages/` の C++ が持つ。
開き方と増やし方は `docs/TOOL_WINDOWS.md`。

## 中身

- `include/mitiru/ui_rml/RmlUiHost.hpp` — Engine が持つ窓口。RmlUi の型を出さない
- `src/ui_rml/RmlRenderInterfaceDx12.cpp` — エンジンの DX12 デバイスの上の RenderInterface
  (幾何・テクスチャ・切り抜き・stencil のクリップマスク・層・グラデーション・blur / drop-shadow / 色行列 / マスク)
- `include/mitiru/ui_rml/RmlStateModel.hpp` — `hud.set` の値 → data model、`dispatch` / `confirm` / `prompt` / 入力欄 / `drop`
- `include/mitiru/ui_rml/RmlBinderElements.hpp` — `<tween>` `<flash>` `<toast>` と drag / drop の class
- `include/mitiru/ui_rml/RmlImeBridge.hpp` — 変換中の文字を入力欄に出す
- `include/mitiru/ui_rml/RmlPadNavigation.hpp` — パッドの十字キー・スティック・A をフォーカス移動と Enter にする
- `include/mitiru/core/detail/Engine_RmlUi.hpp` — フレームへの組み込み (入力・時計・合成・読み直し)
- `assets/ui/base.rcss` `tokens.rcss` `components.rcss` — エンジン同梱の RCSS (`mitiru:` で引く)
- `include/mitiru/ui_rml/RmlTranslation.hpp` — `[key]` と `[key:N]` の訳 (RmlUi の `TranslateString` から呼ぶ)
- `include/mitiru/input/GlyphLookup.hpp` — 操作と今の機器から絵柄の名前を決める。`assets/glyphs/` — 絵
- `assets/ui/crash_report.rml` `mitiru_strings.json` — host の確認画面と engine の文言
- 設定: `EngineConfig::uiDocument`。CMake は `MITIRU_WITH_RMLUI` (Windows で既定 ON)
