#pragma once

/// @file NarrowPhase3D.hpp
/// @brief 3Dナローフェーズ衝突検出
///
/// GJKベースの凸形状衝突検出とEPA（Expanding Polytope Algorithm）による
/// 接触点・法線・貫通深度の計算を提供する。
/// 球-球、球-ボックス、ボックス-ボックスの特殊化パスも持つ。
///
/// @code
/// mitiru::physics3d::NarrowPhase3D narrow;
/// auto result = narrow.test(colliderA, colliderB);
/// if (result.hasContact) { /* 衝突処理 */ }
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/CollisionDetection3D.hpp"

namespace mitiru::physics3d
{

/// @brief ナローフェーズ衝突結果（拡張版）
///
/// 基本の ContactInfo3D に加えて、接触点の数と
/// 各接触点のローカル座標を保持する。
struct NarrowPhaseResult3D
{
	ContactInfo3D contact{};            ///< 基本接触情報
	sgc::Vec3f localPointA{};           ///< ボディAローカルの接触点
	sgc::Vec3f localPointB{};           ///< ボディBローカルの接触点
};

/// @brief EPA（Expanding Polytope Algorithm）用の三角面
struct EpaFace
{
	std::array<sgc::Vec3f, 3> vertices;  ///< 頂点
	sgc::Vec3f normal{};                  ///< 面の法線（外向き）
	float distance{0.0f};                 ///< 原点から面までの距離
};

/// @brief GJK + EPA ベースのナローフェーズ衝突検出器
///
/// 衝突検出の処理フロー:
///   1. 形状ペアの特殊化パスを試行（球-球、球-AABB、AABB-AABB）
///   2. 特殊化パスがない場合、GJK で交差判定
///   3. 交差している場合、EPA で接触情報を抽出
class NarrowPhase3D
{
public:
	/// @brief デフォルトコンストラクタ
	NarrowPhase3D() = default;

	/// @brief GJK/EPA の最大反復回数を設定する
	/// @param iterations 最大反復回数
	void setMaxIterations(int iterations) noexcept { m_maxIterations = iterations; }

	/// @brief GJK/EPA の最大反復回数を取得する
	[[nodiscard]] constexpr int maxIterations() const noexcept { return m_maxIterations; }

	// ── 特殊化テスト（高速パス）────────────────────────────────

	/// @brief 球と球の衝突テスト
	/// @param a 球A
	/// @param b 球B
	/// @return ナローフェーズ結果
	[[nodiscard]] static NarrowPhaseResult3D testSphereSphere(
		const SphereCollider& a, const SphereCollider& b) noexcept
	{
		NarrowPhaseResult3D result;
		result.contact = mitiru::physics3d::testSphereSphere(a, b);

		if (result.contact.hasContact)
		{
			result.localPointA = result.contact.point - a.center;
			result.localPointB = result.contact.point - b.center;
		}

		return result;
	}

	/// @brief 球とAABBの衝突テスト
	/// @param sphere 球
	/// @param aabb AABB
	/// @return ナローフェーズ結果
	[[nodiscard]] static NarrowPhaseResult3D testSphereBox(
		const SphereCollider& sphere, const AABBCollider3D& aabb) noexcept
	{
		NarrowPhaseResult3D result;
		result.contact = mitiru::physics3d::testSphereAABB(sphere, aabb);

		if (result.contact.hasContact)
		{
			result.localPointA = result.contact.point - sphere.center;
			result.localPointB = result.contact.point - aabb.center();
		}

		return result;
	}

	/// @brief AABB同士の衝突テスト
	/// @param a AABB A
	/// @param b AABB B
	/// @return ナローフェーズ結果
	[[nodiscard]] static NarrowPhaseResult3D testBoxBox(
		const AABBCollider3D& a, const AABBCollider3D& b) noexcept
	{
		NarrowPhaseResult3D result;
		result.contact = mitiru::physics3d::testAABBAABB(a, b);

		if (result.contact.hasContact)
		{
			result.localPointA = result.contact.point - a.center();
			result.localPointB = result.contact.point - b.center();
		}

		return result;
	}

	/// @brief 球とカプセルの衝突テスト
	/// @param sphere 球
	/// @param capsule カプセル
	/// @return ナローフェーズ結果
	[[nodiscard]] static NarrowPhaseResult3D testSphereCapsule(
		const SphereCollider& sphere, const CapsuleCollider& capsule) noexcept
	{
		NarrowPhaseResult3D result;
		result.contact = mitiru::physics3d::testSphereCapsule(sphere, capsule);

		if (result.contact.hasContact)
		{
			result.localPointA = result.contact.point - sphere.center;
			result.localPointB = result.contact.point - capsule.center();
		}

		return result;
	}

