#pragma once

/// @file BroadPhase3D.hpp
/// @brief 3Dブロードフェーズ衝突検出
///
/// Sort-and-Sweep (Sweep-and-Prune) アルゴリズムによる
/// 空間加速構造。X軸ソート後にY/Z軸のAABB重なりを検査し、
/// ナローフェーズに渡す候補ペアを高速に絞り込む。
///
/// @code
/// mitiru::physics3d::BroadPhase3D broadPhase;
/// broadPhase.update(proxies);
/// const auto& pairs = broadPhase.candidatePairs();
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

/// @brief ブロードフェーズ用プロキシ
///
/// 各コライダーのAABBバウンドとボディIDを保持する。
/// Sort-and-Sweep で X軸最小値を基準にソートされる。
struct BroadPhaseProxy3D
{
	BodyId bodyId{INVALID_BODY_ID};    ///< 紐づくボディID
	std::size_t colliderIndex{0};      ///< コライダー配列上のインデックス
	AABBCollider3D bounds{};           ///< 包含AABB

	/// @brief X軸の最小値を返す（ソート用）
	[[nodiscard]] constexpr float minX() const noexcept { return bounds.min.x; }

	/// @brief X軸の最大値を返す
	[[nodiscard]] constexpr float maxX() const noexcept { return bounds.max.x; }
};

/// @brief ブロードフェーズ候補ペア
struct BroadPhasePair3D
{
	std::size_t indexA{0};  ///< コライダーインデックスA
	std::size_t indexB{0};  ///< コライダーインデックスB
	BodyId bodyIdA{INVALID_BODY_ID};  ///< ボディID A
	BodyId bodyIdB{INVALID_BODY_ID};  ///< ボディID B
};

/// @brief Sort-and-Sweep ブロードフェーズ
///
/// 1軸（X軸）でプロキシをソートし、X軸方向で重なりがある間だけ
/// Y軸・Z軸の重なりを検査する。O(n log n + n * k) の計算量で
/// 候補ペアを生成する（kは平均重なり数）。
///
/// @details
/// - マージン（膨張量）を設定可能。コライダーが移動しても
///   すぐにペアが消えないようにする（コヒーレンス改善）。
/// - update() 呼び出しごとにプロキシリストを再ソートし、
///   候補ペアを再生成する。
class BroadPhase3D
{
public:
	/// @brief デフォルトコンストラクタ
	BroadPhase3D() = default;

	/// @brief マージンを指定して構築する
	/// @param margin AABBに加算する膨張量
	explicit BroadPhase3D(float margin) noexcept
		: m_margin(margin)
	{
	}

	// ── 設定 ──────────────────────────────────────────────────

	/// @brief マージン（AABB膨張量）を設定する
	/// @param margin 膨張量（全方向に適用）
	void setMargin(float margin) noexcept { m_margin = margin; }

	/// @brief マージンを取得する
	[[nodiscard]] constexpr float margin() const noexcept { return m_margin; }

	// ── プロキシ管理 ──────────────────────────────────────────

	/// @brief プロキシリストをクリアする
	void clear() noexcept
	{
		m_proxies.clear();
		m_pairs.clear();
	}

	/// @brief プロキシを追加する
	/// @param bodyId ボディID
	/// @param colliderIndex コライダー配列上のインデックス
	/// @param aabb 包含AABB
	void addProxy(BodyId bodyId, std::size_t colliderIndex,
		const AABBCollider3D& aabb) noexcept
	{
		BroadPhaseProxy3D proxy;
		proxy.bodyId = bodyId;
		proxy.colliderIndex = colliderIndex;
		proxy.bounds = inflate(aabb);
		m_proxies.push_back(proxy);
	}

