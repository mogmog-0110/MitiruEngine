#pragma once

/// @file RagdollRig.hpp
/// @brief ragdoll の部位とモデルの骨の対応 (`<名前>.ragdoll.json`) と、アニメの姿勢 ↔ 部位の位置と向きの変換。
/// @details 物理エンジンには依らない。部位はモデルの骨から作るので、カプセルの長さと関節の位置はモデルの骨格に合う。
///          部位 i は骨 node の頭 (レストの位置) から tip までのカプセルで、関節は骨の頭に置く。
///          部位の親は、骨の親をたどって最初に見つかる部位の骨。並びは親が先に来ること (Jolt の骨格と同じ決まり)。
///          姿勢 → 部位: 骨のモデル行列 × 部位の offset (骨の座標系、レストで決まる) をワールドへ。
///          部位 → 姿勢: blendRagdollPose が骨ごとの局所の回転を部位の重みで混ぜ、親から順に組み立てる。
///          混ぜるのが局所の回転なので、重みが途中でも手足は胴から離れない。重みが全部 0 ならアニメの行列をそのまま返す。
///          計算は加減乗除と sqrt だけ (AnimMath.hpp と同じ)。同じ入力からはどの CPU でも同じビットが出る。

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::physics3d
{

inline constexpr int kRagdollMaxParts = 16;

/// @brief 部位 1 つのワールドの位置と向き。28 バイトの POD で GameMemory に置ける
struct RagdollPartPose
{
	sgc::Vec3f position{0, 0, 0};
	sgc::Vec4f rotation{0, 0, 0, 1};
};
static_assert(std::is_trivially_copyable_v<RagdollPartPose>);
static_assert(sizeof(RagdollPartPose) == 28);

/// @brief sidecar の 1 行 (部位の名前・骨・先端・太さ・曲がる範囲)
struct RagdollPartDef
{
	std::string name;
	std::string bone;
	std::string tip;                    ///< 先端の骨の名前 (空なら tipOffset)
	sgc::Vec3f tipOffset{0, 0, 0};      ///< 先端の位置 (モデル空間、骨の頭から)
	float radius = 0.05f;               ///< モデルの単位
	float coneDeg = 45.0f;              ///< 親に対して振れる角度の半分
	float twistDeg = 20.0f;             ///< 部位の軸まわりにねじれる角度 (片側)
	bool upper = false;                 ///< 上半身 (被弾で崩す側)
};

/// @brief モデルから組んだ部位 1 つ (読み取り専用)
struct RagdollPart
{
	std::string name;
	int node = -1;                      ///< 骨
	int parent = -1;                    ///< 親の部位 (-1 = 根)
	sgc::Vec3f restFrom{0, 0, 0};       ///< 関節 (骨の頭) のレストの位置 (モデル空間)
	sgc::Vec3f restTo{0, 0, 0};         ///< 先端のレストの位置
	float radius = 0.05f;
	float coneDeg = 45.0f;
	float twistDeg = 20.0f;
	bool upper = false;
	sgc::Vec3f offsetT{0, 0, 0};        ///< 骨の座標系での部位の中心 (骨のスケールで割った値)
	sgc::Vec4f offsetR{0, 0, 0, 1};     ///< 骨の座標系での部位の向き (部位の +Y が骨の頭から先端へ)
};

struct RagdollRig
{
	std::vector<RagdollPart> parts;
	std::vector<int> nodePart;          ///< ノード → 部位 (-1 = 部位を持たない骨)

	[[nodiscard]] int partCount() const noexcept { return static_cast<int>(parts.size()); }
	[[nodiscard]] bool valid() const noexcept { return !parts.empty(); }
	[[nodiscard]] int findPart(std::string_view name) const noexcept
	{
		for (std::size_t i = 0; i < parts.size(); ++i)
		{
			if (parts[i].name == name) { return static_cast<int>(i); }
		}
		return -1;
	}
};

/// @brief モデルのパスの拡張子を .ragdoll.json に置き換える (hero.glb なら hero.ragdoll.json)
[[nodiscard]] inline std::string ragdollSidecarPath(std::string_view modelPath)
{
	const auto slash = modelPath.find_last_of("/\\");
	const auto dot = modelPath.find_last_of('.');
	const bool hasExt = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	return std::string(hasExt ? modelPath.substr(0, dot) : modelPath) + ".ragdoll.json";
}

namespace detail
{

[[nodiscard]] inline sgc::Vec3f jsonVec3(const nlohmann::json& j, const char* key, sgc::Vec3f fallback)
{
	if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3) { return fallback; }
	const auto f = [&](std::size_t i) { return j[key][i].is_number() ? j[key][i].get<float>() : 0.0f; };
	return {f(0), f(1), f(2)};
}

[[nodiscard]] inline std::optional<RagdollPartDef> parsePartDef(const nlohmann::json& p)
{
	if (!p.is_object() || !p.contains("name") || !p["name"].is_string() || !p.contains("bone") || !p["bone"].is_string())
	{
		return std::nullopt;
	}
	RagdollPartDef d;
	d.name = p["name"].get<std::string>();
	d.bone = p["bone"].get<std::string>();
	if (p.contains("tip") && p["tip"].is_string()) { d.tip = p["tip"].get<std::string>(); }
	d.tipOffset = jsonVec3(p, "tipOffset", {0, 0, 0});
	d.radius = p.value("radius", d.radius);
	d.coneDeg = p.value("cone", d.coneDeg);
	d.twistDeg = p.value("twist", d.twistDeg);
	d.upper = p.value("upper", false);
	return d;
}

/// @brief 部位の親: 骨の親をたどって最初に見つかる部位
[[nodiscard]] inline int parentPartOf(const animation::AnimAsset& asset, const std::vector<int>& nodePart, int node)
{
	for (int n = asset.nodes[static_cast<std::size_t>(node)].parent; n >= 0;
	     n = asset.nodes[static_cast<std::size_t>(n)].parent)
	{
		if (nodePart[static_cast<std::size_t>(n)] >= 0) { return nodePart[static_cast<std::size_t>(n)]; }
	}
	return -1;
}

/// @brief 部位 1 つをレストの骨から組む。組めなければ理由を返す
[[nodiscard]] inline std::string makePart(const animation::AnimAsset& asset, const animation::AnimPose& rest,
                                          const RagdollPartDef& d, RagdollPart& p)
{
	const auto bone = animation::decomposeAffine(rest.model[static_cast<std::size_t>(p.node)]);
	const int tipNode = d.tip.empty() ? -1 : asset.findNode(d.tip);
	if (!d.tip.empty() && tipNode < 0) { return "ragdoll の先端の骨が無い: " + d.tip; }
	p.restFrom = bone.t;
	p.restTo = (tipNode >= 0) ? animation::decomposeAffine(rest.model[static_cast<std::size_t>(tipNode)]).t
	                          : bone.t + d.tipOffset;
	const sgc::Vec3f axis = p.restTo - p.restFrom;
	if (axis.lengthSquared() < 1e-12f) { return "ragdoll の部位の長さが 0: " + d.name; }
	p.radius = d.radius;
	p.coneDeg = d.coneDeg;
	p.twistDeg = d.twistDeg;
	p.upper = d.upper;
	const sgc::Vec4f partR = animation::quatFromTo({0, 1, 0}, axis.normalized());
	const sgc::Vec4f boneInv = animation::quatConj(bone.r);
	const sgc::Vec3f local = animation::quatRotate(boneInv, (p.restFrom + p.restTo) * 0.5f - bone.t);
	const auto safeDiv = [](float v, float s) { return (s > 1e-12f || s < -1e-12f) ? v / s : v; };
	p.offsetT = {safeDiv(local.x, bone.s.x), safeDiv(local.y, bone.s.y), safeDiv(local.z, bone.s.z)};
	p.offsetR = animation::quatNormalize(animation::quatMul(boneInv, partR));
	return {};
}

} // namespace detail

