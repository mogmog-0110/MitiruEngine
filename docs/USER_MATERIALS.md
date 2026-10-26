# 自作の材質

表面の色・法線・発光・透明度だけを自分で書き、照明・影・輪郭線はエンジンに任せる。
書くのは `assets/materials/<name>.slang` の `material()` 1 つと、パラメータを宣言した `<name>.mat.json`。
法線 RT に詰める描画マーカー (輪郭線の下地) はエンジンが書くので、材質を使っても輪郭線は崩れない。
例は [`examples/material3d`](../examples/material3d/material3d_dll.cpp) (溶ける、ホログラム、水面)。設計は [ADR 0100](adr/0100-user-materials.md)。

## ゲームから使う

```cpp
const int melt = s.registerMaterial3D("melt");           // 同じ名前は同じ番号。作れなければ -1
const float params[3] = {amount, 0.08f, 2.0f};            // .mat.json の params の並び
s.material3D(melt, params, 3, gameSeconds);              // 以後の drawMesh / drawModel に効く
s.drawMesh("sphere", pos, scale, rot, color);
s.material3D(0);                                         // 場面の陰影へ戻る (フレームの頭でも戻る)
```

- 登録の時にコンパイルとパイプラインの作成を済ませる。毎フレーム呼んでも、2 回目からは番号を引くだけ
- 秒はゲームの状態 (GameMemory) の時計を渡す。壁時計を渡すと、リプレイと巻き戻しで絵が変わる
- `[render] needs` を宣言するゲームは `user_material` を足す。足さないと登録は -1 で、1 度だけ知らせる (宣言しない旧体制は、そのまま使える)

## .slang

`SurfaceIn` を読み、`SurfaceOut` を書き換える。`SurfaceOut` にはエンジンが先に「材質の色 x 頂点色 x 画像」などを入れてあるので、変えたい項目だけを書けばよい。

```hlsl
void material(SurfaceIn i, inout SurfaceOut o)
{
    float edge = noise(i.worldPos * 3.0) - param_amount;   // param_<名前> が .mat.json の params
    if (edge < 0.0) { o.discardPixel = true; }
    o.emissive = float3(1.0, 0.45, 0.1) * (1.0 - smoothstep(0.0, param_width, edge));
}
```

| `SurfaceIn` | 意味 |
|---|---|
| `uv` | 頂点の UV |
| `worldPos` | 世界座標 |
| `normal` | 面の法線 (世界、正規化済み、法線マップの前) |
| `view` | 面からカメラへの向き |
| `vertexColor` | 頂点色 (線形) |
| `time` | `material3D` に渡した秒 |
| `pixel` | 画面の画素の位置 (左上が 0, 0) |

| `SurfaceOut` | 意味 |
|---|---|
| `baseColor` | 線形の基本色 |
| `alpha` | 透明度 |
| `normal` | 陰影に使う法線 (世界)。輪郭線の下地には出ない |
| `emissive` | 照明の後に足す色 |
| `roughness`、`metallic`、`ao` | PBR の入力。トゥーンと Phong は局所光のつやにだけ使う |
| `discardPixel` | true でこの画素を描かない |
| `clearCoat`、`clearCoatRoughness`、`anisotropy`、`anisotropyRotation`、`sheenColor`、`sheenRoughness` | PBR の層 (下の「PBR の層」)。トゥーン・Phong・照明なしは読まない |

書く言語は HLSL の部分集合で、DX12 の HLSL と Slang の両方に通る書き方に限る (`frac`、`lerp`、`saturate`、`smoothstep` など)。ファイルを読み込む `#include` は使えない。

## PBR の層

PBR の陰影はクリアコート、異方性、シーン (布の縁の光) を持つ。glTF の `KHR_materials_clearcoat` / `KHR_materials_anisotropy` / `KHR_materials_sheen` の係数は、読み込むと `SurfaceOut` の同じ名前の項目に入った状態で `material()` に届くので、材質から足したり変えたりできる。画像 (強さ・粗さ・向きの画像) は読まない。

| 項目 | 範囲 | 意味 |
|---|---|---|
| `clearCoat` | 0〜1 | 上に重ねる透明な膜の強さ (車の塗装)。0 は無し |
| `clearCoatRoughness` | 0〜1 | 膜の粗さ。小さいほど細く鋭い光 |
| `anisotropy` | 0〜1 | 光の伸び。0 は等方。ブラシをかけた金属 |
| `anisotropyRotation` | ラジアン | 伸びる向きを、UV の u の向きから面の法線まわりに回す角 |
| `sheenColor`、`sheenRoughness` | 0〜1 | 布の縁を白っぽく光らせる色と粗さ。黒は無し |

層をすべて 0 にした面は、層の無い PBR と画素まで同じに描く。シーンが下の層を弱める量は、固定の近似を使う。

## .mat.json

```json
{
  "lighting": "scene",
  "params": [
    { "name": "amount", "default": 0.3, "min": 0.0, "max": 1.0 },
    { "name": "width",  "default": 0.08, "min": 0.01, "max": 0.3 }
  ]
}
```

- `lighting`: `scene` (既定) は場面の陰影 (トゥーン、Phong、PBR) で照らす。`unlit` は照明を掛けず `baseColor + emissive` をそのまま出す
- `params`: 最大 16 個。`name` は英数字と `_`。ゲームが渡した値は `min`〜`max` に収め、渡さなかった分と NaN は `default` になる
- `.mat.json` が無ければ、パラメータなしの `scene` の材質になる

## DX12 と Dawn

| | DX12 | Dawn |
|---|---|---|
| 読むもの | `.slang` をそのまま | `tools/bake_materials.py` が焼いた `.wgsl` |
| 実行時に Slang | 要らない | 要らない |
| 陰影 | トゥーン、Phong、PBR、照明なし | トゥーンと照明なし |

Dawn には実行時のコンパイラを持ち込まない (出荷が数十 MB 増える)。`.slang` を書いたら、Dawn で使うときだけ次を流す (slangc が要る。場所は引数 `--slangc` か環境変数 `SLANGC`)。mitiru-cli の `mitiru shader build` から呼ぶ形は、CLI 側の課題として残してある。

```
python tools/bake_materials.py assets/materials
```

`.wgsl` の先頭の行に、焼いたときの `.slang` と `.mat.json` の印がある。焼いた後に `.slang` を書き換えると、Dawn は登録の時に 1 度だけ知らせる。

## ホットリロード

`mitiru_host --watch-assets` が `.slang`、`.mat.json`、`.wgsl` の変更を見て、その材質だけを作り直す。番号は変わらない。書き間違えたときは前のバージョンのまま描き続け、理由を `-v` で出す。

## 効かないもの

- 半透明の描画 (OIT)。材質を頼んでいても場面の陰影で描く。`drawMeshInstanced` は材質が効くが、インスタンスを 1 つずつ描くので、バッチ描画の費用の利点は無い
- 影の深度のパス。`discardPixel` で抜いた形は影に反映されない
- 頂点を動かすこと。材質は画素の入力だけを書く

## 失敗したとき

`registerMaterial3D` が -1 を返し、場面の陰影で描き続ける。理由 (HLSL のエラーは `.slang` の行番号つき) は `-v` で出る。
