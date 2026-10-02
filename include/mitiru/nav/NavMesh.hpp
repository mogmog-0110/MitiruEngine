#pragma once

/// @file NavMesh.hpp
/// @brief 焼いたナビメッシュ blob (.navmesh) を読み、経路を問い合わせる (Detour)。mitiru_nav を link した target 用
/// ゲーム DLL の中で完結する。host への intent も ModuleApi の追加も無い。blob は init で 1 度だけ読む
/// 読み取り専用のレベルデータで、テクスチャと同じく GameMemory の外に置く (中身はポインタを含むので
/// GameMemory に入れない)。GameMemory に持つのは経路の点や agent の位置だけ。
/// 確保は load の時だけ。問い合わせ (findPath / nearest / raycast) は NavQuery.hpp が固定長の作業域で答え、
/// 呼ぶたびに確保しない。同じ blob・同じ入力からは同じビット列の点が返る (Detour は乱数も時刻もスレッドも
/// 使わない) ので、rewind / replay で再計算しても経路はずれない。

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

#include "sgc/math/Vec3.hpp"
#include "mitiru/nav/NavMeshBlob.hpp"
#include "mitiru/nav/NavQuery.hpp"

namespace mitiru::nav
{

class NavMesh
{
public:
	static constexpr int kMaxPathPolys = NavQuery::kMaxPathPolys;
	static constexpr int kMaxNodes = NavQuery::kMaxNodes;

	NavMesh() = default;
	NavMesh(const NavMesh&) = delete;
	NavMesh& operator=(const NavMesh&) = delete;

	/// @brief blob を読む。失敗なら false と理由。読み直すと前の中身は捨てる
	bool load(std::span<const std::uint8_t> blob, std::string* error = nullptr)
	{
		m_query.reset();
		m_mesh.reset();
		const char* why = loadTiles(blob);
		if (why == nullptr && !m_query.init(m_mesh.get(), kMaxNodes)) why = "dtNavMeshQuery を作れない";
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

	[[nodiscard]] bool isLoaded() const noexcept { return m_query.ready(); }

	/// @brief nearest / findPath / raycast が点を探す箱の半径 (既定 2 x 4 x 2 m)
	void setSearchExtents(const sgc::Vec3f& halfExtents) noexcept { m_query.setSearchExtents(halfExtents); }

	[[nodiscard]] std::optional<sgc::Vec3f> nearest(const sgc::Vec3f& p) const { return m_query.nearest(p); }

	NavPathResult findPath(const sgc::Vec3f& start, const sgc::Vec3f& end, std::span<sgc::Vec3f> out)
	{
		return m_query.findPath(start, end, out);
	}

	[[nodiscard]] NavRayHit raycast(const sgc::Vec3f& start, const sgc::Vec3f& end) const { return m_query.raycast(start, end); }

	[[nodiscard]] NavQuery& query() noexcept { return m_query; }
	[[nodiscard]] const NavQuery& query() const noexcept { return m_query; }

private:
	struct MeshDeleter { void operator()(dtNavMesh* p) const noexcept { dtFreeNavMesh(p); } };

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

	std::unique_ptr<dtNavMesh, MeshDeleter> m_mesh;
	NavQuery m_query;   // m_mesh を指すので、m_mesh より先に壊れるよう後ろに置く
};

} // namespace mitiru::nav