	/// @brief カプセル同士の衝突テスト
	/// @param a カプセルA
	/// @param b カプセルB
	/// @return ナローフェーズ結果
	[[nodiscard]] static NarrowPhaseResult3D testCapsuleCapsule(
		const CapsuleCollider& a, const CapsuleCollider& b) noexcept
	{
		NarrowPhaseResult3D result;
		result.contact = mitiru::physics3d::testCapsuleCapsule(a, b);

		if (result.contact.hasContact)
		{
			result.localPointA = result.contact.point - a.center();
			result.localPointB = result.contact.point - b.center();
		}

		return result;
	}

	// ── GJKベース汎用テスト ──────────────────────────────────

	/// @brief GJK + EPA による凸形状同士の衝突テスト
	/// @param a 凸形状A
	/// @param b 凸形状B
	/// @return ナローフェーズ結果
	[[nodiscard]] NarrowPhaseResult3D testConvexConvex(
		const ConvexShape& a, const ConvexShape& b) const noexcept
	{
		NarrowPhaseResult3D result;

		if (a.vertices.empty() || b.vertices.empty()) return result;

		// GJK で交差判定
		std::vector<sgc::Vec3f> simplex;
		if (!gjkIntersectWithSimplex(a, b, simplex))
		{
			return result;
		}

		// EPA で接触情報を抽出
		result.contact = epaContactInfo(a, b, simplex);
		return result;
	}

	// ── 凸形状構築ヘルパー ────────────────────────────────────

	/// @brief 球を凸形状として近似する（正多面体）
	/// @param sphere 球コライダー
	/// @param subdivisions 分割数（精度とコストのトレードオフ）
	/// @return 凸形状
	[[nodiscard]] static ConvexShape sphereToConvex(
		const SphereCollider& sphere, int subdivisions = 2) noexcept
	{
		ConvexShape shape;

		// 正八面体を基底にサブディビジョン
		const std::array<sgc::Vec3f, 6> baseVerts = {{
			sphere.center + sgc::Vec3f{sphere.radius, 0, 0},
			sphere.center + sgc::Vec3f{-sphere.radius, 0, 0},
			sphere.center + sgc::Vec3f{0, sphere.radius, 0},
			sphere.center + sgc::Vec3f{0, -sphere.radius, 0},
			sphere.center + sgc::Vec3f{0, 0, sphere.radius},
			sphere.center + sgc::Vec3f{0, 0, -sphere.radius},
		}};

		shape.vertices.assign(baseVerts.begin(), baseVerts.end());

		// 簡易サブディビジョン：中間点を球面に投影
		for (int s = 0; s < subdivisions; ++s)
		{
			const std::size_t currentSize = shape.vertices.size();
			for (std::size_t i = 0; i < currentSize; ++i)
			{
				for (std::size_t j = i + 1; j < currentSize; ++j)
				{
					sgc::Vec3f mid = (shape.vertices[i] + shape.vertices[j]) * 0.5f;
					const sgc::Vec3f dir = mid - sphere.center;
					const float len = dir.length();
					if (len > 1e-8f)
					{
						mid = sphere.center + dir * (sphere.radius / len);
					}
					shape.vertices.push_back(mid);
				}
			}
		}

		return shape;
	}

	/// @brief AABBを凸形状（8頂点）として構築する
	/// @param aabb AABBコライダー
	/// @return 凸形状
	[[nodiscard]] static ConvexShape aabbToConvex(const AABBCollider3D& aabb) noexcept
	{
		ConvexShape shape;
		shape.vertices = {
			{aabb.min.x, aabb.min.y, aabb.min.z},
			{aabb.max.x, aabb.min.y, aabb.min.z},
			{aabb.min.x, aabb.max.y, aabb.min.z},
			{aabb.max.x, aabb.max.y, aabb.min.z},
			{aabb.min.x, aabb.min.y, aabb.max.z},
			{aabb.max.x, aabb.min.y, aabb.max.z},
			{aabb.min.x, aabb.max.y, aabb.max.z},
			{aabb.max.x, aabb.max.y, aabb.max.z},
		};
		return shape;
	}

private:
	/// @brief GJK交差判定（シンプレックスを返す版）
	/// @param a 凸形状A
	/// @param b 凸形状B
	/// @param outSimplex 出力シンプレックス（4頂点の四面体）
	/// @return 交差している場合 true
	[[nodiscard]] bool gjkIntersectWithSimplex(
		const ConvexShape& a, const ConvexShape& b,
		std::vector<sgc::Vec3f>& outSimplex) const noexcept
	{
		sgc::Vec3f direction{1.0f, 0.0f, 0.0f};

		outSimplex.clear();
		outSimplex.push_back(gjkSupport(a, b, direction));

		direction = -outSimplex[0];
		if (direction.lengthSquared() < 1e-10f) return true;

		for (int iter = 0; iter < m_maxIterations; ++iter)
		{
			const sgc::Vec3f newPoint = gjkSupport(a, b, direction);

			if (newPoint.dot(direction) < 0.0f) return false;

			outSimplex.push_back(newPoint);

			if (processSimplex(outSimplex, direction))
			{
				return true;
			}
		}

		return false;
	}

