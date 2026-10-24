#pragma once

/// @file FrustumCulling.hpp
/// @brief 視錐台カリング
/// @details ViewProjection 行列から 6 平面を抽出し、
///          AABB/球体のビジビリティテストを行う。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Mesh.hpp>

namespace mitiru::render
{

/// @brief 3D 軸整列バウンディングボックス
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

/// @brief ローカルの箱を world で変換して外接する箱を作る (8 隅を変換して min / max を取る)
/// @details 最後の行が (0, 0, 0, 1) の行列は、w が厳密に 1 になるので、w の計算と割り算を省く。1 つの隅の値は
///          ((m0*x + m1*y) + m2*z) + m3 で、積を軸ごとに 2 通りだけ先に作っても同じ丸めで同じ値になる (8 隅で結果が変わらない)。
///          drawMesh ごとに呼ばれる所なので、1 回 50 ナノ秒ほどが 20 ナノ秒ほどになる。
[[nodiscard]] inline AABB transformedBox(const Mesh::AABB& local, const sgc::Mat4f& world) noexcept
{
	constexpr float kBig = std::numeric_limits<float>::max();
	AABB box;
	box.minX = box.minY = box.minZ = kBig;
	box.maxX = box.maxY = box.maxZ = -kBig;
	if (world.m[3][0] == 0.0f && world.m[3][1] == 0.0f && world.m[3][2] == 0.0f && world.m[3][3] == 1.0f)
	{
		float lo[3];
		float hi[3];
		for (int r = 0; r < 3; ++r)
		{
			const float* row = world.m[r];
			const float ax[2] = {row[0] * local.min.x, row[0] * local.max.x};
			const float ay[2] = {row[1] * local.min.y, row[1] * local.max.y};
			const float az[2] = {row[2] * local.min.z, row[2] * local.max.z};
			float mn = kBig;
			float mx = -kBig;
			for (int i = 0; i < 2; ++i)
			{
				for (int j = 0; j < 2; ++j)
				{
					const float xy = ax[i] + ay[j];
					for (int k = 0; k < 2; ++k)
					{
						const float v = (xy + az[k]) + row[3];
						mn = std::min(mn, v);
						mx = std::max(mx, v);
					}
				}
			}
			lo[r] = mn;
			hi[r] = mx;
		}
		box.minX = lo[0]; box.minY = lo[1]; box.minZ = lo[2];
		box.maxX = hi[0]; box.maxY = hi[1]; box.maxZ = hi[2];
		return box;
	}
	const sgc::Vec3f corners[8] = {
		{local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
		{local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
		{local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
		{local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
	};
	for (const auto& corner : corners)
	{
		const auto w = world.transformPoint(corner);
		box.minX = std::min(box.minX, w.x); box.maxX = std::max(box.maxX, w.x);
		box.minY = std::min(box.minY, w.y); box.maxY = std::max(box.maxY, w.y);
		box.minZ = std::min(box.minZ, w.z); box.maxZ = std::max(box.maxZ, w.z);
	}
	return box;
}

/// @brief 視錐台（6 平面）
/// @details ViewProjection 行列の行成分から左右上下前後の
///          6 つのクリッピング平面を抽出する。
///
/// @code
/// float viewProj[16]; // column-major ViewProjection 行列
/// mitiru::render::Frustum frustum;
/// frustum.extractFromViewProj(viewProj);
/// if (frustum.isBoxVisible(aabb)) { /* 描画 */ }
/// @endcode
class Frustum
{
public:
	/// @brief ViewProjection 行列（column-major 4x4）から 6 平面を抽出する
	/// @param m column-major 4x4 行列（m[col*4+row]）
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

	/// @brief AABB が視錐台内に（部分的にでも）含まれるか判定する
	/// @param box テスト対象の AABB
	/// @return true: 可視（完全または部分的に内側）
	[[nodiscard]] bool isBoxVisible(const AABB& box) const noexcept
	{
		for (const auto& plane : m_planes)
		{
			// P 頂点（平面法線方向で最も遠い頂点）を選択
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
	/// @param cx 球体中心 X
	/// @param cy 球体中心 Y
	/// @param cz 球体中心 Z
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

	/// @brief 6 平面への直接アクセス
	/// @return 平面配列への定数参照
	[[nodiscard]] const std::array<Plane, 6>& planes() const noexcept
	{
		return m_planes;
	}

	/// @brief Camera3D から直接視錐台を構築する
	/// @details `viewProjectionMatrix()` (OpenGL 規約 Z[-1,1]) を使う。DX11/DX12 の
	///          描画用射影行列 (Z[0,1] 等、バックエンドごとに規約が違う) とは独立に
	///          Gribb-Hartmann の前提 (row3±rowN) が成立する規約に固定するため。
	void extractFromCamera(const Camera3D& camera) noexcept
	{
		const auto vp = camera.viewProjectionMatrix();
		float m[16];
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				m[c * 4 + r] = vp.m[r][c];
			}
		}
		extractFromViewProj(m);
	}

	/// @brief メッシュのローカル AABB をワールド変換した上で可視判定する
	/// @param localAabb `Mesh::localAABB()` 相当のローカル空間 AABB
	/// @param world ワールド変換行列
	/// @return true: 可視（完全または部分的に視錐台内）
	/// @details 8 頂点を変換して外接する AABB を作る（非一様スケール/回転でも
	///          安全な近似になる。厳密な OBB ではないため余分に残ることはあり得るが
	///          過剰カリング（見えるはずの物が消える）は起きない）。
	[[nodiscard]] bool isMeshVisible(const Mesh::AABB& localAabb,
	                                 const sgc::Mat4f& world) const noexcept
	{
		const AABB worldBox = transformedBox(localAabb, world);
		return isBoxVisible(worldBox);
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

/// @brief オブジェクト群を Frustum でカリングし、可視リストを返す
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