/// @brief sidecar の JSON を読む。`{"parts": [{"name", "bone", "tip" | "tipOffset", "radius", "cone", "twist", "upper"}]}`
[[nodiscard]] inline std::optional<std::vector<RagdollPartDef>> parseRagdollSidecar(std::string_view text,
                                                                                    std::string* error = nullptr)
{
	const auto root = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
	if (root.is_discarded() || !root.is_object() || !root.contains("parts") || !root["parts"].is_array())
	{
		if (error != nullptr) { *error = "ragdoll.json に parts の配列が無い"; }
		return std::nullopt;
	}
	std::vector<RagdollPartDef> out;
	for (const auto& p : root["parts"])
	{
		auto d = detail::parsePartDef(p);
		if (!d)
		{
			if (error != nullptr) { *error = "ragdoll.json の部位に name と bone が無い"; }
			return std::nullopt;
		}
		out.push_back(std::move(*d));
	}
	return out;
}

/// @brief 骨格と部位の定義から組む。骨が見つからない・親が後に来る・部位が多すぎる時は空を返し、warnings に理由を足す
[[nodiscard]] inline RagdollRig buildRagdollRig(const animation::AnimAsset& asset, const std::vector<RagdollPartDef>& defs,
                                                std::vector<std::string>* warnings = nullptr)
{
	const auto fail = [&](const std::string& why) {
		if (warnings != nullptr) { warnings->push_back(why); }
		return RagdollRig{};
	};
	if (defs.empty() || defs.size() > static_cast<std::size_t>(kRagdollMaxParts)) { return fail("部位の数が 1..16 でない"); }
	animation::AnimPose rest;
	animation::evaluatePose(asset, animation::AnimPoseParams{}, rest);
	RagdollRig rig;
	rig.nodePart.assign(asset.nodes.size(), -1);
	for (std::size_t i = 0; i < defs.size(); ++i)
	{
		const int node = asset.findNode(defs[i].bone);
		if (node < 0) { return fail("ragdoll の骨が無い: " + defs[i].bone); }
		rig.nodePart[static_cast<std::size_t>(node)] = static_cast<int>(i);
	}
	for (std::size_t i = 0; i < defs.size(); ++i)
	{
		RagdollPart p;
		p.name = defs[i].name;
		p.node = asset.findNode(defs[i].bone);
		p.parent = detail::parentPartOf(asset, rig.nodePart, p.node);
		if (p.parent >= static_cast<int>(i)) { return fail("ragdoll の部位は親を先に書く: " + p.name); }
		if (p.parent < 0 && i != 0) { return fail("ragdoll の根は先頭の 1 つだけ: " + p.name); }
		if (auto why = detail::makePart(asset, rest, defs[i], p); !why.empty()) { return fail(why); }
		rig.parts.push_back(std::move(p));
	}
	return rig;
}

