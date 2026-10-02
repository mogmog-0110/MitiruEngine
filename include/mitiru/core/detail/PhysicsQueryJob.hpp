#pragma once

/// @file PhysicsQueryJob.hpp
/// @brief 物理問い合わせ job (ABI v37、HE2 の PhysicsQueryJob / PhysicsRaycastJob 相当) の host 側。
/// game が `FrameIntents::physicsQueries` に積んだ要求を、次フレームの `InputSnapshot::physicsResults`
/// へ答える純関数と、`--collision` の JSON から静的 world を作る読み込みだけを持つ。Engine を
/// 持ち込まずに単体テストできるよう切り出してある。
///
/// world は Jolt (JoltPhysicsWorld3D)。Jolt を build していなければ world を作らず、問い合わせは
/// kPhysicsHitUnsupported で返る。

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
#include <mitiru/physics/JoltPhysicsWorld3D.hpp>

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
		r.bodyLayer = hit->layer;
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

namespace collision_json
{

/// @brief 地形の座標として扱える数か (float に収まる有限値)。範囲外を Jolt に渡すと形状の計算がおかしくなる
[[nodiscard]] inline bool readCoord(const nlohmann::json& v, float& out)
{
	if (!v.is_number()) return false;
	const double d = v.get<double>();
	if (!std::isfinite(d) || std::fabs(d) > 1.0e7) return false;
	out = static_cast<float>(d);
	return true;
}

[[nodiscard]] inline bool readVec3(const nlohmann::json& j, sgc::Vec3f& out)
{
	if (!j.is_array() || j.size() < 3) return false;
	return readCoord(j[0], out.x) && readCoord(j[1], out.y) && readCoord(j[2], out.z);
}

/// @brief `{"min":[..],"max":[..]}` を箱の BodyDesc にする
[[nodiscard]] inline bool readBox(const nlohmann::json& e, physics3d::BodyDesc& desc)
{
	sgc::Vec3f lo, hi;
	if (!e.contains("min") || !e.contains("max") || !readVec3(e["min"], lo) || !readVec3(e["max"], hi)) return false;
	desc.shape = physics3d::BodyDesc::Shape::Box;
	desc.position = (lo + hi) * 0.5f;
	desc.halfExtents = (hi - lo) * 0.5f;
	return true;
}

/// @brief `{"vertices":[[x,y,z],..],"indices":[..]}` を三角形メッシュの BodyDesc にする (配列は呼び出し側が持つ)
[[nodiscard]] inline bool readMesh(const nlohmann::json& e, std::vector<sgc::Vec3f>& vertices,
	std::vector<std::uint32_t>& indices, physics3d::BodyDesc& desc)
{
	if (!e.contains("vertices") || !e.contains("indices") || !e["vertices"].is_array() || !e["indices"].is_array())
		return false;
	vertices.clear();
	indices.clear();
	for (const auto& v : e["vertices"])
	{
		sgc::Vec3f p;
		if (!readVec3(v, p)) return false;
		vertices.push_back(p);
	}
	for (const auto& i : e["indices"])
	{
		if (!i.is_number_unsigned() || i.get<std::uint64_t>() > 0xFFFFFFFFull) return false;
		indices.push_back(static_cast<std::uint32_t>(i.get<std::uint64_t>()));
	}
	desc.shape = physics3d::BodyDesc::Shape::Mesh;
	desc.meshVertices = vertices.data();
	desc.meshVertexCount = vertices.size();
	desc.meshIndices = indices.data();
	desc.meshIndexCount = indices.size();
	return true;
}

/// @brief `layer` (省略時 0) を読む。0-31 の整数でなければ false (例外で host を落とさない)
[[nodiscard]] inline bool readLayer(const nlohmann::json& e, std::uint32_t& out)
{
	if (!e.contains("layer")) { out = 0; return true; }
	const auto& v = e["layer"];
	if (!v.is_number_unsigned() || v.get<std::uint64_t>() > 31u) return false;
	out = static_cast<std::uint32_t>(v.get<std::uint64_t>());
	return true;
}

/// @brief JSON の root から地形の列を取り出す。形が違えば nullptr
[[nodiscard]] inline const nlohmann::json* entries(const nlohmann::json& root)
{
	if (root.is_array()) return &root;
	if (root.is_object() && root.contains("boxes") && root["boxes"].is_array()) return &root["boxes"];
	return nullptr;
}

} // namespace collision_json

#ifdef MITIRU_HAS_JOLT

/// @brief 地形の列を静的ボディとして積んだ world を作る。読めた件数を added に返す
inline std::unique_ptr<physics3d::IPhysicsWorld3D> buildCollisionWorld(const nlohmann::json& list, int& added)
{
	physics3d::JoltWorldConfig cfg;
	cfg.maxBodies = static_cast<std::uint32_t>(list.size()) + 16u;
	auto world = std::make_unique<physics3d::JoltPhysicsWorld3D>(cfg);
	std::vector<sgc::Vec3f> vertices;
	std::vector<std::uint32_t> indices;
	added = 0;
	for (const auto& e : list)
	{
		if (!e.is_object()) continue;
		physics3d::BodyDesc desc;
		desc.type = physics3d::BodyDesc::Type::Static;
		const bool ok = collision_json::readLayer(e, desc.layer) &&
			(collision_json::readBox(e, desc) || collision_json::readMesh(e, vertices, indices, desc));
		if (ok && world->addBody(desc) != physics3d::kInvalidBodyId) ++added;
	}
	world->optimizeBroadPhase();
	return world;
}

#else

inline std::unique_ptr<physics3d::IPhysicsWorld3D> buildCollisionWorld(const nlohmann::json&, int& added)
{
	added = 0;
	debug::warnOnceFix("physics.collision.nojolt", "--collision を読んだが、物理問い合わせに答える Jolt が build されていない",
		"external/jolt が無い状態で configure した", "git submodule update --init external/jolt してから configure し直す");
	return nullptr;
}

#endif // MITIRU_HAS_JOLT

/// @brief 箱 (`min` / `max`) と三角形メッシュ (`vertices` / `indices`) を混ぜた地形の列を静的 world にする。
/// 各要素は `layer` を持てる。root は配列か `{"boxes":[...]}`。読めなければ nullptr (warnOnce 1 行)。
/// 動くものは持たない: 問い合わせ専用の地形。
inline std::unique_ptr<physics3d::IPhysicsWorld3D> loadCollisionWorld(const std::string& path)
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
	const nlohmann::json* list = collision_json::entries(j);
	if (list == nullptr)
	{
		debug::warnOnceFix("physics.collision.shape", "--collision の JSON が地形の列ではない: " + path,
			"root が配列でも {\"boxes\":[...]} でもない", "配列か {\"boxes\":[...]} の形にする");
		return nullptr;
	}

	int added = 0;
	auto world = buildCollisionWorld(*list, added);
	if (world != nullptr && added == 0)
	{
		debug::warnOnce("physics.collision.empty", "--collision の JSON に読める箱もメッシュも 1 つも無い: " + path);
	}
	return world;
}

}  // namespace mitiru::detail
