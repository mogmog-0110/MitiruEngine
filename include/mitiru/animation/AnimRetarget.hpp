#pragma once

/// @file AnimRetarget.hpp
/// @brief 別の骨格で作ったクリップを、骨の名前の対応表 (`*.bonemap.json`) で自分の骨格へ写す (読み込みの時に 1 回)。
/// @details 骨ごとに、元のクリップの「モデル空間でレスト姿勢から回った量」を、こちらのレスト姿勢へ掛ける。
///          レスト姿勢の違い (T ポーズと A ポーズ、骨の局所の軸の取り方) は、対応する子の骨へ向かう向きを
///          元の骨格の向きへそろえる回転 (レスト姿勢の補正) で吸収するので、どの時刻でも骨の向きが元と一致する。
///          骨の長さはこちらの骨格のまま。ルートの骨の移動だけを写し、腰の高さの比 (無ければ骨格の大きさの比) を掛ける。
///          両方の骨格は同じ向き (+Z が前、+Y が上) で置かれている前提。FBX は取り込みでこの向きにそろう。
///          使う演算は加減乗除と sqrt だけなので、DLL と host が同じファイルから同じクリップを作る。

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/animation/AnimAssetBuild.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::animation
{

/// @brief 骨の名前の対応表。bones は (元の骨, こちらの骨) の組
struct AnimBoneMap
{
	std::vector<std::pair<std::string, std::string>> bones;
	std::string root;      ///< 移動を写す元の骨。空なら対応の中で最も根に近い骨
	float scale = 0.0f;    ///< ルートの移動に掛ける倍率。0 なら腰の高さの比
};

/// @brief `{ "root": "mixamorig:Hips", "scale": 1.0, "bones": { "mixamorig:Hips": "pelvis", ... } }` を読む
[[nodiscard]] inline std::optional<AnimBoneMap> parseBoneMap(const nlohmann::json& j, std::string* error = nullptr)
{
	if (!j.is_object() || !j.contains("bones") || !j["bones"].is_object())
	{
		if (error != nullptr) { *error = "bonemap: { \"bones\": { \"元の骨\": \"こちらの骨\" } } の形で書く"; }
		return std::nullopt;
	}
	AnimBoneMap map;
	for (const auto& [src, dst] : j["bones"].items())
	{
		if (dst.is_string()) { map.bones.emplace_back(src, dst.get<std::string>()); }
	}
	if (j.contains("root") && j["root"].is_string()) { map.root = j["root"].get<std::string>(); }
	if (j.contains("scale") && j["scale"].is_number()) { map.scale = j["scale"].get<float>(); }
	return map;
}

namespace detail
{

struct RetargetPair
{
	int src = -1;
	int dst = -1;
	sgc::Vec4f srcRestInv{0, 0, 0, 1};   ///< 元のレストのモデル空間の回転の逆
	sgc::Vec4f dstRef{0, 0, 0, 1};       ///< 補正を掛けたこちらのレストのモデル空間の回転
};

[[nodiscard]] inline std::vector<sgc::Mat4f> restModels(const std::vector<render::GltfNode>& nodes, const std::vector<int>& order)
{
	std::vector<sgc::Mat4f> out(nodes.size(), sgc::Mat4f::identity());
	for (const int i : order)
	{
		const auto& n = nodes[static_cast<std::size_t>(i)];
		const auto local = localMatrix({n.translation, n.rotation, n.scale});
		out[static_cast<std::size_t>(i)] = n.parent >= 0 ? out[static_cast<std::size_t>(n.parent)] * local : local;
	}
	return out;
}

[[nodiscard]] inline sgc::Vec3f matPos(const sgc::Mat4f& m) { return {m.m[0][3], m.m[1][3], m.m[2][3]}; }

[[nodiscard]] inline bool isAncestor(const std::vector<render::GltfNode>& nodes, int ancestor, int node)
{
	std::size_t guard = 0;
	for (int p = nodes[static_cast<std::size_t>(node)].parent; p >= 0 && guard < nodes.size(); p = nodes[static_cast<std::size_t>(p)].parent, ++guard)
	{
		if (p == ancestor) { return true; }
	}
	return false;
}

[[nodiscard]] inline int nodeDepth(const std::vector<render::GltfNode>& nodes, int node)
{
	int d = 0;
	for (int p = nodes[static_cast<std::size_t>(node)].parent; p >= 0 && d < static_cast<int>(nodes.size()); p = nodes[static_cast<std::size_t>(p)].parent) { ++d; }
	return d;
}

/// 両方の骨格で子孫に当たる対応のうち、こちらの骨格で最も近いもの (無ければ -1)
[[nodiscard]] inline int nearestMappedChild(const std::vector<RetargetPair>& pairs, std::size_t self,
                                            const std::vector<render::GltfNode>& src, const std::vector<render::GltfNode>& dst)
{
	int best = -1;
	int bestDepth = 0;
	for (std::size_t k = 0; k < pairs.size(); ++k)
	{
		if (k == self || !isAncestor(dst, pairs[self].dst, pairs[k].dst) || !isAncestor(src, pairs[self].src, pairs[k].src)) { continue; }
		const int d = nodeDepth(dst, pairs[k].dst);
		if (best < 0 || d < bestDepth) { best = static_cast<int>(k); bestDepth = d; }
	}
	return best;
}

struct RetargetRig
{
	std::vector<RetargetPair> pairs;   ///< こちらの骨格の親から子の順
	std::vector<int> dstOrder;
	std::vector<int> dstMapped;        ///< こちらのノードごとの pairs の添字 (-1 は対応なし)
	std::vector<sgc::Mat4f> srcRest, dstRest;
	int root = -1;                     ///< pairs の添字
	float scale = 1.0f;
};

[[nodiscard]] inline std::vector<RetargetPair> resolvePairs(const AnimBoneMap& map, const std::vector<render::GltfNode>& src,
                                                            const std::vector<render::GltfNode>& dst, std::vector<std::string>& warnings)
{
	const auto find = [](const std::vector<render::GltfNode>& nodes, const std::string& name) {
		for (std::size_t i = 0; i < nodes.size(); ++i) { if (nodes[i].name == name) { return static_cast<int>(i); } }
		return -1;
	};
	std::vector<RetargetPair> pairs;
	for (const auto& [s, d] : map.bones)
	{
		RetargetPair p{find(src, s), find(dst, d)};
		const bool dup = std::any_of(pairs.begin(), pairs.end(), [&](const RetargetPair& q) { return q.dst == p.dst; });
		if (p.src < 0 || p.dst < 0 || dup) { warnings.push_back("retarget: 対応を捨てた: " + s + " -> " + d); continue; }
		pairs.push_back(p);
	}
	std::stable_sort(pairs.begin(), pairs.end(), [&](const RetargetPair& a, const RetargetPair& b) { return nodeDepth(dst, a.dst) < nodeDepth(dst, b.dst); });
	return pairs;
}

/// レスト姿勢の補正: 対応する子へ向かう向きを元の骨格の向きへそろえる。子の無い骨は親の補正を引き継ぐ
inline void computeRestCorrection(RetargetRig& rig, const std::vector<render::GltfNode>& src, const std::vector<render::GltfNode>& dst)
{
	std::vector<sgc::Vec4f> fix(rig.pairs.size(), kQuatIdentity);
	for (std::size_t i = 0; i < rig.pairs.size(); ++i)
	{
		auto& p = rig.pairs[i];
		const int child = nearestMappedChild(rig.pairs, i, src, dst);
		const auto at = [&](const std::vector<sgc::Mat4f>& m, int n) { return matPos(m[static_cast<std::size_t>(n)]); };
		if (child >= 0)
		{
			const auto& c = rig.pairs[static_cast<std::size_t>(child)];
			const auto dt = at(rig.dstRest, c.dst) - at(rig.dstRest, p.dst);
			const auto ds = at(rig.srcRest, c.src) - at(rig.srcRest, p.src);
			if (dt.lengthSquared() > 1e-12f && ds.lengthSquared() > 1e-12f) { fix[i] = quatFromTo(dt.normalized(), ds.normalized()); }
		}
		else
		{
			for (int a = dst[static_cast<std::size_t>(p.dst)].parent; a >= 0; a = dst[static_cast<std::size_t>(a)].parent)
			{
				const int k = rig.dstMapped[static_cast<std::size_t>(a)];
				if (k >= 0) { fix[i] = fix[static_cast<std::size_t>(k)]; break; }
			}
		}
		p.srcRestInv = quatConj(decomposeAffine(rig.srcRest[static_cast<std::size_t>(p.src)]).r);
		p.dstRef = quatNormalize(quatMul(fix[i], decomposeAffine(rig.dstRest[static_cast<std::size_t>(p.dst)]).r));
	}
}

/// ルートの移動の倍率: 腰の高さの比、腰が地面にあれば対応した骨の広がりの比
[[nodiscard]] inline float retargetScale(const RetargetRig& rig)
{
	const auto& r = rig.pairs[static_cast<std::size_t>(rig.root)];
	const float hs = matPos(rig.srcRest[static_cast<std::size_t>(r.src)]).y;
	const float hd = matPos(rig.dstRest[static_cast<std::size_t>(r.dst)]).y;
	if (hs > 1e-4f && hd > 1e-4f) { return hd / hs; }
	float es = 0.0f, ed = 0.0f;
	for (const auto& p : rig.pairs)
	{
		es = std::max(es, (matPos(rig.srcRest[static_cast<std::size_t>(p.src)]) - matPos(rig.srcRest[static_cast<std::size_t>(r.src)])).length());
		ed = std::max(ed, (matPos(rig.dstRest[static_cast<std::size_t>(p.dst)]) - matPos(rig.dstRest[static_cast<std::size_t>(r.dst)])).length());
	}
	return es > 1e-6f ? ed / es : 1.0f;
}

} // namespace detail

} // namespace mitiru::animation

#include <mitiru/animation/detail/AnimRetarget_impl.hpp>