/// @brief モデルの隣の `<名前>.ragdoll.json` を読んで組む (asset pack を mount 済みならそこから)。無い・読めないなら空
[[nodiscard]] inline RagdollRig loadRagdollRig(const animation::AnimAsset& asset, std::string_view modelPath,
                                               std::vector<std::string>* warnings = nullptr)
{
	const auto bytes = vfs::readGlobal(ragdollSidecarPath(modelPath));
	if (!bytes) { return {}; }
	std::string error;
	const auto defs = parseRagdollSidecar(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
	                                      &error);
	if (!defs)
	{
		if (warnings != nullptr) { warnings->push_back(error); }
		return {};
	}
	return buildRagdollRig(asset, *defs, warnings);
}

/// @brief 配置の行列 (平行移動・回転・一様なスケール) を分けた形
struct RagdollPlacement
{
	sgc::Vec3f t{0, 0, 0};
	sgc::Vec4f r{0, 0, 0, 1};
	float s = 1.0f;

	[[nodiscard]] static RagdollPlacement of(const sgc::Mat4f& instanceWorld)
	{
		const auto trs = animation::decomposeAffine(instanceWorld);
		return {trs.t, trs.r, trs.s.x};
	}
};

/// @brief 姿勢から部位のワールドの位置と向きを出す (motor と運動学で追わせる目標)
inline void ragdollTargetsFromPose(const RagdollRig& rig, std::span<const sgc::Mat4f> nodeModel,
                                   const sgc::Mat4f& instanceWorld, std::span<RagdollPartPose> out)
{
	const RagdollPlacement place = RagdollPlacement::of(instanceWorld);
	for (std::size_t i = 0; i < rig.parts.size() && i < out.size(); ++i)
	{
		const auto& p = rig.parts[i];
		const auto bone = animation::decomposeAffine(nodeModel[static_cast<std::size_t>(p.node)]);
		const sgc::Vec3f scaled{p.offsetT.x * bone.s.x, p.offsetT.y * bone.s.y, p.offsetT.z * bone.s.z};
		const sgc::Vec3f modelPos = bone.t + animation::quatRotate(bone.r, scaled);
		const sgc::Vec4f modelRot = animation::quatMul(bone.r, p.offsetR);
		out[i].position = place.t + animation::quatRotate(place.r, modelPos * place.s);
		out[i].rotation = animation::quatNormalize(animation::quatMul(place.r, modelRot));
	}
}