	/// @brief シンプレックスを処理し、原点を含むかチェックする
	/// @param simplex シンプレックス頂点列（変更される）
	/// @param direction 次の探索方向（変更される）
	/// @return 原点がシンプレックス内にある場合 true
	[[nodiscard]] static bool processSimplex(
		std::vector<sgc::Vec3f>& simplex, sgc::Vec3f& direction) noexcept
	{
		const std::size_t size = simplex.size();

		if (size == 2)
		{
			return processLine(simplex, direction);
		}
		if (size == 3)
		{
			return processTriangle(simplex, direction);
		}
		if (size == 4)
		{
			return processTetrahedron(simplex, direction);
		}

		return false;
	}

	/// @brief 線分シンプレックスを処理する
	[[nodiscard]] static bool processLine(
		std::vector<sgc::Vec3f>& simplex, sgc::Vec3f& direction) noexcept
	{
		const sgc::Vec3f& a = simplex[1]; // 最新点
		const sgc::Vec3f& b = simplex[0];

		const sgc::Vec3f ab = b - a;
		const sgc::Vec3f ao = -a;

		if (ab.dot(ao) > 0.0f)
		{
			direction = tripleProduct(ab, ao, ab);
			if (direction.lengthSquared() < 1e-10f) return true;
		}
		else
		{
			simplex = {a};
			direction = ao;
		}

		return false;
	}

	/// @brief 三角形シンプレックスを処理する
	[[nodiscard]] static bool processTriangle(
		std::vector<sgc::Vec3f>& simplex, sgc::Vec3f& direction) noexcept
	{
		const sgc::Vec3f& a = simplex[2]; // 最新点
		const sgc::Vec3f& b = simplex[1];
		const sgc::Vec3f& c = simplex[0];

		const sgc::Vec3f ab = b - a;
		const sgc::Vec3f ac = c - a;
		const sgc::Vec3f ao = -a;
		const sgc::Vec3f abcNormal = ab.cross(ac);

		if (abcNormal.cross(ac).dot(ao) > 0.0f)
		{
			if (ac.dot(ao) > 0.0f)
			{
				simplex = {c, a};
				direction = tripleProduct(ac, ao, ac);
				if (direction.lengthSquared() < 1e-10f) return true;
			}
			else
			{
				simplex = {b, a};
				return processLine(simplex, direction);
			}
		}
		else if (ab.cross(abcNormal).dot(ao) > 0.0f)
		{
			simplex = {b, a};
			return processLine(simplex, direction);
		}
		else
		{
			if (abcNormal.dot(ao) > 0.0f)
			{
				direction = abcNormal;
			}
			else
			{
				simplex = {b, c, a};
				direction = -abcNormal;
			}
		}

		return false;
	}

	/// @brief 四面体シンプレックスを処理する
	[[nodiscard]] static bool processTetrahedron(
		std::vector<sgc::Vec3f>& simplex, sgc::Vec3f& direction) noexcept
	{
		const sgc::Vec3f& a = simplex[3]; // 最新点
		const sgc::Vec3f& b = simplex[2];
		const sgc::Vec3f& c = simplex[1];
		const sgc::Vec3f& d = simplex[0];

		const sgc::Vec3f ab = b - a;
		const sgc::Vec3f ac = c - a;
		const sgc::Vec3f ad = d - a;
		const sgc::Vec3f ao = -a;

		const sgc::Vec3f abcNormal = ab.cross(ac);
		const sgc::Vec3f acdNormal = ac.cross(ad);
		const sgc::Vec3f adbNormal = ad.cross(ab);

		if (abcNormal.dot(ao) > 0.0f)
		{
			simplex = {c, b, a};
			direction = abcNormal;
			return false;
		}

		if (acdNormal.dot(ao) > 0.0f)
		{
			simplex = {d, c, a};
			direction = acdNormal;
			return false;
		}

		if (adbNormal.dot(ao) > 0.0f)
		{
			simplex = {b, d, a};
			direction = adbNormal;
			return false;
		}

		// 原点は四面体内部
		return true;
	}

