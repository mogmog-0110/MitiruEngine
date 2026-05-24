#pragma once

/// @file MeshFactory.hpp
/// @brief 基本3Dメッシュデータの生成
///
/// キューブ・球・平面・シリンダーなどのプリミティブメッシュデータを生成する。
/// 法線・テクスチャ座標を含む頂点データとインデックスデータを返す。
///
/// @code
/// auto cube = sgc::graphics3d::createCubeMesh(2.0f);
/// auto sphere = sgc::graphics3d::createSphereMesh(1.0f, 32, 32);
/// @endcode

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "sgc/graphics3d/IMesh.hpp"

namespace sgc::graphics3d
{

/// @brief メッシュデータ（頂点+インデックス）
struct MeshData
{
	std::vector<Vertex3D> vertices;    ///< 頂点配列
	std::vector<uint32_t> indices;     ///< インデックス配列
};

/// @brief キューブメッシュを生成する
/// @param size 辺の長さ
/// @return メッシュデータ
[[nodiscard]] inline MeshData createCubeMesh(float size = 1.0f)
{
	const float h = size * 0.5f;
	MeshData data;
	data.vertices.reserve(24);
	data.indices.reserve(36);

	// 各面に4頂点ずつ（法線が異なるため共有不可）
	// 面順: +Z, -Z, +X, -X, +Y, -Y
	struct FaceInfo
	{
		Vec3f normal;
		Vec3f up;
		Vec3f right;
	};

	const FaceInfo faces[6] =
	{
		{{0, 0, 1},  {0, 1, 0},  {1, 0, 0}},   // 前面 (+Z)
		{{0, 0, -1}, {0, 1, 0},  {-1, 0, 0}},   // 背面 (-Z)
		{{1, 0, 0},  {0, 1, 0},  {0, 0, -1}},   // 右面 (+X)
		{{-1, 0, 0}, {0, 1, 0},  {0, 0, 1}},    // 左面 (-X)
		{{0, 1, 0},  {0, 0, -1}, {1, 0, 0}},    // 上面 (+Y)
		{{0, -1, 0}, {0, 0, 1},  {1, 0, 0}},    // 下面 (-Y)
	};

	for (int i = 0; i < 6; ++i)
	{
		const auto& f = faces[i];
		const Vec3f center = f.normal * h;
		const auto base = static_cast<uint32_t>(data.vertices.size());

		// 4頂点（左下、右下、右上、左上）
		data.vertices.push_back({center - f.right * h - f.up * h, f.normal, {0, 0}});
		data.vertices.push_back({center + f.right * h - f.up * h, f.normal, {1, 0}});
		data.vertices.push_back({center + f.right * h + f.up * h, f.normal, {1, 1}});
		data.vertices.push_back({center - f.right * h + f.up * h, f.normal, {0, 1}});

		// 2三角形
		data.indices.push_back(base);
		data.indices.push_back(base + 1);
		data.indices.push_back(base + 2);
		data.indices.push_back(base);
		data.indices.push_back(base + 2);
		data.indices.push_back(base + 3);
	}

	return data;
}

/// @brief 球メッシュを生成する
/// @param radius 半径
/// @param segments 経度方向の分割数
/// @param rings 緯度方向の分割数
/// @return メッシュデータ
[[nodiscard]] inline MeshData createSphereMesh(
	float radius = 0.5f,
	int32_t segments = 16,
	int32_t rings = 16)
{
	MeshData data;
	const auto vertCount = static_cast<std::size_t>((rings + 1) * (segments + 1));
	data.vertices.reserve(vertCount);
	data.indices.reserve(static_cast<std::size_t>(rings * segments * 6));

	constexpr float pi = std::numbers::pi_v<float>;

	for (int32_t r = 0; r <= rings; ++r)
	{
		const float phi = pi * static_cast<float>(r) / static_cast<float>(rings);
		const float sinP = std::sin(phi);
		const float cosP = std::cos(phi);

		for (int32_t s = 0; s <= segments; ++s)
		{
			const float theta = 2.0f * pi * static_cast<float>(s) / static_cast<float>(segments);
			const float sinT = std::sin(theta);
			const float cosT = std::cos(theta);

			Vec3f normal{sinP * cosT, cosP, sinP * sinT};
			Vec2f uv{
				static_cast<float>(s) / static_cast<float>(segments),
				static_cast<float>(r) / static_cast<float>(rings)
			};

			data.vertices.push_back({normal * radius, normal, uv});
		}
	}

	for (int32_t r = 0; r < rings; ++r)
	{
		for (int32_t s = 0; s < segments; ++s)
		{
			const auto curr = static_cast<uint32_t>(r * (segments + 1) + s);
			const auto next = curr + static_cast<uint32_t>(segments + 1);

			data.indices.push_back(curr);
			data.indices.push_back(next);
			data.indices.push_back(curr + 1);

			data.indices.push_back(curr + 1);
			data.indices.push_back(next);
			data.indices.push_back(next + 1);
		}
	}

	return data;
}

/// @brief 平面メッシュを生成する
/// @param width 幅（X軸方向）
/// @param depth 奥行き（Z軸方向）
/// @param subdivX X方向の分割数
/// @param subdivZ Z方向の分割数
/// @return メッシュデータ
[[nodiscard]] inline MeshData createPlaneMesh(
	float width = 1.0f,
	float depth = 1.0f,
	int32_t subdivX = 1,
	int32_t subdivZ = 1)
{
	MeshData data;
	const auto cols = subdivX + 1;
	const auto rows = subdivZ + 1;
	data.vertices.reserve(static_cast<std::size_t>(cols * rows));
	data.indices.reserve(static_cast<std::size_t>(subdivX * subdivZ * 6));

	const float halfW = width * 0.5f;
	const float halfD = depth * 0.5f;

	for (int32_t z = 0; z < rows; ++z)
	{
		for (int32_t x = 0; x < cols; ++x)
		{
			const float u = static_cast<float>(x) / static_cast<float>(subdivX);
			const float v = static_cast<float>(z) / static_cast<float>(subdivZ);

			data.vertices.push_back({
				{u * width - halfW, 0.0f, v * depth - halfD},
				{0.0f, 1.0f, 0.0f},
				{u, v}
			});
		}
	}

	for (int32_t z = 0; z < subdivZ; ++z)
	{
		for (int32_t x = 0; x < subdivX; ++x)
		{
			const auto base = static_cast<uint32_t>(z * cols + x);
			const auto nextRow = base + static_cast<uint32_t>(cols);

			data.indices.push_back(base);
			data.indices.push_back(nextRow);
			data.indices.push_back(base + 1);

			data.indices.push_back(base + 1);
			data.indices.push_back(nextRow);
			data.indices.push_back(nextRow + 1);
		}
	}

	return data;
}

/// @brief シリンダーメッシュを生成する
/// @param radius 底面の半径
/// @param height 高さ
/// @param segments 円周方向の分割数
/// @return メッシュデータ
[[nodiscard]] inline MeshData createCylinderMesh(
	float radius = 0.5f,
	float height = 1.0f,
	int32_t segments = 16)
{
	MeshData data;
	constexpr float pi2 = 2.0f * std::numbers::pi_v<float>;
	const float halfH = height * 0.5f;

	// 側面の頂点（上下2リング）
	for (int32_t i = 0; i <= segments; ++i)
	{
		const float theta = pi2 * static_cast<float>(i) / static_cast<float>(segments);
		const float cosT = std::cos(theta);
		const float sinT = std::sin(theta);
		const float u = static_cast<float>(i) / static_cast<float>(segments);

		Vec3f normal{cosT, 0.0f, sinT};

		// 下端
		data.vertices.push_back({{cosT * radius, -halfH, sinT * radius}, normal, {u, 0.0f}});
		// 上端
		data.vertices.push_back({{cosT * radius, halfH, sinT * radius}, normal, {u, 1.0f}});
	}

	// 側面インデックス
	for (int32_t i = 0; i < segments; ++i)
	{
		const auto base = static_cast<uint32_t>(i * 2);
		data.indices.push_back(base);
		data.indices.push_back(base + 2);
		data.indices.push_back(base + 1);

		data.indices.push_back(base + 1);
		data.indices.push_back(base + 2);
		data.indices.push_back(base + 3);
	}

	// 上面キャップ
	const auto topCenter = static_cast<uint32_t>(data.vertices.size());
	data.vertices.push_back({{0, halfH, 0}, {0, 1, 0}, {0.5f, 0.5f}});
	for (int32_t i = 0; i <= segments; ++i)
	{
		const float theta = pi2 * static_cast<float>(i) / static_cast<float>(segments);
		data.vertices.push_back({
			{std::cos(theta) * radius, halfH, std::sin(theta) * radius},
			{0, 1, 0},
			{std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f}
		});
	}
	for (int32_t i = 0; i < segments; ++i)
	{
		data.indices.push_back(topCenter);
		data.indices.push_back(topCenter + static_cast<uint32_t>(i) + 1);
		data.indices.push_back(topCenter + static_cast<uint32_t>(i) + 2);
	}

	// 下面キャップ
	const auto botCenter = static_cast<uint32_t>(data.vertices.size());
	data.vertices.push_back({{0, -halfH, 0}, {0, -1, 0}, {0.5f, 0.5f}});
	for (int32_t i = 0; i <= segments; ++i)
	{
		const float theta = pi2 * static_cast<float>(i) / static_cast<float>(segments);
		data.vertices.push_back({
			{std::cos(theta) * radius, -halfH, std::sin(theta) * radius},
			{0, -1, 0},
			{std::cos(theta) * 0.5f + 0.5f, std::sin(theta) * 0.5f + 0.5f}
		});
	}
	for (int32_t i = 0; i < segments; ++i)
	{
		data.indices.push_back(botCenter);
		data.indices.push_back(botCenter + static_cast<uint32_t>(i) + 2);
		data.indices.push_back(botCenter + static_cast<uint32_t>(i) + 1);
	}

	return data;
}

} // namespace sgc::graphics3d