/// @brief blendRagdollPose の作業場所 (ノード数ぶん。同じモデルなら 2 回目から確保しない)
struct RagdollBlendScratch
{
	std::vector<animation::NodeTRS> physical;   ///< 部位から組んだモデル空間の姿勢
	std::vector<animation::NodeTRS> animModel;  ///< アニメのモデル空間の姿勢を分けたもの
};

namespace detail
{

[[nodiscard]] inline animation::NodeTRS composeTRS(const animation::NodeTRS& parent, const animation::NodeTRS& local)
{
	animation::NodeTRS out;
	const sgc::Vec3f scaled{parent.s.x * local.t.x, parent.s.y * local.t.y, parent.s.z * local.t.z};
	out.t = parent.t + animation::quatRotate(parent.r, scaled);
	out.r = animation::quatNormalize(animation::quatMul(parent.r, local.r));
	out.s = {parent.s.x * local.s.x, parent.s.y * local.s.y, parent.s.z * local.s.z};
	return out;
}

/// @brief 部位のワールドの姿勢から、その骨のモデル空間の姿勢 (スケールはアニメのもの) を出す
[[nodiscard]] inline animation::NodeTRS boneFromPart(const RagdollPart& p, const RagdollPartPose& part,
                                                     const RagdollPlacement& place, const sgc::Vec3f& boneScale)
{
	const sgc::Vec4f placeInv = animation::quatConj(place.r);
	const float invS = (place.s > 1e-12f) ? 1.0f / place.s : 1.0f;
	const sgc::Vec3f modelPos = animation::quatRotate(placeInv, part.position - place.t) * invS;
	const sgc::Vec4f modelRot = animation::quatMul(placeInv, part.rotation);
	animation::NodeTRS bone;
	bone.r = animation::quatNormalize(animation::quatMul(modelRot, animation::quatConj(p.offsetR)));
	bone.s = boneScale;
	const sgc::Vec3f scaled{p.offsetT.x * boneScale.x, p.offsetT.y * boneScale.y, p.offsetT.z * boneScale.z};
	bone.t = modelPos - animation::quatRotate(bone.r, scaled);
	return bone;
}

/// @brief 部位から組んだ姿勢 (部位を持たない骨は親に付いたままアニメの局所の姿勢)
inline void physicalPose(const animation::AnimAsset& asset, const RagdollRig& rig, const animation::AnimPose& anim,
                         std::span<const RagdollPartPose> parts, const RagdollPlacement& place, RagdollBlendScratch& s)
{
	s.physical.resize(asset.nodes.size());
	s.animModel.resize(asset.nodes.size());
	for (const int idx : asset.evalOrder)
	{
		const auto u = static_cast<std::size_t>(idx);
		const int parent = asset.nodes[u].parent;
		s.animModel[u] = animation::decomposeAffine(anim.model[u]);
		const int part = rig.nodePart[u];
		if (part >= 0)
		{
			s.physical[u] = boneFromPart(rig.parts[static_cast<std::size_t>(part)], parts[static_cast<std::size_t>(part)],
			                             place, s.animModel[u].s);
			continue;
		}
		s.physical[u] = (parent >= 0) ? composeTRS(s.physical[static_cast<std::size_t>(parent)], anim.local[u])
		                              : s.animModel[u];
	}
}

} // namespace detail

