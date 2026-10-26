#pragma once

/// @file GltfMorphBuild.hpp
/// @brief cgltf の primitive のモーフターゲットから、頂点ごとの CSR (GltfMorphSet) を作る。
/// @details アクセサは cgltf_accessor_unpack_floats でスパースまで展開して読む。値が 0 の (頂点, モーフ) は持たない。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/GltfTypes.hpp>

#include <cgltf.h>

namespace mitiru::render::detail
{

/// @brief アクセサを count 個の Vec3 に展開する。数が合わない・読めないときは false (out は触らない)
[[nodiscard]] inline bool unpackVec3(const cgltf_accessor* accessor, cgltf_size count, std::vector<float>& out)
{
	if (accessor == nullptr || accessor->count != count || accessor->type != cgltf_type_vec3) { return false; }
	std::vector<float> tmp(count * 3);
	if (cgltf_accessor_unpack_floats(accessor, tmp.data(), tmp.size()) != tmp.size()) { return false; }
	out = std::move(tmp);
	return true;
}

[[nodiscard]] inline bool anyNonZero(const float* v) noexcept { return v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f; }

/// @brief primitive のモーフの差分を CSR に詰める。モーフが無い・差分がすべて 0 なら空
[[nodiscard]] inline GltfMorphSet buildMorphSet(const cgltf_primitive& prim, cgltf_size vertexCount)
{
	GltfMorphSet set;
	if (prim.targets_count == 0 || vertexCount == 0) { return set; }
	set.targetCount = static_cast<std::uint32_t>(prim.targets_count);
	set.maxAbsPosition.assign(prim.targets_count, sgc::Vec3f{0, 0, 0});

	struct Hit
	{
		std::uint32_t vertex;
		sgc::Vec3f dp;
		sgc::Vec3f dn;
	};
	std::vector<std::vector<Hit>> perTarget(prim.targets_count);
	std::vector<std::uint32_t> rowCount(vertexCount, 0);
	std::vector<float> dp, dn;
	for (cgltf_size t = 0; t < prim.targets_count; ++t)
	{
		const cgltf_accessor* posAccessor = nullptr;
		const cgltf_accessor* normAccessor = nullptr;
		for (cgltf_size a = 0; a < prim.targets[t].attributes_count; ++a)
		{
			const auto& attr = prim.targets[t].attributes[a];
			if (attr.type == cgltf_attribute_type_position) { posAccessor = attr.data; }
			else if (attr.type == cgltf_attribute_type_normal) { normAccessor = attr.data; }
		}
		dp.clear();
		dn.clear();
		const bool hasPos = unpackVec3(posAccessor, vertexCount, dp);
		const bool hasNorm = unpackVec3(normAccessor, vertexCount, dn);
		if (!hasPos && !hasNorm) { continue; }
		auto& hits = perTarget[t];
		auto& maxAbs = set.maxAbsPosition[t];
		for (cgltf_size v = 0; v < vertexCount; ++v)
		{
			const float* p = hasPos ? &dp[v * 3] : nullptr;
			const float* n = hasNorm ? &dn[v * 3] : nullptr;
			if (!((p != nullptr && anyNonZero(p)) || (n != nullptr && anyNonZero(n)))) { continue; }
			Hit h{static_cast<std::uint32_t>(v), {0, 0, 0}, {0, 0, 0}};
			if (p != nullptr)
			{
				h.dp = {p[0], p[1], p[2]};
				maxAbs = {std::max(maxAbs.x, std::fabs(p[0])), std::max(maxAbs.y, std::fabs(p[1])), std::max(maxAbs.z, std::fabs(p[2]))};
			}
			if (n != nullptr) { h.dn = {n[0], n[1], n[2]}; }
			hits.push_back(h);
			++rowCount[v];
		}
	}

	std::size_t total = 0;
	for (const auto& hits : perTarget) { total += hits.size(); }
	if (total == 0)
	{
		set.targetCount = 0;
		set.maxAbsPosition.clear();
		return set;
	}
	set.rowStart.assign(vertexCount + 1, 0);
	for (cgltf_size v = 0; v < vertexCount; ++v) { set.rowStart[v + 1] = set.rowStart[v] + rowCount[v]; }
	set.entries.resize(total);
	std::vector<std::uint32_t> cursor(set.rowStart.begin(), set.rowStart.end() - 1);
	for (cgltf_size t = 0; t < prim.targets_count; ++t)
	{
		for (const Hit& h : perTarget[t])
		{
			set.entries[cursor[h.vertex]++] = {static_cast<std::uint32_t>(t), h.dp, h.dn};
		}
	}
	return set;
}

} // namespace mitiru::render::detail
