# 地形・草・水面 (world.json)

屋外の 1 枚 (高さマップの地形、材質の層、草、撒いた岩や木、水面) は `*.world.json` 1 つに書く。
host は `drawOutdoor` に渡されたこのファイルを描き、ゲーム DLL は同じファイルを `mitiru/terrain/` で読んで当たり判定を作る。
見た目と判定が同じファイルから来るので、キャラが歩く面と描かれた面はずれない。設計は [ADR 0060](adr/0060-outdoor-terrain-grass-water.md)、
見本は `examples/outdoor3d`。

## 書き方

パスは world.json の置き場所からの相対。省いた欄は既定値になる。

```json
{
  "terrain": {"heightmap": "island_height.png", "size": [128, 128], "height": 24, "center": [0, 0, 0],
              "lod": {"patchCells": 32, "lodCount": 4}},
  "layers": [
    {"albedo": "grass.png", "normal": "grass_n.png", "tile": 6, "roughness": 0.95},
    {"albedo": "sand.png", "tile": 4, "rule": {"height": [-20, 4.2], "slope": [-90, 90], "blend": 0.5}},
    {"albedo": "rock.png", "tile": 5, "rule": {"height": [-20, 100], "slope": [30, 120], "blend": 4}}
  ],
  "grass": {"density": "island_grass.png", "bladesPerM2": 36, "patchSize": 6, "fadeStart": 28, "fadeEnd": 42,
            "height": 0.45, "width": 0.045, "wind": {"dir": [1, 0.35], "strength": 0.16, "speed": 1.4}},
  "scatter": [{"model": "pine.glb", "density": "island_trees.png", "perM2": 0.05, "seed": 11, "scale": [0.8, 1.4],
               "maxSlope": 30, "height": [5, 18], "collideRadius": 0.18, "collideHeight": 3}],
  "water": [{"height": 3, "min": [-1500, -1500], "max": [1500, 1500], "foamDepth": 0.4, "toonBands": 0}],
  "level": "level.glb"
}
```

- **terrain**: `heightmap` は 16 bit のグレースケール PNG (Blender、World Machine、Gaea の書き出し) か RAW (`.r16` / `.raw`、
  リトルエンディアン、正方形でなければ `rawWidth`)。画像の上端が z の小さい側 (Blender の +Y、北)。`size` は x と z の全長 (m)、
  `height` は標本 65535 の高さ (m)。置き場所は `center` (x と z の中心、y は標本 0 の高さ) か `origin` (最小の角)。
- **layers**: 8 枚まで。`terrain.splat` に RGBA の splat マップ (1 枚で 4 層) を 2 枚まで書けば、その重みで混ぜる。
  無ければ各層の `rule` (高さと傾きの範囲、縁のぼかし `blend`) から重みを作り、規則の無い層 0 が残りを受け持つ。
  画像は辺が 4 の倍数なら BC7 / BC5 に圧縮して隣に DDS を置く (材質のマップと同じ。[TEXTURE_COMPRESSION.md](TEXTURE_COMPRESSION.md))。
- **grass**: 描画だけ。`density` (8 bit) の 0 の所には生えない。`patchSize` m 角の区画ごとに、`lodStart` から `lodStep` m ごとに
  本数を半分にし、`fadeStart` から `fadeEnd` で縮めて消す。風は drawOutdoor に渡す時刻 (と風の指定) だけで決まる。
- **scatter**: 揺らした格子に 1 つずつ候補を置き、密度・傾き・高さで残す。`collideRadius` を付けると当たり判定に縦の箱が入る。
- **water**: 高さ一定の長方形。水深で色と吸収、`foamDepth` より浅い所 (岸と水面を貫く物の周り) に泡、空の映り込み。
  `toonBands` (2..4) で段に切る。海は長方形を水平線まで広げ、カメラの遠い面を伸ばす。

## ゲームから使う

