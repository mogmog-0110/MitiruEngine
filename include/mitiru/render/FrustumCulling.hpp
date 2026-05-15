#pragma once

/// @file FrustumCulling.hpp
/// @brief 視錐台カリング
/// @details ViewProjection行列から6平面を抽出し、
///          AABB/球体のビジビリティテストを行う。

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace mitiru::render
{

/// @brief 3D軸整列バウンディングボックス
struct AABB
{
	float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
	float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
};

/// @brief 平面方程式 (ax + by + cz + d = 0)
struct Plane
{
	float a = 0.0f;
	float b = 0.0f;
	float c = 0.0f;
	float d = 0.0f;

	/// @brief 平面を正規化する
	void normalize() noexcept
	{
		const float len = std::sqrt(a * a + b * b + c * c);
		if (len > 1e-8f)
		{
			const float inv = 1.0f / len;
			a *= inv;
			b *= inv;
			c *= inv;
			d *= inv;
		}
	}

	/// @brief 点と平面の符号付き距離を計算する
	[[nodiscard]] float distanceTo(float x, float y, float z) const noexcept
	{
		return a * x + b * y + c * z + d;
	}
};

/// @brief 視錐台（6平面）
/// @details ViewProjection行列の行成分から左右上下前後の
///          6つのクリッピング平面を抽出する。
///
/// @code
/// float viewProj[16]; // column-major ViewProjection行列
/// mitiru::render::Frustum frustum;
/// frustum.extractFromViewProj(viewProj);
/// if (frustum.isBoxVisible(aabb)) { /* 描画 */ }
/// @endcode
class Frustum
{
public:
	/// @brief ViewProjection行列（column-major 4x4）から6平面を抽出する
	/// @param m column-major 4x4行列（m[col*4+row]）
	void extractFromViewProj(const float m[16]) noexcept
	{
		// Gribb-Hartmann法: row-majorでのアクセスに変換
		// m[col*4+row] -> 要素 (row, col)
		// row 0: m[0], m[4], m[8],  m[12]
		// row 1: m[1], m[5], m[9],  m[13]
		// row 2: m[2], m[6], m[10], m[14]
		// row 3: m[3], m[7], m[11], m[15]

		// Left:   row3 + row0
		m_planes[0] = {m[3] + m[0], m[7] + m[4], m[11] + m[8],  m[15] + m[12]};
		// Right:  row3 - row0
		m_planes[1] = {m[3] - m[0], m[7] - m[4], m[11] - m[8],  m[15] - m[12]};
		// Bottom: row3 + row1
		m_planes[2] = {m[3] + m[1], m[7] + m[5], m[11] + m[9],  m[15] + m[13]};
		// Top:    row3 - row1
		m_planes[3] = {m[3] - m[1], m[7] - m[5], m[11] - m[9],  m[15] - m[13]};
		// Near:   row3 + row2
		m_planes[4] = {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]};
		// Far:    row3 - row2
		m_planes[5] = {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]};

		for (auto& plane : m_planes)
		{
			plane.normalize();
		}
	}

	/// @brief AABBが視錐台内に（部分的にでも）含まれるか判定する
	/// @param box テスト対象のAABB
	/// @return true: 可視（完全または部分的に内側）
	[[nodiscard]] bool isBoxVisible(const AABB& box) const noexcept
	{
		for (const auto& plane : m_planes)
		{
			// P頂点（平面法線方向で最も遠い頂点）を選択
			const float px = (plane.a >= 0.0f) ? box.maxX : box.minX;
			const float py = (plane.b >= 0.0f) ? box.maxY : box.minY;
			const float pz = (plane.c >= 0.0f) ? box.maxZ : box.minZ;

			if (plane.distanceTo(px, py, pz) < 0.0f)
			{
				return false; // 完全に外側
			}
		}
		return true;
	}

	/// @brief 球体が視錐台内に（部分的にでも）含まれるか判定する
	/// @param cx 球体中心X
	/// @param cy 球体中心Y
	/// @param cz 球体中心Z
	/// @param radius 球体半径
	/// @return true: 可視
	[[nodiscard]] bool isSphereVisible(float cx, float cy, float cz,
	                                   float radius) const noexcept
	{
		for (const auto& plane : m_planes)
		{
			if (plane.distanceTo(cx, cy, cz) < -radius)
			{
				return false;
			}
		}
		return true;
	}

	/// @brief 6平面への直接アクセス
	/// @return 平面配列への定数参照
	[[nodiscard]] const std::array<Plane, 6>& planes() const noexcept
	{
		return m_planes;
	}

private:
	std::array<Plane, 6> m_planes{};
};

/// @brief オブジェクトのカリング対象エントリ
/// @tparam T オブジェクト識別子の型
template <typename T>
struct CullEntry
{
	T object{};
	AABB bounds{};
};

/// @brief オブジェクト群をFrustumでカリングし、可視リストを返す
/// @tparam T オブジェクト識別子の型
/// @param entries カリング対象のオブジェクト群
/// @param frustum テスト用の視錐台
/// @return 可視オブジェクトのリスト
template <typename T>
[[nodiscard]] std::vector<T> cullObjects(
	const std::vector<CullEntry<T>>& entries,
	const Frustum& frustum)
{
	std::vector<T> visible;
	visible.reserve(entries.size());

	for (const auto& entry : entries)
	{
		if (frustum.isBoxVisible(entry.bounds))
		{
			visible.push_back(entry.object);
		}
	}
	return visible;
}

} // namespace mitiru::render
