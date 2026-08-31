# NativeEngine物理バックエンド(opt-in)

MitiruEngineの3D物理は既定で`PhysicsSystem3D` (engine内蔵)を使う。
NativeEngineは差し替え可能なもう1つのバックエンドで、内蔵側に無い性質を2つ持つ:

- **周期境界** — 世界の端が反対側に繋がる。端をまたいだ衝突もそのまま解く。
  トーラス状のフィールド(Asteroids的な世界、分子動力学的な箱)がそのまま書ける。
- **決定的** — 同じビルド・同じ入力なら結果はビット単位で一致する。

コードは別リポジトリ(https://github.com/mogmog-0110/NativeEngine)。MitiruEngine側にあるのはアダプタ
`include/mitiru/physics/NativePhysicsBridge.hpp` (`scene::ISystem`)だけ。

## 有効化

2段構え。configureで「ビルドするか」、linkで「そのtargetから見えるか」を決める。

```bash
git clone https://github.com/mogmog-0110/NativeEngine external/NativeEngine
cmake --preset default -DMITIRU_USE_NATIVEPHYS=ON
```

ソースの在り処は`-DMITIRU_NATIVEPHYS_ROOT=<path>` → `external/NativeEngine` (submodule) →
環境変数`NATIVEENGINE_ROOT`の順に探す。別の作業コピーでbackend側を弄っている間は
`MITIRU_NATIVEPHYS_ROOT`でそちらを向ければよい。

```cmake
target_link_libraries(mygame PRIVATE mitiru_nativephys)
```

linkしたtargetにだけ`MITIRU_HAS_NATIVEPHYS`が付く。`mitiru`本体には
defineもinclude pathも足さないので、ONにしても既存ビルドは再コンパイルされない。
linkしていないtargetではアダプタは**何もしない** (重力すら効かない)システムになる。

## 使い方

`PhysicsSystem3D`と同じ契約 — `TransformComponent` + `RigidBodyComponent3D`を持つ
エンティティを拾い、固定ステップで進め、姿勢を`TransformComponent`へ書き戻す。
差し替えは`SystemRunner`への登録を変えるだけ。

```cpp
#include "mitiru/physics/NativePhysicsBridge.hpp"

mitiru::physics3d::NativePhysicsSystem::Config cfg;
cfg.periodic = true;      // 端が繋がった世界
cfg.periodicHalf = 25.0f; // [-25, 25] の立方体
runner.addSystem(std::make_unique<mitiru::physics3d::NativePhysicsSystem>(cfg), 100);
```

既定は壁の無い開いた世界。`periodic = true`の時だけ周期境界に切り替わる
(backend単体の既定は ±12mの反射壁なので、アダプタ側で明示的に開けている)。

`RigidBodyComponent3D`のmass / friction / restitution / damping / isKinematicと
コライダー(Sphere / AABB / Capsule)がそのまま反映される。mass <= 0は静的、
`isKinematic`はTransformを正として押し込む(動く床は速度も接触相手に伝わる)。

ジョイント・レイキャスト・キャラクタコントローラ等はアダプタの外側の機能なので、
`backend()`でバックエンドを直接触る。

## 注意点

- **周期境界を理解しているのは物理だけ**。scene graph・カメラ・描画・engine側の
  raycastは無限に広がる空間を前提にしている。`periodic = true`は「世界が本当に
  トーラスでよい」ゲームだけで使う。
- **決定性はビルド単位**。コンパイラ・最適化フラグが変われば値は変わる。
- 接触はbox-box以外が単点(warm startingで安定はする)。バックエンド側の
  クエリはmesh / convex / planeをまだ見ない。

## テスト

- `tests/mitiru/TestNativePhysicsBridge.cpp` — 1ファイルで2つのtargetに入る。
  - `mitiru_tests_core` (既定ビルド): backend無しでno-opである契約
  - `mitiru_tests_nativephys` (`MITIRU_USE_NATIVEPHYS=ON`の時だけ登録): 着地・
    決定性・周期境界の回り込み・エンティティ削除でボディが消えること
- 実行: `ctest --test-dir build -C Debug -L nativephys`
