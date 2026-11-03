# 材質が背景を読む

自作の材質 ([USER_MATERIALS.md](USER_MATERIALS.md)) の `material()` から、その物の後ろに見える場面の色を読む。ガラスの屈折、ゼリーの透けに使う。設計は ADR 0123。

## 使い方

`.mat.json` に `"background": true` を書き、`.slang` で `mitiruBackground(pixel)` を呼ぶ。

```hlsl
// shift — 背景を 5 画素右へずらして読む
void material(SurfaceIn i, inout SurfaceOut o)
{
    o.baseColor = mitiruBackground(i.pixel + float2(5.0, 0.0));
}
```

| 名前 | 内容 |
|---|---|
| `float3 mitiruBackground(float2 pixel)` | 背景の線形 HDR の色。`pixel` は `SurfaceIn.pixel` と同じ空間 (左上が 0, 0、右が +x、下が +y、内部解像度)。端の外は端の色。画素の間は線形に補間する |

`background` を立てない材質が `mitiruBackground` を呼ぶと、コンパイルが止まって登録は -1 になる (`-v` に理由が出る)。Dawn は `python tools/bake_materials.py <materials のフォルダ>` で焼き直す。

例は [`examples/material3d`](../examples/material3d/assets/materials/glass.slang) の `glass`。面の手前で 1 回屈折させ、向きの変わりぶん (単位ベクトルの差) を `reach` 倍した画素だけずらして背景を読む。画面の右と下の向きは、視線 `i.view` の 1 画素ぶんの変化 (`ddx`、`ddy`) から取るので、球の面の分割に左右されない。

## 何が写るか

- 不透明の物、clod、空 (大気を含む)、水面。フレームに 1 回、半透明の前に写す
- 半透明の物、粒の半透明、Effekseer は写らない
- 背景を読む材質の物は、読まない物より後に描く。**自分も、重なった別の読む物も写らない** (後ろの読む物は、前の読む物を通して見えない)
- Dawn の背景には、空気遠近と体積フォグが入らない (どちらも resolve の後に掛ける)。読む物の色には、読む物自身の位置のフォグが掛かる

## 気をつけること

- 物の色の alpha が 1 未満、または半透明の指定の物は、半透明の経路 (OIT) へ回り、自作の材質は効かない (背景も読めない)。`material()` の中で `alpha` を下げるのは、不透明のアルファ混合で描かれる。透けは `mitiruBackground` の色を混ぜて作る
- 読む物が多くても写しは 1 枚 (読む材質を描かないフレームは 0 枚)
- DX12 は、読む物を呼んだ時に定数を積み、後から描く。材質の値、陰影、光のレイヤーは呼んだ時のまま。主光源など光の全体の値は描く時の値
- 副ビューにも効く (主ビューと同じ経路。絵では確かめていない)
