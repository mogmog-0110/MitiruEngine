#pragma once

/// @file NativePhysicsWorld3D.hpp
/// @brief NativeEngine を `IPhysicsWorld3D` として使うアダプタ
///
/// `mitiru::nativephys::NativePhysicsWorld`（`scene::ISystem` 統合を前提にした
/// `NativePhysicsBridge.hpp` とは別に、ワールドを直接 step/raycast したい用途向け）
/// を包む。`overlapBox` は wrapper 側に無いため `native()` エスケープハッチ経由で
/// `ne::PhysicsWorld::overlapBox` を直接呼ぶ。周期境界を跨ぐ raycast/overlapは
/// NativeEngine側が既に処理するので、呼ぶだけでよい（`native_physics_world.hpp`
/// の「scene queries (PBC-aware, layer-filtered)」を参照）。
///
/// `MITIRU_HAS_NATIVEPHYS` が未定義のビルドではこのクラス自体が存在しない
/// （呼び出し側は `#if MITIRU_HAS_NATIVEPHYS` で存在チェックすること）。

#include "mitiru/physics/IPhysicsWorld3D.hpp"

#ifdef MITIRU_HAS_NATIVEPHYS

#include <unordered_set>

#include <native_physics_world.hpp>

namespace mitiru::physics3d
{

class NativePhysicsWorld3D : public IPhysicsWorld3D
{
public:
	/// @param periodic true なら端が反対側に繋がる周期境界の世界にする（既定は開いた世界）
	/// @param periodicHalf 周期境界の半径（[-periodicHalf, periodicHalf]の立方体）
	explicit NativePhysicsWorld3D(bool periodic = false, float periodicHalf = 25.0f)
	{
		m_world.setGravity(m_gravity);
		// 既定は開いた世界。言わないとbackend既定の反射壁が見えない障害物として残る
		// (NativePhysicsBridge.hppのNativePhysicsSystemコンストラクタと同じ理由)
		if (periodic) m_world.setPeriodicBox(periodicHalf, true);
		else m_world.setOpenBoundary(true);
	}

	// ── IPhysicsWorld3D 実装 ──────────────────────────────────

	void step(float dt) noexcept override { m_world.update(dt); }

	void setGravity(const sgc::Vec3f& gravity) noexcept override
	{
		m_gravity = gravity;
		m_world.setGravity(gravity);
	}

	[[nodiscard]] const sgc::Vec3f& gravity() const noexcept override { return m_gravity; }

	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist, std::uint32_t mask) const noexcept override
	{
		const auto hit = m_world.raycast(ray.origin, ray.direction, maxDist, mask);
		if (!hit.hit) return std::nullopt;
		return RayHit3D{hit.point, hit.normal, hit.distance};
	}

	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept override
	{
		for (const auto& hit : m_world.raycastAll(ray.origin, ray.direction, maxDist, mask))
			outHits.push_back(RayHit3D{hit.point, hit.normal, hit.distance});
	}

	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		for (const auto bodyId : m_world.overlapSphere(center, radius, mask))
			outResults.push_back(OverlapResult3D{static_cast<BodyId>(bodyId), 0});
	}

	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		const ne::V3 center = nativephys::toNe(aabb.center());
		const ne::V3 half = nativephys::toNe(aabb.halfExtents());
		for (const auto bodyId : m_world.native().overlapBox(center, ne::Q{1.0, 0.0, 0.0, 0.0}, half, mask))
			outResults.push_back(OverlapResult3D{static_cast<BodyId>(bodyId), 0});
	}

	[[nodiscard]] BodyId addBody(const BodyDesc& desc) noexcept override
	{
		nativephys::BodyDesc d;
		switch (desc.type)
		{
		case BodyDesc::Type::Static:    d.type = nativephys::BodyDesc::Type::Static; break;
		case BodyDesc::Type::Kinematic: d.type = nativephys::BodyDesc::Type::Kinematic; break;
		case BodyDesc::Type::Dynamic:   d.type = nativephys::BodyDesc::Type::Dynamic; break;
		}
		switch (desc.shape)
		{
		case BodyDesc::Shape::Sphere:
			d.shape = nativephys::BodyDesc::Shape::Sphere;
			d.radius = desc.radius;
			break;
		case BodyDesc::Shape::Box:
			d.shape = nativephys::BodyDesc::Shape::Box;
			d.halfExtents = desc.halfExtents;
			break;
		case BodyDesc::Shape::Capsule:
			d.shape = nativephys::BodyDesc::Shape::Capsule;
			d.radius = desc.radius;
			d.halfHeight = desc.halfHeight;
			break;
		}
		d.position = desc.position;
		d.rotation = desc.rotation;
		d.mass = desc.mass;
		d.layer = desc.layer;
		d.mask = desc.mask;

		const nativephys::BodyId id = m_world.createBody(d);
		if (id != nativephys::kInvalidBody) m_liveBodies.insert(id);
		return static_cast<BodyId>(id);
	}

	void removeBody(BodyId id) noexcept override
	{
		const auto nid = static_cast<nativephys::BodyId>(id);
		m_world.removeBody(nid);
		m_liveBodies.erase(nid);
	}

	[[nodiscard]] std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept override
	{
		const auto nid = static_cast<nativephys::BodyId>(id);
		if (m_liveBodies.find(nid) == m_liveBodies.end()) return std::nullopt;
		return BodyTransform{m_world.getPosition(nid), m_world.getRotation(nid)};
	}

private:
	// native()がconstでない（NativeEngine側のAPI形）ため、overlapBox()をconstのまま
	// 呼べるようにmutableにする。他メソッドは全てNativePhysicsWorld側のconst APIで足りる
	mutable nativephys::NativePhysicsWorld m_world;
	sgc::Vec3f m_gravity{0.0f, -9.81f, 0.0f};

	/// @brief 生成済みボディの集合（`bodyTransform()`の存在確認用。
	/// NativeEngineに「このIDは有効か」を問うAPIが無いための自前台帳）
	std::unordered_set<nativephys::BodyId> m_liveBodies;
};

} // namespace mitiru::physics3d

#endif // MITIRU_HAS_NATIVEPHYS
