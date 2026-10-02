#pragma once

/// @file DynamicNavMesh.hpp
/// @brief 障害物 (扉、壊せる壁) の周囲のタイルを作り直すナビメッシュ。mitiru_nav をリンクしたターゲット用
/// @details .navcache (NavCacheBake.hpp)を読み、最大 kMaxObstacles 個の NavObstacle を sync() で反映する。
/// 問い合わせには query() (NavQuery.hpp)を使う。NavCrowd.hpp の群衆もこのナビメッシュ上を歩く。
///
/// 巻き戻しで同じ形に戻せるよう、Detour のナビメッシュは各タイルのデータと世代だけで決まる。
/// - タイルのデータは、焼いた層とそのタイルに掛かる有効な障害物だけで決まり、障害物を置く順序には左右されない
/// - 変わったタイルは世代を 1 つ上げ、世代から決まる salt で番号を付ける。古い番号の経路は無効になる
/// - ナビメッシュが変わるたびに全タイルを外し、層の順に同じ番号で足し直す。隣のタイルとの接続の順序と
///   番号の空きは、それまでの変更履歴に左右されない
///
/// 状態 (障害物、世代、変更回数) は writeImage / readImage で bytes にする。readImage は障害物からタイルを
/// 作り直し、世代を戻して同じ手順で組むため、記録時と同じナビメッシュになる。
/// メモリの確保は load と、障害物の変更でタイルを作り直すときだけ行う。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>

#include "mitiru/nav/NavCacheBlob.hpp"
#include "mitiru/nav/NavObstacle.hpp"
#include "mitiru/nav/NavQuery.hpp"
#include "mitiru/nav/detail/NavCacheTile.hpp"

namespace mitiru::nav
{

class DynamicNavMesh
{
public:
	static constexpr int kMaxObstacles = 64;

	DynamicNavMesh() = default;
	DynamicNavMesh(const DynamicNavMesh&) = delete;
	DynamicNavMesh& operator=(const DynamicNavMesh&) = delete;

	/// @brief .navcache を読み、障害物のないナビメッシュを組む。読み直すと以前の内容と障害物を捨てる
	bool load(std::span<const std::uint8_t> blob, std::string* error = nullptr)
	{
		clear();
		const char* why = parse(blob);
		if (why == nullptr) why = buildAll();
		if (why == nullptr) why = initMesh();
		if (why == nullptr) why = compose();
		if (why != nullptr)
		{
			clear();
			if (error != nullptr) *error = why;
			return false;
		}
		m_levelHash = fnv1a(blob);
		return true;
	}

	bool loadFile(const char* path, std::string* error = nullptr)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
		{
			clear();
			if (error != nullptr) *error = std::string("ナビメッシュの元 (.navcache) を開けない: ") + path;
			return false;
		}
		const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		return load(bytes, error);
	}

	[[nodiscard]] bool isLoaded() const noexcept { return m_query.ready(); }
	[[nodiscard]] const NavCacheHeader& info() const noexcept { return m_header; }
	[[nodiscard]] NavQuery& query() noexcept { return m_query; }
	[[nodiscard]] const NavQuery& query() const noexcept { return m_query; }
	[[nodiscard]] dtNavMesh* detour() noexcept { return m_mesh.get(); }

	/// @brief slot の障害物を置き換える。次の sync() で反映する
	void setObstacle(int slot, const NavObstacle& o) noexcept
	{
		if (slot >= 0 && slot < kMaxObstacles) m_desired[static_cast<std::size_t>(slot)] = o;
	}

	void setObstacleEnabled(int slot, bool on) noexcept
	{
		if (slot >= 0 && slot < kMaxObstacles) m_desired[static_cast<std::size_t>(slot)].enabled = on ? 1 : 0;
	}

	[[nodiscard]] const NavObstacle& obstacle(int slot) const noexcept { return m_desired[static_cast<std::size_t>(std::clamp(slot, 0, kMaxObstacles - 1))]; }

	/// @brief 障害物をタイルへ反映する。ナビメッシュが変わると epoch を 1 つ進めて true を返す
	/// タイルを作れなければ前の形のまま false を返し (error() に理由)、次の sync でもう一度試す
	bool sync()
	{
		if (!isLoaded() || std::memcmp(m_desired, m_applied, sizeof(m_desired)) == 0) return false;
		if (!buildChanged(m_desired)) return false;
		std::memcpy(m_applied, m_desired, sizeof(m_applied));
		if (m_pending.empty()) return false;
		for (auto& [i, data] : m_pending)
		{
			m_tiles[i].swap(data);
			++m_gen[i];
		}
		m_pending.clear();
		m_error = compose();
		++m_epoch;
		return true;
	}

