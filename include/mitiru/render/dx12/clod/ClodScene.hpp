#pragma once

/// @file ClodScene.hpp
/// @brief .clod モデルの CPU 側シーン (連結配列 + BVH + テクスチャ decode)
/// @details 複数モデルを 1 つの連結 index 空間に積む。モデル追加毎に
///          revision が進み、GPU 側 (ClodRenderer) が静的バッファを作り直す。

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/dx12/clod/ClodFormat.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <stb_image.h>

namespace mitiru::render::clod
{

/// @brief CPU デコード済みテクスチャ (mip 連鎖込み)
struct CpuTexture
{
	std::vector<std::vector<uint8_t>> mips;   ///< RGBA8、[0] = フル解像度
	uint32_t width = 0;
	uint32_t height = 0;
	bool srgb = true;
	bool hasAlpha = false;
};

/// @brief 1 モデル分の連結配列内レンジ
struct ClodModel
{
	uint32_t clusterBase = 0;
	uint32_t clusterCount = 0;
	uint32_t groupBase = 0;
	uint32_t groupCount = 0;
	uint32_t bvhRoot = 0;
	float radius = 0.0f;   ///< 原点中心 bake 後の包含球半径
};

/// @brief .clod モデル群の CPU シーン
class ClodScene
{
public:
	/// @brief .clod blob を連結シーンへ追加する
	/// @param texDir テクスチャ相対パスの基準 (vfs パス、末尾スラッシュ込み)
	/// @return 追加したモデル index。失敗は -1
	int appendModel(const uint8_t* data, size_t size, const std::string& texDir)
	{
		if (m_models.size() >= kClodMaxMeshes) { return -1; }
		const auto* hdr = reinterpret_cast<const ClodFileHeader*>(data);
		if (size < sizeof(ClodFileHeader) || hdr->magic != kClodMagic) { return -1; }
		if (!layoutValid(*hdr, size)) { return -1; }

		ClodModel model{};
		appendGeometry(*hdr, data, model);
		appendMaterials(*hdr, data, texDir);
		if (!buildGroupRanges(model)) { return -1; }
		buildBvh(model);
		m_models.push_back(model);
		++m_revision;
		return static_cast<int>(m_models.size()) - 1;
	}

	[[nodiscard]] uint32_t revision() const noexcept { return m_revision; }
	[[nodiscard]] const std::vector<ClodModel>& models() const noexcept { return m_models; }
	[[nodiscard]] uint32_t maxLodDepth() const noexcept { return m_maxLodDepth; }

	[[nodiscard]] const std::vector<float>& positions() const noexcept { return m_positions; }
	[[nodiscard]] const std::vector<float>& normals() const noexcept { return m_normals; }
	[[nodiscard]] const std::vector<float>& uvs() const noexcept { return m_uvs; }
	[[nodiscard]] const std::vector<ClodGroup>& groups() const noexcept { return m_groups; }
	[[nodiscard]] const std::vector<ClodCluster>& clusters() const noexcept { return m_clusters; }
	[[nodiscard]] const std::vector<uint32_t>& clusterVerts() const noexcept { return m_clusterVerts; }
	[[nodiscard]] const std::vector<uint8_t>& clusterTris() const noexcept { return m_clusterTris; }
	[[nodiscard]] const std::vector<GpuMaterial>& materials() const noexcept { return m_materials; }
	[[nodiscard]] const std::vector<uint32_t>& groupRanges() const noexcept { return m_groupRanges; }
	[[nodiscard]] const std::vector<GpuBvhNode>& bvhNodes() const noexcept { return m_bvhNodes; }
	[[nodiscard]] const std::vector<CpuTexture>& textures() const noexcept { return m_textures; }

private:
	[[nodiscard]] static bool layoutValid(const ClodFileHeader& h, size_t size) noexcept
	{
		const size_t expected = sizeof(ClodFileHeader)
			+ static_cast<size_t>(h.vertexCount) * 32
			+ static_cast<size_t>(h.groupCount) * sizeof(ClodGroup)
			+ static_cast<size_t>(h.clusterCount) * sizeof(ClodCluster)
			+ static_cast<size_t>(h.vertIdxCount) * 4
			+ h.triIdxByteCount
			+ static_cast<size_t>(h.materialCount) * sizeof(ClodFileMaterial);
		return expected == size;
	}

