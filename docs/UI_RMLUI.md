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
`examples/hello_preview` (スライダー・チェック・ボタン)、`examples/html_settings` (同梱の設定画面)、`examples/html_tooltip` (ツールチップ)、
`examples/html_vlist` (10 万行の一覧)、`examples/html_worldtags` (敵の頭上の HP バーとダメージ数字)、`examples/beko_run` (ゲームの HUD)。

## 値を見せる (C++ → UI)

C++ の `hud.set("view.level", 42)` は、`data-model="view"` の要素の中で `level` という名前で引ける。
キーは `view.` で始める。文字列が `[` か `{` で始まると JSON として読むので、配列とオブジェクトも送れる。
`view.` の後ろの名前に `size` `it` `it_index` `ev` `literal` `true` `false` は使えない (RmlUi の式の予約語で、送っても UI に届かない)。
同じキーに直前と同じ値を送り続けても、UI は読み直さない (`setText` も同じ)。利用者が入力欄で書き換えた値は、ゲームが同じ文字列を送り続けるかぎり元に戻らない。値を決め直したいときは、前と違う値を送る。

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
| `data-style-プロパティ="式"` | RCSS の値を式で作る。式の変数は、最初に `hud.set` するまで 0 として計算する (`level + '%'` は `0%`)。ただし `data-style-background-color="bg"` のように背景色を変数 1 つで決める式だけは `transparent` として計算する (0 は色にならない)。その変数を `data-if` や `{{ }}` でも使うなら 0 のまま |
| `data-attr-属性="式"` | 属性を式で作る |
| `data-for="v : 配列"` | 配列の数だけ要素を複製する。行の値を使う `data-style-*` は、`data-for` を書いた要素の中の要素に書く (同じ要素だと、配列が縮む更新で行が消える前に式が評価されて、`left: px;` のような警告が出ることがある。[UI_WORLD_TAGS.md](UI_WORLD_TAGS.md)) |
| `<spark data-attr-value="x" cap="90">` | 値が変わるたびに 1 点足す折れ線 |
| `<tween data-attr-value="x" format="comma" ms="300">` | 値が変わると ms かけて数え上げて出す。最初の値と数でない値はそのまま出す |
| `<flash data-attr-watch="x" ms="400">…</flash>` | 値が変わった瞬間から ms の間、自分に class `m-flash` を付ける (最初の値では付けない) |
| `<toast data-attr-value="t" ms="4000" gap="160">` | `{"kind":"warn","message":"…"}` が変わると文を出し、`kind-warn` と `is-visible` を付け、ms 後に `is-visible` を外す。出ている間に届いた分は順に並び (8 件まで。超えたら古い未表示の物を捨てる)、消えてから gap ms 空けて次を出す。同じ文を続けて出すには `{"message":"保存した","n":2}` のように違う値にする |
| `<phrase>{{ talk_text }}</phrase>` | 日本語の文を文節ごとの `<span class="m-phrase-part">` に置き直し、文節の途中で折れないようにする ([PHRASE_BREAK.md](PHRASE_BREAK.md)) |
| `<ruby>親<rt>るび</rt></ruby>` | 親文字の上にルビを置く。箱の幅は親文字とルビの長い方で、短いほうは均等に割り付け、行は行間に収まらない分だけ高くなる ([RUBY_ELEMENT.md](RUBY_ELEMENT.md))。`<phrase>` の中にも置ける |
| `<include src="mitiru:talk.rml"/>` | 別ファイルの RML の断片をその場所に置く。`mitiru:` はエンジン同梱の UI 資産 (会話のウィンドウ `talk.rml` と `talk.rcss`、ロード画面 `loading.rml` と `loading.rcss`、設定画面 `settings.rml` と `settings.rcss` = [SETTINGS_SCREEN.md](SETTINGS_SCREEN.md))、ほかは文書からの相対 |

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
  RmlUi は空白でしか行を折らないので、`body` に `word-break: break-word` を置いて空白の無い日本語の文も枠の幅で折る
  (字の途中で折れる。文節で折りたい文は `<phrase>` で包む。[PHRASE_BREAK.md](PHRASE_BREAK.md))。
  `overflow: auto` の送りの棒 (`scrollbarvertical` など) の幅と色もここで決める
- `tokens.rcss`: 色・文字の大きさ・影の変数 (`--mitiru-accent` `--mitiru-text-md` `--mitiru-shadow-2` など)。
  `var(--mitiru-accent)` で引く。別の配色にするときは後から読む RCSS の `body` で同じ名前を書き直す
  (`python tools/design_md.py export --format mitiru-tokens DESIGN.md -o tokens.rcss` が DESIGN.md からその RCSS を書き出す)
