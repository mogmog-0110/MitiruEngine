#pragma once

/// @file PointShadowFaces.hpp
/// @brief 点光源の影を 6 面に撮るときの、面の選び方と各面の view * proj (DX12 の影の深度パスと PS が同じ規則を使う)
/// @details 面は光から画素へ向かう向きの絶対値が最大の軸で選び、同点は x、y、z の順に取る。PS (DX12LitShaders.hpp の
///          pointShadowAt) が同じ順で選ぶので、ここを変えたら向こうも直す。各面は 90 度より少し広く撮り、
///          面の境で PCF の読み出しが隣の面の外へ出ないようにする。

#include <algorithm>
#include <cmath>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/GlmBridge.hpp>

namespace mitiru::render
{

inline constexpr int kPointShadowFaceCount = 6;

/// 面の視野の半角の tan。1 だとちょうど 90 度で、1.03 は 1024 画素の面で境の外に約 15 画素の余白を持つ
inline constexpr float kPointShadowTanHalfFov = 1.03f;

/// @brief 光から画素へ向かう向き (dx, dy, dz) が入る面。0 = +X、1 = -X、2 = +Y、3 = -Y、4 = +Z、5 = -Z
[[nodiscard]] inline int pointShadowFace(float dx, float dy, float dz) noexcept
{
	const float ax = std::abs(dx);
	const float ay = std::abs(dy);
	const float az = std::abs(dz);
	if (ax >= ay && ax >= az) { return dx >= 0.0f ? 0 : 1; }
	if (ay >= az) { return dy >= 0.0f ? 2 : 3; }
	return dz >= 0.0f ? 4 : 5;
}

[[nodiscard]] inline sgc::Vec3f pointShadowFaceDirection(int face) noexcept
{
	switch (face)
	{
	case 0: return {1.0f, 0.0f, 0.0f};
	case 1: return {-1.0f, 0.0f, 0.0f};
	case 2: return {0.0f, 1.0f, 0.0f};
	case 3: return {0.0f, -1.0f, 0.0f};
	case 4: return {0.0f, 0.0f, 1.0f};
	default: return {0.0f, 0.0f, -1.0f};
	}
}

/// @brief 面 face の光から見た view と proj。near は range の 0.5%、far は range
inline void pointShadowFaceMatrices(const sgc::Vec3f& position, float range, int face, glm::mat4& view, glm::mat4& proj)
{
	const sgc::Vec3f dir = pointShadowFaceDirection(face);
	const sgc::Vec3f up = (face == 2 || face == 3) ? sgc::Vec3f{0.0f, 0.0f, 1.0f} : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	const float farZ = std::max(range, 0.1f);
	const float fov = 2.0f * std::atan(kPointShadowTanHalfFov);
	view = lookAt(position, position + dir, up);
	proj = perspective(fov, 1.0f, std::max(farZ * 0.005f, 0.02f), farZ);
}

}  // namespace mitiru::render