	/// @brief 頂点/クラスタ/グループを原点中心へ bake しつつ連結配列へ積む
	void appendGeometry(const ClodFileHeader& h, const uint8_t* data, ClodModel& model)
	{
		const uint8_t* base = data + sizeof(ClodFileHeader);
		const auto* pPos = reinterpret_cast<const float*>(base);
		const float* pNorm = pPos + static_cast<size_t>(h.vertexCount) * 3;
		const float* pUv = pNorm + static_cast<size_t>(h.vertexCount) * 3;
		const auto* pGroups = reinterpret_cast<const ClodGroup*>(base + static_cast<size_t>(h.vertexCount) * 32);
		const auto* pClusters = reinterpret_cast<const ClodCluster*>(
			reinterpret_cast<const uint8_t*>(pGroups) + static_cast<size_t>(h.groupCount) * sizeof(ClodGroup));
		const auto* pVerts = reinterpret_cast<const uint32_t*>(
			reinterpret_cast<const uint8_t*>(pClusters) + static_cast<size_t>(h.clusterCount) * sizeof(ClodCluster));
		const uint8_t* pTris = reinterpret_cast<const uint8_t*>(pVerts) + static_cast<size_t>(h.vertIdxCount) * 4;

		float ctr[3];
		for (int k = 0; k < 3; ++k) { ctr[k] = (h.boundsMin[k] + h.boundsMax[k]) * 0.5f; }
		{
			const float e[3] = { h.boundsMax[0] - h.boundsMin[0], h.boundsMax[1] - h.boundsMin[1],
			                     h.boundsMax[2] - h.boundsMin[2] };
			model.radius = 0.5f * std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
		}

		const auto vertexBase = static_cast<uint32_t>(m_positions.size() / 3);
		const auto groupBase = static_cast<uint32_t>(m_groups.size());
		const auto clusterBase = static_cast<uint32_t>(m_clusters.size());
		const auto vertIdxBase = static_cast<uint32_t>(m_clusterVerts.size());
		const auto triBase = static_cast<uint32_t>(m_clusterTris.size());
		const auto matBase = static_cast<uint32_t>(m_materials.size());

		for (uint32_t i = 0; i < h.vertexCount; ++i)
		{
			for (int k = 0; k < 3; ++k) { m_positions.push_back(pPos[i * 3 + k] - ctr[k]); }
		}
		m_normals.insert(m_normals.end(), pNorm, pNorm + static_cast<size_t>(h.vertexCount) * 3);
		m_uvs.insert(m_uvs.end(), pUv, pUv + static_cast<size_t>(h.vertexCount) * 2);
		for (uint32_t i = 0; i < h.groupCount; ++i)
		{
			ClodGroup g = pGroups[i];
			for (int k = 0; k < 3; ++k) { g.center[k] -= ctr[k]; }
			m_groups.push_back(g);
		}
		for (uint32_t i = 0; i < h.clusterCount; ++i)
		{
			ClodCluster c = pClusters[i];
			c.ownGroup += static_cast<int32_t>(groupBase);
			if (c.refined >= 0) { c.refined += static_cast<int32_t>(groupBase); }
			for (int k = 0; k < 3; ++k) { c.cull[k] -= ctr[k]; }
			c.vertOffset += vertIdxBase;
			c.triOffset += triBase;
			c.materialId += matBase;
			m_maxLodDepth = c.lodDepth > m_maxLodDepth ? c.lodDepth : m_maxLodDepth;
			m_clusters.push_back(c);
		}
		for (uint32_t i = 0; i < h.vertIdxCount; ++i) { m_clusterVerts.push_back(pVerts[i] + vertexBase); }
		m_clusterTris.insert(m_clusterTris.end(), pTris, pTris + h.triIdxByteCount);

		model.clusterBase = clusterBase;
		model.clusterCount = h.clusterCount;
		model.groupBase = groupBase;
		model.groupCount = h.groupCount;
	}

	void appendMaterials(const ClodFileHeader& h, const uint8_t* data, const std::string& texDir)
	{
		const uint8_t* base = data + sizeof(ClodFileHeader);
		const auto* pMats = reinterpret_cast<const ClodFileMaterial*>(
			base + static_cast<size_t>(h.vertexCount) * 32
			+ static_cast<size_t>(h.groupCount) * sizeof(ClodGroup)
			+ static_cast<size_t>(h.clusterCount) * sizeof(ClodCluster)
			+ static_cast<size_t>(h.vertIdxCount) * 4 + h.triIdxByteCount);
		for (uint32_t mi = 0; mi < h.materialCount; ++mi)
		{
			GpuMaterial gm{};
			std::memcpy(gm.baseColor, pMats[mi].baseColor, 16);
			gm.texIndex = 0xFFFFFFFFu;
			gm.normalTex = 0xFFFFFFFFu;
			if (pMats[mi].albedo[0] != '\0') { gm.texIndex = loadTexture(texDir + pMats[mi].albedo, true); }
			if (pMats[mi].normal[0] != '\0') { gm.normalTex = loadTexture(texDir + pMats[mi].normal, false); }
			if (gm.texIndex != 0xFFFFFFFFu)
			{
				if (m_textures[gm.texIndex].hasAlpha) { gm.flags |= 1u; }
				// map_Kd の減衰係数はテクスチャと二重になるため使わない
				gm.baseColor[0] = gm.baseColor[1] = gm.baseColor[2] = 1.0f;
			}
			m_materials.push_back(gm);
		}
	}