- `components.rcss`: ゲームの画面で使う部品。`m-btn` `m-bar` `m-list` `m-overlay` + `m-dialog`
  `m-menu` `m-toast` `m-tooltip` `m-vlist` `m-wtag` `m-dmg` `m-tabs` `m-nav` `mitiru-modal` (confirm と prompt のダイアログ) `mitiru-preloader` `m-compact`。
  `m-dialog` は窓より高くなると窓の中に収まり、中身を枠の中で送る。RmlUi 本体の部品の見た目は下の表

RmlUi 本体の部品 (`examples/html_controls` が 1 画面に並べる)。どれも値を 1 つだけ `dispatch` し、今の値は C++ が送る。

| 部品 | 書き方 |
|---|---|
| 1 つ選ぶ | `<select class="m-select" data-attr-value="count" data-event-change="dispatch('set.count', ev.value)"><option value="3">3 こ</option></select>`。選んでいる間は Enter (A) で一覧が開き、上下で値が替わる |
| 面の切り替え | `<tabset class="m-tabset"><tab>うごき</tab><panel>…</panel><tab>みため</tab><panel>…</panel></tabset>`。どの面かは UI の中だけの状態。tab は Tab と方向キーで選べ、Enter (A) で切り替わる。パッドの LB / RB は面を左右に回す |
| 進み具合 | `<progress class="m-progress" direction="right" data-attr-value="lap"/>`。value は 0〜1 (`max` で変えられる)。`direction` は `top` `right` `bottom` `left` `clockwise` `counter-clockwise`。`m-progress--success` `--warning` `--danger` で色 |
| 入 / 切 | `<input type="checkbox" class="m-check" data-checked="outline" data-event-change="dispatch('set.outline', ev.checked)"/>`。印は C++ が送る真偽 (`data-checked`) で決まり、押すと反対の値を頼む |
| どれか 1 つ | `<input type="radio" class="m-radio" name="speed" value="fast" data-checked="speed" data-event-change="dispatch('set.speed', ev.value)"/>`。`data-checked` の値が `value` と同じ物に印が付く。印が外れた側の `change` は `dispatch` しない (選んだ側の 1 件だけが届く) |
| つまみ | `<input type="range" class="m-range" min="20" max="80" step="4" data-attr-value="radius" data-event-change="dispatch('set.radius', ev.value)"/>`。選んでいる間は左右 (十字キーの左右) で 1 step 動く |

配色・角丸・文字の大きさは `tokens.rcss` の変数で決まる。章の白系にするなら、文書の `body` で `--mitiru-red: #0a84ff;`
`--mitiru-paper-hi: #ffffff;` `--mitiru-radius-md: 10dp;` のように書き直す (`html_controls` の `main.rml`)

`tokens.rcss` と `components.rcss` の長さは `dp` で書いてあり、設定の UI の倍率 (`accessibility.uiScale`) で大きくなる。
倍率 1 では `px` と同じ大きさになる。

書体は `"M PLUS Rounded 1c"` (Regular 400 / Bold 700 / Black 900) を同梱し、仮名と漢字の代替書体にも使う。
game の `assets/ui/fonts/` と `assets/fonts/` にある `.ttf` / `.otf` も読む。名前はファイルの中の書体名になる。
画像は PNG と JPEG を読める。

## ツールチップ・仮想リスト・ワールド追従

| 部品 | 書き方 | 解説 |
|---|---|---|
| ツールチップ | 要素に `title="説明"`。マウスは止めたとき、キーとパッドはフォーカスが来たときに、host が箱を足して出す | [UI_TOOLTIP.md](UI_TOOLTIP.md) |
| 仮想リスト | C++ の `ui::VirtualList` が見えている行だけを `hud.set` し、RML は `m-vlist` と `data-for` で並べる。10 万行でも UI の行は 8 つ | [UI_VIRTUAL_LIST.md](UI_VIRTUAL_LIST.md) |
| ワールド追従 | C++ の `ui::WorldTagFrame` と `ui::FloatingNumbers` が `pick3d::worldToScreen` の座標を送り、RML が `m-wtag` と `m-dmg` を絶対配置する | [UI_WORLD_TAGS.md](UI_WORLD_TAGS.md) |

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
`mitiru dist --check` は、`assets/` の下の `strings.json` の訳の抜けと、同梱と `fonts/` のどの書体にも無い字を並べる
(SAVE_AND_SETTINGS.md の「翻訳」)。

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

