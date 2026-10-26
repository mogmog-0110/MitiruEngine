#pragma once

/// @file MorphTargets.hpp
/// @brief モーフターゲットの差分を頂点に足す CPU 実装と、包含箱を広げる量。
/// @details DX12 と Dawn の compute (Dx12SkinningCompute のモーフ用、shaders/dawn/skin_morph.slang) は同じ式を同じ順で回す。
///          この関数がその答え合わせのリファレンスで、Dawn が compute を使えないときの CPU のパスでもある。
///          順序は「モーフを足してからスキン」(glTF の仕様)。足す順は行の中の target の昇順で、GPU も同じ。
///          法線はここでは正規化しない。スキンのある頂点はスキンの後で、ない頂点は morphVertices の最後に正規化する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/Skinning.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// GPU に渡す差分の 1 要素の大きさ (uint32 の数): target、位置の差分 xyz、法線の差分 xyz、パディング
inline constexpr std::uint32_t kMorphEntryWords = 8;

/// 描画へ渡す重みの絶対値の上限。NaN は 0、これを超える値は丸める (包含箱を広げる量が無限にならないように)
inline constexpr float kMorphWeightLimit = 16.0f;

[[nodiscard]] inline float sanitizeMorphWeight(float w) noexcept
{
	if (!(w == w)) { return 0.0f; }
	return std::clamp(w, -kMorphWeightLimit, kMorphWeightLimit);
}

/// @brief 重みの配列を描画用に整える (NaN、範囲外)。dst は src と同じ数。0 でない重みがあれば true
inline bool sanitizeMorphWeights(std::span<const float> src, std::vector<float>& dst)
{
	dst.resize(src.size());
	bool any = false;
	for (std::size_t i = 0; i < src.size(); ++i)
	{
		dst[i] = sanitizeMorphWeight(src[i]);
		any = any || dst[i] != 0.0f;
	}
	return any;
}

/// @brief prim の重み (slotBase 番から) に 0 でないものがあるか。スロットの先は weights の外なら 0 扱い。
///        targetCount が 0 の set (差分の無い prim) は常に false。包含箱の情報だけを持つ殻でも使える
[[nodiscard]] inline bool morphActive(const GltfMorphSet& set, std::span<const float> weights, std::uint32_t slotBase) noexcept
{
	for (std::uint32_t t = 0; t < set.targetCount; ++t)
	{
		const std::size_t slot = std::size_t{slotBase} + t;
		if (slot < weights.size() && weights[slot] != 0.0f) { return true; }
	}
	return false;
}

/// @brief base の位置と法線にモーフの差分を足す。スキンで正規化されない頂点 (バインドが無い、または重みの和が 0) は、最後に法線を正規化する
/// @param weights   モデル全体のスロットの重み。slotBase は prim のモーフ 0 番のスロット
/// @param binding   スキンのバインド。スキンの後で法線が正規化される頂点は、ここでは正規化しない。nullptr は全頂点を正規化する
[[nodiscard]] inline std::vector<Vertex3D> morphVertices(const std::vector<Vertex3D>& base, const GltfMorphSet& set,
                                                         std::span<const float> weights, std::uint32_t slotBase,
                                                         const std::vector<SkinVertexBinding>* binding = nullptr)
{
	std::vector<Vertex3D> out = base;
	if (set.rowStart.size() != base.size() + 1) { return out; }
	for (std::size_t v = 0; v < base.size(); ++v)
	{
		bool touched = false;
		for (std::uint32_t e = set.rowStart[v]; e < set.rowStart[v + 1]; ++e)
		{
			const GltfMorphEntry& entry = set.entries[e];
			const std::size_t slot = std::size_t{slotBase} + entry.target;
			const float w = slot < weights.size() ? weights[slot] : 0.0f;
			if (w == 0.0f) { continue; }
			out[v].position = out[v].position + entry.positionDelta * w;
			out[v].normal = out[v].normal + entry.normalDelta * w;
			touched = true;
		}
		const bool skinNormalizes = binding != nullptr && binding->size() == base.size() &&
		                            (*binding)[v].weights[0] + (*binding)[v].weights[1] + (*binding)[v].weights[2] + (*binding)[v].weights[3] > 1e-8f;
		if (touched && !skinNormalizes)
		{
			const sgc::Vec3f n = out[v].normal;
			const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			if (len > 1e-7f) { out[v].normal = {n.x / len, n.y / len, n.z / len}; }
		}
	}
	return out;
}

/// @brief モーフ → スキンの順で変形した頂点列。スキンのバインドが無ければモーフだけ
[[nodiscard]] inline std::vector<Vertex3D> morphSkinVertices(const std::vector<Vertex3D>& base,
                                                             const std::vector<SkinVertexBinding>& binding,
                                                             const GltfMorphSet& set, std::span<const float> weights,
                                                             std::uint32_t slotBase,
                                                             const std::vector<sgc::Mat4f>& inverseBind,
                                                             const std::vector<sgc::Mat4f>& worldPose)
{
	const bool skinnable = binding.size() == base.size() && !inverseBind.empty() && inverseBind.size() == worldPose.size();
	const std::vector<Vertex3D> morphed = morphVertices(base, set, weights, slotBase, skinnable ? &binding : nullptr);
	return skinnable ? skinVertices(morphed, binding, inverseBind, worldPose) : morphed;
}

