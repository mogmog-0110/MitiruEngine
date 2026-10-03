# 割れる物・布・揺れ物

物が割れる、マントや旗がはためく、尻尾や髪の房が遅れて揺れる。3 つとも game DLL が自分で link する header で、
同じフレームの判定に使え、巻き戻し・リプレイ・ロールバックでビットまで同じに戻る。決めた理由は [ADR 0069](adr/0069-physics-fx.md)。

| 物 | 部品 | 状態の置き場 | 見本 |
|---|---|---|---|
| 割れる物 | `physics/Fracture.hpp`、`physics/JoltBreakables.hpp` | 破片の body は `JoltGameWorld` の窓口、割れたか・時刻・描く姿勢は `BreakableState` (GameMemory) | [`destruction3d`](../examples/destruction3d/destruction3d_dll.cpp) |
| 布 | `physics/JoltCloth.hpp`、`physics/ClothMesh.hpp` | 頂点の位置・速度・留め具は Jolt の soft body (窓口) | [`cloth3d`](../examples/cloth3d/cloth3d_dll.cpp) |
| 揺れ物 | `animation/AnimSpringBones.hpp` | 骨の先端の位置 `SpringBoneState` (GameMemory) | [`springbones3d`](../examples/springbones3d/springbones3d_dll.cpp) |

## 割れる物

破片は読み込みの時に作る。凸な立体 (直方体、角柱) を種の数だけの Voronoi の小部屋に割り、破片ごとに描画のメッシュと
凸包の点を出す。切り口の面は `FractureDesc::inner` の頂点色になる。種が同じなら、どの PC でも同じビットの破片になる。

```cpp
const phys::FractureResult kCrate = phys::fractureConvex(phys::ConvexPolyhedron::box({0.4f, 0.4f, 0.4f}),
	{.pieces = 12, .seed = 5, .outer = hex(0xB07A45), .inner = hex(0xE3C38E)});
phys::JoltBreakables g_breakables;

void buildWorld(phys::JoltGameWorld& world, const void*)
{
	g_breakables.beginBuild(world);   // 接触の知らせを受ける
	g_breakables.add(world, {.fracture = &kCrate, .position = {0, 0.4f, 0}, .breakImpulse = 60.0f, .debrisLifetime = 4.0f});
}

// update: step の後に割れ方を進める。攻撃が当たった時は shatter で今すぐ割る
g_world.step(dt);
const int n = g_breakables.afterStep(g_world, state.things, dt, events);   // events に「今割れた」点と衝撃
g_breakables.shatter(g_world, i, state.things[i], hitPoint, impulse);
```

- 割る前の body と破片の body は組み立てで全部作る。破片は `addReserveBody` で world の外に置き、割れた瞬間に割る前の body と
  入れ替える。破片は割る前の速度・回転を引き継ぎ、当たった点から外へ `scatterSpeed` で散り、`debrisLifetime` 秒で world から外れる。
- 割れる強さは接触の衝撃 (近づく速さ × 2 つの物の換算質量、N·s) で決める。動かない柱 (`dynamic = false`) も、ぶつかった物の質量で割れる。
- 描くときは `whole` と `pieces[k]` を `registerMesh3D` で 1 回だけ登録し (返り値が meshId)、`BreakableState` の姿勢を
  `render::PieceInstancePod` に入れて、全部の物の破片を `drawMeshPieces` に 1 回で渡す (ABI v51)。同じ meshId の破片は 1 回の
  instanced draw になる。寿命で消える破片には物と破片の番号から `motionKey` を付ける (0 だと、消えた破片の後ろが隣の破片と
  動きベクトルの対になる)。細かい破片は `flags = kPieceNoShadow` で影を省ける。
- 音と粒は `events` と `BreakableState::sinceBreak` から出す。粒は割れてからの秒を渡すので、巻き戻すとその時刻の粒が出る。
- 凸でない立体は割れない。L 字の壁は凸な塊を並べて作る。地形は割らない (ADR 0060)。

## 布

格子の布を Jolt の soft body で解く。上の行 (マント) か左の列 (旗) を 1 本の関節に留め、毎 tick その関節のワールド行列へ運ぶ。

