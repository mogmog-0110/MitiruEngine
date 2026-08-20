#pragma once

/// @file Mesh.hpp
/// @brief 3Dメッシュデータ
/// @details 頂点データとインデックスデータを保持する3Dメッシュクラス。
///          プリミティブ生成用のファクトリメソッドも提供する。

#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "Vertex3D.hpp"

namespace mitiru::render
{

/// @brief 3Dメッシュデータ
/// @details 頂点配列とインデックス配列を保持し、基本プリミティブの
///          生成ヘルパーを提供する。
///
/// @code
/// auto cube = mitiru::render::Mesh::createCube(2.0f);
/// auto sphere = mitiru::render::Mesh::createSphere(1.0f, 32);
/// auto plane = mitiru::render::Mesh::createPlane(10.0f, 10.0f);
/// @endcode
class Mesh
{
public:
	/// @brief デフォルトコンストラクタ（空メッシュ）
	Mesh() noexcept = default;

	/// @brief 頂点データを設定する
	/// @param vertices 頂点配列
	void setVertices(std::vector<Vertex3D> vertices)
	{
		m_vertices = std::move(vertices);
		m_revision = nextRevision();
	}

	/// @brief インデックスデータを設定する
	/// @param indices インデックス配列
	void setIndices(std::vector<uint32_t> indices)
	{
		m_indices = std::move(indices);
		m_revision = nextRevision();
	}

	/// @brief 内容の世代番号（改変ごとに process 全体で一意の値へ更新）
	/// @details GPU 側 VB/IB cache の失効判定用。アドレス再利用でも衝突しない。
	[[nodiscard]] uint64_t revision() const noexcept
	{
		return m_revision;
	}

	/// @brief 頂点数を取得する
	/// @return 頂点数
	[[nodiscard]] std::size_t vertexCount() const noexcept
	{
		return m_vertices.size();
	}

	/// @brief インデックス数を取得する
	/// @return インデックス数
	[[nodiscard]] std::size_t indexCount() const noexcept
	{
		return m_indices.size();
	}

	/// @brief 軸平行バウンディングボックス
	struct AABB
	{
		sgc::Vec3f min{FLT_MAX, FLT_MAX, FLT_MAX};
		sgc::Vec3f max{-FLT_MAX, -FLT_MAX, -FLT_MAX};

		/// @brief AABBの中心を計算する
		[[nodiscard]] sgc::Vec3f center() const noexcept
		{
			return (min + max) * 0.5f;
		}

		/// @brief AABBの半径（各軸の半分のサイズ）を計算する
		[[nodiscard]] sgc::Vec3f extent() const noexcept
		{
			return (max - min) * 0.5f;
		}
	};

	/// @brief メッシュ頂点からAABBを計算する
	/// @return 全頂点を包含するAABB
	[[nodiscard]] AABB computeAABB() const noexcept
	{
		AABB aabb;
		for (const auto& v : m_vertices)
		{
			aabb.min.x = std::min(aabb.min.x, v.position.x);
			aabb.min.y = std::min(aabb.min.y, v.position.y);
			aabb.min.z = std::min(aabb.min.z, v.position.z);
			aabb.max.x = std::max(aabb.max.x, v.position.x);
			aabb.max.y = std::max(aabb.max.y, v.position.y);
			aabb.max.z = std::max(aabb.max.z, v.position.z);
		}
		return aabb;
	}

	/// @brief 頂点データの参照を取得する
	/// @return 頂点配列の定数参照
	[[nodiscard]] const std::vector<Vertex3D>& vertices() const noexcept
	{
		return m_vertices;
	}

	/// @brief インデックスデータの参照を取得する
	/// @return インデックス配列の定数参照
	[[nodiscard]] const std::vector<uint32_t>& indices() const noexcept
	{
		return m_indices;
	}

	/// @brief 立方体メッシュを生成する
	/// @param size 一辺の長さ
	/// @return 立方体メッシュ（24頂点・36インデックス）
	[[nodiscard]] static Mesh createCube(float size = 1.0f)
	{
		const float h = size * 0.5f;
		Mesh mesh;

		std::vector<Vertex3D> verts;
		verts.reserve(24);

		/// 前面 (+Z)
		verts.push_back({{-h, -h,  h}, { 0,  0,  1}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{ h, -h,  h}, { 0,  0,  1}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{ h,  h,  h}, { 0,  0,  1}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{-h,  h,  h}, { 0,  0,  1}, {0, 0}, sgc::Colorf::white()});

		/// 背面 (-Z)
		verts.push_back({{ h, -h, -h}, { 0,  0, -1}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{-h, -h, -h}, { 0,  0, -1}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{-h,  h, -h}, { 0,  0, -1}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{ h,  h, -h}, { 0,  0, -1}, {0, 0}, sgc::Colorf::white()});

		/// 上面 (+Y)
		verts.push_back({{-h,  h,  h}, { 0,  1,  0}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{ h,  h,  h}, { 0,  1,  0}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{ h,  h, -h}, { 0,  1,  0}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{-h,  h, -h}, { 0,  1,  0}, {0, 0}, sgc::Colorf::white()});

		/// 下面 (-Y)
		verts.push_back({{-h, -h, -h}, { 0, -1,  0}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{ h, -h, -h}, { 0, -1,  0}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{ h, -h,  h}, { 0, -1,  0}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{-h, -h,  h}, { 0, -1,  0}, {0, 0}, sgc::Colorf::white()});

		/// 右面 (+X)
		verts.push_back({{ h, -h,  h}, { 1,  0,  0}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{ h, -h, -h}, { 1,  0,  0}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{ h,  h, -h}, { 1,  0,  0}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{ h,  h,  h}, { 1,  0,  0}, {0, 0}, sgc::Colorf::white()});

		/// 左面 (-X)
		verts.push_back({{-h, -h, -h}, {-1,  0,  0}, {0, 1}, sgc::Colorf::white()});
		verts.push_back({{-h, -h,  h}, {-1,  0,  0}, {1, 1}, sgc::Colorf::white()});
		verts.push_back({{-h,  h,  h}, {-1,  0,  0}, {1, 0}, sgc::Colorf::white()});
		verts.push_back({{-h,  h, -h}, {-1,  0,  0}, {0, 0}, sgc::Colorf::white()});

		std::vector<uint32_t> idx;
		idx.reserve(36);

		/// 6面×2三角形×3頂点 = 36インデックス
		for (uint32_t face = 0; face < 6; ++face)
		{
			const uint32_t base = face * 4;
			idx.push_back(base + 0);
			idx.push_back(base + 1);
			idx.push_back(base + 2);
			idx.push_back(base + 0);
			idx.push_back(base + 2);
			idx.push_back(base + 3);
		}

		mesh.setVertices(std::move(verts));
		mesh.setIndices(std::move(idx));
		return mesh;
	}