## Debugger

デバッグメニュー (F1) の「UI debugger」を入れると、RmlUi 同梱の Debugger が UI の上に重なる。Element Info で
要素を選ぶと箱 (content / padding / border / margin) と、効いているプロパティとその宣言元 (ファイルと行) が出る。
Data Models で `hud.set` した値を、Log で RCSS の構文の誤りを見る。既定では何も載せず、headless の撮影
(`--headless-3d`) にも写らない。メニューと host のキーを切った実行 (`--no-dev-keys`、`ship.json`) では出せない。

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

| パッド | UI での働き |
|---|---|
| B | 戻る。開いている `<select>` の一覧は、開く前の値へ戻して閉じる。無ければ開いている `confirm` / `prompt` のダイアログを何も送らずに閉じる。どちらも無ければ Escape のキーとして文書へ渡す。キーの Esc も同じ |
| LB / RB | 選んでいる要素を含む `<tabset>` (無ければ文書の最初の `<tabset>`) の面を左右に回す。端を越えると回り込む。面の中を選んでいれば、新しい面の見出しへ移る。`focus-trap` の要素が見えている間は、その中の `<tabset>` だけを回す |

戻るで一覧かダイアログを閉じたフレームは、host が同じ B と Esc をゲームへ渡さない。ゲーム自身の画面は、ゲームが InputSnapshot の B (`in.cancelPressed()`) で閉じる作りにでき、ダイアログを閉じただけで画面ごと閉じることは無い。

ダイアログの要素に `focus-trap=""` を付けると、それが見えている間はキーでもパッドでもフォーカスがその外へ出ない
(Xbox Accessibility Guideline 113)。現れた時に中の最初の選べる要素を選び、閉じたら現れる前に選んでいた要素へ戻す。
`mitiru-modal-root` と `m-overlay` の書き方の例はこの属性を付けてある。メニューの端で回り込ませたいときは、
RmlUi の `nav-up: #last` のように、端の要素に行き先を書く。タブの見出しから下へ行く先も、見出しに `nav-down: #最初の部品` と書く
(書かないと、一番近い要素へ動くので、面の外の下のボタンへ行くことがある)。

ゲームが `MITIRU_ACTIONS` で割り当てたボタン (`jump` の `pad:A` など) は、組み替えた後の押下から消えるので、そのままでは UI に届かない。
`focus-trap` の要素が見えている間だけ、host が十字キー・A・B・LB・RB を UI の操作に足す。`focus-trap` の無い HUD のメニューで
パッドの A を使うなら、その A を操作の表の `reads` に入れない。

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
| 絶対配置の子を、箱の `overflow: hidden` で切る | `overflow: hidden` だけでは切れない。RmlUi は、流れの中の中身が箱の右か下へはみ出すときしか切らず、絶対配置の子ははみ出しに数えない。箱に `clip: always` を足す (`tests/mitiru/TestRmlOverflowClip.cpp`) |

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
- `include/mitiru/ui_rml/RmlTooltip.hpp` — `title` 属性の説明の出し入れ (マウスのホバーとパッドのフォーカス、置く位置、時間)
- `include/mitiru/ui/VirtualList.hpp` `WorldTags.hpp` — ゲーム側の部品。見えている行・頭上のバー・浮かぶ数字を `hud.set` へ送る (ゲームの DLL で使う。RmlUi には依らない)
- `include/mitiru/ui_rml/RmlImeBridge.hpp` — 変換中の文字を入力欄に出す
- `include/mitiru/ui_rml/RmlPadNavigation.hpp` — パッドの十字キー・スティック・A をフォーカス移動と Enter に、B を戻るに、LB / RB を tabset の面の切り替えにする。`focus-trap` の閉じ込めもここ
- `include/mitiru/core/detail/Engine_RmlUi.hpp` — フレームへの組み込み (入力・時計・合成・読み直し)
- `assets/ui/base.rcss` `tokens.rcss` `components.rcss` — エンジン同梱の RCSS (`mitiru:` で引く)
- `include/mitiru/ui_rml/RmlTranslation.hpp` — `[key]` と `[key:N]` の訳 (RmlUi の `TranslateString` から呼ぶ)
- `include/mitiru/input/GlyphLookup.hpp` — 操作と今の機器から絵柄の名前を決める。`assets/glyphs/` — 絵
- `assets/ui/crash_report.rml` `mitiru_strings.json` — host の確認画面と engine の文言
- 設定: `EngineConfig::uiDocument`。CMake は `MITIRU_WITH_RMLUI` (Windows で既定 ON)