/// @brief モーフで頂点が動く最大の量 (軸ごと)。包含箱をこの分だけ広げれば、どの頂点も箱に収まる
[[nodiscard]] inline sgc::Vec3f morphBoundsMargin(const GltfMorphSet& set, std::span<const float> weights,
                                                  std::uint32_t slotBase) noexcept
{
	sgc::Vec3f margin{0, 0, 0};
	for (std::uint32_t t = 0; t < set.targetCount && t < set.maxAbsPosition.size(); ++t)
	{
		const std::size_t slot = std::size_t{slotBase} + t;
		if (slot >= weights.size()) { break; }
		const float w = std::fabs(weights[slot]);
		margin = {margin.x + w * set.maxAbsPosition[t].x, margin.y + w * set.maxAbsPosition[t].y,
		          margin.z + w * set.maxAbsPosition[t].z};
	}
	return margin;
}

/// @brief モーフごとの密な差分 (頂点数と同じ長さ。空なら無し) から CSR を作る。0 の差分は持たない。FBX の blend shape のインポートが使う
[[nodiscard]] inline GltfMorphSet morphSetFromDense(std::size_t vertexCount, std::span<const std::vector<sgc::Vec3f>> positions,
                                                    std::span<const std::vector<sgc::Vec3f>> normals)
{
	GltfMorphSet set;
	if (vertexCount == 0 || positions.empty()) { return set; }
	set.maxAbsPosition.assign(positions.size(), sgc::Vec3f{0, 0, 0});
	set.rowStart.assign(1, 0);
	const auto nonZero = [](const sgc::Vec3f& v) { return v.x != 0.0f || v.y != 0.0f || v.z != 0.0f; };
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		for (std::size_t t = 0; t < positions.size(); ++t)
		{
			const bool hasPos = positions[t].size() == vertexCount;
			const bool hasNorm = t < normals.size() && normals[t].size() == vertexCount;
			const sgc::Vec3f dp = hasPos ? positions[t][v] : sgc::Vec3f{0, 0, 0};
			const sgc::Vec3f dn = hasNorm ? normals[t][v] : sgc::Vec3f{0, 0, 0};
			if (!nonZero(dp) && !nonZero(dn)) { continue; }
			set.entries.push_back({static_cast<std::uint32_t>(t), dp, dn});
			auto& m = set.maxAbsPosition[t];
			m = {std::max(m.x, std::fabs(dp.x)), std::max(m.y, std::fabs(dp.y)), std::max(m.z, std::fabs(dp.z))};
		}
		set.rowStart.push_back(static_cast<std::uint32_t>(set.entries.size()));
	}
	if (set.entries.empty()) { return GltfMorphSet{}; }
	set.targetCount = static_cast<std::uint32_t>(positions.size());
	return set;
}

/// @brief CSR をモーフごとの密な配列に戻す (glb へ書くとき)。positions / normals はモーフ数ぶん、頂点数の長さ
inline void morphSetToDense(const GltfMorphSet& set, std::size_t vertexCount, std::vector<std::vector<sgc::Vec3f>>& positions,
                            std::vector<std::vector<sgc::Vec3f>>& normals)
{
	positions.assign(set.targetCount, std::vector<sgc::Vec3f>(vertexCount, sgc::Vec3f{0, 0, 0}));
	normals.assign(set.targetCount, std::vector<sgc::Vec3f>(vertexCount, sgc::Vec3f{0, 0, 0}));
	if (set.rowStart.size() != vertexCount + 1) { return; }
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		for (std::uint32_t e = set.rowStart[v]; e < set.rowStart[v + 1]; ++e)
		{
			const GltfMorphEntry& entry = set.entries[e];
			if (entry.target >= set.targetCount) { continue; }
			positions[entry.target][v] = entry.positionDelta;
			normals[entry.target][v] = entry.normalDelta;
		}
	}
}

/// @brief GPU の要素の並び (kMorphEntryWords 個ずつ)。シェーダーが Load4 を 2 回で読む
[[nodiscard]] inline std::vector<std::uint32_t> packMorphEntries(const GltfMorphSet& set)
{
	std::vector<std::uint32_t> words(set.entries.size() * kMorphEntryWords, 0u);
	for (std::size_t i = 0; i < set.entries.size(); ++i)
	{
		const GltfMorphEntry& e = set.entries[i];
		const float f[6] = {e.positionDelta.x, e.positionDelta.y, e.positionDelta.z, e.normalDelta.x, e.normalDelta.y, e.normalDelta.z};
		std::uint32_t* w = words.data() + i * kMorphEntryWords;
		w[0] = e.target;
		std::memcpy(w + 1, f, sizeof(f));
	}
	return words;
}

/// @brief 元の頂点の添字 vertices (LOD の段) だけを詰めた CSR。差分は元の頂点の添字で引く
[[nodiscard]] inline GltfMorphSet compactMorphSet(const GltfMorphSet& set, std::span<const std::uint32_t> vertices)
{
	GltfMorphSet out;
	if (set.empty()) { return out; }
	out.targetCount = set.targetCount;
	out.maxAbsPosition = set.maxAbsPosition;
	out.rowStart.reserve(vertices.size() + 1);
	out.rowStart.push_back(0);
	for (const std::uint32_t v : vertices)
	{
		if (v + 1 < set.rowStart.size())
		{
			out.entries.insert(out.entries.end(), set.entries.begin() + set.rowStart[v], set.entries.begin() + set.rowStart[v + 1]);
		}
		out.rowStart.push_back(static_cast<std::uint32_t>(out.entries.size()));
	}
	return out;
}

} // namespace mitiru::render
