#pragma once

/// @file FluidMeshVertices.hpp
/// @brief FluidMesh を Screen::updateMesh3D に渡す頂点 (Vertex3D) へ写す。
/// @details 添字 (indices) と前のフレームの位置 (previousPositions) は FluidMesh の配列をそのまま updateMesh3D に渡す。

#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex3D.hpp>
#include <mitiru/softbody/FluidSurface.hpp>

namespace mitiru::softbody
{

/// @brief out に mesh.vertexCount 個の頂点を書く (out は kFluidMeshMaxVertices 個ぶん用意する)
inline void fluidMeshVertices(const FluidMesh& mesh, const sgc::Colorf& color, render::Vertex3D* out) noexcept
{
	for (int i = 0; i < mesh.vertexCount; ++i) { out[i] = render::Vertex3D{mesh.positions[i], mesh.normals[i], {0.0f, 0.0f}, color}; }
}

} // namespace mitiru::softbody