	/// @brief vfs からテクスチャを読み RGBA8 + box mip 連鎖に decode する
	/// @return m_textures 内 index。失敗は 0xFFFFFFFF
	uint32_t loadTexture(const std::string& path, bool srgb)
	{
		const std::string key = path + (srgb ? "|s" : "|l");
		if (const auto it = m_textureIndex.find(key); it != m_textureIndex.end()) { return it->second; }

		const auto blob = vfs::readGlobal(path);
		if (!blob || blob->empty()) { m_textureIndex.emplace(key, 0xFFFFFFFFu); return 0xFFFFFFFFu; }
		int w = 0, h = 0, comp = 0;
		auto* img = stbi_load_from_memory(blob->data(), static_cast<int>(blob->size()), &w, &h, &comp, 4);
		if (img == nullptr) { m_textureIndex.emplace(key, 0xFFFFFFFFu); return 0xFFFFFFFFu; }

		CpuTexture tex;
		tex.width = static_cast<uint32_t>(w);
		tex.height = static_cast<uint32_t>(h);
		tex.srgb = srgb;
		for (size_t px = 3; px < static_cast<size_t>(w) * h * 4; px += 4)
		{
			if (img[px] < 250) { tex.hasAlpha = true; break; }
		}
		tex.mips.emplace_back(img, img + static_cast<size_t>(w) * h * 4);
		stbi_image_free(img);
		buildMips(tex);

		const auto idx = static_cast<uint32_t>(m_textures.size());
		m_textures.push_back(std::move(tex));
		m_textureIndex.emplace(key, idx);
		return idx;
	}

	static void buildMips(CpuTexture& tex)
	{
		uint32_t mw = tex.width, mh = tex.height;
		while (mw > 1 || mh > 1)
		{
			const uint32_t nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
			const std::vector<uint8_t>& src = tex.mips.back();
			std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
			for (uint32_t y = 0; y < nh; ++y)
			{
				for (uint32_t x = 0; x < nw; ++x)
				{
					const uint32_t sx = x * 2, sy = y * 2;
					const uint32_t sx1 = sx + 1 < mw ? sx + 1 : sx;
					const uint32_t sy1 = sy + 1 < mh ? sy + 1 : sy;
					for (int k = 0; k < 4; ++k)
					{
						const int s = src[(static_cast<size_t>(sy) * mw + sx) * 4 + k]
							+ src[(static_cast<size_t>(sy) * mw + sx1) * 4 + k]
							+ src[(static_cast<size_t>(sy1) * mw + sx) * 4 + k]
							+ src[(static_cast<size_t>(sy1) * mw + sx1) * 4 + k];
						dst[(static_cast<size_t>(y) * nw + x) * 4 + k] = static_cast<uint8_t>(s / 4);
					}
				}
			}
			tex.mips.push_back(std::move(dst));
			mw = nw;
			mh = nh;
		}
	}

	/// @brief group → 連結クラスタ範囲。クラスタは group 単位で連続している前提を検査
	bool buildGroupRanges(const ClodModel& model)
	{
		const size_t rangeBase = m_groupRanges.size();
		m_groupRanges.resize(rangeBase + static_cast<size_t>(model.groupCount) * 2, 0);
		std::vector<uint32_t> first(model.groupCount, 0xFFFFFFFFu);
		std::vector<uint32_t> count(model.groupCount, 0);
		for (uint32_t i = 0; i < model.clusterCount; ++i)
		{
			const uint32_t ci = model.clusterBase + i;
			const auto gi = static_cast<uint32_t>(m_clusters[ci].ownGroup) - model.groupBase;
			if (gi >= model.groupCount) { return false; }
			if (first[gi] == 0xFFFFFFFFu) { first[gi] = ci; }
			else if (ci != first[gi] + count[gi]) { return false; }
			++count[gi];
		}
		for (uint32_t gi = 0; gi < model.groupCount; ++gi)
		{
			m_groupRanges[rangeBase + static_cast<size_t>(gi) * 2] = first[gi];
			m_groupRanges[rangeBase + static_cast<size_t>(gi) * 2 + 1] = count[gi];
		}
		return true;
	}

