# 設定画面

エンジン同梱の設定画面 (`mitiru:settings.rml`) を、ゲームの RML に 1 行で足す。音量・画質・垂直同期・ボーダーレス・見やすさ・
キー割り当てが、パッドだけでも動く。変えた値は host がすぐ反映して `settings.json` へ保存する。動く例は `examples/html_settings`。

## 足し方

```html
<link type="text/rcss" href="mitiru:settings.rcss"/>   <!-- head に (base / tokens / components の後) -->
<include src="mitiru:settings.rml"/>                    <!-- body (data-model="view") の中に -->
```

何も出ない状態から始まる。ゲームが `view.settings_open` を `hud.set` で送った間だけ、画面が開く。開く操作と閉じる操作はゲームが決める。

```cpp
if (in.action("menu.settings") || in.padPressed(Pad::Start)) { settingsOpen = true; }
if (in.action("settings.close") || in.cancelPressed()) { settingsOpen = false; }
hud.set("view.settings_open", settingsOpen);
```

閉じるボタンは `settings.close` をゲームへ返すだけ。B (Esc) の扱いは下の「戻る」。

## 画面の中身

| 面 | 部品 | 変える設定 (`settings.set` の key) |
|---|---|---|
| 音 | スライダー x3 | `audio.master` `audio.music` `audio.sfx` |
| 画面 | 1 つ選ぶ (画質)、切り替え x2 (垂直同期、ボーダーレス) | `graphics.quality` `graphics.vsync` `graphics.windowMode` |
| 見やすさ | スライダー (画面の文字の大きさ、画面の揺れ)、1 つ選ぶ (色覚の補正) | `accessibility.uiScale` `accessibility.screenShake` `accessibility.colorFilter.mode` |
| 操作 | 操作ごとの行。割り当ての枠、`+` (枠を足す)、戻す。上に「すべてのキーを戻す」 | `settings.rebind` `settings.reset_binding` |

画面の下に「初期設定に戻す」(確認ダイアログが開き、OK で `settings.reset`) と「閉じる」がある。
適用と取り消しの 2 段階は無い。触った値は host がその場で反映し、保存する (ADR 0124)。

## host に要るもの

host が値を UI に送るのは、設定の置き場 (`ship.json` の `gameName`、または `--settings <file>`) があるか、操作の表
(`MITIRU_ACTIONS` か `input_actions.json`) がある時だけ。どちらも無い開発中の起動では host が何も送らないので、この画面は
動かない。開発中は `mitiru_host --settings <file>` を付けるか、操作の表を置く。

「操作」の面の行は、操作の表の `kind` が `button` の物だけが並ぶ (軸の操作はボタンを割り当てられない)。名前は表の `label`。
同じ context の操作どうしで割り当てが重なると、その行が `.m-bind--conflict` で赤くなる (SAVE_AND_SETTINGS.md)。

host が UI へ送る値の名前は SAVE_AND_SETTINGS.md の「設定画面から変える」にある。この画面が読むのは
`settings_volume_*` `settings_quality` `settings_vsync` `settings_borderless` `settings_ui_scale` `settings_screen_shake`
`settings_color_filter` と `settings_actions` (操作ごとの行の配列)。

## パッドで操作する

| ボタン | 画面での働き |
|---|---|
| 十字キー・左スティック | 部品の間を動く。タブの見出しから下へ行くと、その面の最初の部品に入る |
| A | 押す。選んだ一覧を開く。割り当ての枠ならキャプチャを始める |
| 左右 (スライダーの上) | 1 step 動かす |
| LB / RB | 面を左右に回す (端を越えると回り込む) |
| B | 開いている一覧を閉じる (開く前の値へ戻す)。確認ダイアログがあれば閉じる。どちらも無ければ、ゲームが閉じる |

画面が開いている間、フォーカスはこの画面の中から出ない (`focus-trap`)。確認ダイアログが開けば、その中だけになる。
このとき A・RB など、ゲームの操作に割り当てたボタンも UI の操作として効く (host が UI の使うボタンを足す)。

### 戻る (B / Esc)

UI が B (Esc) で一覧かダイアログを閉じたフレームは、host が同じ B (Esc) をゲームへ渡さない。ゲームが `in.cancelPressed()` で
設定画面を閉じる作りでも、ダイアログを閉じただけで画面ごと閉じることは無い。もう一度押すと、ゲームが閉じる。

### キーのキャプチャ

枠を選んで A を押すと、次に押した入力 (キー・マウスのボタン・パッドのボタン・ホイール) がその枠の割り当てになる。キャプチャ中は
枠に「押してください…」と出て、ゲームには何も渡らない。押した入力は、離すまで UI にも渡らない (十字キーを割り当てても、
フォーカスは動かない)。キーボードの Esc は取り消しになり、設定画面は閉じない。パッドのボタンはどれも割り当てになるので、
パッドだけではキャプチャをやめられない (B も割り当てられる)。やめたい時はキーボードの Esc を押す。

## 見た目を変える

並べ方は `mitiru:settings.rcss`、部品は `mitiru:components.rcss`、色・角丸・文字の大きさは `mitiru:tokens.rcss` の変数。
ゲームの RCSS の `body` で変数を書き直すと変わる (`examples/html_settings/assets/ui/main.rml`)。幅は
`.m-settings { --m-dialog-max-w: 720dp; }`。文言は鍵 `mitiru.settings.*` で、ゲームの `strings.json` に同じ鍵を書くと置き換わる。

確認ダイアログは `ui_confirm_*` を使う。ゲームの RML に同じ名前の確認ダイアログがあると 2 枚出るので、この画面を入れるなら 1 つにする。

## 確かめ方

実際のパッドの経路 (`--input-script` の `pad` 行) で動かす。

```
mitiru_host html_settings.dll --headless --headless-3d --backend dx12 --input-script tests/e2e/input_scripts/html_settings_flow.txt --settings s.json
```

`ctest -R e2e_ui_html_settings` が、音量・一覧・切り替え・タブ・キーのキャプチャと取り消し・重なり・確認ダイアログ・初期設定へ戻すを台本で流し、
`settings.json` の値とゲームへ届いた操作を照合する。RmlUi の警告が 1 件でも出れば落ちる。実機のパッドの手触りは測れない。
