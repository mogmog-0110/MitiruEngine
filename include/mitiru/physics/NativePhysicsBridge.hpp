#pragma once
#ifndef MITIRU_PHYSICS_NATIVE_PHYSICS_BRIDGE_HPP
#define MITIRU_PHYSICS_NATIVE_PHYSICS_BRIDGE_HPP

/// @file NativePhysicsBridge.hpp
/// @brief NativeEngine (native periodic boundaries) を物理バックエンドとして
///        MitiruEngine の ECS に接続する scene::ISystem アダプタ。
///
/// RigidBodyComponent3D + TransformComponent を持つエンティティを NativeEngine の
/// ボディへ写し、毎フレーム固定ステップで進め、姿勢を TransformComponent へ書き戻す
/// (PhysicsSystem3D と同じ契約)。
///
/// 有効化は 2 段構え:
///   1. CMake を `-DMITIRU_USE_NATIVEPHYS=ON` で configure (NativeEngine を build)
///   2. 使う target だけが `mitiru_nativephys` を link (MITIRU_HAS_NATIVEPHYS が付く)
/// link していない target では本システムは何もしない no-op になる。

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mitiru/scene/GameWorld.hpp"          // scene::GameWorld / TransformComponent / EntityId
#include "mitiru/scene/SystemRunner.hpp"       // scene::ISystem
#include "mitiru/physics/PhysicsSystem3D.hpp"  // physics3d::RigidBodyComponent3D
#include "mitiru/physics/PhysicsWorld3D.hpp"   // physics3d::ColliderType3D

#ifdef MITIRU_HAS_NATIVEPHYS
#include <native_physics_world.hpp>            // mitiru::nativephys::NativePhysicsWorld
#endif

namespace mitiru {
namespace physics3d {

/// @brief NativeEngine を使う ECS 物理システム。
class NativePhysicsSystem : public scene::ISystem
{
public:
	/// @brief 設定。periodic を true にすると周期境界 (端で反対側へ繋がる世界) を使う。
	struct Config
	{
		sgc::Vec3f gravity{0.0f, -9.81f, 0.0f};
		float fixedTimeStep{1.0f / 60.0f};
		int maxSubSteps{8};
		bool sleepEnabled{true};
		bool periodic{false};   ///< 周期境界を使うか (NativeEngine 固有の能力)
		float periodicHalf{25.0f};
	};

	explicit NativePhysicsSystem(const Config& cfg = {}) : m_cfg(cfg)
	{
#ifdef MITIRU_HAS_NATIVEPHYS
		m_world.setGravity(cfg.gravity);
		m_world.setTimestep(cfg.fixedTimeStep);
		m_world.setSleepEnabled(cfg.sleepEnabled);
		// 既定は開いた世界。これを言わないと backend 既定の反射壁 (±12m) が
		// 見えない壁として残る。周期境界を頼んだ時だけそちらへ切り替える。
		if (cfg.periodic) m_world.setPeriodicBox(cfg.periodicHalf, true);
		else m_world.setOpenBoundary(true);
#endif
	}

	[[nodiscard]] std::string name() const override { return "NativePhysicsSystem"; }

	void update(scene::GameWorld& world, float dt) override
	{
#ifdef MITIRU_HAS_NATIVEPHYS
		pruneRemoved(world);
		syncToPhysics(world);

		m_accum += dt;
		int steps = 0;
		while (m_accum >= m_cfg.fixedTimeStep && steps < m_cfg.maxSubSteps)
		{
			m_world.update(m_cfg.fixedTimeStep);
			m_accum -= m_cfg.fixedTimeStep;
			++steps;
		}

		syncFromPhysics(world);
#else
		(void)world;
		(void)dt;
#endif
	}

	/// @brief 設定を取得する。
	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

	/// @brief バックエンドが保持しているボディ数 (backend 無効時は 0)。
	[[nodiscard]] std::size_t bodyCount() const noexcept
	{
#ifdef MITIRU_HAS_NATIVEPHYS
		return m_world.bodyCount();
#else
		return 0;
#endif
	}

#ifdef MITIRU_HAS_NATIVEPHYS
	/// @brief バックエンドへの直接アクセス (クエリ・ジョイント・キャラクタ等の高度な用途)。
	[[nodiscard]] nativephys::NativePhysicsWorld& backend() noexcept { return m_world; }
#endif

private:
#ifdef MITIRU_HAS_NATIVEPHYS
	/// @brief RigidBodyComponent3D が消えたエンティティのボディを破棄する。
	/// これをやらないと removeEntity 後もボディが残り、見えない障害物になる。
	void pruneRemoved(scene::GameWorld& world)
	{
		m_dead.clear();
		for (const auto& [entityId, body] : m_entityToBody)
		{
			if (!world.getComponent<RigidBodyComponent3D>(entityId)) m_dead.push_back(entityId);
		}
		for (const scene::EntityId entityId : m_dead)
		{
			m_world.removeBody(m_entityToBody[entityId]);
			m_entityToBody.erase(entityId);
		}
	}

