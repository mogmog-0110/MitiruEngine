# UI に SVG を出す

RmlUi の SVG plugin を、opt-in の feature `svg` として使える。UI の文書に `<svg src="icons/star.svg"/>` と書くと、アイコンが
描く大きさで描き直され、2 倍に拡大しても縁が滲まない。既定では OFF で、使わないゲームには何も入らない。

## 使う

1. `external/lunasvg` に [lunasvg](https://github.com/sammycage/lunasvg) 3.5.0 (MIT。plutovg を同梱) を置く。エンジンのリポジトリが持っているので、取り直しは要らない (出どころは `external/lunasvg/PROVENANCE.txt`、ADR 0121)。
2. `mitiru.toml` の `[engine]` に `features = ["svg"]` を書く (CMake の `MITIRU_WITH_SVG`、既定 OFF)。
3. UI の文書に書く。

```html
<svg src="icons/star.svg"/>
```

```css
svg { width: 48dp; height: 48dp; flex: none; }
.badge { decorator: svg(icons/star.svg); }   /* 背景に敷く書き方 */
```

`src` は文書からの相対パス。`<img src="x.svg">` では読めない (RmlUi の SVG plugin が受けるのは `<svg>` の要素と `svg()` の decorator、
文書に直接書いた `<svg>…</svg>`)。見本は `examples/html_svg`。

## 大きさの決まり

- 大きさは `dp` で書く。`px` は設定の UI の倍率 (`accessibility.uiScale`) に追従しない。`width` と `height` を書かなければ、SVG 自身の `width` / `height` が元の大きさになり、起動時の倍率が掛かる。表示中に倍率を変えると (設定画面で `uiScale` を動かすなど)、`dp` で大きさを書いた svg だけが追従し、大きさを書かない svg は読み込み直すまで元の大きさのまま (RmlUi の `SetDensityIndependentPixelRatio` は `dp` の値だけを描き直す)。
- 縦横を同じ比で与える。`flex` の行が溢れて縮むと、箱の縦横比が SVG と変わり、円が楕円に描かれる (箱の大きさへそのまま引き伸ばされる)。並べる `<svg>` には `flex: none` を付ける。

## 外れているとき

- `external/lunasvg` が無いのに `MITIRU_WITH_SVG=ON` にすると、configure が警告を出して OFF に戻す。
- features に `svg` を書かずに `<svg>` を使うと、RmlUi は未知の要素として扱い、何も出ない。

## 配るもの

- 一式 (`THIRD_PARTY_NOTICES.txt`) に lunasvg と plutovg の表記が入るのは、`MITIRU_HAS_SVG` の build だけ。
- lunasvg のソースは `tools/release/release_allowlist.yaml` の `external/**` で配る。使わない見本と CI の設定は除く。

## 確かめたこと (2026-11-02、ADR 0121)

| 何を | 結果 |
|---|---|
| 組む | `MITIRU_WITH_SVG=ON` (Debug、MSVC) で lunasvg、plutovg、RmlUi の SVG plugin が組めた。`mitiru_host` と `html_svg` まで通る |
| 絵 (`--headless-3d --backend dx12`、大学の PC の GTX 1660 SUPER) | `examples/html_svg` の 4 つのアイコン (home、star、heart、check) が、100% と 200% (`accessibility.uiScale` = 2.0) の両方で描かれた。200% は 32dp が 64px、96dp が 192px で、縁は滲まず、欠けも向きの反転も無い。画素で測った: check の円の直径は 80px と 160px、白いチェックは右端が左端より上、heart は上が 2 つの塊で下が 1 点 |
| 試験 | `tests/mitiru/TestRmlSvg.cpp` (`[rmlui][svg]`、3 手順)。`<svg src>` が `star.svg` の 24 x 24 を元の大きさに取り、読み込み前に倍率 2.0 にすると 48 x 48 になる。`dp` で書いた svg は表示中の倍率の変更に追従する。RCSS の大きさ指定はそのまま効く |

見つけた落とし穴: 最初の `html_svg` は `px` で大きさを書き、倍率 2.0 でも大きくならなかった。また 144dp を 4 つ並べると 1280 の幅を超え、`flex` の縮みで円が楕円になった (96dp と `flex: none` に直した)。

## まだ確かめていないこと

- 本物の窓 (スワップチェーン) と、この PC の GPU での絵。上は大学の PC の headless の描画。
- `svg()` の decorator (`.badge { decorator: svg(...) }`) の絵。書き方は RmlUi の仕様どおりだが、描いていない。
- SVG に `width` / `height` が無く `viewBox` だけのときの元の大きさ。
- Dawn (WebGPU) の経路で UI を描くときの `<svg>` の絵。
- Release 構成と、`mitiru build` が features の svg を通してゲームを組む流れ (mitiru-cli 側は、`mitiru_svg` の target の有無で見分ける ADR 0089 の見張りまで)。
- グラデーション、フィルター、フォントを含む大きな SVG の描画の速さと、`<svg>` の直書き (`src` なし) の絵。
