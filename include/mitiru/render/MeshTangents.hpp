#pragma once

/// @file MeshTangents.hpp
/// @brief 法線マップ用の接線を MikkTSpace で作る
/// @details glTF の仕様は、TANGENT の無いメッシュの接線を MikkTSpace で作るよう求めている。Blender で焼いた
///          法線マップも同じ接線で焼かれているので、同じ式で作れば面の縫い目が合う。結果は頂点ごとの
///          (x, y, z, w) で、xyz が単位の接線、w が ±1 の向き。従法線は w * cross(normal, tangent)。
///          同じ位置・法線・UV の角は同じ接線になる (MikkTSpace が内部で溶接する) ので、頂点に書き戻せる。
///          実体は src/mesh_tangents.cpp (mikktspace_impl)。

#include <cstdint>
#include <vector>

#include <sgc/math/Vec4.hpp>

#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief 三角形リストの頂点ごとの接線。法線か UV が無い (全部 0) 頂点は接線も 0 になる
/// @param indices 3 の倍数。範囲外の番号があれば空を返す
[[nodiscard]] std::vector<sgc::Vec4f> computeMikkTangents(const std::vector<Vertex3D>& vertices,
                                                          const std::vector<std::uint32_t>& indices);

} // namespace mitiru::render
