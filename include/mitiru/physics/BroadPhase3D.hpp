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
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/TracyZones.hpp"
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
	bool isStatic{false};              ///< 静的ボディ由来か（sweep() の再ソート省略判定に使う）

	/// @brief X軸の最小値を返す（ソート用）
	[[nodiscard]] constexpr float minX() const noexcept { return bounds.min.x; }

	/// @brief X軸の最大値を返す
	[[nodiscard]] constexpr float maxX() const noexcept { return bounds.max.x; }

	/// @brief 静的プロキシ申告が前フレームと同一か判定するための比較
	[[nodiscard]] constexpr bool operator==(const BroadPhaseProxy3D& rhs) const noexcept
	{
		return bodyId == rhs.bodyId && colliderIndex == rhs.colliderIndex &&
			isStatic == rhs.isStatic &&
			bounds.min == rhs.bounds.min && bounds.max == rhs.bounds.max;
	}
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

	/// @brief 動的プロキシと今フレームの静的プロキシ申告をクリアする
	///
	/// @details 静的プロキシの確定リスト（m_staticProxies、sweep() が実際にソートへ
	///          使う側）はここでは消さない。sweep() が m_staticPending との内容比較で
	///          再ソートの要否を判断する
	void clear() noexcept
	{
		// m_dynamicProxies 自体は消さない。毎フレーム clear+push_back で作り直す代わりに、
		// addProxy() がコライダー添字（申告順ではなくソート後に動く可能性のある物理識別子）で
		// 対応するスロットを引いて値だけ更新する。m_dynamicSeen は今フレーム
		// 申告されたスロットの印（未申告＝削除されたコライダー）
		m_dynamicSeen.assign(m_dynamicProxies.size(), false);
		m_staticPending.clear();
		m_pairs.clear();
	}

	/// @brief プロキシを追加する
	/// @param bodyId ボディID
	/// @param colliderIndex コライダー配列上のインデックス
	/// @param aabb 包含AABB
	/// @param isStatic 静的ボディ由来か。true なら sweep() 側のキャッシュ判定に回る
	void addProxy(BodyId bodyId, std::size_t colliderIndex,
		const AABBCollider3D& aabb, bool isStatic = false) noexcept
	{
		BroadPhaseProxy3D proxy;
		proxy.bodyId = bodyId;
		proxy.colliderIndex = colliderIndex;
		proxy.bounds = inflate(aabb);
		proxy.isStatic = isStatic;

		if (isStatic)
		{
			m_staticPending.push_back(proxy);
			return;
		}

		// 申告順（＝コライダー配列の添字順）は前フレームのソート順と一致する保証がない
		// （sweep() で minX 順に並べ替わるため）。colliderIndex をキーにスロットを引いて
		// 値だけ書き換えることで、配列内の位置（＝前フレームのソート順）を変えずに更新する
		if (colliderIndex >= m_dynamicSlotOf.size())
		{
			m_dynamicSlotOf.resize(colliderIndex + 1, kNoSlot);
		}

		const std::size_t existing = m_dynamicSlotOf[colliderIndex];
		if (existing != kNoSlot && existing < m_dynamicProxies.size())
		{
			m_dynamicProxies[existing].bodyId = proxy.bodyId;
			m_dynamicProxies[existing].bounds = proxy.bounds;
			m_dynamicSeen[existing] = true;
		}
		else
		{
			const std::size_t slot = m_dynamicProxies.size();
			m_dynamicProxies.push_back(proxy);
			m_dynamicSeen.push_back(true);
			m_dynamicSlotOf[colliderIndex] = slot;
		}
	}

	/// @brief Sort-and-Sweep を実行し候補ペアを生成する
	///
	/// @details 動的プロキシは毎フレームソートするが、静的プロキシは今フレームの
	///          申告内容（bodyId・colliderIndex・AABB）が m_staticProxies と一致する
	///          限りソート済み配列を使い回す。両者はそれぞれ独立にソート済みなので
	///          std::merge で線形にマージしてから、既存の1本のスイープと同じ要領で
	///          候補ペアを出す
	void sweep() noexcept
	{
		MITIRU_ZONE_PHYSICS("BroadPhase3D::sweep");

		m_pairs.clear();

		// m_staticProxies はソート後の並びなので、比較は申告順のまま持つ
		// m_staticPendingPrev に対して行う
		m_staticProxiesReused = (m_staticPending == m_staticPendingPrev);
		if (!m_staticProxiesReused)
		{
			m_staticProxies = m_staticPending;
			std::sort(m_staticProxies.begin(), m_staticProxies.end(),
				[](const BroadPhaseProxy3D& a, const BroadPhaseProxy3D& b) { return a.minX() < b.minX(); });
			m_staticPendingPrev = m_staticPending;
		}

		// 今フレーム申告されなかった（削除された）動的コライダーのスロットを取り除く。
		// addProxy() が既存スロットを値だけ更新する運用なので、通常フレーム（削除なし）は
		// この走査だけで済み、前フレームのソート順がそのまま保たれる
		if (std::any_of(m_dynamicSeen.begin(), m_dynamicSeen.end(), [](bool seen) { return !seen; }))
		{
			compactDynamicProxies();
		}

		// 動的プロキシの X 順入れ替わりは insertion sort の O(n) 想定より多いため、
		// O(n log n) の std::sort（イントロソート）の方が安定して速い。
		// kDynamic=5000 規模では本体を直接 sort する方が、軽量キー sort + gather の
		// SoA 化より速いため、本体を直接触る形に留める
		std::sort(m_dynamicProxies.begin(), m_dynamicProxies.end(),
			[](const BroadPhaseProxy3D& a, const BroadPhaseProxy3D& b) { return a.minX() < b.minX(); });

		// ソートで配列内の位置が変わったので、次フレームの addProxy() が正しいスロットを
		// 引けるようマッピングを追従させる（これを省くと次フレームの更新が別コライダーの
		// スロットに誤って書き込まれる）
		for (std::size_t i = 0; i < m_dynamicProxies.size(); ++i)
		{
			m_dynamicSlotOf[m_dynamicProxies[i].colliderIndex] = i;
		}

		m_merged.clear();
		m_merged.reserve(m_staticProxies.size() + m_dynamicProxies.size());
		std::merge(m_staticProxies.begin(), m_staticProxies.end(),
			m_dynamicProxies.begin(), m_dynamicProxies.end(),
			std::back_inserter(m_merged),
			[](const BroadPhaseProxy3D& a, const BroadPhaseProxy3D& b) { return a.minX() < b.minX(); });

		const std::size_t count = m_merged.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto& proxyA = m_merged[i];

			for (std::size_t j = i + 1; j < count; ++j)
			{
				const auto& proxyB = m_merged[j];

				// X軸で離れていたら後続もすべて離れている
				if (proxyB.bounds.min.x > proxyA.bounds.max.x) break;

				// 同一ボディは除外。static同士は互いに動かないが、トリガー/センサー用途では
				// static-static の接触イベントも成立しうるため候補ペアからは除外しない
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

	/// @brief プロキシ数を返す（静的+動的の合計）
	[[nodiscard]] std::size_t proxyCount() const noexcept
	{
		// sweep() が未申告スロットを取り除いた後の値。sweep() 前に呼ぶと
		// 削除されたコライダー分だけ多く数える
		return m_staticPending.size() + m_dynamicProxies.size();
	}

	/// @brief 静的プロキシの再ソートを省略できたか（直近の sweep() 時点）
	[[nodiscard]] constexpr bool staticProxiesReused() const noexcept { return m_staticProxiesReused; }

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
	/// @brief 今フレーム申告のなかった動的スロットを削除する（swap-and-pop）
	/// @details 削除されたスロットの colliderIndex マッピングは無効化する（そうしないと
	///          その colliderIndex が将来別のコライダーに再割当てされたとき、compaction 後に
	///          無関係なスロットへ誤って値を書き込む）。末尾要素を詰めた位置のマッピングは
	///          新しいスロット番号へ張り替える
	void compactDynamicProxies() noexcept
	{
		std::size_t i = 0;
		while (i < m_dynamicProxies.size())
		{
			if (m_dynamicSeen[i])
			{
				++i;
				continue;
			}

			m_dynamicSlotOf[m_dynamicProxies[i].colliderIndex] = kNoSlot;

			const std::size_t last = m_dynamicProxies.size() - 1;
			m_dynamicProxies[i] = m_dynamicProxies[last];
			m_dynamicSeen[i] = m_dynamicSeen[last];
			m_dynamicProxies.pop_back();
			m_dynamicSeen.pop_back();

			if (i < m_dynamicProxies.size())
			{
				m_dynamicSlotOf[m_dynamicProxies[i].colliderIndex] = i;
			}
			// i は据え置き、移動してきた要素を再チェックする
		}
	}

	/// @brief AABBをマージン分膨張させる
	/// @param aabb 元のAABB
	/// @return 膨張後のAABB
	[[nodiscard]] AABBCollider3D inflate(const AABBCollider3D& aabb) const noexcept
	{
		if (m_margin <= 0.0f) return aabb;

		const sgc::Vec3f marginVec{m_margin, m_margin, m_margin};
		return {aabb.min - marginVec, aabb.max + marginVec};
	}

	/// @brief m_dynamicSlotOf の「未割当」を表す番兵値
	static constexpr std::size_t kNoSlot = (std::numeric_limits<std::size_t>::max)();

	float m_margin{0.0f};                                  ///< AABB膨張量
	std::vector<BroadPhaseProxy3D> m_dynamicProxies;       ///< 動的プロキシ。フレームをまたいで保持し、
	                                                        ///< addProxy() がスロット単位で値だけ更新する
	std::vector<std::size_t> m_dynamicSlotOf;              ///< colliderIndex → m_dynamicProxies 上のスロット番号
	std::vector<bool> m_dynamicSeen;                       ///< 今フレーム addProxy() で更新されたスロットか
	std::vector<BroadPhaseProxy3D> m_staticPending;        ///< 今フレーム申告された静的プロキシ（未ソート）
	std::vector<BroadPhaseProxy3D> m_staticPendingPrev;    ///< 直近ソートを行った時点の申告内容（変化検知用）
	std::vector<BroadPhaseProxy3D> m_staticProxies;        ///< ソート済み静的プロキシ（sweep() が使う側）
	std::vector<BroadPhaseProxy3D> m_merged;               ///< static/dynamic のマージ作業バッファ
	std::vector<BroadPhasePair3D> m_pairs;                 ///< 候補ペアリスト
	bool m_staticProxiesReused{false};                     ///< 直近の sweep() で静的の再ソートを省けたか
};

} // namespace mitiru::physics3d
