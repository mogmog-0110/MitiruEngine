#pragma once

/// @file NavQuery.hpp
/// @brief 読み込んだナビメッシュ (dtNavMesh) への問い合わせ。経路、近い点、raycast。mitiru_nav を link した target 用
/// NavMesh.hpp と DynamicNavMesh.hpp のナビメッシュに使う。
/// 作業域は固定長配列と init 時に確保する Detour の node pool だけを使い、問い合わせ時には確保しない。
/// Detour は乱数、時刻、スレッドを使わないため、同じナビメッシュと入力には同じビット列を返す。

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "sgc/math/Vec3.hpp"

namespace mitiru::nav
{

enum class NavPathStatus : std::uint8_t
{
	Found,     ///< 終点まで届いた
	Partial,   ///< 終点へは届かず、届く所で一番近い点まで
	NoStart,   ///< 始点の近くにナビメッシュが無い
	NoEnd,     ///< 終点の近くにナビメッシュが無い
	NotLoaded,
};

struct NavPathResult
{
	int pointCount = 0;   ///< out に書いた点の数 (始点と終点を含む)
	NavPathStatus status = NavPathStatus::NotLoaded;
};

/// @brief start から end までナビメッシュの面上を直進した結果。XZ 平面で判定する
struct NavRayHit
{
	bool ok = false;        ///< start の近くにナビメッシュがあり、調べられた
	bool hit = false;       ///< end の手前でナビメッシュの縁 (壁・穴・崖) に当たった
	float t = 1.0f;         ///< 当たった所の start→end の割合。当たらなければ 1
	sgc::Vec3f point{};     ///< 当たった所。当たらなければ end
	sgc::Vec3f normal{};    ///< 当たった縁の外向きの法線 (XZ)
};

class NavQuery
{
public:
	static constexpr int kMaxPathPolys = 256;
	static constexpr int kMaxNodes = 2048;

	NavQuery() { m_filter.setIncludeFlags(1); m_filter.setExcludeFlags(0); }
	NavQuery(const NavQuery&) = delete;
	NavQuery& operator=(const NavQuery&) = delete;

	/// @return 作れなければ false。mesh は NavQuery より長く生きること
	bool init(const dtNavMesh* mesh, int maxNodes = kMaxNodes)
	{
		m_query.reset(dtAllocNavMeshQuery());
		if (!m_query || dtStatusFailed(m_query->init(mesh, maxNodes)))
		{
			m_query.reset();
			return false;
		}
		return true;
	}

	void reset() noexcept { m_query.reset(); }
	[[nodiscard]] bool ready() const noexcept { return m_query != nullptr; }

	/// @brief nearest、findPath、raycast が点を探す箱の半径。既定は 2 x 4 x 2 m
	void setSearchExtents(const sgc::Vec3f& halfExtents) noexcept { m_extents = halfExtents; }

	/// @brief 歩ける面のフラグ。焼いた面はフラグ 1 のため、既定では 1 を含む面だけを通る
	[[nodiscard]] const dtQueryFilter& filter() const noexcept { return m_filter; }
	[[nodiscard]] dtNavMeshQuery* detour() const noexcept { return m_query.get(); }

	[[nodiscard]] std::optional<sgc::Vec3f> nearest(const sgc::Vec3f& p) const
	{
		if (!ready()) return std::nullopt;
		float pt[3];
		if (nearestRef(p, pt) == 0) return std::nullopt;
		return sgc::Vec3f{pt[0], pt[1], pt[2]};
	}