	/// @brief Sort-and-Sweep を実行し候補ペアを生成する
	///
	/// @details 手順:
	///   1. X軸最小値でプロキシをソート
	///   2. 各プロキシについて、後続プロキシとX軸で重なる間ループ
	///   3. Y軸・Z軸でも重なりがあれば候補ペアとして登録
	///   4. 同一ボディのペアは除外
	void sweep() noexcept
	{
		m_pairs.clear();

		if (m_proxies.size() < 2) return;

		// X軸でソート
		std::sort(m_proxies.begin(), m_proxies.end(),
			[](const BroadPhaseProxy3D& a, const BroadPhaseProxy3D& b)
			{
				return a.minX() < b.minX();
			});

		// Sweep
		const std::size_t count = m_proxies.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto& proxyA = m_proxies[i];

			for (std::size_t j = i + 1; j < count; ++j)
			{
				const auto& proxyB = m_proxies[j];

				// X軸で離れていたら後続もすべて離れている
				if (proxyB.bounds.min.x > proxyA.bounds.max.x) break;

				// 同一ボディは除外
				if (proxyA.bodyId == proxyB.bodyId) continue;

				// Y軸の重なり判定
				if (proxyA.bounds.max.y < proxyB.bounds.min.y ||
					proxyA.bounds.min.y > proxyB.bounds.max.y) continue;

				// Z軸の重なり判定
				if (proxyA.bounds.max.z < proxyB.bounds.min.z ||
					proxyA.bounds.min.z > proxyB.bounds.max.z) continue;

				// 候補ペアとして登録
				BroadPhasePair3D pair;
				pair.indexA = proxyA.colliderIndex;
				pair.indexB = proxyB.colliderIndex;
				pair.bodyIdA = proxyA.bodyId;
				pair.bodyIdB = proxyB.bodyId;
				m_pairs.push_back(pair);
			}
		}
	}

	/// @brief 候補ペアのリストを取得する
	/// @return 候補ペアの参照
	[[nodiscard]] const std::vector<BroadPhasePair3D>& candidatePairs() const noexcept
	{
		return m_pairs;
	}

	/// @brief 候補ペア数を返す
	[[nodiscard]] std::size_t pairCount() const noexcept { return m_pairs.size(); }

	/// @brief プロキシ数を返す
	[[nodiscard]] std::size_t proxyCount() const noexcept { return m_proxies.size(); }

	// ── ユーティリティ ────────────────────────────────────────

	/// @brief コライダーからAABBを計算する（静的ヘルパー）
	/// @param type コライダータイプ
	/// @param sphere 球コライダー（Sphere時のみ使用）
	/// @param aabb AABBコライダー（AABB時のみ使用）
	/// @param capsule カプセルコライダー（Capsule時のみ使用）
	/// @return 包含AABB
	[[nodiscard]] static AABBCollider3D computeAABB(
		const SphereCollider& sphere) noexcept
	{
		const sgc::Vec3f r{sphere.radius, sphere.radius, sphere.radius};
		return {sphere.center - r, sphere.center + r};
	}

	/// @brief AABBコライダーの包含AABBを返す（そのまま返す）
	[[nodiscard]] static constexpr AABBCollider3D computeAABB(
		const AABBCollider3D& aabb) noexcept
	{
		return aabb;
	}

	/// @brief カプセルコライダーの包含AABBを計算する
	[[nodiscard]] static AABBCollider3D computeAABB(
		const CapsuleCollider& capsule) noexcept
	{
		const sgc::Vec3f r{capsule.radius, capsule.radius, capsule.radius};
		const sgc::Vec3f minPt = sgc::Vec3f::min(capsule.pointA, capsule.pointB) - r;
		const sgc::Vec3f maxPt = sgc::Vec3f::max(capsule.pointA, capsule.pointB) + r;
		return {minPt, maxPt};
	}

private:
	/// @brief AABBをマージン分膨張させる
	/// @param aabb 元のAABB
	/// @return 膨張後のAABB
	[[nodiscard]] AABBCollider3D inflate(const AABBCollider3D& aabb) const noexcept
	{
		if (m_margin <= 0.0f) return aabb;

		const sgc::Vec3f marginVec{m_margin, m_margin, m_margin};
		return {aabb.min - marginVec, aabb.max + marginVec};
	}

	float m_margin{0.0f};                         ///< AABB膨張量
	std::vector<BroadPhaseProxy3D> m_proxies;      ///< プロキシリスト
	std::vector<BroadPhasePair3D> m_pairs;         ///< 候補ペアリスト
};

} // namespace mitiru::physics3d