/// @brief アニメの姿勢と部位の姿勢を部位ごとの重みで混ぜ、ノードのモデル行列 (drawModelNodeMatrices の並び) を出す
/// @param weights 部位ごとの重み (0 = アニメ、1 = 物理)。足りない部位は 0
/// @details 部位を持たない骨は親に付いたままアニメの局所の姿勢で動く。根の部位の骨は位置もモデル空間で混ぜる
///          (体ごと倒れる・起き上がる動きは位置を持つため)
inline void blendRagdollPose(const animation::AnimAsset& asset, const RagdollRig& rig, const animation::AnimPose& anim,
                             std::span<const RagdollPartPose> parts, std::span<const float> weights,
                             const sgc::Mat4f& instanceWorld, RagdollBlendScratch& scratch, std::span<sgc::Mat4f> outModel)
{
	const std::size_t n = std::min(asset.nodes.size(), outModel.size());
	const auto weightOf = [&](int part) {
		return (part >= 0 && static_cast<std::size_t>(part) < weights.size())
			? std::clamp(weights[static_cast<std::size_t>(part)], 0.0f, 1.0f) : 0.0f;
	};
	bool any = false;
	for (std::size_t i = 0; i < rig.parts.size(); ++i) { any = any || weightOf(static_cast<int>(i)) > 0.0f; }
	std::copy_n(anim.model.begin(), n, outModel.begin());
	if (!any || parts.size() < rig.parts.size() || rig.nodePart.size() != asset.nodes.size()) { return; }

	detail::physicalPose(asset, rig, anim, parts, RagdollPlacement::of(instanceWorld), scratch);
	for (const int idx : asset.evalOrder)
	{
		const auto u = static_cast<std::size_t>(idx);
		const int parent = asset.nodes[u].parent;
		const int part = rig.nodePart[u];
		const float w = weightOf(part);
		if (u >= n || (parent < 0 && part < 0)) { continue; }
		if (part >= 0 && rig.parts[static_cast<std::size_t>(part)].parent < 0)
		{
			animation::NodeTRS m = scratch.animModel[u];
			m.t = animation::lerp3(m.t, scratch.physical[u].t, w);
			m.r = animation::quatSlerp(m.r, scratch.physical[u].r, w);
			outModel[u] = animation::localMatrix(m);
			continue;
		}
		animation::NodeTRS local = anim.local[u];
		if (w > 0.0f)
		{
			const auto& pp = scratch.physical[static_cast<std::size_t>(parent)];
			const sgc::Vec4f physLocal =
				animation::quatNormalize(animation::quatMul(animation::quatConj(pp.r), scratch.physical[u].r));
			local.r = animation::quatSlerp(animation::quatNormalize(local.r), physLocal, w);
		}
		outModel[u] = outModel[static_cast<std::size_t>(parent)] * animation::localMatrix(local);
	}
}

} // namespace mitiru::physics3d
