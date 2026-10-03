#pragma once

/// @file AnimSpringBones.hpp
/// @brief 揺れ物 (尻尾・耳・髪の房) の骨のばね。評価した姿勢の後、スキニングの前に掛ける (ADR 0069、docs/PHYSICS_FX.md)。
/// @details 骨の先端をワールドで Verlet 積分し (慣性・アニメの向きへ戻る力・重力)、骨の長さに戻してから
///          当たりの球の外へ押し出し、骨をその先端へ向ける。先端の位置だけを SpringBoneState (flat POD) に
///          持つので、GameMemory に置けば巻き戻しとリプレイで揺れも戻る。update で stepSpringBones (進めて姿勢へ掛ける)、
///          draw で applySpringBones (進めずに姿勢へ掛ける) を呼ぶと、同じ状態から同じ姿勢になる。
///          力の強さは骨の長さを 1 とした割合なので、モデルの単位 (m / cm) に依らない。演算は加減乗除と sqrt だけ。
///          骨の並びはモデルの隣の `<モデル>.springs.json` に書く。

#include <algorithm>
#include <cmath>
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

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimIK.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::animation
{

inline constexpr int kSpringMaxJoints = 32;

struct SpringJointState
{
	sgc::Vec3f current{0, 0, 0};    ///< 骨の先端 (ワールド)
	sgc::Vec3f previous{0, 0, 0};
};

/// @brief 揺れの状態。GameMemory に置く。initialized が 0 なら次の step でアニメの姿勢から始める
struct SpringBoneState
{
	std::uint32_t jointCount = 0;
	std::uint32_t initialized = 0;
	sgc::Vec3f origin{0, 0, 0};     ///< 前の step の配置の原点 (体ごとの平行移動を慣性から除く)
	float reserved = 0.0f;
	SpringJointState joints[kSpringMaxJoints];
};

static_assert(std::is_trivially_copyable_v<SpringBoneState>);
static_assert(sizeof(SpringBoneState) == 24 + 24 * kSpringMaxJoints);

/// @brief 根の骨から、子が 1 つの間たどる 1 本の鎖
struct SpringChainDef
{
	std::string root;
	float stiffness = 1.0f;          ///< アニメの向きへ戻る強さ
	float drag = 0.4f;               ///< 0..1。速さを捨てる割合
	float gravity = 0.0f;            ///< 重力の強さ
	sgc::Vec3f gravityDir{0, -1, 0};
	float radius = 0.0f;             ///< 先端の当たりの半径 (モデルの単位)
};

/// @brief 骨に付けた当たりの球 (体・脚)。offset と radius はモデルの単位
struct SpringColliderDef
{
	std::string bone;
	sgc::Vec3f offset{0, 0, 0};
	float radius = 0.1f;
};

struct SpringJoint
{
	int node = -1;
	int child = -1;                  ///< 鎖の次の骨。-1 なら先端は tipLocal
	sgc::Vec3f tipLocal{0, 0, 0};    ///< 末端の骨の先端 (骨の局所)
	float stiffness = 1.0f;
	float drag = 0.4f;
	float gravity = 0.0f;
	sgc::Vec3f gravityDir{0, -1, 0};
	float radius = 0.0f;
};

struct SpringCollider
{
	int node = -1;
	sgc::Vec3f offset{0, 0, 0};
	float radius = 0.1f;
};

/// @brief 骨格に合わせて組んだ揺れ物 (読み取り専用。DLL と host が同じものを持てる)
struct SpringBoneSet
{
	std::vector<SpringJoint> joints;
	std::vector<SpringCollider> colliders;

	[[nodiscard]] bool valid() const noexcept { return !joints.empty(); }
	[[nodiscard]] int jointCount() const noexcept { return static_cast<int>(joints.size()); }
};

namespace detail::spring
{

/// @brief 唯一の子 (無いか複数なら -1)
[[nodiscard]] inline int onlyChild(const AnimAsset& asset, int node)
{
	const auto& ch = asset.nodes[static_cast<std::size_t>(node)].children;
	return ch.size() == 1 ? ch[0] : -1;
}

[[nodiscard]] inline sgc::Vec3f vec3(const nlohmann::json& j, const char* key, sgc::Vec3f fallback)
{
	if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3) { return fallback; }
	const auto f = [&](std::size_t i) { return j[key][i].is_number() ? j[key][i].get<float>() : 0.0f; };
	return {f(0), f(1), f(2)};
}

/// @brief 骨の局所の長さ 1 がワールドで何になるか (配置とモデルの行列のスケール)
[[nodiscard]] inline float worldScale(const sgc::Mat4f& world, const AnimPose& pose, int node)
{
	return (world * pose.model[static_cast<std::size_t>(node)]).transformVector({1, 0, 0}).length();
}

/// @brief 骨の先端 (今の姿勢、モデル空間)
[[nodiscard]] inline sgc::Vec3f animatedTip(const AnimPose& pose, const SpringJoint& j)
{
	const sgc::Vec3f local = j.child >= 0 ? pose.local[static_cast<std::size_t>(j.child)].t : j.tipLocal;
	return pose.model[static_cast<std::size_t>(j.node)].transformPoint(local);
}

/// @brief 骨を、モデル空間の先端が tipModel に来る向きへ回す
inline void orient(const AnimAsset& asset, AnimPose& pose, const SpringJoint& j, const sgc::Vec3f& tipModel)
{
	const sgc::Vec3f head = modelPosition(pose, j.node);
	animation::detail::rotateBoneToward(asset, pose, j.node, animatedTip(pose, j) - head, tipModel - head);
}

[[nodiscard]] inline sgc::Vec3f pushOut(const sgc::Vec3f& p, const sgc::Vec3f& c, float r)
{
	const sgc::Vec3f d = p - c;
	const float len2 = d.lengthSquared();
	if (len2 >= r * r || len2 <= 1e-20f) { return p; }
	return c + d * (r / std::sqrt(len2));
}

[[nodiscard]] inline sgc::Vec3f toLength(const sgc::Vec3f& head, const sgc::Vec3f& p, float length, const sgc::Vec3f& fallback)
{
	const sgc::Vec3f d = p - head;
	const float dl = d.length();
	return dl > 1e-12f ? head + d * (length / dl) : fallback;
}

/// @brief 1 本の骨の先端を 1 step 進める (head は骨の付け根、restTip はアニメの先端。どちらもワールド)。
/// @details carry は体の平行移動 (この step の配置の原点の差)。体と一緒に動く分は drag で減らさないので、
///          まっすぐ走るだけでは尻尾は後ろへ流れず、曲がる・止まる・跳ねる時に遅れる
[[nodiscard]] inline sgc::Vec3f integrate(const SpringJoint& j, const SpringJointState& s, const sgc::Vec3f& head,
                                          const sgc::Vec3f& restTip, float length, const sgc::Vec3f& carry, float dt)
{
	const sgc::Vec3f restDir = (restTip - head) / length;
	const sgc::Vec3f next = s.current + carry + (s.current - s.previous - carry) * (1.0f - j.drag)
	                        + restDir * (j.stiffness * dt * length) + j.gravityDir * (j.gravity * dt * length);
	return toLength(head, next, length, restTip);
}

} // namespace detail::spring

