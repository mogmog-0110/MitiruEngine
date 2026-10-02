# 3D のピッキング — 画面座標と 3D の世界を行き来する

`#include <mitiru/render/Picking3D.hpp>`。マウスで 3D の物を掴む、床の上へ落とす、キャラクターの頭上に
HTML の吹き出しを置く、といった場面で使う。GPU にも `Screen` にも触れない純粋関数なので、`update` の中で呼べる。

カメラの式（右手系、縦の視野角、アスペクト = 幅 / 高さ）は `Screen::camera3D` と同じもの。ゲーム側へ式を写すと、
エンジンが規約を変えたときに黙ってずれるので、正本はここに置く。描画との一致は 2 段で検査している:
`render::Camera3D` の行列と同じ画素を返すこと、実際に DX12 で描いた立方体の画素の重心と 2px 以内で合うこと
（`tests/mitiru/TestPicking3D.cpp` と `TestDx12GoldenVrt.cpp` の `[picking]`）。

## 使い方

```cpp
#include <mitiru/render/Picking3D.hpp>
namespace pick = mitiru::pick3d;

// draw で screen.camera3D(eye, target, 55.0f) に渡すのと同じ値を入れる
const pick::CameraView view{ eye, target, 55.0f };

// マウスの下にある箱を探す
const pick::Ray ray = pick::screenToRay(view, in.mouseX(), in.mouseY());
float t = 0.0f;
if (pick::rayAabb(ray, boxCenter, boxHalfExtents, t)) { /* 掴んだ。複数あれば t が小さい方が手前 */ }

// 床 (y = 0) のどこを指しているか
sgc::Vec3f floorPoint;
if (pick::rayPlaneY(ray, 0.0f, floorPoint)) { /* ここへ落とす */ }

// キャラクターの頭上に吹き出しを置く
float sx = 0.0f, sy = 0.0f;
if (pick::worldToScreen(view, headPosition, sx, sy)) { hud.set("bubble.x", sx); hud.set("bubble.y", sy); }
```

## 関数

| 関数 | 返すもの |
|---|---|
| `screenToRay(view, sx, sy)` | 画面座標を通る光線。`origin` はカメラの位置、`dir` は正規化済み |
| `worldToScreen(view, world, sx, sy)` | 画面座標。カメラの背後か `nearClip` より手前なら `false`。画面の外でも `true` |
| `rayAabb(ray, center, halfExtents, t)` | 軸に沿った箱との交差。`t` は手前側の距離 |
| `rayPlaneY(ray, planeY, hit)` | 水平面との交点。光線が面から遠ざかるなら `false` |

## `CameraView`

| フィールド | 既定 | 意味 |
|---|---|---|
| `eye` / `target` | | `camera3D` と同じ |
| `fovDeg` | 55 | 縦の視野角（度） |
| `rollDeg` | 0 | 視線軸まわりの傾き。`camera3D(eye, target, fov, roll)` と同じ |
| `screenW` / `screenH` | 1280 / 720 | 論理スクリーンの大きさ。`in.mouseX()` / `mouseY()` と同じ座標系 |
| `nearClip` | 0.1 | これより手前は `worldToScreen` が `false` |

カメラを動かす game は、`draw` で `camera3D` に渡す値と、`update` で `CameraView` に入れる値を
同じ場所（GameMemory か進行データ）から取ること。別々に持つと 1 フレームずれる。

## 台本での検証

マウス操作は `--input-script` の `pos <x> <y>` で再現できる（[INPUT_SCRIPT.md](INPUT_SCRIPT.md)）。
押したい物の画面座標は `worldToScreen` で出して、台本に書く。
