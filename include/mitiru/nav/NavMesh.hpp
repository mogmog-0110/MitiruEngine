#pragma once

/// @file NavMesh.hpp
/// @brief 焼いたナビメッシュ blob (.navmesh) を読み、経路を問い合わせる (Detour)。mitiru_nav を link した target 用
/// ゲーム DLL の中で完結する。host への intent も ModuleApi の追加も無い。blob は init で 1 度だけ読む
/// 読み取り専用のレベルデータで、テクスチャと同じく GameMemory の外に置く (中身はポインタを含むので
/// GameMemory に入れない)。GameMemory に持つのは経路の点や agent の位置だけ。
/// 確保は load の時だけ。findPath / nearest は作業域を NavMesh 自身の固定長配列と Detour の node pool
/// (load 時に確保) で賄い、呼ぶたびに確保しない。同じ blob・同じ入力からは同じビット列の点が返る
/// (Detour は乱数も時刻もスレッドも使わない) ので、rewind / replay で再計算しても経路はずれない。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "sgc/math/Vec3.hpp"
#include "mitiru/nav/NavMeshBlob.hpp"

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

class NavMesh
{
public:
	static constexpr int kMaxPathPolys = 256;
	static constexpr int kMaxNodes = 2048;

	NavMesh() { m_filter.setIncludeFlags(1); m_filter.setExcludeFlags(0); }
	NavMesh(const NavMesh&) = delete;
	NavMesh& operator=(const NavMesh&) = delete;

	/// @brief blob を読む。失敗なら false と理由。読み直すと前の中身は捨てる
	bool load(std::span<const std::uint8_t> blob, std::string* error = nullptr)
	{
		m_query.reset();
		m_mesh.reset();
		const char* why = loadTiles(blob);
		if (why == nullptr) why = initQuery();
		if (why != nullptr)
		{
			m_query.reset();
			m_mesh.reset();
			if (error != nullptr) *error = why;
			return false;
		}
		return true;
	}

	bool loadFile(const char* path, std::string* error = nullptr)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
		{
			// load と同じく、失敗したら前の中身も捨てる (古い経路を答え続けない)
			m_query.reset();
			m_mesh.reset();
			if (error != nullptr) *error = std::string("ナビメッシュを開けない: ") + path;
			return false;
		}
		const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		return load(bytes, error);
	}

	[[nodiscard]] bool isLoaded() const noexcept { return m_query != nullptr; }

	/// @brief nearest / findPath が点を探す箱の半径 (既定 2 x 4 x 2 m)
	void setSearchExtents(const sgc::Vec3f& halfExtents) noexcept { m_extents = halfExtents; }

	/// @brief p に一番近いナビメッシュ上の点
	[[nodiscard]] std::optional<sgc::Vec3f> nearest(const sgc::Vec3f& p) const
	{
		if (!isLoaded()) return std::nullopt;
		float pt[3];
		const dtPolyRef ref = nearestRef(p, pt);
		if (ref == 0) return std::nullopt;
		return sgc::Vec3f{pt[0], pt[1], pt[2]};
	}

	/// @brief start から end までの折れ線 (角だけ) を out に書く。out に入りきらない分は切り捨てる
	NavPathResult findPath(const sgc::Vec3f& start, const sgc::Vec3f& end, std::span<sgc::Vec3f> out)
	{
		if (!isLoaded() || out.empty()) return {0, NavPathStatus::NotLoaded};
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
		// out が足りず終点まで書けなかった折れ線は、届いた扱いにしない
		const bool truncated = dtStatusDetail(sst, DT_BUFFER_TOO_SMALL);
		for (int i = 0; i < count; ++i)
		{
			const float* v = &m_straight[static_cast<std::size_t>(i) * 3];
			out[static_cast<std::size_t>(i)] = sgc::Vec3f{v[0], v[1], v[2]};
		}
		return {count, (partial || truncated) ? NavPathStatus::Partial : NavPathStatus::Found};
	}

