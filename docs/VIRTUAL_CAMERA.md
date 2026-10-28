# 仮想カメラ

ゲーム中の 3D のカメラを複数用意して、priority で選び、替わるときに blend で混ぜる。カットシーンの外で使う。カットシーンの中は `docs/CUTSCENES.md` (同じ勝者の選び方を共有している)。設計の経緯は ADR 0107。

見本は `examples/vcam3d` (廊下の固定カメラ、追従、ボス戦の演出カメラ、被弾の揺れ)。

## 使い方

```cpp
#include <mitiru/camera/CameraImpulse.hpp>
#include <mitiru/camera/CameraOcclusion.hpp>
#include <mitiru/camera/VirtualCameraStack.hpp>

namespace cam = mitiru::camera;

struct Game {
    cam::VirtualCameraStack<4> cams;    // GameMemory に置く (flat POD)
    cam::CameraImpulseSet<4>   shakes;
    cam::CameraView            view;
    float                      t = 0;
};
```

init で一度、カメラごとの priority と blend を決める。

```cpp
cams.configure(0, /*priority*/ 1, /*blendSec*/ 1.0f);                         // 追従。1 秒かけて混ざる
cams.configure(1, 5, 0.0f);                                                    // 固定。0 秒は混ぜずに切り替える
cams.configure(2, 9, 1.2f, cam::BlendCurve::EaseOut);                          // 演出
```

毎 tick、各カメラの視点を書き、出番のあるものを立てて、`resolveVirtualCamera` に固定刻みの dt を渡す。

```cpp
cams.setView(0, followView);
cams.setActive(1, hero.position.z < 9.0f);
view = cam::resolveVirtualCamera(cams, dt);
if (cams.cut != 0) { hud.cameraCut(); }     // 混ぜずに替わったフレームだけ 1
```

draw では揺れを足して `camera3D` に渡す。`cameraShakeScale()` は利用者の設定の倍率で、draw でだけ読む。

```cpp
const cam::CameraView v = cam::applyCameraShake(view, cam::sampleCameraShake(shakes, t), s.cameraShakeScale());
s.camera3D(v.eye, v.target, v.up, Deg{v.fovDeg}, 0.1f, 80.0f);
```

被弾などの衝撃は `shakes.add(t, 強さ 0..1, 向き, 秒, seed)` で足す。向きは視点から見て x が右、y が上、z が奥。

## 決まり

- 一番高い priority のカメラが映る。同点は添字の小さい方。映すものが無ければ最後の映像を保つ。
- 勝者が替わったフレームから、勝者の `blendSec` かけて直前の映像から混ぜる。混ぜの途中で替わったら、途中の姿から混ぜ直す。勝者の視点が動いていれば、混ぜている間もその今の視点へ寄る。
- 混ぜの途中の姿は `VirtualCameraStack` の中にあるので、GameMemory を巻き戻すとその姿に戻る。
- 目が壁に入るのを避けたいときは、注視の根元を渡して `avoidWalls(view, world, anchor)` を通し、通した視点を `cams.commitShown(view)` で覚えさせる (覚えさせないと、次の切り替えが画面に出ていた視点でなく、壁に入る前の視点から混ぜ始まる)。

## 揺れ

揺れの量は、`add` した時刻と seed と強さの純関数で、毎フレーム求める。GameMemory に置くのは発生の記録だけ。倍率は PC ごとに違うので update では読まない。

| 設定 | 値 | 効く所 |
|---|---|---|
| `cameraShake` | 0 か 1 | `cameraShakeScale()`。このページの揺れ |
| `screenShake` | 0..1 | `screenShakeScale()`。host の `hud.shake` が使う |