```cpp
#include <mitiru/terrain/OutdoorCollision.hpp>
#include <mitiru/terrain/OutdoorWorldLoad.hpp>

namespace ter = mitiru::terrain;
// GameMemory の外。ファイルの中身だけで決まるので、読み直せば同じものができる
const std::unique_ptr<ter::OutdoorWorld> kWorld = ter::loadOutdoorWorld("game/assets/island.world.json").world;
const mitiru::action::CollisionLevel kLevel = [] {
	mitiru::action::CollisionLevelBuilder b;
	if (kWorld) { ter::addOutdoorWorld(b, *kWorld, 0, 1); }   // 地形は layer 0、撒いた物は layer 1
	return b.build();
}();

void update(Input in, float dt)
{
	const mitiru::action::CollisionWorld world(&kLevel);   // stepCharacter や raycast はこれに問い合わせる
	const float ground = kWorld->terrain.heightAt(x, z);   // 当たり判定の三角形と同じ値
	const float depth = ter::waterDepthAt(kWorld->water[0], kWorld->terrain, x, z);
	const int layer = kWorld->dominantLayerAt(x, z);       // 足音の種類など
}

void draw(Screen& s) const
{
	mitiru::render::OutdoorDrawPod pod;
	pod.timeSec = t;                       // ゲームの状態の時刻 (風と波)
	pod.benderCount = 1;                   // キャラの足元の草を倒す (中心 xyz と半径)
	pod.benders[0][0] = x; pod.benders[0][1] = y; pod.benders[0][2] = z; pod.benders[0][3] = 0.9f;
	s.drawOutdoor("game/assets/island.world.json", pod);
}
```

置き場所はファイルで決まり、当たり判定と同じ座標になる。描けるのは DX12 だけ (ほかは何も描かない)。`drawModel` に world.json を
渡しても描かない (1 度だけ警告する)。

`OutdoorDrawPod` (ABI v49、[ADR 0062](adr/0062-abi-v49-render-boundary.md)) は描画だけの毎フレームの指定で、既定の値なら world.json に
書いたとおりに描く。

| 欄 | 意味 |
|---|---|
| `timeSec` | 草のなびきと波の時刻 |
| `windDir` / `windStrength` | 風の向き (x, z) と草の先端のずれ (m)。向きが両方 0、強さが負なら world.json の値 |
| `waterOffset` | 水面の高さに足す m (潮)。当たり判定と `waterDepthAt` には効かないので、ゲームは同じ値を自分で足す |
| `waveScale` | 波の強さに掛ける倍率 |
| `flags` | `kOutdoorNoGrass` / `kOutdoorNoScatter` / `kOutdoorNoWater` で草・撒いた物・水面を描かない |
| `benders[8]` / `benderCount` | 草を押し倒す球 (中心 xyz と半径)。中心から外へ倒し、背を低くする |

## Blender から置く

アドオン (`tools/blender/mitiru_level_export.py`) の `terrain` / `water` / `scatter` の印を Empty に付け、world.json の `"level"` に
書き出した glb を書く。`terrain` は位置が地形の中心と高さ 0 の y になる (`height` のプロパティで高さの幅を上書き)。`water` と
`scatter` は立方体表示の Empty で、箱の x と z の広がりが水面と撒く範囲になる。水面の y は Empty の位置。プロパティは
`shallow_color` `deep_color` `foam_depth` `toon_bands` (水面)、`model` `per_m2` `seed` `scale_min` `scale_max` `max_slope` `collide_radius`
(撒く物)。Empty の回転は見ない。[LEVEL_FROM_BLENDER.md](LEVEL_FROM_BLENDER.md)

## 制限

- 広い屋外は区画に分けて region.json で読む。目に近い区画だけを読み、遠くなった区画は手放す ([STREAMING.md](STREAMING.md))。

- 地形は実行中に変えない。遠くの段の LOD の高さは当たり判定と一致しない (足元の段 0 は高さマップそのもの)。
- 草は影を落とさず、動きベクトルも書かない (風の揺れは TAA が少しにじませる)。水面の映り込みは空だけで、SSR は無い。
- 2 トーンのトゥーンは光に背いた斜面を影の側に入れる。低い日だと丘の片側が全部影になり、落ちた影が見えなくなる。
