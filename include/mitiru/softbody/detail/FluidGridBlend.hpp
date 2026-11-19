#pragma once

/// @file FluidGridBlend.hpp
/// @brief 距離グリッドの時間方向の平滑化 (FluidSurface.hpp から読む)。ワールド座標のまま混ぜると、速い体では前のフレームの外側の値が新しい体の内側に混ざってメッシュが潰れるので、重心の動きに合わせてずらす。

#include <algorithm>
#include <cmath>

#include <mitiru/softbody/FluidMeshParams.hpp>

namespace mitiru::softbody::detail
{

[[nodiscard]] inline Vec3 fluidParticleCenter(const FluidBodyState& state) noexcept
{
	Vec3 sum{};
	for (int i = 0; i < state.count; ++i) { sum += state.x[i]; }
	return sum / static_cast<float>(std::max(state.count, 1));
}

[[nodiscard]] inline float fluidGridLerp(float a, float b, float t) noexcept { return a * (1.0f - t) + b * t; }

/// @brief 前のフレームの body を重心の移動分だけずらし、細かな移動は三線形補間して blend の割合で w.body に混ぜ、現在の body を保存する。
/// グリッド間隔が変わったときや前のフレームがないときは、混ぜずに現在の body だけを保存する。
inline void fluidBlendPreviousGrid(const FluidBodyState& state, float blend, FluidSurfaceScratch& w) noexcept
{
	const int nx = w.dim[0], ny = w.dim[1], nz = w.dim[2];
	const long long origin[3] = {std::llround(w.origin.x / w.voxel), std::llround(w.origin.y / w.voxel), std::llround(w.origin.z / w.voxel)};
	const Vec3 center = fluidParticleCenter(state);
	if (w.gridPrevValid != 0 && w.gridPrevVoxel == w.voxel)
	{
		const Vec3 moved = (center - w.gridPrevCenter) / w.voxel;
		const float offset[3] = {static_cast<float>(origin[0] - w.gridPrevOrigin[0]) - moved.x, static_cast<float>(origin[1] - w.gridPrevOrigin[1]) - moved.y,
		                         static_cast<float>(origin[2] - w.gridPrevOrigin[2]) - moved.z};
		const int px = w.gridPrevDim[0], py = w.gridPrevDim[1], pz = w.gridPrevDim[2];
		for (int z = 0; z < nz; ++z)
		{
			const float gz = static_cast<float>(z) + offset[2];
			const int iz = static_cast<int>(std::floor(gz));
			if (iz < 0 || iz + 1 >= pz) { continue; }
			const float tz = gz - static_cast<float>(iz);
			for (int y = 0; y < ny; ++y)
			{
				const float gy = static_cast<float>(y) + offset[1];
				const int iy = static_cast<int>(std::floor(gy));
				if (iy < 0 || iy + 1 >= py) { continue; }
				const float ty = gy - static_cast<float>(iy);
				for (int x = 0; x < nx; ++x)
				{
					const float gx = static_cast<float>(x) + offset[0];
					const int ix = static_cast<int>(std::floor(gx));
					if (ix < 0 || ix + 1 >= px) { continue; }
					const float* c = w.gridPrev + (static_cast<std::size_t>(iz) * static_cast<std::size_t>(py) + static_cast<std::size_t>(iy)) * static_cast<std::size_t>(px) + static_cast<std::size_t>(ix);
					const std::size_t row = static_cast<std::size_t>(px), plane = static_cast<std::size_t>(px) * static_cast<std::size_t>(py);
					const float tx = gx - static_cast<float>(ix);
					const float c0 = fluidGridLerp(fluidGridLerp(c[0], c[1], tx), fluidGridLerp(c[row], c[row + 1], tx), ty);
					const float c1 = fluidGridLerp(fluidGridLerp(c[plane], c[plane + 1], tx), fluidGridLerp(c[plane + row], c[plane + row + 1], tx), ty);
					float& now = w.body[(z * ny + y) * nx + x];
					now = (1.0f - blend) * now + blend * fluidGridLerp(c0, c1, tz);
				}
			}
		}
	}
	std::copy(w.body, w.body + nx * ny * nz, w.gridPrev);
	for (int a = 0; a < 3; ++a)
	{
		w.gridPrevDim[a] = w.dim[a];
		w.gridPrevOrigin[a] = origin[a];
	}
	w.gridPrevVoxel = w.voxel;
	w.gridPrevCenter = center;
	w.gridPrevValid = 1;
}

} // namespace mitiru::softbody::detail
