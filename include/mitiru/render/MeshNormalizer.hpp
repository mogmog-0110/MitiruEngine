#pragma once

/// @file MeshNormalizer.hpp
/// @brief メッシュの自動正規化 — サイズ統一、底面y=0合わせ、Y-up統一

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>
#include <sgc/math/Vec3.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mitiru::render
{

/// @brief メッシュのバウンディングボックス
struct MeshBounds
{
	sgc::Vec3f min{};
	sgc::Vec3f max{};

	[[nodiscard]] sgc::Vec3f center() const noexcept
	{
		return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
	}

	[[nodiscard]] sgc::Vec3f size() const noexcept
	{
		return {max.x - min.x, max.y - min.y, max.z - min.z};
	}

	[[nodiscard]] float maxDimension() const noexcept
	{
		auto s = size();
		return std::max({s.x, s.y, s.z});
	}
};

/// @brief メッシュ正規化ユーティリティ
/// @details OBJ等から読み込んだメッシュを統一的なサイズ・座標系に変換する。
///
/// @code
/// auto mesh = mitiru::render::loadObj("model.obj");
/// mitiru::render::MeshNormalizer::normalize(mesh, 1.0f);
/// // mesh は最大寸法1.0、底面y=0、XZ中央揃えになる
/// @endcode
class MeshNormalizer
{
public:
	/// @brief メッシュのバウンディングボックスを計算する
	/// @param mesh 対象メッシュ
	/// @return バウンディングボックス（頂点が0個の場合はゼロ初期化）
	[[nodiscard]] static MeshBounds computeBounds(const Mesh& mesh)
	{
		const auto& verts = mesh.vertices();
		if (verts.empty())
		{
			return {};
		}

		constexpr float fMax = std::numeric_limits<float>::max();
		constexpr float fMin = std::numeric_limits<float>::lowest();
		MeshBounds bounds;
		bounds.min = {fMax, fMax, fMax};
		bounds.max = {fMin, fMin, fMin};

		for (const auto& v : verts)
		{
			bounds.min.x = std::min(bounds.min.x, v.position.x);
			bounds.min.y = std::min(bounds.min.y, v.position.y);
			bounds.min.z = std::min(bounds.min.z, v.position.z);
			bounds.max.x = std::max(bounds.max.x, v.position.x);
			bounds.max.y = std::max(bounds.max.y, v.position.y);
			bounds.max.z = std::max(bounds.max.z, v.position.z);
		}

		return bounds;
	}

	/// @brief メッシュを正規化する（サイズ統一 + 底面y=0 + 中央揃え）
	/// @param mesh 対象メッシュ（変更される）
	/// @param targetSize 正規化後の最大寸法（デフォルト: 1.0）
	/// @param centerXZ true=XZ平面の中心を(0,0)に合わせる
	/// @param bottomY0 true=底面のY座標を0に合わせる
	static void normalize(Mesh& mesh, float targetSize = 1.0f,
	                      bool centerXZ = true, bool bottomY0 = true)
	{
		const auto bounds = computeBounds(mesh);
		const float maxDim = bounds.maxDimension();

		if (maxDim < 1e-8f)
		{
			return; // 退縮メッシュ — 何もしない
		}

		const float scale = targetSize / maxDim;
		const auto c = bounds.center();

		auto verts = mesh.vertices(); // コピーして変更

		for (auto& v : verts)
		{
			// XZ中央揃え
			if (centerXZ)
			{
				v.position.x -= c.x;
				v.position.z -= c.z;
			}

			// 底面y=0合わせ
			if (bottomY0)
			{
				v.position.y -= bounds.min.y;
			}
			else
			{
				v.position.y -= c.y;
			}

			// スケール適用
			v.position.x *= scale;
			v.position.y *= scale;
			v.position.z *= scale;
		}

		mesh.setVertices(std::move(verts));
	}

	/// @brief Z-upモデルをY-upに変換する（Blender等からの読み込み対応）
	/// @param mesh 対象メッシュ（変更される）
	/// @details Z-up座標系（Blender標準）からY-up座標系（エンジン標準）に変換する。
	///          位置と法線の両方を変換する。変換: (x, y, z) -> (x, z, -y)
	static void convertZUpToYUp(Mesh& mesh)
	{
		auto verts = mesh.vertices();

		for (auto& v : verts)
		{
			// 位置を変換: (x, y, z) -> (x, z, -y)
			const float oldY = v.position.y;
			v.position.y = v.position.z;
			v.position.z = -oldY;

			// 法線も同じ変換
			const float oldNY = v.normal.y;
			v.normal.y = v.normal.z;
			v.normal.z = -oldNY;
		}

		mesh.setVertices(std::move(verts));
	}
};

} // namespace mitiru::render