	/// @brief 直前の sync / readImage がタイルを作れなかった、または組めなかった理由。無ければ nullptr
	[[nodiscard]] const char* error() const noexcept { return m_error; }

	/// @brief ナビメッシュの変更回数。値が変わらない間は、以前に取得した面の番号も有効
	[[nodiscard]] std::uint32_t epoch() const noexcept { return m_epoch; }
	[[nodiscard]] std::uint32_t generation(int layer) const noexcept { return m_gen[static_cast<std::size_t>(layer)]; }
	[[nodiscard]] int layerCount() const noexcept { return static_cast<int>(m_layers.size()); }

	// ── 状態の bytes (NavCrowd の窓口用) ───────────────────────────────

	[[nodiscard]] std::size_t imageSize() const noexcept { return sizeof(ImageHeader) + sizeof(m_applied) * 2 + m_gen.size() * 4; }

	void writeImage(std::uint8_t* dst) const noexcept
	{
		const ImageHeader h{kImageMagic, static_cast<std::uint32_t>(m_layers.size()), kMaxObstacles, m_epoch, m_levelHash};
		dst = put(dst, &h, sizeof(h));
		dst = put(dst, m_applied, sizeof(m_applied));
		dst = put(dst, m_desired, sizeof(m_desired));
		put(dst, m_gen.data(), m_gen.size() * 4);
	}

	/// @brief writeImage の bytes から状態を戻す。別のレベルや形の bytes なら何も変えずに false を返す
	bool readImage(std::span<const std::uint8_t> src)
	{
		ImageHeader h{};
		if (!isLoaded() || src.size() != imageSize()) return false;
		std::memcpy(&h, src.data(), sizeof(h));
		if (h.magic != kImageMagic || h.layerCount != m_layers.size() || h.obstacleCount != kMaxObstacles || h.levelHash != m_levelHash) return false;
		const std::uint8_t* p = src.data() + sizeof(h);
		NavObstacle applied[kMaxObstacles];
		std::memcpy(applied, p, sizeof(applied));
		if (!buildChanged(applied)) return false;
		std::memcpy(m_applied, applied, sizeof(m_applied));
		std::memcpy(m_desired, p + sizeof(applied), sizeof(m_desired));
		bool changed = !m_pending.empty();
		for (auto& [i, data] : m_pending) m_tiles[i].swap(data);
		m_pending.clear();
		const std::uint8_t* gens = p + sizeof(applied) * 2;
		if (std::memcmp(gens, m_gen.data(), m_gen.size() * 4) != 0)
		{
			std::memcpy(m_gen.data(), gens, m_gen.size() * 4);
			changed = true;
		}
		if (changed) m_error = compose();
		m_epoch = h.epoch;
		return true;
	}

	[[nodiscard]] std::uint64_t levelHash() const noexcept { return m_levelHash; }

private:
	struct MeshDeleter { void operator()(dtNavMesh* p) const noexcept { dtFreeNavMesh(p); } };
	struct ImageHeader
	{
		std::uint32_t magic;
		std::uint32_t layerCount;
		std::uint32_t obstacleCount;
		std::uint32_t epoch;
		std::uint64_t levelHash;
	};
	static constexpr std::uint32_t kImageMagic = 0x4E59444Du;   // "MDYN"

	static std::uint8_t* put(std::uint8_t* dst, const void* src, std::size_t n) noexcept
	{
		std::memcpy(dst, src, n);
		return dst + n;
	}

