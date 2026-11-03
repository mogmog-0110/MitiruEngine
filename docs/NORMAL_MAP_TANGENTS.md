# 法線マップの接線 (MikkTSpace)

glTF の仕様は、`TANGENT` の無いメッシュの接線を [MikkTSpace](https://github.com/mmikk/MikkTSpace) で作るよう求めている。
Blender で焼いた法線マップも同じ接線で焼かれているので、読む側が同じ式で接線を作れば、面の縫い目と鏡写しの UV の継ぎ目が合う。

## 読み込みで作る

`render/GltfLoader.hpp` は primitive ごとに次の順で接線を決め、`GltfMeshPrimitive::tangents` (頂点ごとの xyz + 向きの w) に持つ。

| 条件 | 接線 |
|---|---|
| `TANGENT` がある | そのまま読む (w は ±1 に丸める) |
| 法線と UV があり、材質が法線マップを持つ | `computeMikkTangents` (`render/MeshTangents.hpp`) で作る |
| それ以外 | 作らない (空)。法線マップを使わない材質に費やさない |

`computeMikkTangents(vertices, indices)` は三角形リストを MikkTSpace に渡し、角ごとの結果を頂点へ書き戻す。同じ位置・法線・UV の角は
MikkTSpace が内部で溶接して同じ接線を返すので、頂点に書き戻しても角の結果と変わらない。

## 描画で使う (DX12)

glb のモデル (`drawModel*`) は、段ごとの接線を次の 2 とおりで頂点と並べて渡す。陰影の PS (`perturbNormal`) は頂点の接線があればそれを使い、
従法線は `w * cross(N, T)`。

| prim | 接線の渡し方 |
|---|---|
| 骨で動かす (スキン、モーフ) | GPU スキニングの入力に接線を持ち、骨と同じ行列で回して、頂点と並ぶ接線のバッファ (slot 1、instanced では slot 2) に出す |
| 骨で動かさない (静的) | モデルを読む段で UPLOAD heap に接線を置き、描く前に mesh の接線のキャッシュへ結ぶ。`drawModelInstanced` と、同じ mesh のまとめ描きも同じバッファを slot 2 に束ねる |

接線を持たない描画 (drawMesh、地形、草、clod の世界のモデル) は slot を束ねず 0 が読まれ、今までどおり画面の微分から接空間を組む
(Schuler の cotangent frame)。世界行列が鏡映のときは、頂点シェーダーが従法線の向き w の符号を戻す。

Dawn はまだ画面の微分のまま (接線のストリームを通していない)。低ポリの曲面に貼った法線マップは、面の境で DX12 と Dawn の陰影が
少し違う。

## できないこと

- モーフは接線を動かさない。位置と法線を変えるモーフを法線マップつきのモデルに当てると、接線はモーフ前のまま (骨で回す分だけ追従する)。
- 同じ頂点の番号を使う三角形どうしで MikkTSpace が別の接線を返す形 (同じ位置・法線・UV で向きの違う面が頂点を共有する) では、
  最後に書いた角の接線が残る。通常のモデルは、頂点を共有するなら同じ接線になる。

## 確かめ方

- `tests/mitiru/TestMeshTangents.cpp`: 答えの分かる面 (XY 平面の正方形は +x と w=+1、u を鏡写しにすると -x と w=-1、円筒の帯は
  法線と直交して円周の向き) と、glTF の読み込みがどちらの接線を持つか。
- `tests/mitiru/TestTangentRenderDx12.cpp`: 8 角の円筒に、一様に +u へ傾けた法線マップを貼って描く。法線マップなしの絵から「明るさ = k * (N・L) + c」を当て、
  傾けた法線の明るさを予測して、前 (画面の微分の接線) と後 (MikkTSpace) の絵と比べる。後の誤差は 9.2 で、前の 19.7 より小さく、T の向きを逆にした予測
  (63.6) より遥かに小さい。前後の絵の差は平均 17 で、接線が描画へ届いている。絵は `tests/render/dx12_current/tangent_{plain,before,after}.png`。
