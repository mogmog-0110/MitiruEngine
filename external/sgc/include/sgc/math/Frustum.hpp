#pragma once

/// @file Frustum.hpp
/// @brief 視錐台（フラスタム）型 Frustum<T>
///
/// VP行列（View * Projection）から6平面を抽出し、
/// カリング（AABB/Sphere判定）に使用する。

#include "sgc/math/Plane.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc
{

/// @brief 視錐台の平面インデックス
enum class FrustumPlane
{
	Left = 0,
	Right,
	Bottom,
	Top,
	Near,
	Far
};

/// @brief 視錐台（6平面で囲まれた空間）
/// @tparam T 浮動小数点型
///
/// @code
/// auto vp = projection * view;
/// auto frustum = sgc::Frustumf::fromViewProjection(vp);
/// if (frustum.intersectsAABB(aabb)) { /* 描画する */ }
/// @endcode
template <FloatingPoint T>
struct Frustum
{
	Plane<T> planes[6];  ///< 6平面（Left, Right, Bottom, Top, Near, Far）

	/// @brief デフォルトコンストラクタ
	constexpr Frustum() noexcept = default;

	/// @brief VP行列（View * Projection）から視錐台を構築する
	/// @param vp ビュー×投影行列（行優先）
	/// @return 正規化された6平面の視錐台
	///
	/// @note Gribb & Hartmann法で行列の行から平面を抽出する
	[[nodiscard]] static Frustum fromViewProjection(const Mat4<T>& vp) noexcept
	{
		Frustum f;

		// 行優先の場合、列を取り出すには m[row][col] を使う
		// row4 = (m[3][0], m[3][1], m[3][2], m[3][3])
		// Left:   row4 + row1
		// Right:  row4 - row1
		// Bottom: row4 + row2
		// Top:    row4 - row2
		// Near:   row4 + row3
		// Far:    row4 - row3

		auto extractPlane = [](T a, T b, T c, T d) -> Plane<T>
		{
			const T len = Vec3<T>{a, b, c}.length();
			if (len == T{0}) return {};
			const T invLen = T{1} / len;
			return {Vec3<T>{a * invLen, b * invLen, c * invLen}, d * invLen};
		};

		// Left
		f.planes[0] = extractPlane(
			vp.m[3][0] + vp.m[0][0],
			vp.m[3][1] + vp.m[0][1],
			vp.m[3][2] + vp.m[0][2],
			vp.m[3][3] + vp.m[0][3]);

		// Right
		f.planes[1] = extractPlane(
			vp.m[3][0] - vp.m[0][0],
			vp.m[3][1] - vp.m[0][1],
			vp.m[3][2] - vp.m[0][2],
			vp.m[3][3] - vp.m[0][3]);

		// Bottom
		f.planes[2] = extractPlane(
			vp.m[3][0] + vp.m[1][0],
			vp.m[3][1] + vp.m[1][1],
			vp.m[3][2] + vp.m[1][2],
			vp.m[3][3] + vp.m[1][3]);

		// Top
		f.planes[3] = extractPlane(
			vp.m[3][0] - vp.m[1][0],
			vp.m[3][1] - vp.m[1][1],
			vp.m[3][2] - vp.m[1][2],
			vp.m[3][3] - vp.m[1][3]);

		// Near
		f.planes[4] = extractPlane(
			vp.m[3][0] + vp.m[2][0],
			vp.m[3][1] + vp.m[2][1],
			vp.m[3][2] + vp.m[2][2],
			vp.m[3][3] + vp.m[2][3]);

		// Far
		f.planes[5] = extractPlane(
			vp.m[3][0] - vp.m[2][0],
			vp.m[3][1] - vp.m[2][1],
			vp.m[3][2] - vp.m[2][2],
			vp.m[3][3] - vp.m[2][3]);

		return f;
	}

	/// @brief 点が視錐台内に含まれるか判定する
	/// @param point 判定する点
	[[nodiscard]] bool containsPoint(const Vec3<T>& point) const noexcept
	{
		for (int i = 0; i < 6; ++i)
		{
			if (planes[i].signedDistanceTo(point) < T{0})
			{
				return false;
			}
		}
		return true;
	}

	/// @brief AABBが視錐台と交差しているか判定する
	/// @param aabb 判定するAABB
	/// @return 完全に外側ならfalse、交差または内包ならtrue
	[[nodiscard]] bool intersectsAABB(const AABB3<T>& aabb) const noexcept
	{
		for (int i = 0; i < 6; ++i)
		{
			// 法線方向に最も遠い角を選択
			const Vec3<T> positive{
				(planes[i].normal.x >= T{0}) ? aabb.max.x : aabb.min.x,
				(planes[i].normal.y >= T{0}) ? aabb.max.y : aabb.min.y,
				(planes[i].normal.z >= T{0}) ? aabb.max.z : aabb.min.z
			};

			if (planes[i].signedDistanceTo(positive) < T{0})
			{
				return false;
			}
		}
		return true;
	}

	/// @brief 球が視錐台と交差しているか判定する
	/// @param sphere 判定する球
	[[nodiscard]] bool intersectsSphere(const Sphere<T>& sphere) const noexcept
	{
		for (int i = 0; i < 6; ++i)
		{
			if (planes[i].signedDistanceTo(sphere.center) < -sphere.radius)
			{
				return false;
			}
		}
		return true;
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Frustumf = Frustum<float>;    ///< float版 Frustum
using Frustumd = Frustum<double>;   ///< double版 Frustum

} // namespace sgc