/// @brief 骨格と鎖・当たりの定義から組む。見つからない骨は warnings に書いて飛ばす
[[nodiscard]] inline SpringBoneSet buildSpringBones(const AnimAsset& asset, std::span<const SpringChainDef> chains,
                                                    std::span<const SpringColliderDef> colliders,
                                                    std::vector<std::string>* warnings = nullptr)
{
	const auto warn = [&](const std::string& why) { if (warnings != nullptr) { warnings->push_back(why); } };
	SpringBoneSet set;
	for (const auto& c : chains)
	{
		int node = asset.findNode(c.root);
		if (node < 0) { warn("springs: 骨が無い: " + c.root); continue; }
		while (node >= 0 && set.jointCount() < kSpringMaxJoints)
		{
			SpringJoint j{node, detail::spring::onlyChild(asset, node), {0, 0, 0}, c.stiffness, c.drag, c.gravity, c.gravityDir, c.radius};
			// 末端は、親から自分までと同じ長さと向きでもう 1 本続くものとする
			if (j.child < 0) { j.tipLocal = asset.nodes[static_cast<std::size_t>(node)].translation; }
			set.joints.push_back(j);
			node = j.child;
		}
	}
	for (const auto& c : colliders)
	{
		const int node = asset.findNode(c.bone);
		if (node < 0) { warn("springs: 当たりの骨が無い: " + c.bone); continue; }
		set.colliders.push_back({node, c.offset, c.radius});
	}
	return set;
}

/// @brief springs.json を読む。鎖は chains (root, stiffness, drag, gravity, gravityDir, radius)、当たりは colliders (bone, offset, radius)
[[nodiscard]] inline std::optional<SpringBoneSet> parseSpringSidecar(const AnimAsset& asset, std::string_view text,
                                                                     std::vector<std::string>* warnings = nullptr)
{
	const auto root = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
	if (root.is_discarded() || !root.is_object() || !root.contains("chains") || !root["chains"].is_array()) { return std::nullopt; }
	std::vector<SpringChainDef> chains;
	for (const auto& c : root["chains"])
	{
		if (!c.is_object() || !c.contains("root") || !c["root"].is_string()) { return std::nullopt; }
		SpringChainDef d;
		d.root = c["root"].get<std::string>();
		d.stiffness = c.value("stiffness", d.stiffness);
		d.drag = c.value("drag", d.drag);
		d.gravity = c.value("gravity", d.gravity);
		d.gravityDir = detail::spring::vec3(c, "gravityDir", d.gravityDir);
		d.radius = c.value("radius", d.radius);
		chains.push_back(std::move(d));
	}
	std::vector<SpringColliderDef> colliders;
	if (root.contains("colliders") && root["colliders"].is_array())
	{
		for (const auto& c : root["colliders"])
		{
			if (!c.is_object() || !c.contains("bone") || !c["bone"].is_string()) { return std::nullopt; }
			colliders.push_back({c["bone"].get<std::string>(), detail::spring::vec3(c, "offset", {0, 0, 0}), c.value("radius", 0.1f)});
		}
	}
	return buildSpringBones(asset, chains, colliders, warnings);
}

