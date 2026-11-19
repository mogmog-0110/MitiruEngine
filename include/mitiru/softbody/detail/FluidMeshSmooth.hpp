#pragma once

/// @file FluidMeshSmooth.hpp
/// @brief メッシュの頂点のラプラシアンの平滑化 (FluidSurface.hpp から読む)。頂点の並びが毎フレーム変わるので、時間方向でなく 1 フレームの中だけで掛ける。

#include <algorithm>

#include <mitiru/softbody/FluidMeshParams.hpp>

namespace mitiru::softbody::detail
{

inline constexpr float kFluidMeshSmoothPull = 0.5f;

inline void fluidMeshDegrees(const FluidMesh& m, FluidSurfaceScratch& w) noexcept
{
	std::fill(w.meshDegree, w.meshDegree + m.vertexCount, 0);
	for (int t = 0; t + 2 < m.indexCount; t += 3)
	{
		for (int e = 0; e < 3; ++e)
		{
			const auto v = static_cast<int>(m.indices[t + e]);
			if (v < m.vertexCount) { w.meshDegree[v] += 2; }
		}
	}
}

inline void fluidMeshNeighborSum(const FluidMesh& m, FluidSurfaceScratch& w) noexcept
{
	std::fill(w.meshAcc, w.meshAcc + m.vertexCount, Vec3{});
	for (int t = 0; t + 2 < m.indexCount; t += 3)
	{
		const auto a = static_cast<int>(m.indices[t]), b = static_cast<int>(m.indices[t + 1]), c = static_cast<int>(m.indices[t + 2]);
		if (a >= m.vertexCount || b >= m.vertexCount || c >= m.vertexCount) { continue; }
		w.meshAcc[a] += m.positions[b] + m.positions[c];
		w.meshAcc[b] += m.positions[a] + m.positions[c];
		w.meshAcc[c] += m.positions[a] + m.positions[b];
	}
}

inline void fluidMeshAreaNormals(FluidMesh& m, FluidSurfaceScratch& w) noexcept
{
	std::fill(w.meshAcc, w.meshAcc + m.vertexCount, Vec3{});
	for (int t = 0; t + 2 < m.indexCount; t += 3)
	{
		const auto a = static_cast<int>(m.indices[t]), b = static_cast<int>(m.indices[t + 1]), c = static_cast<int>(m.indices[t + 2]);
		if (a >= m.vertexCount || b >= m.vertexCount || c >= m.vertexCount) { continue; }
		const Vec3 n = (m.positions[b] - m.positions[a]).cross(m.positions[c] - m.positions[a]);
		w.meshAcc[a] += n;
		w.meshAcc[b] += n;
		w.meshAcc[c] += n;
	}
	for (int v = 0; v < m.vertexCount; ++v)
	{
		if (length(w.meshAcc[v]) > 1e-12f) { m.normals[v] = normalizeOr(w.meshAcc[v]); }
	}
}

inline void fluidSmoothMesh(int iterations, FluidSurfaceScratch& w, FluidMesh& out) noexcept
{
	if (iterations <= 0 || out.vertexCount == 0) { return; }
	fluidMeshDegrees(out, w);
	for (int it = 0; it < std::min(iterations, kFluidMeshSmoothMaxIterations); ++it)
	{
		fluidMeshNeighborSum(out, w);
		for (int v = 0; v < out.vertexCount; ++v)
		{
			if (w.meshDegree[v] == 0) { continue; }
			const Vec3 moved = (w.meshAcc[v] / static_cast<float>(w.meshDegree[v]) - out.positions[v]) * kFluidMeshSmoothPull;
			out.positions[v] += moved;
			out.previousPositions[v] += moved;   // 動きのベクトル (TAA と動きのぼけ) を保つ
		}
	}
	fluidMeshAreaNormals(out, w);   // 距離の勾配の法線を平滑化するより、寄せたメッシュの面から取り直したほうがフレーム間の変化が小さい
	fluidMeshBounds(out);
}

} // namespace mitiru::softbody::detail