	static std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint64_t h = 0xCBF29CE484222325ull;
		for (const std::uint8_t b : bytes) h = (h ^ b) * 0x100000001B3ull;
		return h;
	}

	void clear()
	{
		m_query.reset();
		m_mesh.reset();
		m_layers.clear();
		m_layerBounds.clear();
		m_tiles.clear();
		m_gen.clear();
		m_dirty.clear();
		m_pending.clear();
		m_error = nullptr;
		std::memset(m_desired, 0, sizeof(m_desired));
		std::memset(m_applied, 0, sizeof(m_applied));
		m_epoch = 0;
		m_levelHash = 0;
	}

	[[nodiscard]] const char* parse(std::span<const std::uint8_t> blob)
	{
		if (blob.size() < sizeof(m_header)) return "ナビメッシュの元が短すぎる";
		std::memcpy(&m_header, blob.data(), sizeof(m_header));
		if (m_header.magic != kNavCacheMagic) return "ナビメッシュの元 (.navcache) ではない";
		if (m_header.version != kNavCacheVersion) return "ナビメッシュの元の版が違う (焼き直す)";
		if (m_header.layerCount == 0 || m_header.layerCount > m_header.maxTiles) return "ナビメッシュの元の層の数が不正";
		std::size_t at = sizeof(m_header);
		for (std::uint32_t i = 0; i < m_header.layerCount; ++i)
		{
			NavBlobTile t;
			if (at > blob.size() || blob.size() - at < sizeof(t)) return "ナビメッシュの元が途中で切れている";
			std::memcpy(&t, blob.data() + at, sizeof(t));
			at += sizeof(t);
			if (blob.size() - at < t.dataSize || t.dataSize < sizeof(dtTileCacheLayerHeader)) return "ナビメッシュの元が途中で切れている";
			m_layers.emplace_back(blob.begin() + static_cast<std::ptrdiff_t>(at), blob.begin() + static_cast<std::ptrdiff_t>(at + t.dataSize));
			at += navBlobPadded(t.dataSize);
			dtTileCacheLayerHeader lh;
			std::memcpy(&lh, m_layers.back().data(), sizeof(lh));
			if (lh.magic != DT_TILECACHE_MAGIC || lh.version != DT_TILECACHE_VERSION) return "層の版が違う (Detour の版が焼いた時と違う可能性)";
			detail::NavBounds b;
			std::memcpy(b.lo, lh.bmin, sizeof(b.lo));
			std::memcpy(b.hi, lh.bmax, sizeof(b.hi));
			m_layerBounds.push_back(b);
		}
		m_tiles.resize(m_layers.size());
		m_gen.assign(m_layers.size(), 0);
		m_dirty.assign(m_layers.size(), 0);
		return nullptr;
	}

	[[nodiscard]] const char* buildAll()
	{
		for (std::size_t i = 0; i < m_layers.size(); ++i)
		{
			if (!detail::buildCacheTile(m_layers[i], m_header, m_applied, m_tiles[i])) return "層からタイルを作れない";
		}
		return nullptr;
	}

	[[nodiscard]] const char* initMesh()
	{
		dtNavMeshParams params{};
		std::memcpy(params.orig, m_header.origin, sizeof(params.orig));
		params.tileWidth = params.tileHeight = m_header.tileWidth;
		params.maxTiles = static_cast<int>(m_header.maxTiles);
		params.maxPolys = static_cast<int>(m_header.maxPolysPerTile);
		m_mesh.reset(dtAllocNavMesh());
		if (!m_mesh || dtStatusFailed(m_mesh->init(&params))) return "dtNavMesh を作れない";
		m_saltMask = (1u << saltBits(params)) - 1u;
		if (!m_query.init(m_mesh.get())) return "dtNavMeshQuery を作れない";
		return nullptr;
	}

	/// @brief dtNavMesh::init と同じ方法で求める salt の bit 数。32 bit の面番号の残りを使う
	[[nodiscard]] static unsigned saltBits(const dtNavMeshParams& p) noexcept
	{
		const unsigned tileBits = dtIlog2(dtNextPow2(static_cast<unsigned>(p.maxTiles)));
		const unsigned polyBits = dtIlog2(dtNextPow2(static_cast<unsigned>(p.maxPolys)));
		return std::min(31u, 32u - tileBits - polyBits);
	}

	/// @brief 世代から salt を求める。Detour が使わない 0 は飛ばす
	[[nodiscard]] unsigned saltOf(std::uint32_t gen) const noexcept { return 1u + gen % m_saltMask; }

	void markDirty(const NavObstacle (&next)[kMaxObstacles])
	{
		std::fill(m_dirty.begin(), m_dirty.end(), std::uint8_t{0});
		for (int s = 0; s < kMaxObstacles; ++s)
		{
			const NavObstacle& a = m_applied[s];
			const NavObstacle& b = next[s];
			if (std::memcmp(&a, &b, sizeof(a)) == 0) continue;
			if (a.active()) markOverlapping(detail::obstacleBounds(a, m_header.agentRadius));
			if (b.active()) markOverlapping(detail::obstacleBounds(b, m_header.agentRadius));
		}
	}

	void markOverlapping(const detail::NavBounds& b)
	{
		for (std::size_t i = 0; i < m_layerBounds.size(); ++i)
		{
			if (m_layerBounds[i].overlaps(b)) m_dirty[i] = 1;
		}
	}

	/// @brief 障害物を next にしたとき中身が変わるタイルを作り、m_pending に (層の番号, データ) で積む。
	/// 今のタイルには触らないので、1 枚でも作れなければ何も変えずに false を返せる
	bool buildChanged(const NavObstacle (&next)[kMaxObstacles])
	{
		m_pending.clear();
		m_error = nullptr;
		markDirty(next);
		std::vector<std::uint8_t> data;
		for (std::size_t i = 0; i < m_layers.size(); ++i)
		{
			if (m_dirty[i] == 0) continue;
			if (!detail::buildCacheTile(m_layers[i], m_header, next, data))
			{
				m_pending.clear();
				m_error = "障害物を塗ったタイルを作れない";
				return false;
			}
			if (data != m_tiles[i]) m_pending.emplace_back(i, std::move(data));
			data.clear();
		}
		return true;
	}

	/// @brief 全タイルを外し、層の順に「層の番号と世代の salt」で決まる番号を付けて足し直す
	/// 写しを先に全部確保するので、確保できなければ今のナビメッシュのまま理由を返す
	[[nodiscard]] const char* compose()
	{
		std::vector<unsigned char*> copies(m_tiles.size(), nullptr);
		for (std::size_t i = 0; i < m_tiles.size(); ++i)
		{
			if (m_tiles[i].empty()) continue;
			copies[i] = static_cast<unsigned char*>(dtAlloc(static_cast<int>(m_tiles[i].size()), DT_ALLOC_PERM));
			if (copies[i] == nullptr)
			{
				for (unsigned char* c : copies) dtFree(c);
				return "ナビメッシュのメモリが取れない";
			}
			std::memcpy(copies[i], m_tiles[i].data(), m_tiles[i].size());
		}
		const dtNavMesh& mesh = *m_mesh;
		for (int i = 0; i < mesh.getMaxTiles(); ++i)
		{
			const dtMeshTile* t = mesh.getTile(i);
			if (t != nullptr && t->header != nullptr) m_mesh->removeTile(m_mesh->getTileRef(t), nullptr, nullptr);
		}
		const char* why = nullptr;
		for (std::size_t i = 0; i < m_tiles.size(); ++i)
		{
			if (copies[i] == nullptr) continue;
			const dtTileRef ref = m_mesh->encodePolyId(saltOf(m_gen[i]), static_cast<unsigned>(i), 0);
			if (dtStatusFailed(m_mesh->addTile(copies[i], static_cast<int>(m_tiles[i].size()), DT_TILE_FREE_DATA, ref, nullptr)))
			{
				dtFree(copies[i]);
				why = "タイルを足せない (Detour の版が焼いた時と違う可能性)";
			}
		}
		return why;
	}

	NavCacheHeader m_header{};
	std::vector<std::vector<std::uint8_t>> m_layers;   ///< 焼いた層 (展開は buildCacheTile が毎回する)
	std::vector<detail::NavBounds> m_layerBounds;
	std::vector<std::vector<std::uint8_t>> m_tiles;    ///< 層ごとの、今の障害物を塗ったタイルデータ (空 = 歩ける所が無い)
	std::vector<std::uint32_t> m_gen;
	std::vector<std::uint8_t> m_dirty;
	std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> m_pending;   ///< buildChanged が作った、入れ替えるタイル
	const char* m_error = nullptr;
	NavObstacle m_desired[kMaxObstacles]{};   ///< setObstacle で置いた物
	NavObstacle m_applied[kMaxObstacles]{};   ///< タイルに塗ってある物
	std::uint32_t m_epoch = 0;
	std::uint64_t m_levelHash = 0;
	unsigned m_saltMask = 1;
	std::unique_ptr<dtNavMesh, MeshDeleter> m_mesh;
	NavQuery m_query;   // m_mesh を指すので、m_mesh より先に壊れるよう後ろに置く
};

} // namespace mitiru::nav