```cpp
phys::JoltCloth g_cape;
phys::KinematicCapsule g_torso;   // 布が当たる体。骨から骨までのカプセル

// 組み立て: 胸の骨の後ろに垂らす
g_cape.create(world, {.columns = 9, .rows = 13, .width = 0.5f, .height = 0.95f},
              sgc::Mat4f::translation({0, 0.14f, -0.2f}), chestWorld);
g_torso.create(world, hips, neck, 0.16f);

// update: step の前に関節と体を運び、風を足す。step の後に頂点を GameMemory へ写す
g_cape.follow(g_world, chestWorld);
g_torso.follow(g_world, hips, neck, dt);
g_cape.wind(g_world, wind, 1.0f, dt);
g_world.step(dt);
g_cape.readPositions(g_world, state.cape);

// draw: 両面のメッシュを作り、最初に登録した後は updateMesh3D で頂点だけを送る (ABI v51)
phys::buildClothVertices(9, 13, state.cape, Color{1, 1, 1, 1}, verts);
if (s.hasMesh3D("cape")) { s.updateMesh3D("cape", verts.data(), 2 * 9 * 13); }
else { s.registerMesh3D("cape", verts.data(), 2 * 9 * 13, indices.data(), indexCount); }
s.drawMesh("cape", {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, hex(0xB0303A));
```

- `updateMesh3D` と、`registerMesh3D` に同じ名前・同じ頂点数と添字数で渡したものは、host が GPU のバッファを作り直さずに中身だけ写す。
- `follow` に渡すのは `create` に渡したのと同じ関節の行列で、布の置き場 (`sheetToJoint`) は掛けない。
- 布の頂点は、world にある他の body (床、竿、カプセル、破片) に当たる。キャラには `KinematicCapsule` を骨に沿わせて当てる。
- 風は面に垂直な速さの成分を風の速さへ寄せるだけで、状態を持たない。強さの揺らぎは GameMemory の時刻から決める。
- `updateMesh3D` で送った布は、前のフレームの形からの頂点ごとの動きベクトルを TAA・FSR・動きのぼけへ渡す。`registerMesh3D` で
  送り直した物は別の形への差し替えとみなし、止まった物として扱う。

## 揺れ物

骨のばねは、評価した姿勢の後、スキニングの前に掛ける。骨の先端をワールドで積分し (慣性・アニメの向きへ戻る力・重力)、
骨の長さへ戻し、当たりの球の外へ押し出してから、骨を先端へ向ける。体ごと平行に動く分は減衰させないので、まっすぐ走るだけでは
尻尾は後ろへ流れず、曲がる・止まる・跳ねる時に遅れる。トゥーン調のキャラの髪・耳・尻尾・紐はこれで足りる。

```json
{ "chains": [{ "root": "b_Tail01_012", "stiffness": 1.6, "drag": 0.3, "gravity": 0.4, "radius": 3 }],
  "colliders": [{ "bone": "b_Hip_01", "offset": [0, 0, 0], "radius": 9 }] }
```

- `<モデル>.springs.json` に書く。鎖は根の骨から子が 1 つの間たどり、末端は親からの長さでもう 1 本続く。半径と offset はモデルの単位。
- 強さは骨の長さを 1 とした割合なので、cm のモデルも m のモデルも同じ値で同じように揺れる。
- update で `stepSpringBones` (進めて姿勢に掛ける)、draw で `applySpringBones` (進めずに掛ける) を呼ぶ。同じ `AnimPoseParams` と
  同じ状態からは同じビットの姿勢になるので、当たり判定の骨と描く骨はずれない。描くのは `drawModelNodeMatrices`。
- ワープした時は `resetSpringBones` で、次の step にアニメの姿勢から始め直す。

## 決定論

- 破片の形と布の初期形は加減乗除と sqrt だけで作る。Jolt は 1 スレッドで解き、body を足す順は組み立てで決まる。
- 接触の知らせ (`JoltBreakables` が受ける) は step の中で毎回同じ順に届く。受けた衝撃は afterStep で使い切り、step をまたいで残さない。
- 揺れ物の積分も加減乗除と sqrt だけで、CPU の FMA の有無で変わる CRT の三角関数を使わない。
- `TestJoltBreakables` と `TestJoltCloth` は途中の GameMemory と world の bytes から再実行してビットまで同じことを、
  `determinism_destruction3d` / `determinism_cloth3d` / `determinism_springbones3d` は host の記録と再生が一致することを確かめる。

## 予算

`docs/PERFORMANCE.md` の physics_fx (木箱 24 個を順に割り、マントの人形 8 体と尻尾の狐 16 匹) で、開発機の Release のメインスレッドは
p50 2.8 ms・p99 4.4 ms、確保は 0 回。飛んでいる破片 100 片ごとに 1 ms ほど増える。布 2 枚と体のカプセル 5 本は 1 フレーム 0.10 ms、
骨のばね 3 本は 0.001 ms に届かない。

## 限り

- 接触の知らせを受ける物は world に 1 つ (`JoltGameWorld::setContactListener`)。割れる物と別の受け手が要る時は、受け手の中から
  `JoltBreakables::OnContactAdded` を呼ぶ。
- 布は 1 本の関節に留める。肩幅の広いマントを左右の肩で別々に留めたい時は、布を 2 枚に分ける。
- 割れた破片が更に割れることはない。