	/// @brief group 上の 8 分木 (leaf = 1 group、中心座標の最大軸で等分割)
	void buildBvh(ClodModel& model)
	{
		std::vector<uint32_t> ids(model.groupCount);
		for (uint32_t i = 0; i < model.groupCount; ++i) { ids[i] = model.groupBase + i; }
		const auto rootSlot = static_cast<uint32_t>(m_bvhNodes.size());
		m_bvhNodes.resize(rootSlot + 1);
		m_bvhDepth = 1;
		buildBvhInto(rootSlot, ids.data(), 0, model.groupCount, 1);
		model.bvhRoot = rootSlot;
	}

	void buildBvhInto(uint32_t slot, uint32_t* ids, uint32_t lo, uint32_t hi, uint32_t depth)
	{
		m_bvhDepth = depth > m_bvhDepth ? depth : m_bvhDepth;
		if (hi - lo == 1)
		{
			const ClodGroup& g = m_groups[ids[lo]];
			GpuBvhNode n{};
			std::memcpy(n.sphere, g.center, 12);
			n.sphere[3] = g.radius;
			n.maxErr = g.error;
			n.groupId = ids[lo];
			m_bvhNodes[slot] = n;
			return;
		}
		sortByWidestAxis(ids, lo, hi);
		const uint32_t n = hi - lo;
		const uint32_t k = n < 8 ? n : 8;
		const auto base = static_cast<uint32_t>(m_bvhNodes.size());
		m_bvhNodes.resize(base + k);
		for (uint32_t c = 0; c < k; ++c)
		{
			const auto clo = lo + static_cast<uint32_t>(static_cast<uint64_t>(n) * c / k);
			const auto chi = lo + static_cast<uint32_t>(static_cast<uint64_t>(n) * (c + 1) / k);
			buildBvhInto(base + c, ids, clo, chi, depth + 1);
		}
		m_bvhNodes[slot] = unionOfChildren(base, k);
	}

	void sortByWidestAxis(uint32_t* ids, uint32_t lo, uint32_t hi)
	{
		float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (uint32_t i = lo; i < hi; ++i)
		{
			for (int a = 0; a < 3; ++a)
			{
				const float v = m_groups[ids[i]].center[a];
				mn[a] = v < mn[a] ? v : mn[a];
				mx[a] = v > mx[a] ? v : mx[a];
			}
		}
		int axis = 0;
		for (int a = 1; a < 3; ++a)
		{
			if (mx[a] - mn[a] > mx[axis] - mn[axis]) { axis = a; }
		}
		std::sort(ids + lo, ids + hi, [this, axis](uint32_t a, uint32_t b)
		          { return m_groups[a].center[axis] < m_groups[b].center[axis]; });
	}

	[[nodiscard]] GpuBvhNode unionOfChildren(uint32_t base, uint32_t k) const
	{
		GpuBvhNode nd{};
		double ctr[3] = {};
		for (uint32_t c = 0; c < k; ++c)
		{
			for (int a = 0; a < 3; ++a) { ctr[a] += m_bvhNodes[base + c].sphere[a]; }
		}
		for (int a = 0; a < 3; ++a) { nd.sphere[a] = static_cast<float>(ctr[a] / k); }
		for (uint32_t c = 0; c < k; ++c)
		{
			const GpuBvhNode& ch = m_bvhNodes[base + c];
			const float dx = ch.sphere[0] - nd.sphere[0];
			const float dy = ch.sphere[1] - nd.sphere[1];
			const float dz = ch.sphere[2] - nd.sphere[2];
			const float d = std::sqrt(dx * dx + dy * dy + dz * dz) + ch.sphere[3];
			nd.sphere[3] = d > nd.sphere[3] ? d : nd.sphere[3];
			nd.maxErr = ch.maxErr > nd.maxErr ? ch.maxErr : nd.maxErr;
		}
		nd.firstChild = base;
		nd.childCount = k;
		return nd;
	}

public:
	[[nodiscard]] uint32_t bvhMaxDepth() const noexcept { return m_bvhDepth; }

private:
	std::vector<ClodModel> m_models;
	std::vector<float> m_positions;
	std::vector<float> m_normals;
	std::vector<float> m_uvs;
	std::vector<ClodGroup> m_groups;
	std::vector<ClodCluster> m_clusters;
	std::vector<uint32_t> m_clusterVerts;
	std::vector<uint8_t> m_clusterTris;
	std::vector<GpuMaterial> m_materials;
	std::vector<uint32_t> m_groupRanges;   ///< group 毎に (先頭 cluster, 数)
	std::vector<GpuBvhNode> m_bvhNodes;
	std::vector<CpuTexture> m_textures;
	std::map<std::string, uint32_t> m_textureIndex;
	uint32_t m_maxLodDepth = 0;
	uint32_t m_bvhDepth = 1;
	uint32_t m_revision = 0;
};

} // namespace mitiru::render::clod
