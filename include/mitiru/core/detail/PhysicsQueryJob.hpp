#pragma once

/// @file PhysicsQueryJob.hpp
/// @brief 物理問い合わせ job (ABI v37、HE2 の PhysicsQueryJob / PhysicsRaycastJob 相当) の host 側。
/// game が `FrameIntents::physicsQueries` に積んだ要求を、次フレームの `InputSnapshot::physicsResults`
/// へ答える純関数と、`--collision` の JSON から静的 world を作る読み込みだけを持つ。Engine を
/// 持ち込まずに単体テストできるよう切り出してある。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/physics/IPhysicsWorld3D.hpp>
#include <mitiru/physics/PhysicsWorld3D.hpp>

namespace mitiru::detail
{

/// @brief 要求 1 件に答える。`world` が nullptr なら hit = kPhysicsHitUnsupported (「当たらない」と区別する)。
inline module::PhysicsResult answerPhysicsQuery(const physics3d::IPhysicsWorld3D* world,
                                                const module::PhysicsQuery& q) noexcept
{
	module::PhysicsResult r{};
	r.tag = q.tag;
	if (world == nullptr) { r.hit = module::kPhysicsHitUnsupported; return r; }
	const std::uint32_t mask = (q.mask == 0u) ? 0xFFFFFFFFu : q.mask;

	switch (q.kind)
	{
	case module::kPhysicsQueryRaycast:
	{
		sgc::Vec3f dir{q.b[0], q.b[1], q.b[2]};
		const float len = dir.length();
		if (len <= 1e-8f) { r.hit = module::kPhysicsHitNone; return r; }
		dir = dir * (1.0f / len);
		const float maxDist = (q.radius > 0.0f) ? q.radius : 1e6f;
		const physics3d::Ray3D ray{sgc::Vec3f{q.a[0], q.a[1], q.a[2]}, dir};
		const auto hit = world->raycast(ray, maxDist, mask);
		if (!hit.has_value()) { r.hit = module::kPhysicsHitNone; return r; }
		r.hit = module::kPhysicsHitYes;
		r.point[0] = hit->point.x;   r.point[1] = hit->point.y;   r.point[2] = hit->point.z;
		r.normal[0] = hit->normal.x; r.normal[1] = hit->normal.y; r.normal[2] = hit->normal.z;
		r.t = hit->distance;
		return r;
	}
	case module::kPhysicsQueryOverlapSphere:
	{
		std::vector<physics3d::OverlapResult3D> hits;
		world->overlapSphere(sgc::Vec3f{q.a[0], q.a[1], q.a[2]}, q.radius, mask, hits);
		r.hit = hits.empty() ? module::kPhysicsHitNone : module::kPhysicsHitYes;
		r.t   = static_cast<float>(hits.size());
		r.point[0] = q.a[0]; r.point[1] = q.a[1]; r.point[2] = q.a[2];
		return r;
	}
	default:
		r.hit = module::kPhysicsHitNone;
		return r;
	}
}

/// @brief `queries[0..n)` を要求順に `out[0..cap)` へ答え、書いた件数を返す。
inline int answerPhysicsQueries(const physics3d::IPhysicsWorld3D* world,
                                const module::PhysicsQuery* queries, int n,
                                module::PhysicsResult* out, int cap) noexcept
{
	if (queries == nullptr || out == nullptr || n <= 0 || cap <= 0) { return 0; }
	const int count = n < cap ? n : cap;
	for (int i = 0; i < count; ++i) { out[i] = answerPhysicsQuery(world, queries[i]); }
	return count;
}

/// @brief `[{"min":[x,y,z],"max":[x,y,z],"layer":0}, ...]` (または `{"boxes":[...]}`) を静的 world にする。
/// 読めなければ nullptr (warnOnce 1 行)。動くものは持たない: 問い合わせ専用の地形。
inline std::unique_ptr<physics3d::PhysicsWorld3D> loadCollisionWorld(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		debug::warnOnceFix("physics.collision.open", "--collision の JSON を開けない: " + path,
			"パスが違うか、まだ書いていない", "[{\"min\":[x,y,z],\"max\":[x,y,z],\"layer\":0}] の箱の列を書く");
		return nullptr;
	}
	nlohmann::json j;
	try { in >> j; }
	catch (...)
	{
		debug::warnOnceFix("physics.collision.parse", "--collision の JSON が壊れている: " + path,
			"構文エラー", "配列か {\"boxes\":[...]} の形にする");
		return nullptr;
	}
	const nlohmann::json* boxes = j.is_array() ? &j : (j.is_object() && j.contains("boxes") ? &j["boxes"] : nullptr);
	if (boxes == nullptr || !boxes->is_array())
	{
		debug::warnOnceFix("physics.collision.shape", "--collision の JSON が箱の列ではない: " + path,
			"root が配列でも {\"boxes\":[...]} でもない", "配列か {\"boxes\":[...]} の形にする");
		return nullptr;
	}

	auto world = std::make_unique<physics3d::PhysicsWorld3D>();
	int added = 0;
	for (const auto& b : *boxes)
	{
		if (!b.is_object() || !b.contains("min") || !b.contains("max")) { continue; }
		const auto& mn = b["min"]; const auto& mx = b["max"];
		if (!mn.is_array() || !mx.is_array() || mn.size() < 3 || mx.size() < 3) { continue; }
		const sgc::Vec3f lo{mn[0].get<float>(), mn[1].get<float>(), mn[2].get<float>()};
		const sgc::Vec3f hi{mx[0].get<float>(), mx[1].get<float>(), mx[2].get<float>()};
		const std::uint32_t layer = b.value("layer", 0u) & 31u;

		physics3d::BodyDesc desc;
		desc.type     = physics3d::BodyDesc::Type::Static;
		desc.shape    = physics3d::BodyDesc::Shape::Box;
		desc.position = (lo + hi) * 0.5f;
		desc.halfExtents = (hi - lo) * 0.5f;
		desc.layer    = layer;
		(void)world->addBody(desc);
		++added;
	}
	if (added == 0)
	{
		debug::warnOnce("physics.collision.empty", "--collision の JSON に箱が 1 つも無い: " + path);
	}
	return world;
}

}  // namespace mitiru::detail
