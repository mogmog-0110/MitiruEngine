# NativeEngine 物理バックエンド (opt-in)

MitiruEngine の 3D 物理は既定で `PhysicsSystem3D` (engine 内蔵) を使う。
NativeEngine は差し替え可能なもう 1 つのバックエンドで、内蔵側に無い性質を 2 つ持つ:

- **周期境界** — 世界の端が反対側に繋がる。端をまたいだ衝突もそのまま解く。
  トーラス状のフィールド (Asteroids 的な世界、分子動力学的な箱) がそのまま書ける。
- **決定的** — 同じビルド・同じ入力なら結果はビット単位で一致する。

コードは別リポジトリ (https://github.com/mogmog-0110/NativeEngine)。MitiruEngine 側にあるのはアダプタ
`include/mitiru/physics/NativePhysicsBridge.hpp` (`scene::ISystem`) だけ。

## 有効化

2 段構え。configure で「ビルドするか」、link で「その target から見えるか」を決める。

```bash
git clone https://github.com/mogmog-0110/NativeEngine external/NativeEngine
cmake --preset default -DMITIRU_USE_NATIVEPHYS=ON
```

ソースの在り処は `-DMITIRU_NATIVEPHYS_ROOT=<path>` → `external/NativeEngine` (submodule) →
環境変数 `NATIVEENGINE_ROOT` の順に探す。別の作業コピーで backend 側を弄っている間は
`MITIRU_NATIVEPHYS_ROOT` でそちらを向ければよい。

```cmake
target_link_libraries(mygame PRIVATE mitiru_nativephys)
```

link した target にだけ `MITIRU_HAS_NATIVEPHYS` が付く。`mitiru` 本体には
define も include path も足さないので、ON にしても既存ビルドは再コンパイルされない。
link していない target ではアダプタは**何もしない** (重力すら効かない) システムになる。

## 使い方

`PhysicsSystem3D` と同じ契約 — `TransformComponent` + `RigidBodyComponent3D` を持つ
エンティティを拾い、固定ステップで進め、姿勢を `TransformComponent` へ書き戻す。
差し替えは `SystemRunner` への登録を変えるだけ。

```cpp
#include "mitiru/physics/NativePhysicsBridge.hpp"

mitiru::physics3d::NativePhysicsSystem::Config cfg;
cfg.periodic = true;      // 端が繋がった世界
cfg.periodicHalf = 25.0f; // [-25, 25] の立方体
runner.addSystem(std::make_unique<mitiru::physics3d::NativePhysicsSystem>(cfg), 100);
```

既定は壁の無い開いた世界。`periodic = true` の時だけ周期境界に切り替わる
(backend 単体の既定は ±12m の反射壁なので、アダプタ側で明示的に開けている)。

`RigidBodyComponent3D` の mass / friction / restitution / damping / isKinematic と
コライダー (Sphere / AABB / Capsule) がそのまま反映される。mass <= 0 は静的、
`isKinematic` は Transform を正として押し込む (動く床は速度も接触相手に伝わる)。

ジョイント・レイキャスト・キャラクタコントローラ等はアダプタの外側の機能なので、
`backend()` でバックエンドを直接触る。

## 注意点

- **周期境界を理解しているのは物理だけ**。scene graph・カメラ・描画・engine 側の
  raycast は無限に広がる空間を前提にしている。`periodic = true` は「世界が本当に
  トーラスでよい」ゲームだけで使う。
- **決定性はビルド単位**。コンパイラ・最適化フラグが変われば値は変わる。
- 接触は box-box 以外が単点 (warm starting で安定はする)。バックエンド側の
  クエリは mesh / convex / plane をまだ見ない。

## テスト

- `tests/mitiru/TestNativePhysicsBridge.cpp` — 1 ファイルで 2 つの target に入る。
  - `mitiru_tests_core` (既定ビルド): backend 無しで no-op である契約
  - `mitiru_tests_nativephys` (`MITIRU_USE_NATIVEPHYS=ON` の時だけ登録): 着地・
    決定性・周期境界の回り込み・エンティティ削除でボディが消えること
- 実行: `ctest --test-dir build -C Debug -L nativephys`
