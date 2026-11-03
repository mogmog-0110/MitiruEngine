# gltfpack で縮めた glTF を読む (EXT_meshopt_compression)

glTF の読み込み口は、`EXT_meshopt_compression` で縮めた bufferView をそのまま戻して読む。縮めた glTF は元の 3 割から 5 割の大きさになり、
配布の一式 (ADR 0089) が軽くなる。ゲームの側に書く物は無い。モデルのパスは今までと同じ。

## 縮める

[gltfpack](https://github.com/zeux/meshoptimizer/tree/master/gltf) (meshoptimizer の一部) で縮める。エンジンには同梱しない。
meshoptimizer のソースを `-DMESHOPT_BUILD_GLTFPACK=ON` で組むか、npm の `gltfpack` を使う。

```
gltfpack -i model.glb -o model.packed.glb -cc        # 既定の量子化 (位置 14 bit、法線 8 bit) + 圧縮
gltfpack -i model.glb -o model.packed.glb -cc -noq   # 量子化なし。頂点の値は float のまま
```

`-kn` で名前つきのノードを残す (アニメーションの対象をコードから名前で引くモデルは付ける)。`-km` で材質の名前を残す。
gltfpack は同じ頂点を 1 つにまとめ、頂点を並べ替え、アニメーションを 30 Hz に取り直す。

既定の量子化は UV を 12 bit にし、元へ戻す倍率を材質の `KHR_texture_transform` に書く。読み込みは、材質の全テクスチャが同じ平行移動と
倍率 (回転なし) を持つときだけ、それを UV に焼き込む。テクスチャごとに違う transform や回転は読まないので、そういうモデルは `-vtf` で UV を float のままにする。

## 読む側で起きること

| 段 | 何をするか |
|---|---|
| `cgltf_parse` | 拡張の記述 (縮めた元の buffer、count、stride、mode、filter) を読む。cgltf はここまで |
| `cgltf_load_buffers` | 縮めた列の buffer を読む。`fallback: true` の buffer は中身が無いまま |
| `decodeMeshoptBufferViews` | 縮めた bufferView ごとに元の大きさの領域を取り、`meshopt_decode*` で戻して `view->data` に置く。フィルタ (八面体の法線、四元数、指数、色) も当てる |
| `cgltf_accessor_read_*` | `view->data` を読む。以後は縮めていない glTF と同じ |

`decodeMeshoptBufferViews` (`render/GltfMeshoptDecode.hpp`) は glTF を読む 5 つの口 (GltfLoader、LevelLoader、GltfAnimationLoader、
焼きのシーン、clod の取り込み) が `cgltf_load_buffers` の直後に呼ぶ。壊れた列は読み込みの失敗になり、落ちない。
コーデックは meshoptimizer のうち `vertexcodec` / `indexcodec` / `vertexfilter` だけを `meshopt_codec` に切り出し、wasm を含むどの構成にも結ぶ。

## 確かめ方

`tests/mitiru/TestGltfMeshopt.cpp` が `tests/fixtures/meshopt/` の 2 本 (`-cc -noq` と `-cc`) を元の `rig_reference.glb` と比べる。
三角形の集まり、頂点ごとのスキンの束縛、クリップの長さが一致する (量子化した方は許容差の中で)。fixture の作り直しは
`python tests/fixtures/meshopt/make_meshopt_fixtures.py <gltfpack>`。
