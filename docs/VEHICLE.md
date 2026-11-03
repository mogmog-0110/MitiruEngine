# 車

車輪のある車を走らせる。Jolt の vehicle (車輪ごとのサスペンション、エンジン、変速機、差動) を `JoltGameWorld` の窓口に載せるので、
走りも変速も、巻き戻し・分岐・セーブ・ロールバックでビットまで同じに戻る。見本は [`examples/vehicle3d`](../examples/vehicle3d/vehicle3d_dll.cpp)。

## 使い方

`mitiru/physics/JoltVehicle.hpp` の `JoltVehicle` を static に 1 つ置き、world の組み立てで `build` を呼ぶ。`ensureBuilt` が world を作るたびに
組み立てが走るので、巻き戻しやホットリロードの後も同じ車が同じ順で入る。

```cpp
phys::JoltVehicle g_car;

void buildWorld(phys::JoltGameWorld& world, const void*)
{
	world.addBody(/* 地面 */);
	g_car.build(world, phys::makeDefaultCarSpec(), JPH::RVec3(0, 1.0f, 0));
}

// update: 運転の入力を渡してから step する。入力は GameMemory の値から作る
g_car.drive(g_world, forward, right, brake, handBrake);   // forward と right は -1〜1 (right は右が正)、brake と handBrake は 0〜1
g_world.step(dt);

// 描く物は GameMemory へ写す
const auto body = g_car.bodyPose(g_world);        // 車体の箱の中心と向き
const auto wheel = g_car.wheelPose(i);            // 車輪 i。drawMesh("sphere", pos, {2r, width, 2r}, rotDeg) で描ける
const float speed = g_car.forwardSpeed(g_world);  // 前向きの m/s
```

後ろへ走らせるときは `forward` を負にする。走っている向きと逆に踏まれたときに `brake` へ回すかどうかはゲームが決める(見本は回す)。

車輪の食いつきは、踏んでいる相手の body の `mFriction` で決まる。Jolt の既定の 0.2 だと車輪が空転して、変速もしない。走る面と段差の body は 1.0 前後にする。

## 向きの約束

車体の局所で、前が +z、上が +y、左が +x(右が -x)。車輪は 2 つずつ組にし、組の最初が左、次が右。差動とアンチロールバーは組ごとに付く。
`right` を正にすると右(-x の側)へ曲がる。これも、操舵を入れた 30 フレーム後の前の向きで測って確かめてある。`wheelPose` の `rotDeg` は、y を車軸にした円盤の向き(車輪の回転と舵を含む)。この向きは、車体の角に置いた車輪の位置と、円盤の軸が車体の左右と平行なことで測って確かめてある。

## 設定

`WheeledVehicleSpec`(C++ の struct)か、JSON から `readVehicleSpec` で読む。JSON の鍵はどれも省けて、無い鍵は `makeDefaultCarSpec` の値になる。

```json
{
  "body": {"halfExtents": [0.9, 0.2, 2.0], "mass": 1500, "centerOfMassDrop": 0.2},
  "engine": {"maxTorque": 500, "minRpm": 1000, "maxRpm": 6000},
  "transmission": {"gearRatios": [2.66, 1.78, 1.3, 1.0, 0.74], "reverseGearRatios": [-2.9]},
  "drive": "front",
  "tester": "cylinder",
  "wheels": [
    {"at": [0.9, -0.18, 1.4], "radius": 0.3, "maxSteerDeg": 30, "suspension": {"min": 0.3, "max": 0.5, "frequency": 1.5, "damping": 0.5}},
    {"at": [-0.9, -0.18, 1.4], "radius": 0.3, "maxSteerDeg": 30},
    {"at": [0.9, -0.18, -1.4], "radius": 0.3, "handBrakeTorque": 4000},
    {"at": [-0.9, -0.18, -1.4], "radius": 0.3, "handBrakeTorque": 4000}
  ]
}
```

読めない値は `false` を返し、`error` にその場所(`wheels[1].at` など)を入れる。`tester` は車輪と地面の当たりの取り方で、
`ray`(線 1 本、軽い)、`sphere`、`cylinder`(車輪の形に近い)。`drive` は `front`(前の 1 組)か `all`(全部)。

## 保存と巻き戻し

保存に入るのは、車輪の角度と角速度、サスペンションの長さ、接地の情報、運転の入力、エンジンの回転、変速機のギアとクラッチの時間。
巻き戻すと変速の最中にも戻り、同じ入力を流し直すと同じ bytes になる。

保存に入らない物は、エンジンのトルクやギア比、サスペンションの硬さなどの設定。`build` で決めるので、世界を作り直せば同じ値になる。

- 車は拘束の 1 つとして数える。保存の先頭にある拘束の印には、車体の body と車輪の数が入る。車輪の数が違う車の bytes は戻さずに断る。
- 車の body は `despawn` が断る。車は world に入れたままにして、見せたくないときは遠くへ置く。
- `build` は組み立ての中だけ。組み立ての後に呼ぶと車は付かず、`built()` が false のまま。