	/// @brief EPA による接触情報の抽出
	/// @param a 凸形状A
	/// @param b 凸形状B
	/// @param simplex GJK から得た四面体シンプレックス
	/// @return 接触情報
	[[nodiscard]] ContactInfo3D epaContactInfo(
		const ConvexShape& a, const ConvexShape& b,
		const std::vector<sgc::Vec3f>& simplex) const noexcept
	{
		ContactInfo3D result;
		result.hasContact = true;

		if (simplex.size() < 4)
		{
			// 退化ケース：簡易的な結果を返す
			result.depth = 0.001f;
			result.normal = sgc::Vec3f::unitY();
			return result;
		}

		// ポリトープを初期化（四面体の4面）
		std::vector<EpaFace> faces;
		faces.reserve(64);

		auto addFace = [&faces](const sgc::Vec3f& v0, const sgc::Vec3f& v1,
			const sgc::Vec3f& v2)
		{
			EpaFace face;
			face.vertices = {v0, v1, v2};
			face.normal = (v1 - v0).cross(v2 - v0);
			const float len = face.normal.length();
			if (len > 1e-8f)
			{
				face.normal = face.normal / len;
			}
			else
			{
				face.normal = sgc::Vec3f::unitY();
			}
			face.distance = face.normal.dot(v0);
			if (face.distance < 0.0f)
			{
				face.normal = -face.normal;
				face.distance = -face.distance;
				std::swap(face.vertices[1], face.vertices[2]);
			}
			faces.push_back(face);
		};

		addFace(simplex[0], simplex[1], simplex[2]);
		addFace(simplex[0], simplex[2], simplex[3]);
		addFace(simplex[0], simplex[3], simplex[1]);
		addFace(simplex[1], simplex[3], simplex[2]);

		for (int iter = 0; iter < m_maxIterations; ++iter)
		{
			if (faces.empty()) break;

			// 最も原点に近い面を見つける
			std::size_t closestIdx = 0;
			float closestDist = faces[0].distance;

			for (std::size_t i = 1; i < faces.size(); ++i)
			{
				if (faces[i].distance < closestDist)
				{
					closestDist = faces[i].distance;
					closestIdx = i;
				}
			}

			const sgc::Vec3f searchDir = faces[closestIdx].normal;

			// ミンコフスキー差上の新しいサポート点
			const sgc::Vec3f newPoint = gjkSupport(a, b, searchDir);
			const float newDist = newPoint.dot(searchDir);

			// 収束判定
			if (newDist - closestDist < 1e-4f)
			{
				result.normal = faces[closestIdx].normal;
				result.depth = closestDist;
				result.point = result.normal * result.depth * 0.5f;
				return result;
			}

			// 新しい点が見える面を削除し、新しい面を追加
			std::vector<std::pair<sgc::Vec3f, sgc::Vec3f>> edges;

			for (std::size_t i = faces.size(); i > 0; --i)
			{
				const std::size_t fi = i - 1;
				if (faces[fi].normal.dot(newPoint - faces[fi].vertices[0]) > 0.0f)
				{
					// この面は新しい点から見える → エッジを記録して削除
					addEdge(edges, faces[fi].vertices[0], faces[fi].vertices[1]);
					addEdge(edges, faces[fi].vertices[1], faces[fi].vertices[2]);
					addEdge(edges, faces[fi].vertices[2], faces[fi].vertices[0]);

					faces[fi] = faces.back();
					faces.pop_back();
				}
			}

			// エッジから新しい面を構築
			for (const auto& [edgeA, edgeB] : edges)
			{
				addFace(edgeA, edgeB, newPoint);
			}
		}

		// 最大反復回数に達した場合、最も近い面の情報を返す
		if (!faces.empty())
		{
			std::size_t closestIdx = 0;
			float closestDist = faces[0].distance;
			for (std::size_t i = 1; i < faces.size(); ++i)
			{
				if (faces[i].distance < closestDist)
				{
					closestDist = faces[i].distance;
					closestIdx = i;
				}
			}
			result.normal = faces[closestIdx].normal;
			result.depth = closestDist;
			result.point = result.normal * result.depth * 0.5f;
		}

		return result;
	}

	/// @brief EPA用エッジ管理（共有エッジを除去する）
	/// @param edges エッジリスト
	/// @param a エッジ端点A
	/// @param b エッジ端点B
	static void addEdge(
		std::vector<std::pair<sgc::Vec3f, sgc::Vec3f>>& edges,
		const sgc::Vec3f& a, const sgc::Vec3f& b) noexcept
	{
		// 逆方向のエッジが既にあれば両方削除（共有エッジ）
		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if ((edges[i].first - b).lengthSquared() < 1e-8f &&
				(edges[i].second - a).lengthSquared() < 1e-8f)
			{
				edges[i] = edges.back();
				edges.pop_back();
				return;
			}
		}
		edges.emplace_back(a, b);
	}

	int m_maxIterations{64};  ///< GJK/EPAの最大反復回数
};

} // namespace mitiru::physics3d
