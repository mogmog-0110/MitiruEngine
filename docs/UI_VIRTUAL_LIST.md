# 仮想リスト (大量の行の一覧)

10 万行の一覧を、見えている 8 行だけ UI に送る。RmlUi の `data-for` は渡された配列の数だけ要素を作るので、全部を送ると
全部が要素になる。`ui::VirtualList` は「どこから何行見せるか」を C++ が持ち、その範囲の行だけを `hud.set` で送り直す。
動く例は `examples/html_vlist` (10 万行、見えている行は常に 8)。

## C++ 側

`#include <mitiru/ui/VirtualList.hpp>`。状態は int が 5 つだけで、GameMemory にそのまま置ける (巻き戻すと一覧の位置も戻る)。

```cpp
ui::VirtualList list;   // GameMemory のメンバー。init で list.rows = 8; list.setCount(総数);

void update(Input in, Hud hud, float)
{
	const int clicked = list.handle(in, "inv");      // 操作を受ける。クリックされた行の番号、無ければ -1
	list.push(hud, "inv", [&](int i, ui::RowWriter& row) {
		row.set("name", items[i].name).set("qty", items[i].qty);   // 見えている行の分だけ呼ばれる
	});
}
```

| 呼び出し | 働き |
|---|---|
| `setCount(n)` | 総数。減ったら先頭と選びを範囲に収める |
| `handle(in, "名前")` | UI の `名前.wheel` と `名前.pick`、十字キー (パッドとキー)、PageUp / PageDown / LB / RB、Home / End を受ける |
| `push(hud, "名前", makeRow)` | `view.名前_rows` に見えている行の配列を、`view.名前_bar_on` `_bar_top` `_bar_h` につまみの値を送る |
| `moveCursor(d)` / `select(i)` | 選んでいる行を動かす。見えている範囲の外へ出たら、先頭が追いかける |
| `scrollBy(d)` | 先頭だけを動かす (ホイール)。選んでいる行は動かない |
| `list.rows` | 一度に見せる行数 (既定 8)。UI の枠の高さと合わせる |

行は `{"i":番号,"sel":選んでいるか,…}` で、`makeRow(番号, 書き手)` が足りないフィールドを足す。文字列は `"` と `\` を逃がす。
十字キーの上下は、押し続けると 20 フレームの後から 4 フレームごとに繰り返す。ホイールは 1 目盛りで 3 行。

1 回の `hud.set` に入る文字列は約 3.9KB (statePush の上限) で、行のフィールドが多いと見える行数より前に頭打ちになる。
入りきらなかった行は送らず、`push` が `false` を返す (送った分は有効な JSON のまま)。

## RML 側

```html
<link type="text/rcss" href="mitiru:components.rcss"/>
<div class="m-vlist" data-event-mousescroll="dispatch('inv.wheel', ev.wheel_delta_y)">
  <div class="m-vlist__row" data-for="r : inv_rows" data-class-sel="r.sel"
       data-event-click="dispatch('inv.pick', 'i', r.i)">{{ r.i }} {{ r.name }}</div>
  <div class="m-vlist__bar" data-if="inv_bar_on">
    <div class="m-vlist__thumb" data-style-top="inv_bar_top + '%'" data-style-height="inv_bar_h + '%'"></div>
  </div>
</div>
```

`m-vlist` が枠、`m-vlist__row` が行 (高さは `--m-vlist-row-h`、既定 36dp)、`m-vlist__bar` がスクロールの位置の表示。
つまみは C++ が送った位置を映すだけで、掴んで動かすことはできない (動かすのはホイールと十字キーとクリック)。
枠の高さは行の数で決まる。総数が `rows` より少なければ、枠はその分だけ縮み、つまみは出ない。

行に `data-style-*` を書くなら、行の `data-for` は外側の要素に書き、`data-style-*` は中の要素に書く ([UI_WORLD_TAGS.md](UI_WORLD_TAGS.md) の「RML 側」)。

UI の要素は、見えている行 + `data-for` を書いた要素 (アンカー) 1 つ (RmlUi が `display: none` で残す) + つまみだけで、総数に依らない。

## 持ち主

| もの | 持ち主 |
|---|---|
| 総数・先頭・選んでいる行 | C++ (`VirtualList`)。UI は持たない |
| ホイールの量・クリックされた行 | UI から `dispatch` で C++ へ。行の中身も C++ が作る |
| つまみの位置 | C++ が計算して送る。UI の `overflow` のスクロールは使わない |

行をフォーカスして選ぶ作り (`nav: auto`) にしないのは、行の要素が先頭の移動のたびに別の物の表示へ替わり、フォーカスが
別の行に乗ってしまうため。選びは C++ が持ち、パッドは十字キーをゲームが受ける。

## 確かめ方

`ctest -R "VirtualList"` が、行を作る回数が総数 (20・1000・10 万) に依らず見える行数だけであること、スクロール・選び・端の
挙動、文字列の上限、UI の要素の数 (10 万行でも `rows + 2`) と、スクロール後に出る行の中身を測る。
`ctest -R e2e_ui_html_vlist` は、ホイール・クリック・十字キー・End・Home・PageDown の入力スクリプトを `--headless-3d` で流し、
最終の先頭 (`view.inv_first`)・選び・見えている行数 (`view.inv_shown`)・クリックした行を照合し、RmlUi の警告 0 件を見る。
実機のホイールの感触と、パッドの押し続けの繰り返しの速さは測れない。
