# ノイズ (simplex、cellular、value、domain warp)

`mitiru/math/Noise.hpp` は [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) (MIT、`external/FastNoiseLite`) の薄い包み。
従来の `math/PerlinNoise.hpp` (Perlin だけ) はそのまま残る。

```cpp
#include <mitiru/math/Noise.hpp>
using namespace mitiru::noise;

Noise n(Settings{.kind = Kind::OpenSimplex2, .seed = 42, .frequency = 0.05f,
                 .fractal = Fractal::FBm, .octaves = 4});
float v = n.at(x, y);          // [-1, 1]
float h = n.at01(x, y, z);     // [0, 1]

Warp w(WarpSettings{.seed = 3, .frequency = 0.02f, .amplitude = 30.0f});
w.apply(x, y);                 // 座標をずらしてから n.at(x, y) へ渡すと流れる模様になる
```

| 設定 | 値 |
|---|---|
| `kind` | `OpenSimplex2` `OpenSimplex2S` `Cellular` `Perlin` `ValueCubic` `Value` |
| `fractal` | `None` `FBm` `Ridged` `PingPong` (`octaves` `lacunarity` `gain` `weightedStrength` `pingPongStrength`) |
| cellular | `cellularDistance` (`Euclidean` `EuclideanSq` `Manhattan` `Hybrid`)、`cellularReturn` (`CellValue` `Distance` `Distance2` …)、`cellularJitter` |
| warp | `kind` (`OpenSimplex2` `OpenSimplex2Reduced` `BasicGrid`)、`amplitude`、`fractal` (`None` `Progressive` `Independent`) |

## 決定論

- 純関数。同じ設定と同じ座標なら、同じビルドの中では bit 単位で同じ値を返す (`tests/mitiru/TestNoise.cpp` が 2 回流して memcmp)
- float の計算なので、別のコンパイラ・別の最適化・別の CPU との bit 一致は保証しない (`DETERMINISM.md` の「浮動小数点モード」と同じ立場)。
  この PC の MSVC では `/Od` と `/O2` が同じ bit だった (表の値を `TestNoise.cpp` に置いてある)
- シミュレーションの状態 (地形の高さ、敵の配置) に使うなら、replay と同じ「同じビルドで再生する」範囲で再現する。
  機種を跨いで同じ世界を作りたいときは、生成した結果 (高さの表) を保存して配る
- 絵のゆらぎ (雲、粒) に使う分には、決定論の範囲に入らない

地形・雲・粒に使う例は、まだ無い。