	/// @brief UV球メッシュを生成する
	/// @param radius 半径
	/// @param segments セグメント数（経度・緯度方向）
	/// @return 球メッシュ
	[[nodiscard]] static Mesh createSphere(float radius = 0.5f, int segments = 16)
	{
		Mesh mesh;

		const int stacks = segments;
		const int slices = segments;

		std::vector<Vertex3D> verts;
		verts.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));

		/// 頂点を生成（緯度×経度のグリッド）
		for (int i = 0; i <= stacks; ++i)
		{
			const float phi = std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(stacks);
			const float sinPhi = std::sin(phi);
			const float cosPhi = std::cos(phi);

			for (int j = 0; j <= slices; ++j)
			{
				const float theta = 2.0f * std::numbers::pi_v<float> * static_cast<float>(j) / static_cast<float>(slices);
				const float sinTheta = std::sin(theta);
				const float cosTheta = std::cos(theta);

				const sgc::Vec3f normal{sinPhi * cosTheta, cosPhi, sinPhi * sinTheta};
				const sgc::Vec3f pos = normal * radius;
				const sgc::Vec2f uv{
					static_cast<float>(j) / static_cast<float>(slices),
					static_cast<float>(i) / static_cast<float>(stacks)
				};

				verts.push_back({pos, normal, uv, sgc::Colorf::white()});
			}
		}

		/// インデックスを生成
		std::vector<uint32_t> idx;
		idx.reserve(static_cast<std::size_t>(stacks * slices * 6));

		for (int i = 0; i < stacks; ++i)
		{
			for (int j = 0; j < slices; ++j)
			{
				const auto a = static_cast<uint32_t>(i * (slices + 1) + j);
				const auto b = static_cast<uint32_t>(a + static_cast<uint32_t>(slices + 1));
				const auto c = a + 1;
				const auto d = b + 1;

				idx.push_back(a);
				idx.push_back(b);
				idx.push_back(c);

				idx.push_back(c);
				idx.push_back(b);
				idx.push_back(d);
			}
		}

		mesh.setVertices(std::move(verts));
		mesh.setIndices(std::move(idx));
		return mesh;
	}

	/// @brief 平面メッシュを生成する
	/// @param width 幅
	/// @param height 高さ
	/// @return 平面メッシュ（XZ平面、Y=0、4頂点・6インデックス）
	[[nodiscard]] static Mesh createPlane(float width = 1.0f, float height = 1.0f)
	{
		const float hw = width * 0.5f;
		const float hh = height * 0.5f;

		Mesh mesh;

		std::vector<Vertex3D> verts =
		{
			{{-hw, 0, -hh}, {0, 1, 0}, {0, 0}, sgc::Colorf::white()},
			{{ hw, 0, -hh}, {0, 1, 0}, {1, 0}, sgc::Colorf::white()},
			{{ hw, 0,  hh}, {0, 1, 0}, {1, 1}, sgc::Colorf::white()},
			{{-hw, 0,  hh}, {0, 1, 0}, {0, 1}, sgc::Colorf::white()},
		};

		// 0,1,2 / 0,2,3 だと幾何学的な表が -Y を向き、宣言した法線 (0,+1,0) と食い違う。
		// メッシュ用パイプラインの既定は背面カリングなので、法線の側 (上) から見ると
		// この面ごと消える -- 「plane だけ描画されない」の正体。表裏は法線に合わせる。
		std::vector<uint32_t> idx = {0, 2, 1, 0, 3, 2};

		mesh.setVertices(std::move(verts));
		mesh.setIndices(std::move(idx));
		return mesh;
	}

private:
	/// @brief process 全体で単調増加する世代番号を払い出す
	[[nodiscard]] static uint64_t nextRevision() noexcept
	{
		static std::atomic<uint64_t> counter{0};
		return counter.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	std::vector<Vertex3D> m_vertices;   ///< 頂点データ
	std::vector<uint32_t> m_indices;    ///< インデックスデータ
	uint64_t m_revision = 0;            ///< 内容世代（0 = 未設定）
};

} // namespace mitiru::render