private:
	struct MeshDeleter { void operator()(dtNavMesh* p) const noexcept { dtFreeNavMesh(p); } };
	struct QueryDeleter { void operator()(dtNavMeshQuery* p) const noexcept { dtFreeNavMeshQuery(p); } };

	[[nodiscard]] dtPolyRef nearestRef(const sgc::Vec3f& p, float* outPt) const
	{
		const float c[3] = {p.x, p.y, p.z};
		const float ext[3] = {m_extents.x, m_extents.y, m_extents.z};
		dtPolyRef ref = 0;
		m_query->findNearestPoly(c, ext, &m_filter, &ref, outPt);
		return ref;
	}

	[[nodiscard]] const char* loadTiles(std::span<const std::uint8_t> blob)
	{
		NavBlobHeader h;
		if (blob.size() < sizeof(h)) return "ナビメッシュが短すぎる";
		std::memcpy(&h, blob.data(), sizeof(h));
		if (h.magic != kNavBlobMagic) return "ナビメッシュ (.navmesh) ではない";
		if (h.version != kNavBlobVersion) return "ナビメッシュの版が違う (mitiru_navbake で焼き直す)";

		dtNavMeshParams params{};
		std::memcpy(params.orig, h.origin, sizeof(params.orig));
		params.tileWidth = h.tileWidth;
		params.tileHeight = h.tileHeight;
		params.maxTiles = static_cast<int>(h.maxTiles);
		params.maxPolys = static_cast<int>(h.maxPolysPerTile);
		m_mesh.reset(dtAllocNavMesh());
		if (!m_mesh || dtStatusFailed(m_mesh->init(&params))) return "dtNavMesh を作れない";

		std::size_t at = sizeof(h);
		for (std::uint32_t i = 0; i < h.tileCount; ++i)
		{
			if (const char* why = addTile(blob, at)) return why;
		}
		return nullptr;
	}

	[[nodiscard]] const char* addTile(std::span<const std::uint8_t> blob, std::size_t& at)
	{
		NavBlobTile t;
		if (at > blob.size() || blob.size() - at < sizeof(t)) return "ナビメッシュが途中で切れている";
		std::memcpy(&t, blob.data() + at, sizeof(t));
		at += sizeof(t);
		if (blob.size() - at < t.dataSize) return "ナビメッシュが途中で切れている";
		auto* data = static_cast<unsigned char*>(dtAlloc(t.dataSize, DT_ALLOC_PERM));
		if (data == nullptr) return "ナビメッシュのメモリが取れない";
		std::memcpy(data, blob.data() + at, t.dataSize);
		at += navBlobPadded(t.dataSize);
		// DT_TILE_FREE_DATA: data の持ち主は以後 dtNavMesh (失敗時はここで返す)
		if (dtStatusFailed(m_mesh->addTile(data, static_cast<int>(t.dataSize), DT_TILE_FREE_DATA, 0, nullptr)))
		{
			dtFree(data);
			return "タイルを足せない (Detour の版が焼いた時と違う可能性)";
		}
		return nullptr;
	}

	[[nodiscard]] const char* initQuery()
	{
		m_query.reset(dtAllocNavMeshQuery());
		if (!m_query || dtStatusFailed(m_query->init(m_mesh.get(), kMaxNodes))) return "dtNavMeshQuery を作れない";
		return nullptr;
	}

	std::unique_ptr<dtNavMesh, MeshDeleter> m_mesh;
	std::unique_ptr<dtNavMeshQuery, QueryDeleter> m_query;
	dtQueryFilter m_filter;
	sgc::Vec3f m_extents{2.0f, 4.0f, 2.0f};
	std::array<dtPolyRef, kMaxPathPolys> m_polys{};
	std::array<float, kMaxPathPolys * 3> m_straight{};
};

} // namespace mitiru::nav
