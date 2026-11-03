# アクセシビリティ

画面を見ずに遊ぶ人、音を聞かずに遊ぶ人、色の見え方が違う人のために host が持っている仕組みを 3 つ書く。どれもゲームの DLL には何も見せず、録画と再生の状態にも入らない。

## 1. Narrator に字幕と通知を読ませる

RML の要素に `data-a11y` を付けると、その文字が出た時、または変わった時に、読み上げソフト (Narrator など、UI Automation の客) へ通知として渡る。同梱の会話の窓 (`mitiru:talk.rml`) と効果音の字幕 (`mitiru:captions.rml`) は付けてある。

```html
<div data-a11y="polite" data-a11y-id="game.toast">{{ toast_text }}</div>
<div data-a11y="assertive" data-attr-aria-label="'警告: ' + alert_text">{{ alert_text }}</div>
```

| 属性 | 意味 |
|---|---|
| `data-a11y` | `polite` は次の文が来たら古い文を捨てる。`assertive` は読み上げ中でも割り込む |
| `data-a11y-id` | 通知の系統の名前 (省略は `mitiru.ui`) |
| `aria-label` | あれば画面の文字より先に読む。画面に出す文より短い文を読ませたい時、話者の名前を足したい時に使う |

読む文は、訳と data model の展開が済んだ画面の文字そのものなので、言語を替えても字幕と読み上げが食い違わない。`data-if` などで隠れている要素と、隠れている子の文字は読まない。

読み上げソフトが動いていない間は、文書を走査しない。動き始めた最初の 1 回は、画面に出ている文字を基準として覚えるだけで読まない (起動した瞬間に字幕を読み上げ始めないため)。

### まだできないこと

- 段階 1 だけでは、UI の木 (ボタン、スライダー、入力欄) は読めない。木を読ませるのは次の「UI の木を読ませる」(AccessKit を組み込んだビルドだけ)。
- 会話の選択肢は読まれない。台詞の行だけを読む。
- 3D の世界は読めない。世界を語らせる設計はゲームごとの仕事で、エンジンが渡すのは字幕と通知まで。
- 窓ができるより前に Narrator が窓を調べた場合、最初の 1 回は提供者が付く前の答えになる。Narrator が窓へ移った時に調べ直す。

### 実機 (Narrator) での確かめ方

自動の試験は、UI Automation の客 (`IUIAutomation`) から窓の名前・役割・通知の種類と文が届くところまでを確かめる。聞こえ方は人が確かめる。

1. `Win + Ctrl + Enter` で Narrator を起動する。
2. 会話のある例 (`cutscene3d`) を `mitiru_host` で普通に起動する。
3. 会話を進める。台詞の行が、話者の名前つきで読まれること。
4. 環境の字幕を入れた設定 (`accessibility.captions.ambient: true`) で `captions` 例を起動し、Z / X / C を押す。ドアや足音の字幕が 1 行ずつ読まれ、C (警報) は読み上げ中でも割り込むこと。
5. Narrator を止める。その後は host が文書を走査しないこと (`UiaClientsAreListening` が偽)。

### UI の木を読ませる (段階 2、AccessKit)

