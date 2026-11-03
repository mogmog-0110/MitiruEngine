# ツールチップ

RML の要素に `title="説明"` と書くと、その要素のそばに説明が出る。文字のないボタン (色見本、アイコン) の名前や、
押すと何が起きるかの補足に使う。動く例は `examples/html_tooltip`。

```html
<link type="text/rcss" href="mitiru:components.rcss"/>   <!-- 見た目 (m-tooltip) はここにある -->
<button class="sw" title="あお" data-event-click="dispatch('pick.color', 'id', 1)"></button>
```

足すものは `title` だけ。説明の箱は、出す時に host が文書の末尾へ `<div class="m-tooltip">` を 1 つ足す。
`title` のない文書には何も足さないので、使わない画面には何も出ず、何も増えない。

## いつ出るか

出すきっかけは、最後に使った入力で決まる。

| 入力 | 出る | 引っ込む |
|---|---|---|
| マウス | 要素の上で 0.5 秒止めたとき | 要素から出る・クリック・ホイール・何かを打つ・B (Esc) |
| キー・パッド (十字キー、Tab、Enter、スティック) | フォーカスが来て 0.3 秒たったとき | フォーカスが別の要素へ動く・B (Esc)・何かを打つ |

引っ込めた説明は、その要素を離れる (フォーカスなら別へ動く) まで出し直さない。マウスが動いたらホバー、十字キーを押したら
フォーカスで出す側に替わる。ゲームの操作に使うキー (WASD など) では切り替わらない。

十字キーで次々に動かしている間は、止まるまで出ない (0.3 秒の待ち)。

## 書き方

| やりたいこと | 書き方 |
|---|---|
| 固定の説明 | `title="あお"` |
| C++ の値で変わる説明 | `data-attr-title="stock > 0 ? 'のこり ' + stock + ' こ' : 'もうない'"`。出ている間に変わっても、その場で書き換わる |
| 文言の訳 | `title="[menu.start]"`。`strings.json` のキーになり、言語を替えると訳し直す ([UI_RMLUI.md](UI_RMLUI.md) の「文言を訳す」) |
| 中の要素にも効かせる | 親に `title` を書く。子の上にいても親の説明が出る。子にも `title` があれば、近い方 |

説明は文字だけ (RML の記号は文字として出る)。2 行以上が要るほど長いなら、ツールチップでなくダイアログの仕事。

## 置かれ方

要素の下の中央に、8dp 空けて置く。下に入らなければ要素の上へ、横は画面の端から 8dp 内側に収める。幅は `--m-tooltip-max-w`
(既定 280dp) までで、それを超える文は折り返す。位置と大きさは `dp` なので、UI の倍率 (`accessibility.uiScale`) に従う。

出る箱はクリックを受けず (`pointer-events: none`)、読み上げにも出さない (`aria-hidden`)。読み上げには、要素の `title`
が名前として届く ([UiAccessTree](../include/mitiru/ui_rml/UiAccessTree.hpp))。

## 見た目を変える

箱は `components.rcss` の `.m-tooltip`。ゲームの RCSS で変数を書き直す。

```css
body { --mitiru-radius-sm: 6dp; }
.m-tooltip { --m-tooltip-bg: #1d1d1f; --m-tooltip-fg: #ffffff; --m-tooltip-max-w: 320dp; }
```

`components.rcss` を読まない文書は、`.m-tooltip` を自分で書く。箱は通常は見えず、`is-visible` が付いている間だけ見える作りにする
(host は class の付け外しだけで出し入れする)。

## 確かめ方

`ctest -R "RmlUiHost: .*tooltip"` が、待ち時間・位置 (要素の下の中央、端での折り返し)・クリックでの引っ込め・
パッドのフォーカス・変わる説明・訳を、描いた画素の外接枠で測る。入力スクリプトの流し方は次のとおり。

```
mitiru_host html_tooltip.dll --headless --headless-3d --backend dx12 --input-script tests/e2e/input_scripts/html_tooltip_pad.txt --capture-dir out --capture-every 10
```

`ctest -R e2e_ui_html_tooltip` は入力スクリプトのクリックとパッドの操作がゲームへ届くこと、RmlUi の警告が 0 件であることを見る。
説明が絵に出ているか (位置・見た目) は PNG で確かめる。実機のマウスの止め具合と、パッドを使った感触は測れない。
