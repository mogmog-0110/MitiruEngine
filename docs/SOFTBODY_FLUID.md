# 流れる体 (mitiru/softbody/)

粒でできた体の輪郭が、液体のように流れる。粒は Position Based Fluids (Macklin と Müller 2013) で非圧縮に保ち、
芯の姿勢に付いた柔らかい器の外へ出た粒だけを内へ押す。中の粒は自由に流れ、片寄り、着地で水たまりに広がってまた集まる。
game DLL が自分でリンクして同じフレームの中で使う header-only の部品で、host は呼ばない (ADR 0005)。ABI も変えない。

見本は `examples/fluid3d/`。設計の理由は [ADR 0078](adr/0078-softbody-fluid-body.md)。

## データの置き場所

| データ | 置き場所 | 理由 |
|---|---|---|
| 体の状態 (`FluidBodyState`、約 23 KB) | GameMemory | 粒の位置と速さ、仮足、手足、顔まで固定長の POD。巻き戻しと記録／再生がそのまま効く |
| 調整値 (`FluidParams`) | DLL の static か GameMemory | 読むだけ。`parseFluidParams` で JSON から読める |
| 作業領域 (`FluidScratch`、`FluidSurfaceScratch`) と網 (`FluidMesh`) | DLL の static | 毎回作り直す中身だけ。巻き戻した後に別の作業領域で続けても同じビットになる |

## 使い方

```cpp
sb::FluidBodyState body;                       // ゲームの全状態の中
sb::FluidScratch scratch;                      // DLL の static
sb::FluidSurfaceScratch surface;
sb::FluidMesh mesh;

// 最初に 1 回: 休みの形に粒を並べて置く
const int n = sb::fluidSampleRest(params, rest, sb::kFluidMaxParticles);
sb::fluidReset(body, params, rest, n, pose, scratch);

// update: 芯と動きの層が決めた姿勢を渡して進める
sb::FluidStepInput in;
in.pose = pose;                 // 足元・上・前・傾き・上下・足と腕の振り
in.coreVelocity = coreVelocity;
in.mode = sb::FluidMode::Ground;  // Air / Wall / Stick
in.phase = phase;  in.cadence = cadence;
in.planes = floors;  in.planeCount = 1;
sb::fluidStep(body, params, in, dt, scratch);

// draw: 網を作り、前のフレームの位置といっしょに送る (ADR 0076)
sb::buildFluidMesh(body, params, nullptr, 0, surface, mesh);
sb::fluidMeshVertices(mesh, color, verts);
s.updateMesh3D("blob", verts, mesh.vertexCount, mesh.indices, mesh.indexCount, mesh.previousPositions);
```

`registerMesh3D` には最初に `kFluidMeshMaxVertices` と `kFluidMeshMaxIndices` を渡して容量にする。目と口は `body.face` (粒の重心を
ばねで追う点) から `body.faceForward` へ `fluidSurfaceAlong` で進めた面の上に置く。

## 押し出す形 (仮足と手足)

仮足と手足は同じ `FluidShape` (筒・楕円体・超楕円体) で表す。

- 仮足: `fluidPodShoot(body, slot, target)` で伸ばし、先が着くと `Grab` になる。`fluidPodSetState(..., Release)` で戻す。筒は器に
  足すので、根元に近い粒から筒へ移り、体は移った体積だけ縮んで流れ込んで見える
- 手足: 床の上を歩く間だけ生える (行き過ぎるばねで 1.3 倍ほど伸びてから落ち着く)。粒は持たず、面にだけ `limbFillet` で滑らかに
  足す。根元は粒の塊に付ける。最高速では足を `tuckShare` まで畳む
- ゲームの形: `FluidStepInput::shapes` に渡すと器に足し、粒が入る。`buildFluidMesh` の `shapes` に渡すと面にだけ足す

## 決定論

同じ入力なら同じバイトになる。反復回数は固定、近くの粒の表は粒ごとの行に番号順で並べ、順序の決まらない容器は使わない。
粒 i の合計は i の行だけから集めるので、`FluidWorkers` で仕事を分けても直列と同じビットになる。近くの粒の候補の表は位置だけで
決まる並びにしてあるので、巻き戻して別の作業領域で続けても同じビットになる (`TestFluidBody.cpp`)。

## 面の作り方

1. 粒の位置を近くの平均へ 85% 寄せる (Yu と Turk 2013)
2. 粗い格子 (`meshVoxelSize` の 2 倍) にガウスで広げ、Zhu と Bridson の距離 (重み付き平均位置からの距離 − 半径) を作ってぼかす
3. 面の近くの粗いセルだけを 2 × 2 × 2 に割り、細かい格子点を 3 線形補間で埋め、手足の形を足し、Surface Nets で網にする
4. 法線は距離の勾配、前のフレームの位置は「頂点 − 近くの粒の動きの重み付き平均」(手足の所は前の手足の同じ所)

体が丸ごと動けば前の位置は正確にその分だけずれ、TAA と動きのぼけに渡る。

## 参照実装との一致

metamorphosis の Python 参照実装 (tools/previs/fluid.py ほか) と同じ台本で動かし、同じ描き方で並べる道具がある。

```bash
python tools/softbody/fluid_reference_compare.py --repo E:/user/metamorphosis --replay build/tests/mitiru_fluid_replay.exe
```

7 つの台本 (walk・dash・idle・run・jump・wall・tentacle) で、Zhu と Bridson の面の体積の重なり (IoU) は平均 0.95〜0.99、
重心のずれは平均 1〜4 mm、C++ の網の頂点から参照の面までの距離は平均 2 mm。粒の動きは混沌としていて小さな差が育つので、
粒 1 つずつの位置ではなく形で比べる。

## 予算

335 粒、60 Hz の 1 フレーム (体の 4 回の小さな刻みと網 1 枚、`meshVoxelSize` 0.04 m) の時間。Core i5-12400 (6 コア 12 スレッド) の PC で、
Release (`/O2`、SSE2 まで) の `mitiru_fluid_replay --bench` を参照実装の台本に流して測った。ほかの build が走っていない時に測っている。

| 台本 | 1 本で回す (体 + 網、中央値) | `FluidWorkers(4)` (体 + 網、中央値) | `FluidWorkers(4)` の最も速いフレーム |
|---|---|---|---|
| idle | 1.27 + 1.05 ms | 0.57 + 0.55 ms | 0.77 ms |
| walk | 1.34 + 1.61 ms | 0.63 + 0.73 ms | 0.84 ms |
| dash | 1.41 + 1.84 ms | 0.66 + 0.88 ms | 0.87 ms |
| tentacle | 1.44 + 1.26 ms | 0.68 + 0.63 ms | 0.81 ms |

1 ms に収まるのは最も速いフレームだけで、中央値は 4 本で 1.1〜1.5 ms。体の時間の多くは器の距離 (超楕円体の累乗) と
密度の 3 回の反復で、網の時間の多くは手足の近くのセルで形の距離を測る所。`FluidWorkers` を `FluidStepInput::parallel` と
`buildFluidMesh` に渡すと粒と格子のセルを分けて回す (結果のビットは 1 本で回したときと同じ)。

## 限り

- 粒は 512 個まで、仮足は 2 本、ゲームの形は器に 8 個、手足は 16 個まで
- 手足の細さは細かい格子の 2 つ分くらいまで (`meshVoxelSize`)。それより細いと網が途切れる
- 床と壁は平面で渡す。ほかの地形は `FluidStepInput::collide` で押し出す