ボタン、スライダー、入力欄、チェック、見出し、ダイアログを、読み上げソフトに role と name つきの木として見せる。Narrator の Tab / 矢印でたどり、押す・値を変える操作を受ける。[AccessKit](https://github.com/AccessKit/accesskit) を使い、組み込んだビルドだけで動く (ADR 0117)。

```
python tools/fetch_accesskit.py                          # accesskit.dll などを external/accesskit へ置く (初回だけ)
cmake -S . -B build -DMITIRU_WITH_ACCESSKIT=ON           # 既定は OFF。ON にすると accesskit.dll が exe の隣に増える
```

組み込まなかったビルドと、`accesskit.dll` が無い PC では、これまでどおり上の通知だけが動く (同じ窓に UI Automation の提供者は 1 つしか付けられないので、どちらか 1 つ)。

RML 側で書くもの。特別な属性は要らず、普通の RML に次の規則で role と name が付く。

| RML | role | name |
|---|---|---|
| `<button>`、`<input type="button">` | button | 中の文字。`aria-label` があればそれが先 |
| `data-event-click` か `tab-index` を持つ `<div>` / `<span>` | button | 中の文字 |
| `<input type="checkbox">` / `radio` | check box / radio | `aria-label`。状態は `checked` |
| `<input type="range">` | slider | `aria-label`。`min` `max` `step` と今の値 |
| `<input type="text">` / `<textarea>` | text input (`password` は値を渡さない) | `aria-label` |
| `<select>`、`<progress>`、`<a>`、`<img alt>`、`<h1>`〜`<h6>`、`<tab>`、`<ul>` / `<li>` | combo box、progress、link、image、heading、tab、list | 同上 |
| `focus-trap` を付けた要素 | dialog (modal) | `aria-label` |
| `role="..."` | 書いた role を優先 (button、link、checkbox、radio、slider、progressbar、tab、tablist、tabpanel、dialog、heading、list、listitem、group、img、textbox、combobox、none) | 同上 |
| `data-a11y="polite"` / `"assertive"` | live region | 通知の文 (段階 1 と同じ規則) |
| 文字だけの `<div>` / `<p>` / `<span>` | label | 文字 |

`data-if` で隠れた要素と、`aria-hidden="true"` の要素は木に入らない。アイコンだけのボタンは、文字が無いので `aria-label` を書く。modal の文書 (`openOverlay`) が開いている間は、その下の文書は木に入らない。

読み上げソフトからの操作は、利用者が触ったときと同じ event になる。Invoke は click、値の設定は `change` を起こすので、`data-event-click` / `data-event-change` から `dispatch` と `takeActions` を通って game の action に届く。UI は状態を持たない。操作は `InputSnapshot` に載らないので録画には入らず、窓の無い実行 (`--headless`、`--replay`) では起きない。

読み上げソフトが動いていない間は、文書をたどらない。動いているかは OS の設定 (`SPI_GETSCREENREADER`) と UI Automation の客の有無で見る。読み上げソフトが無い PC で木の経路だけを確かめるときは、環境変数 `MITIRU_SCREEN_READER=1` を付ける。

#### 実機 (Narrator) での確かめ方 (UI の木、未確認)

自動の試験は、本物の窓と `accesskit.dll` を `IUIAutomation` の客から読み、名前・役割・トグルの状態が見えること、変えた節が届くこと、Invoke が操作として届くことまでを確かめている (`mitiru_tests_accesskit`)。**Narrator の聞こえ方と、live region の読み上げは確かめていない。**

1. 上の手順で `MITIRU_WITH_ACCESSKIT=ON` で組む (`build/apps/mitiru_host/accesskit.dll` が host の隣にあること)。
2. `Win + Ctrl + Enter` で Narrator を起動し、メニューのある例を `mitiru_host` で普通に起動する (例: F1 のデバッグメニュー、`debug_menu.rml`)。
3. Narrator のキー (Caps Lock + 右矢印) で一つずつたどる。ボタンは「名前、ボタン」と読まれること。スライダーは値が読まれること。
4. 選んだボタンで Enter (Caps Lock + Enter) を押す。押したときと同じ動きをすること。
5. 確認ダイアログ (`focus-trap`) を開く。ダイアログの外が読まれなくなること。
6. `data-a11y` を付けた行 (会話、字幕) の文字が変わったとき、読まれること (live region)。段階 1 の通知と同じ聞こえ方かを比べる。
7. Narrator を止める。その後は host が文書をたどらないこと。

読まれなかったときは、環境変数 `MITIRU_SCREEN_READER=1` を付けて起動し、Inspect (Windows SDK の `inspect.exe`) で木が見えるかを先に確かめる。木が見えて Narrator が読まないなら、live region か role の対応の問題。

## 2. 効果音の字幕

`assets/audio/sounds.json` の音に `caption` を書くと、その音が鳴った時に字幕が出る。

```json
"door":  { "caption": { "textKey": "sfx.door",  "kind": "ambient",  "directional": true } },
"hello": { "caption": { "textKey": "sfx.hello", "kind": "dialogue", "speakerKey": "npc.guard" } }
```

| 項目 | 意味 |
|---|---|
| `textKey` | 字幕の文の鍵。`strings.json` に書き、翻訳に乗る (必須) |
| `kind` | `dialogue` / `ambient` / `music` / `alert`。省略は `ambient` |
| `directional` | `true` で、音の来る方向 ((左) (右) (前) (後ろ)) を頭に添える。位置のある音 (`hud.play(...).at(pos)`) だけ |
| `speakerKey` | 話者の名前の鍵 |

`defaults` には `caption` を書けない。全部の音に同じ字幕は付けられないため。

出る種類は利用者の設定 `accessibility.captions` で選ぶ。初期設定は `dialogue` と `alert` だけ。画面が字幕で埋まらないよう、同じ音が続けば 1 行にまとめて「×2」と回数を付け、画面に出るのは新しい方から 4 行までで、3 秒で消える。時間は音のステップ (60 Hz) を積んで数えるので、録画の再生でも `--headless-3d` の撮影でも同じ絵になる。

画面には `view.caption_*` が届くので、ゲームの RML に `<include src="mitiru:captions.rml"/>` と `mitiru:captions.rcss` を読ませる。字幕の大きさと下地の濃さは `accessibility.subtitleScale` と `subtitleBackground` に従う。話者の名前は `speakerNames` で切れる (会話の窓の名札も同じ設定に従う)。

字幕は鳴らす要求が来た時に出る。同時に鳴らせる数の上限 (`maxInstances` など) で捨てられた音にも出る。`hud.playAt` で時刻を決めた音は、鳴る時刻でなく要求した時に出る。

曲 (`hud.music`) と声 (`hud.voice`) も、`sounds.json` に同じ id で書けば字幕が付く。止める要求と BGM の一時停止などは字幕にならない。

例: `examples/captions`。

字幕の絵は、音を鳴らす起動でだけ出る。`--headless` と `--headless-3d` は音の処理を持たないので字幕も出ず、撮る時は `--audio-capture <wav>` を足す (デバイスを開かずに音を作る起動になる)。

```
python tools/grab_scene.py --dll captions.dll --gpu --input-script keys.txt --host-arg=--audio-capture --host-arg=a.wav --out evidence/captions.png
```

## 3. 色覚とコントラストの検査

どちらも撮影の道具 (`tools/grab_scene.py`) の引数で、画面に出る道具は増やしていない。UI まで描くので `--gpu` (DX12) が要る。

```
python tools/grab_scene.py --dll game.dll --gpu --cvd all --out evidence/menu.png
python tools/grab_scene.py --dll game.dll --gpu --contrast --out evidence/menu.png
```

`--cvd` (`protan` / `deutan` / `tritan` / `all`) は、色覚の見え方を画面全体に掛けた絵を `menu_deutan.png` のように並べて出す。Machado (2009) の行列による近似で、見分けられる保証ではない。

`--contrast` は、画面に出ている文字ごとに、計算後の文字色と下地の色からコントラスト比を測る (画素は読まない)。基準は XAG 102 で、標準の文字は 4.5:1、大きな文字 (PC は 1080p で 36 px、画面の高さに比例) は 3:1。

| 判定 | 意味 |
|---|---|
| `pass` | 基準に届く。下地が透ける文字は、下の絵が黒から白のどれでも届く時だけ |
| `fail` | 下地が不透明で、基準に届かない。終了コード 4 |
| `unknown` | 下地がゲームの絵に透ける、または画像・gradient・opacity・filter で色だけでは測れない。実際の絵の上で見て確かめる |

`unknown` を `pass` に数えない。一覧の末尾に件数を出す。色覚の型ごとの比 (`underColorVision`) は不透明な組だけに付く。

host の引数は `--cvd <型>` と `--ui-contrast <file>` (JSON に書く)。