/// @brief モデルの隣の `<名前>.springs.json` を読んで組む (asset pack を mount 済みならそこから)。無い・読めないなら空
[[nodiscard]] inline SpringBoneSet loadSpringBones(const AnimAsset& asset, std::string_view modelPath,
                                                   std::vector<std::string>* warnings = nullptr)
{
	const auto dot = modelPath.find_last_of('.');
	const auto slash = modelPath.find_last_of("/\\");
	const bool hasExt = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	const auto bytes = vfs::readGlobal(std::string(hasExt ? modelPath.substr(0, dot) : modelPath) + ".springs.json");
	if (!bytes) { return {}; }
	auto set = parseSpringSidecar(asset, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), warnings);
	if (!set && warnings != nullptr) { warnings->push_back("springs.json に chains の配列が無いか、鎖に root が無い"); }
	return set.value_or(SpringBoneSet{});
}

/// @brief 次の step でアニメの姿勢から揺れを始め直す (ワープした時など)
inline void resetSpringBones(SpringBoneState& state) noexcept { state.initialized = 0; }

/// @brief 揺れを dt 秒進め、評価済みの pose に掛ける。instanceWorld は描画に渡すのと同じ配置の行列
inline void stepSpringBones(const AnimAsset& asset, const SpringBoneSet& set, const sgc::Mat4f& instanceWorld, float dt,
                            SpringBoneState& state, AnimPose& pose)
{
	namespace sp = detail::spring;
	const auto n = static_cast<std::uint32_t>(std::min(set.jointCount(), kSpringMaxJoints));
	if (state.initialized == 0 || state.jointCount != n)
	{
		for (std::uint32_t i = 0; i < n; ++i)
		{
			const sgc::Vec3f tip = instanceWorld.transformPoint(sp::animatedTip(pose, set.joints[i]));
			state.joints[i] = {tip, tip};
		}
		state.jointCount = n;
		state.initialized = 1;
		state.origin = instanceWorld.transformPoint({0, 0, 0});
	}
	const sgc::Vec3f origin = instanceWorld.transformPoint({0, 0, 0});
	const sgc::Vec3f carry = origin - state.origin;
	state.origin = origin;
	const sgc::Mat4f toModel = instanceWorld.inversed();
	for (std::uint32_t i = 0; i < n; ++i)
	{
		const SpringJoint& j = set.joints[i];
		SpringJointState& s = state.joints[i];
		const sgc::Vec3f head = instanceWorld.transformPoint(modelPosition(pose, j.node));
		const sgc::Vec3f restTip = instanceWorld.transformPoint(sp::animatedTip(pose, j));
		const float length = (restTip - head).length();
		if (length <= 1e-12f) { continue; }
		sgc::Vec3f next = sp::integrate(j, s, head, restTip, length, carry, dt);
		// 押し出しと骨の長さへの戻しを交互に数回。戻すと球へ少し入り直すので 1 回では足りない
		for (int pass = 0; pass < 3 && !set.colliders.empty(); ++pass)
		{
			for (const auto& c : set.colliders)
			{
				const float scale = sp::worldScale(instanceWorld, pose, c.node);
				const sgc::Vec3f center = (instanceWorld * pose.model[static_cast<std::size_t>(c.node)]).transformPoint(c.offset);
				next = sp::pushOut(next, center, (c.radius + j.radius) * scale);
			}
			next = sp::toLength(head, next, length, restTip);
		}
		s.previous = s.current;
		s.current = next;
		sp::orient(asset, pose, j, toModel.transformPoint(next));
	}
}

/// @brief 進めずに、今の揺れの状態を評価済みの pose に掛ける (draw で使う)
inline void applySpringBones(const AnimAsset& asset, const SpringBoneSet& set, const sgc::Mat4f& instanceWorld,
                             const SpringBoneState& state, AnimPose& pose)
{
	if (state.initialized == 0) { return; }
	const sgc::Mat4f toModel = instanceWorld.inversed();
	const auto n = std::min(state.jointCount, static_cast<std::uint32_t>(set.jointCount()));
	for (std::uint32_t i = 0; i < n; ++i)
	{
		detail::spring::orient(asset, pose, set.joints[i], toModel.transformPoint(state.joints[i].current));
	}
}

} // namespace mitiru::animation
