# ワールドに追従する UI (頭上の HP バーとダメージ数字)

3D の敵の頭上に付いてくる HP バーと、当たった所から昇るダメージの数字を UI (RML) で出す。画面のどこに出すかは C++ が
`pick3d::worldToScreen` で決めて座標を送り、RML はその座標に絶対配置で置くだけ。動く例は `examples/html_worldtags`。

3D の物の表面に貼る UI (ワールド空間 UI) ではない。UI は画面に重ねる 2D のままで、カメラの向きによる傾きや奥行きの重なりは出ない。

## C++ 側

`#include <mitiru/ui/WorldTags.hpp>`。カメラは `screen.camera3D(...)` に渡すのと同じ値を `pick3d::CameraView` に入れる
([PICKING_3D.md](PICKING_3D.md))。`update` の中 (Screen が無い所) で呼べる。

```cpp
// HP バー: 毎フレームのコンテナ。敵の位置はゲームの状態にあるので、毎フレーム並べ直す
ui::WorldTagFrame tags(kView);
for (const Enemy& e : enemies) { tags.add(e.id, {e.x, e.y + 1.8f, e.z}, e.hp / e.maxHp); }
tags.push(hud, "tags");                    // view.tags

// ダメージ数字: GameMemory のメンバー (int と float だけ)
ui::FloatingNumbers<16> numbers;
numbers.spawn({x, y, z}, damage, crit);    // 当たった瞬間。crit は UI の class を替える種類 (0 か 1 など)
numbers.update(dt);
numbers.push(hud, "damage", kView);        // view.damage
```

| 送る配列 | フィールド |
|---|---|
| `view.tags` | `id`、`x` `y` (画面の座標 px)、`hp` (0〜100)、`s` (近いほど大きい 0.5〜1.5) |
| `view.damage` | `x` `y` (画面の座標 px)、`v` (数)、`k` (種類)、`t` (出てからの経過 0〜1) |

- 並びは奥から手前 (`tags`)、出た順 (`damage`)。後ろの要素が上に重なる
- `add` はカメラの後ろの点を数えない。画面の外でも数える (端から入ってくる時に UI の要素が遅れて現れない)
- 同時に出す上限がある。`WorldTagFrame` は既定 24 で、超えたらカメラに近い物を残す。`FloatingNumbers<N>` は N で、
  満杯なら一番古い物から入れ替わる。上限を決めるのは、UI の要素と 1 回の `hud.set` (約 3.9KB) の大きさのため
- 数字は、同じ点に重なる物を出た順に 真上・右・左・… と最大 `spread` px (既定 16) まで横へずらす
- 数字が出てから消えるまでの秒は `numbers.life` (既定 0.9)

## RML 側

```html
<link type="text/rcss" href="mitiru:components.rcss"/>
<div class="m-wtags">
  <div data-for="t : tags">
    <div class="m-wtag" data-style-left="t.x + 'px'" data-style-top="t.y + 'px'" data-style-transform="'scale(' + t.s + ')'">
      <div class="m-wtag__fill" data-style-width="t.hp + '%'"></div>
    </div>
  </div>
  <div data-for="d : damage">
    <div class="m-dmg" data-class-crit="d.k == 1"
         data-style-left="d.x + 'px'" data-style-top="(d.y - d.t * 56) + 'px'" data-style-opacity="1 - d.t * d.t">{{ d.v }}</div>
  </div>
</div>
```

`data-for` を書く要素には、`t.x` のような行の値を使う式 (`data-style-*` など) を書かず、中の要素に書く。同じ要素に書くと、
配列が縮む更新で RmlUi が行を消す前にその行の式を評価することがあり (同じ要素の view は更新の順が決まらない)、
空の値で `left: px;` のような RCSS の警告が、確率的に出る。

- `m-wtags` は画面いっぱいのコンテナで、クリックを受けない。座標 (px) は画面の左上が原点で、`in.mouseX()` と同じ座標系
- `m-wtag` の枠の下端の中央が、送った座標に来る (拡大しても `transform-origin` が下端中央なので足元は動かない)
- 数字は `t` から上へ昇る量 (上の例は 56px) と透明度を RML の式で作る。CSS の `animation` は使わない。`data-for` の要素は
  配列の添字に付くので、数字が消えて並びが詰まると、時間で動くアニメーションは別の数字のものに入れ替わってしまう。状態から作れば、
  replay でも巻き戻しでも同じ絵になる
- 長さは位置 (`left` `top`) だけ `px`、大きさや文字は `dp`。倍率を上げても、座標は 3D の点に付いたまま

## 確かめ方

座標は目で決めず、既知の点で測る。

- `ctest -R "WorldTag|FloatingNumbers|m-wtag|m-dmg"` が、`worldToScreen` の値と送る座標の一致、注視点が画面の中央 (320, 180) に来ること、
  上限・並び・消える時刻、UI の要素がその座標に ±1px で置かれること (枠の下端の中央)、数が増減しても警告が出ないことを見る
- `examples/html_worldtags` を `--headless-3d` で撮り (`--capture-every 10`)、バーが満タンの 2 枚 (更新 1 回と 11 回の後) で、立方体 4 つの
  天面 (レンダラーが描く) とバーの塗りの位置を、このカメラの式から別に出した射影と比べた (`tools/measure_world_tags.py`)。
  バーの中央は射影から x で ±1.3px、下端は 0.6〜2.3px 上 (枠の太さと拡大の分)。天面の重心も x で ±1.7px 合うので、式がレンダラーと合い、
  UI がその座標に置かれている
- `ctest -R e2e_ui_html_worldtags` が、Space の入力スクリプトで当てた回数・タグ数・残っている数字の数を照合し、RmlUi の警告 0 件を見る

実機での見え方 (数字の読みやすさ、バーの大きさ) は PNG と手元の実行で確かめる。
