# 物理

剛体の衝突解決はエンジンで書かない。3D は [Jolt Physics](https://github.com/jrouwe/JoltPhysics)、
2D は [Box2D v3](https://github.com/erincatto/box2d) が解く (どちらも MIT、`external/` の submodule)。
エンジンが持つのは、それらを ECS・game DLL・決定論につなぐ部分と、GameMemory に置ける小さな当たり判定だけ。

## どこで何が解くか

| 用途 | 使うもの | 入口 |
|---|---|---|
| 3D の剛体 (host 側 / ECS) | Jolt | `PhysicsSystem3D` (`RigidBodyComponent3D` を解く `scene::ISystem`、メッシュ地形も)、`JoltPhysicsWorld3D` (ワールドを直接) |
| 2D の剛体 (host 側) | Box2D | `physics::Box2DWorld` (`physics/Box2DBridge.hpp`) |
| game DLL からのレイキャスト・球の重なり | host の Jolt | `hud.raycast` / `hud.overlapSphere` (ADR 0038)、地形は `--collision` |
| GameMemory の中で完結する当たり判定 | 固定長の部品 | `KinematicCapsule` (3D キャラ)、`moveAabbInTileMap` (タイル)、`Circles2D` (円の山) |
| 端が反対側へ繋がる世界 | NativeEngine (opt-in) | `NativePhysicsSystem`、[PHYSICS_NATIVE_BACKEND.md](PHYSICS_NATIVE_BACKEND.md) |

## game DLL と物理

game DLL は host のワールドを持てない (境界は POD だけ、ADR 0005)。Jolt / Box2D のワールドは
ヒープの中にあって GameMemory に入らないので、game の状態に使うと巻き戻しとリプレイから外れる。

- game の中で済む判定は固定長の部品で書く。状態は値だけなので GameMemory にそのまま置ける。
  `KinematicCapsule` を Jolt の `CharacterVirtual` に置き換えないのはこのため。
- 地形への問い合わせは intent で頼み、次のフレームの `in.physicsResult(tag)` で受け取る。
  答えは `InputSnapshot` に乗るので、リプレイでは host の物理を回さずに同じ値が返る。

## メッシュコライダー (ECS)

`RigidBodyComponent3D` に `colliderType = ColliderType3D::Mesh` と `colliderMesh` (`TriangleMesh3D`、共有の三角形) を
持たせると、`PhysicsSystem3D` が Jolt の MeshShape を作る。頂点には `TransformComponent::scale` を掛ける
(描画と同じ大きさになる)。動かない地形か Kinematic 向けで、Dynamic のメッシュは作らずに警告する。

三角形は描画用のメッシュから作る (`physics/MeshCollider.hpp`)。`triangleMeshFrom(render::Mesh)` か、
ファイルなら `MeshColliderLibrary::load(path)` (.obj / .gltf / .glb、glTF はノードの姿勢を掛ける、同じパスは共有)。
シーン文書では `PhysicsTrait.colliderType = "mesh"` のノードの `MeshTrait.meshPath` から作る:

```cpp
mitiru::physics3d::MeshColliderLibrary meshes("assets");
const auto result = mitiru::scene::instantiate(doc, world,
    [&](const std::string& path) { return meshes.load(path); });
```

読み込み手段を渡さない、または読めないときは箱にして `result.warnings` に理由を積む。

## `--collision` の地形

`mitiru_host --collision terrain.json`。箱と三角形メッシュを混ぜて並べる。`layer` (0-31) は
問い合わせの `mask` で選ぶのに使い、レイキャストの答えの `bodyLayer` に返る。

```json
[
  {"min": [-10, -1, -10], "max": [1, 0, 10], "layer": 3},
  {"vertices": [[1, 1, -10], [10, 1, -10], [10, 1, 10], [1, 1, 10]],
   "indices": [0, 2, 1, 0, 3, 2], "layer": 2}
]
```

三角形は表から見て反時計回りに並べる (裏から撃ったレイは当たらない)。Jolt を build していない
構成では world を作らず、答えは `kPhysicsHitUnsupported` になる。

## 決定論

| | 保証 | どう守っているか |
|---|---|---|
| Jolt (3D) | 同じ順で API を呼べば、OS・コンパイラ・CPU・スレッド数を跨いで bit 一致 | CMake で `CROSS_PLATFORM_DETERMINISTIC=ON` (約 8% 遅い)。`JoltPhysicsWorld3D` は問い合わせ結果と接触を距離 → BodyId 順に並べ直す (Jolt はスレッド次第で並びを変える)。`PhysicsSystem3D` はエンティティを EntityId 順にボディへ写す。`step(dt)` は時間の貯金を持たない |
| Box2D v3.1 (2D) | 同じバイナリならスレッド数を跨いで bit 一致 | プラットフォームを跨ぐ一致は Box2D が v3.1 で謳っているが、こちらでは未検証 |
| 固定長の部品 | 同じ入力なら bit 一致 | 走査順と演算順を固定している |

確かめているテスト: `TestPhysicsBackends.cpp` / `TestJoltPhysicsWorld3D.cpp` / `TestPhysicsSystem3D.cpp` /
`TestBox2DWorld.cpp` の `[determinism]` (同じ入力を 2 回流してハッシュが一致、Jolt はスレッド数違いでも一致)、
`TestPhysicsQueryJob.cpp` (同じ地形と要求から同じバイト列の答え)、e2e の `e2e_physics_query_collision`。
