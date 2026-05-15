#pragma once

/// @file MeshOptimizer.hpp
/// @brief メッシュ最適化 (頂点キャッシュ最適化・LOD生成・エッジ縮約)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::asset
{

/// @brief 最適化済みメッシュデータ
struct OptimizedMesh
{
	std::vector<render::Vertex3D> vertices;
	std::vector<uint32_t> indices;
};

/// @brief LODメッシュ (複数詳細度レベル)
struct MeshLOD
{
	std::vector<OptimizedMesh> levels; ///< LOD0=オリジナル, LOD1+=簡略化
};

/// @brief メッシュ最適化ユーティリティ (キャッシュ最適化・重複除去・LOD生成)
class MeshOptimizer
{
public:
	/// @brief 頂点キャッシュ最適化 (LRUキャッシュシミュレーション)
	[[nodiscard]] static OptimizedMesh optimizeVertexCache(
		const std::vector<render::Vertex3D>& verts,
		const std::vector<uint32_t>& indices)
	{
		if (indices.size() < 3)
		{
			return {verts, indices};
		}

		const auto triCount = indices.size() / 3;

		// 各頂点の隣接三角形リストを構築
		std::vector<std::vector<uint32_t>> adjacency(verts.size());
		for (uint32_t t = 0; t < triCount; ++t)
		{
			adjacency[indices[t * 3 + 0]].push_back(t);
			adjacency[indices[t * 3 + 1]].push_back(t);
			adjacency[indices[t * 3 + 2]].push_back(t);
		}

		// 簡易キャッシュシミュレーション付きグリーディ順序付け
		constexpr int kCacheSize = 32;
		std::vector<bool> emitted(triCount, false);
		std::vector<uint32_t> cache(kCacheSize, UINT32_MAX);
		std::vector<uint32_t> reorderedIndices;
		reorderedIndices.reserve(indices.size());

		// キャッシュ内の頂点セット
		auto isInCache = [&](uint32_t v) -> bool
		{
			return std::find(cache.begin(), cache.end(), v) != cache.end();
		};

		auto pushCache = [&](uint32_t v)
		{
			// LRU: 先頭に挿入、末尾を削除
			for (size_t i = cache.size() - 1; i > 0; --i)
			{
				cache[i] = cache[i - 1];
			}
			cache[0] = v;
		};

		// 最初の三角形から開始
		uint32_t currentTri = 0;
		for (uint32_t t = 0; t < triCount; ++t)
		{
			if (!emitted[t])
			{
				currentTri = t;
				break;
			}
		}

		uint32_t emittedCount = 0;
		while (emittedCount < triCount)
		{
			if (!emitted[currentTri])
			{
				emitted[currentTri] = true;
				++emittedCount;

				for (int j = 0; j < 3; ++j)
				{
					const auto vi = indices[currentTri * 3 + j];
					reorderedIndices.push_back(vi);
					pushCache(vi);
				}
			}

			// キャッシュ内の頂点に隣接する未出力三角形を優先
			uint32_t bestTri = UINT32_MAX;
			int bestScore = -1;

			for (const auto cv : cache)
			{
				if (cv == UINT32_MAX)
				{
					continue;
				}
				for (const auto adjTri : adjacency[cv])
				{
					if (emitted[adjTri])
					{
						continue;
					}
					// スコア: キャッシュ内頂点数が多いほど高い
					int score = 0;
					for (int j = 0; j < 3; ++j)
					{
						if (isInCache(indices[adjTri * 3 + j]))
						{
							++score;
						}
					}
					if (score > bestScore)
					{
						bestScore = score;
						bestTri = adjTri;
					}
				}
			}

			if (bestTri == UINT32_MAX)
			{
				// キャッシュミス — 未出力の任意の三角形を選択
				for (uint32_t t = 0; t < triCount; ++t)
				{
					if (!emitted[t])
					{
						bestTri = t;
						break;
					}
				}
				if (bestTri == UINT32_MAX)
				{
					break;
				}
			}
			currentTri = bestTri;
		}

		return {verts, reorderedIndices};
	}

	/// @brief 退化三角形・重複三角形の除去
	[[nodiscard]] static OptimizedMesh cleanMesh(
		const std::vector<render::Vertex3D>& verts,
		const std::vector<uint32_t>& indices)
	{
		// 重複頂点の統合
		std::vector<render::Vertex3D> uniqueVerts;
		std::vector<uint32_t> remapTable(verts.size());

		for (size_t i = 0; i < verts.size(); ++i)
		{
			bool found = false;
			for (size_t j = 0; j < uniqueVerts.size(); ++j)
			{
				if (verticesEqual(verts[i], uniqueVerts[j]))
				{
					remapTable[i] = static_cast<uint32_t>(j);
					found = true;
					break;
				}
			}
			if (!found)
			{
				remapTable[i] = static_cast<uint32_t>(uniqueVerts.size());
				uniqueVerts.push_back(verts[i]);
			}
		}

		// インデックスをリマップし、退化三角形を除外
		std::vector<uint32_t> cleanedIndices;
		cleanedIndices.reserve(indices.size());
		std::unordered_set<uint64_t> seenTris;

		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			uint32_t i0 = remapTable[indices[i + 0]];
			uint32_t i1 = remapTable[indices[i + 1]];
			uint32_t i2 = remapTable[indices[i + 2]];

			// 退化三角形をスキップ
			if (i0 == i1 || i1 == i2 || i0 == i2)
			{
				continue;
			}

			// 正規化ハッシュで重複チェック
			std::array<uint32_t, 3> sorted = {i0, i1, i2};
			std::sort(sorted.begin(), sorted.end());
			const uint64_t key = (static_cast<uint64_t>(sorted[0]) << 42)
				| (static_cast<uint64_t>(sorted[1]) << 21)
				| sorted[2];

			if (seenTris.insert(key).second)
			{
				cleanedIndices.push_back(i0);
				cleanedIndices.push_back(i1);
				cleanedIndices.push_back(i2);
			}
		}

		return {uniqueVerts, cleanedIndices};
	}

	/// @brief LOD レベルを生成する (targetRatios: 例 {1.0, 0.5, 0.25})
	[[nodiscard]] static MeshLOD generateLODs(
		const std::vector<render::Vertex3D>& verts,
		const std::vector<uint32_t>& indices,
		const std::vector<float>& targetRatios)
	{
		MeshLOD lod;
		const auto triCount = static_cast<int>(indices.size() / 3);

		for (const float ratio : targetRatios)
		{
			const int targetTris = std::max(
				1, static_cast<int>(triCount * std::clamp(ratio, 0.0f, 1.0f)));

			if (ratio >= 1.0f)
			{
				lod.levels.push_back({verts, indices});
			}
			else
			{
				lod.levels.push_back(simplify(verts, indices, targetTris));
			}
		}

		return lod;
	}

	/// @brief メッシュを目標三角形数まで簡略化する (エッジ縮約)
	[[nodiscard]] static OptimizedMesh simplify(
		const std::vector<render::Vertex3D>& verts,
		const std::vector<uint32_t>& indices,
		int targetTriangles)
	{
		if (indices.size() < 3)
		{
			return {verts, indices};
		}

		// 作業用コピー
		auto workVerts = verts;
		auto workIndices = indices;

		// エッジ縮約ループ
		while (static_cast<int>(workIndices.size() / 3) > targetTriangles)
		{
			// 最短エッジを見つける
			float bestCost = std::numeric_limits<float>::max();
			size_t bestEdgeTriIdx = 0;
			int bestEdgeLocal = 0;

			for (size_t t = 0; t + 2 < workIndices.size(); t += 3)
			{
				for (int e = 0; e < 3; ++e)
				{
					const uint32_t v0 = workIndices[t + e];
					const uint32_t v1 = workIndices[t + (e + 1) % 3];

					if (v0 >= workVerts.size() || v1 >= workVerts.size())
					{
						continue;
					}

					const float cost = edgeCollapseError(
						workVerts[v0], workVerts[v1]);

					if (cost < bestCost)
					{
						bestCost = cost;
						bestEdgeTriIdx = t;
						bestEdgeLocal = e;
					}
				}
			}

			if (bestCost >= std::numeric_limits<float>::max())
			{
				break; // 縮約不可
			}

			// エッジ縮約: v1 を v0 に統合
			const uint32_t keepIdx = workIndices[bestEdgeTriIdx + bestEdgeLocal];
			const uint32_t removeIdx =
				workIndices[bestEdgeTriIdx + (bestEdgeLocal + 1) % 3];

			// 中間点に移動
			auto& keepVert = workVerts[keepIdx];
			const auto& removeVert = workVerts[removeIdx];
			keepVert.position.x = (keepVert.position.x + removeVert.position.x) * 0.5f;
			keepVert.position.y = (keepVert.position.y + removeVert.position.y) * 0.5f;
			keepVert.position.z = (keepVert.position.z + removeVert.position.z) * 0.5f;

			// removeIdx の参照をすべて keepIdx に置換
			for (auto& idx : workIndices)
			{
				if (idx == removeIdx)
				{
					idx = keepIdx;
				}
			}

			// 退化三角形を除去
			std::vector<uint32_t> newIndices;
			newIndices.reserve(workIndices.size());
			for (size_t t = 0; t + 2 < workIndices.size(); t += 3)
			{
				if (workIndices[t] != workIndices[t + 1]
					&& workIndices[t + 1] != workIndices[t + 2]
					&& workIndices[t] != workIndices[t + 2])
				{
					newIndices.push_back(workIndices[t]);
					newIndices.push_back(workIndices[t + 1]);
					newIndices.push_back(workIndices[t + 2]);
				}
			}
			workIndices = std::move(newIndices);
		}

		// 未使用頂点を除去してコンパクト化
		return compactMesh(workVerts, workIndices);
	}

private:
	/// @brief 2頂点が同一か判定する (epsilon比較)
	[[nodiscard]] static bool verticesEqual(
		const render::Vertex3D& a, const render::Vertex3D& b)
	{
		constexpr float eps = 1e-6f;
		auto isNear = [eps](float x, float y) { return std::abs(x - y) < eps; };
		return isNear(a.position.x, b.position.x) && isNear(a.position.y, b.position.y)
			&& isNear(a.position.z, b.position.z) && isNear(a.normal.x, b.normal.x)
			&& isNear(a.normal.y, b.normal.y) && isNear(a.normal.z, b.normal.z)
			&& isNear(a.texCoord.x, b.texCoord.x) && isNear(a.texCoord.y, b.texCoord.y);
	}

	/// @brief エッジ縮約コスト (距離ベース)
	[[nodiscard]] static float edgeCollapseError(
		const render::Vertex3D& v0, const render::Vertex3D& v1)
	{
		const float dx = v0.position.x - v1.position.x;
		const float dy = v0.position.y - v1.position.y;
		const float dz = v0.position.z - v1.position.z;
		return dx * dx + dy * dy + dz * dz;
	}

	[[nodiscard]] static OptimizedMesh compactMesh(
		const std::vector<render::Vertex3D>& verts,
		const std::vector<uint32_t>& indices)
	{
		// 使用中の頂点を特定
		std::unordered_map<uint32_t, uint32_t> remap;
		std::vector<render::Vertex3D> compactVerts;
		std::vector<uint32_t> compactIndices;
		compactIndices.reserve(indices.size());

		for (const auto idx : indices)
		{
			auto it = remap.find(idx);
			if (it == remap.end())
			{
				const auto newIdx = static_cast<uint32_t>(compactVerts.size());
				remap[idx] = newIdx;
				compactVerts.push_back(verts[idx]);
				compactIndices.push_back(newIdx);
			}
			else
			{
				compactIndices.push_back(it->second);
			}
		}

		return {compactVerts, compactIndices};
	}
};

} // namespace mitiru::asset