	void syncToPhysics(scene::GameWorld& world)
	{
		world.forEach<RigidBodyComponent3D>(
			[this, &world](scene::EntityId entityId, RigidBodyComponent3D& rb)
			{
				auto* transform = world.getComponent<scene::TransformComponent>(entityId);
				if (!transform) return;

				auto it = m_entityToBody.find(entityId);
				if (it == m_entityToBody.end())
					createBody(entityId, *transform, rb);   // 新規エンティティ
				else
					updateBody(it->second, *transform, rb); // 既存エンティティ
			});
	}

	void createBody(scene::EntityId entityId, const scene::TransformComponent& transform,
	                const RigidBodyComponent3D& rb)
	{
		nativephys::BodyDesc d;
		d.type = rb.isKinematic ? nativephys::BodyDesc::Type::Kinematic
		       : (rb.mass <= 0.0f ? nativephys::BodyDesc::Type::Static
		                          : nativephys::BodyDesc::Type::Dynamic);
		switch (rb.colliderType)
		{
			case ColliderType3D::Sphere:
				d.shape = nativephys::BodyDesc::Shape::Sphere; d.radius = rb.colliderRadius; break;
			case ColliderType3D::AABB:
				d.shape = nativephys::BodyDesc::Shape::Box; d.halfExtents = rb.colliderHalfExtents; break;
			case ColliderType3D::Capsule:
				d.shape = nativephys::BodyDesc::Shape::Capsule;
				d.radius = rb.colliderRadius; d.halfHeight = rb.capsuleHeight * 0.5f; break;
		}
		d.position = transform.position;
		d.rotation = toQuat(transform.rotation);
		d.mass = rb.mass;                    // mass>0 なら density より優先
		d.friction = rb.friction;
		d.restitution = rb.restitution;
		d.linearDamping = rb.linearDamping;
		d.angularDamping = rb.angularDamping;
		d.userData = entityId;               // 接触・クエリからエンティティへ戻れるように

		nativephys::BodyId body = m_world.createBody(d);
		m_world.setVelocity(body, rb.linearVelocity);
		m_world.setAngularVelocity(body, rb.angularVelocity);
		m_entityToBody[entityId] = body;
	}

	/// @brief 既存ボディへ ECS 側の変更を流し込む (PhysicsSystem3D::updateBody と同じ契約)。
	void updateBody(nativephys::BodyId body, const scene::TransformComponent& transform,
	                const RigidBodyComponent3D& rb)
	{
		if (rb.isKinematic)
		{
			// キネマティックは Transform が正。setKinematicTarget は含意速度を与えるので
			// 接触相手にちゃんと velocity が伝わる (動く床・エレベータ)。
			m_world.setKinematicTarget(body, transform.position, toQuat(transform.rotation));
		}
		else if (rb.mass <= 0.0f)
		{
			m_world.setPosition(body, transform.position);
			m_world.setRotation(body, toQuat(transform.rotation));
		}
		m_world.setMaterial(body, rb.friction, rb.restitution);
		m_world.setDamping(body, rb.linearDamping, rb.angularDamping);
	}

	void syncFromPhysics(scene::GameWorld& world)
	{
		for (const auto& [entityId, body] : m_entityToBody)
		{
			auto* transform = world.getComponent<scene::TransformComponent>(entityId);
			if (!transform) continue;
			transform->position = m_world.getPosition(body);
			transform->rotation = m_world.getRotation(body).toEuler();   // quat -> Euler (radians)

			auto* rb = world.getComponent<RigidBodyComponent3D>(entityId);
			if (rb)
			{
				rb->linearVelocity = m_world.getVelocity(body);
				rb->angularVelocity = m_world.getAngularVelocity(body);
			}
		}
	}

	/// @brief TransformComponent のオイラー角 (pitch,yaw,roll ラジアン) をクォータニオンへ。
	[[nodiscard]] static sgc::Quaternionf toQuat(const sgc::Vec3f& euler) noexcept
	{
		return sgc::Quaternionf::fromEuler(euler.x, euler.y, euler.z);
	}

	nativephys::NativePhysicsWorld m_world;
	std::unordered_map<scene::EntityId, nativephys::BodyId> m_entityToBody;
	std::vector<scene::EntityId> m_dead;   ///< pruneRemoved の作業用 (毎フレーム再利用)
#endif  // MITIRU_HAS_NATIVEPHYS

	Config m_cfg;
	float m_accum{0.0f};
};

}  // namespace physics3d
}  // namespace mitiru

#endif  // MITIRU_PHYSICS_NATIVE_PHYSICS_BRIDGE_HPP