	/// @brief start から end までの角を out に書く。入りきらない分は切り捨てる
	NavPathResult findPath(const sgc::Vec3f& start, const sgc::Vec3f& end, std::span<sgc::Vec3f> out)
	{
		if (!ready() || out.empty()) return {0, NavPathStatus::NotLoaded};
		float spos[3], epos[3];
		const dtPolyRef sref = nearestRef(start, spos);
		if (sref == 0) return {0, NavPathStatus::NoStart};
		const dtPolyRef eref = nearestRef(end, epos);
		if (eref == 0) return {0, NavPathStatus::NoEnd};

		int npolys = 0;
		const dtStatus st = m_query->findPath(sref, eref, spos, epos, &m_filter, m_polys.data(), &npolys, kMaxPathPolys);
		if (dtStatusFailed(st) || npolys == 0) return {0, NavPathStatus::NoEnd};
		const bool partial = dtStatusDetail(st, DT_PARTIAL_RESULT) || m_polys[static_cast<std::size_t>(npolys - 1)] != eref;
		if (partial) m_query->closestPointOnPoly(m_polys[static_cast<std::size_t>(npolys - 1)], epos, epos, nullptr);

		const int maxOut = static_cast<int>(std::min<std::size_t>(out.size(), kMaxPathPolys));
		int count = 0;
		const dtStatus sst = m_query->findStraightPath(spos, epos, m_polys.data(), npolys, m_straight.data(),
			nullptr, nullptr, &count, maxOut);
		// out が足りず終点まで書けなければ、到達した扱いにしない
		const bool truncated = dtStatusDetail(sst, DT_BUFFER_TOO_SMALL);
		for (int i = 0; i < count; ++i)
		{
			const float* v = &m_straight[static_cast<std::size_t>(i) * 3];
			out[static_cast<std::size_t>(i)] = sgc::Vec3f{v[0], v[1], v[2]};
		}
		return {count, (partial || truncated) ? NavPathStatus::Partial : NavPathStatus::Found};
	}

	/// @brief start から end へ面上を直進し、縁に当たるか調べる。当たり判定の掃引に映らない床の穴や崖も縁として返す。
	/// ナビメッシュは agent の半径だけ縮めて焼くため、線で体の幅を含めて通れるか調べられる
	[[nodiscard]] NavRayHit raycast(const sgc::Vec3f& start, const sgc::Vec3f& end) const
	{
		NavRayHit r;
		r.point = end;
		if (!ready()) return r;
		float spos[3];
		const dtPolyRef sref = nearestRef(start, spos);
		if (sref == 0) return r;
		const float epos[3] = {end.x, end.y, end.z};
		float t = FLT_MAX;
		float n[3] = {0.0f, 0.0f, 0.0f};
		if (dtStatusFailed(m_query->raycast(sref, spos, epos, &m_filter, &t, n, nullptr, nullptr, 0))) return r;
		r.ok = true;
		if (t > 1.0f) return r;
		r.hit = true;
		r.t = t;
		r.point = sgc::Vec3f{spos[0] + (epos[0] - spos[0]) * t, spos[1] + (epos[1] - spos[1]) * t, spos[2] + (epos[2] - spos[2]) * t};
		r.normal = sgc::Vec3f{n[0], n[1], n[2]};
		return r;
	}

	/// @brief p に最も近い面の番号と面上の点。無ければ 0
	[[nodiscard]] dtPolyRef nearestRef(const sgc::Vec3f& p, float* outPt) const
	{
		const float c[3] = {p.x, p.y, p.z};
		const float ext[3] = {m_extents.x, m_extents.y, m_extents.z};
		dtPolyRef ref = 0;
		m_query->findNearestPoly(c, ext, &m_filter, &ref, outPt);
		return ref;
	}

private:
	struct QueryDeleter { void operator()(dtNavMeshQuery* p) const noexcept { dtFreeNavMeshQuery(p); } };

	std::unique_ptr<dtNavMeshQuery, QueryDeleter> m_query;
	dtQueryFilter m_filter;
	sgc::Vec3f m_extents{2.0f, 4.0f, 2.0f};
	std::array<dtPolyRef, kMaxPathPolys> m_polys{};
	std::array<float, kMaxPathPolys * 3> m_straight{};
};

} // namespace mitiru::nav
