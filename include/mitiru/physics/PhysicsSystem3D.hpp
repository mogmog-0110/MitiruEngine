#pragma once

/// @file PhysicsSystem3D.hpp
/// @brief 3D物理ECSシステム統合
///
/// PhysicsWorld3D とECS（GameWorld）を統合するシステム。
/// ISystem インターフェースを実装し、SystemRunner に登録して使用する。
/// 固定タイムステップで物理シミュレーションを実行し、
/// TransformComponent と RigidBodyComponent3D を同期する。
///
/// @code
/// mitiru::physics3d::PhysicsSystem3DConfig config;
/// config.gravity = {0, -9.81f, 0};
/// config.fixedTimeStep = 1.0f / 60.0f;
///
/// auto system = std::make_unique<mitiru::physics3d::PhysicsSystem3D>(config);
/// runner.addSystem(std::move(system), 100);
///
/// // 毎フレーム
/// runner.updateAll(world, dt);
/// @endcode

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"
#include "mitiru/physics/BroadPhase3D.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/CollisionDetection3D.hpp"
#include "mitiru/physics/Constraints3D.hpp"
#include "mitiru/physics/ContactSolver3D.hpp"
#include "mitiru/physics/NarrowPhase3D.hpp"
#include "mitiru/physics/PhysicsDebugRenderer3D.hpp"
#include "mitiru/physics/PhysicsWorld3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

// ── ECS用コンポーネント ──────────────────────────────────────

/// @brief 3D剛体コンポーネント（ECS用）
///
/// GameWorld に登録して TransformComponent と組み合わせて使用する。
/// PhysicsSystem3D がこのコンポーネントを検出し、
/// 内部の PhysicsWorld3D と同期する。
struct RigidBodyComponent3D
{
	float mass{1.0f};                 ///< 質量（kg）。0以下で静的ボディ
	float restitution{0.3f};          ///< 反発係数
	float friction{0.5f};             ///< 摩擦係数
	float linearDamping{0.01f};       ///< 線形減衰率
	float angularDamping{0.05f};      ///< 角速度減衰率
	ColliderType3D colliderType{ColliderType3D::Sphere};  ///< コライダータイプ
	float colliderRadius{0.5f};       ///< 球/カプセル半径
	sgc::Vec3f colliderHalfExtents{0.5f, 0.5f, 0.5f}; ///< AABB半径サイズ
	float capsuleHeight{1.0f};        ///< カプセル高さ
	bool isKinematic{false};          ///< キネマティックボディか
	sgc::Vec3f linearVelocity{};      ///< 初期線形速度
	sgc::Vec3f angularVelocity{};     ///< 初期角速度
};

/// @brief 物理システム設定
struct PhysicsSystem3DConfig
{
	sgc::Vec3f gravity{0.0f, -9.81f, 0.0f};  ///< 重力加速度
	float fixedTimeStep{1.0f / 60.0f};        ///< 固定タイムステップ（秒）
	int maxSubSteps{8};                        ///< 最大サブステップ数
	int solverIterations{8};                   ///< ソルバー反復回数
	float broadPhaseMargin{0.05f};             ///< ブロードフェーズAABBマージン
	ContactSolverConfig3D solverConfig{};      ///< ソルバー詳細設定
	bool enableDebugDraw{false};               ///< デバッグ描画を有効にするか
};

/// @brief 衝突イベント（ECSレベル）
struct CollisionEvent3D
{
	scene::EntityId entityA{scene::INVALID_ENTITY};
	scene::EntityId entityB{scene::INVALID_ENTITY};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
};

/// @brief 衝突コールバック型
using CollisionCallback3DEcs = std::function<void(const CollisionEvent3D&)>;

/// @brief 3D物理ECSシステム
///
/// ISystem を実装し、SystemRunner に登録する。
/// 毎フレームの update() で以下を実行する:
///
///   1. **同期（ECS → 物理）**: RigidBodyComponent3D + TransformComponent から
///      内部 PhysicsWorld3D のボディを作成/更新する
///   2. **固定タイムステップ積分**: アキュムレータ方式で物理ステップを実行
///      - 重力適用
///      - 速度積分
///      - コライダー同期
///      - ブロードフェーズ（Sort-and-Sweep）
///      - ナローフェーズ（特殊化パス + GJK）
///      - 接触ソルバー（逐次インパルス + ウォームスタート）
///      - 拘束ソルバー
///   3. **同期（物理 → ECS）**: 更新された位置・回転を TransformComponent に書き戻す
///   4. **イベント通知**: 検出された衝突をコールバックに通知する
class PhysicsSystem3D : public scene::ISystem
{
public:
	/// @brief 設定を指定して構築する
	/// @param config 物理システム設定
	explicit PhysicsSystem3D(const PhysicsSystem3DConfig& config = {})
		: m_config(config)
		, m_broadPhase(config.broadPhaseMargin)
		, m_contactSolver(config.solverConfig)
	{
		m_world.setGravity(config.gravity);
		m_world.setSolverIterations(config.solverIterations);
	}

	/// @brief システム名を返す
	[[nodiscard]] std::string name() const override
	{
		return "PhysicsSystem3D";
	}

	/// @brief 物理更新を実行する
	/// @param gameWorld ゲームワールド
	/// @param dt デルタタイム（秒）
	void update(scene::GameWorld& gameWorld, float dt) override
	{
		syncToPhysics(gameWorld);

		m_accumulator += dt;
		int steps = 0;

		while (m_accumulator >= m_config.fixedTimeStep &&
			   steps < m_config.maxSubSteps)
		{
			fixedStep(m_config.fixedTimeStep);
			m_accumulator -= m_config.fixedTimeStep;
			++steps;
		}

		syncFromPhysics(gameWorld);
		notifyCollisions();

		if (m_config.enableDebugDraw)
		{
			gatherDebugDraw();
		}
	}

	// ── 設定アクセス ──────────────────────────────────────────

	/// @brief 重力を設定する
	/// @param gravity 重力加速度
	void setGravity(const sgc::Vec3f& gravity) noexcept
	{
		m_config.gravity = gravity;
		m_world.setGravity(gravity);
	}

	/// @brief 重力を取得する
	[[nodiscard]] const sgc::Vec3f& gravity() const noexcept
	{
		return m_config.gravity;
	}

	/// @brief 設定を取得する
	[[nodiscard]] const PhysicsSystem3DConfig& config() const noexcept
	{
		return m_config;
	}

	// ── 衝突コールバック ──────────────────────────────────────

	/// @brief 衝突コールバックを登録する
	/// @param callback 衝突発生時に呼ばれるコールバック
	void registerCollisionCallback(CollisionCallback3DEcs callback)
	{
		m_ecsCallbacks.push_back(std::move(callback));
	}

	// ── 拘束管理 ──────────────────────────────────────────────

	/// @brief 距離拘束を追加する
	/// @param constraint 距離拘束
	void addDistanceConstraint(DistanceConstraint constraint) noexcept
	{
		m_world.addDistanceConstraint(std::move(constraint));
	}

	/// @brief ヒンジ拘束を追加する
	/// @param constraint ヒンジ拘束
	void addHingeConstraint(HingeConstraint constraint) noexcept
	{
		m_world.addHingeConstraint(std::move(constraint));
	}

	// ── デバッグ描画 ──────────────────────────────────────────

	/// @brief デバッグ描画を有効/無効にする
	/// @param enabled 有効にするか
	void setDebugDrawEnabled(bool enabled) noexcept
	{
		m_config.enableDebugDraw = enabled;
	}

	/// @brief デバッグ描画フラグを設定する
	/// @param flags 描画フラグ
	void setDebugDrawFlags(DebugDrawFlags flags) noexcept
	{
		m_debugRenderer.setFlags(flags);
	}

	/// @brief デバッグレンダラーへの参照を取得する
	[[nodiscard]] const PhysicsDebugRenderer3D& debugRenderer() const noexcept
	{
		return m_debugRenderer;
	}

	// ── 統計情報 ──────────────────────────────────────────────

	/// @brief 管理中のボディ数を返す
	[[nodiscard]] std::size_t bodyCount() const noexcept
	{
		return m_world.bodyCount();
	}

	/// @brief コライダー数を返す
	[[nodiscard]] std::size_t colliderCount() const noexcept
	{
		return m_world.colliderCount();
	}

	/// @brief 最後のステップのコンタクト数を返す
	[[nodiscard]] std::size_t lastContactCount() const noexcept
	{
		return m_lastManifolds.size();
	}

	/// @brief 最後のステップのブロードフェーズペア数を返す
	[[nodiscard]] std::size_t lastBroadPhasePairCount() const noexcept
	{
		return m_broadPhase.pairCount();
	}

	/// @brief レイキャストを実行する
	/// @param ray レイ
	/// @param maxDist 最大検出距離
	/// @return ヒット結果
	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist = 1e6f) const noexcept
	{
		return m_world.raycast(ray, maxDist);
	}

	/// @brief 内部のPhysicsWorld3Dへの参照を取得する（上級者向け）
	[[nodiscard]] PhysicsWorld3D& physicsWorld() noexcept { return m_world; }

	/// @brief 内部のPhysicsWorld3Dへのconst参照を取得する
	[[nodiscard]] const PhysicsWorld3D& physicsWorld() const noexcept { return m_world; }

	/// @brief 最後のマニフォールドを取得する
	[[nodiscard]] const std::vector<ContactManifold3D>& lastManifolds() const noexcept
	{
		return m_lastManifolds;
	}

private:
	// ── ECS同期 ──────────────────────────────────────────────

	/// @brief ECSからPhysicsWorld3Dにデータを同期する
	/// @param gameWorld ゲームワールド
	void syncToPhysics(scene::GameWorld& gameWorld)
	{
		gameWorld.forEach<RigidBodyComponent3D>(
			[this, &gameWorld](scene::EntityId entityId, RigidBodyComponent3D& rb)
			{
				auto* transform = gameWorld.getComponent<scene::TransformComponent>(entityId);
				if (!transform) return;

				auto it = m_entityToBody.find(entityId);

				if (it == m_entityToBody.end())
				{
					// 新規エンティティ → ボディを作成
					createBody(entityId, *transform, rb);
				}
				else
				{
					// 既存エンティティ → 位置・パラメータを同期
					updateBody(it->second, *transform, rb);
				}
			}
		);
	}

	/// @brief PhysicsWorld3Dの結果をECSに書き戻す
	/// @param gameWorld ゲームワールド
	void syncFromPhysics(scene::GameWorld& gameWorld)
	{
		for (const auto& [entityId, bodyId] : m_entityToBody)
		{
			const auto* body = m_world.getBody(bodyId);
			if (!body) continue;

			auto* transform = gameWorld.getComponent<scene::TransformComponent>(entityId);
			if (!transform) continue;

			transform->position = body->position();

			// クォータニオンからオイラー角に変換して書き戻す
			const auto& q = body->rotation();
			transform->rotation = quaternionToEuler(q);

			// 速度をコンポーネントに反映
			auto* rb = gameWorld.getComponent<RigidBodyComponent3D>(entityId);
			if (rb)
			{
				rb->linearVelocity = body->linearVelocity();
				rb->angularVelocity = body->angularVelocity();
			}
		}
	}

	// ── ボディ管理 ────────────────────────────────────────────

	/// @brief 新しいボディを作成して PhysicsWorld3D に登録する
	void createBody(scene::EntityId entityId,
		const scene::TransformComponent& transform,
		const RigidBodyComponent3D& rb)
	{
		auto& body = m_world.addBody();
		const BodyId bodyId = body.id();

		body.setPosition(transform.position);
		body.setRotation(eulerToQuaternion(transform.rotation));
		body.setMass(rb.isKinematic ? 0.0f : rb.mass);
		body.setRestitution(rb.restitution);
		body.setFriction(rb.friction);
		body.setLinearDamping(rb.linearDamping);
		body.setAngularDamping(rb.angularDamping);
		body.setLinearVelocity(rb.linearVelocity);
		body.setAngularVelocity(rb.angularVelocity);

		// コライダーを追加
		switch (rb.colliderType)
		{
		case ColliderType3D::Sphere:
		{
			SphereCollider sphere{transform.position, rb.colliderRadius};
			m_world.addSphereCollider(bodyId, sphere);

			body.setSphereInertiaTensor(rb.colliderRadius);
			break;
		}
		case ColliderType3D::AABB:
		{
			const AABBCollider3D aabb = AABBCollider3D::fromCenterExtents(
				transform.position, rb.colliderHalfExtents);
			m_world.addAABBCollider(bodyId, aabb);

			body.setBoxInertiaTensor(rb.colliderHalfExtents);
			break;
		}
		case ColliderType3D::Capsule:
		{
			const float halfHeight = rb.capsuleHeight * 0.5f;
			CapsuleCollider capsule{
				transform.position + sgc::Vec3f{0, -halfHeight, 0},
				transform.position + sgc::Vec3f{0, halfHeight, 0},
				rb.colliderRadius
			};
			m_world.addCapsuleCollider(bodyId, capsule);
			break;
		}
		}

		m_entityToBody[entityId] = bodyId;
		m_bodyToEntity[bodyId] = entityId;
	}

	/// @brief 既存ボディのパラメータを更新する
	void updateBody(BodyId bodyId,
		const scene::TransformComponent& transform,
		const RigidBodyComponent3D& rb)
	{
		auto* body = m_world.getBody(bodyId);
		if (!body) return;

		if (rb.isKinematic || rb.mass <= 0.0f)
		{
			// キネマティック/静的ボディは ECS の位置に追従
			body->setPosition(transform.position);
			body->setRotation(eulerToQuaternion(transform.rotation));
		}

		body->setRestitution(rb.restitution);
		body->setFriction(rb.friction);
		body->setLinearDamping(rb.linearDamping);
		body->setAngularDamping(rb.angularDamping);
	}

	// ── 固定タイムステップ ────────────────────────────────────

	/// @brief 1固定ステップの物理シミュレーションを実行する
	/// @param dt 固定タイムステップ幅
	void fixedStep(float dt) noexcept
	{
		// PhysicsWorld3D の step() は重力適用・積分・コライダー同期・
		// ブロードフェーズ・ナローフェーズ・接触解決・拘束ソルバーを
		// すべて含むが、ここではより精密なパイプラインを使用する

		// 1. PhysicsWorld3D の基本ステップ（重力・積分・同期）
		m_world.step(dt);

		// 2. 拡張ブロードフェーズ
		runBroadPhase();

		// 3. 拡張ナローフェーズ
		runNarrowPhase();

		// 4. 逐次インパルスソルバー
		if (!m_lastManifolds.empty())
		{
			m_contactSolver.prepare(m_lastManifolds, m_world.bodies());
			m_contactSolver.warmStart(m_lastManifolds, m_world.bodies());
			m_contactSolver.solve(m_lastManifolds, m_world.bodies());
			m_contactSolver.solvePositions(m_lastManifolds, m_world.bodies());
		}
	}

	/// @brief ブロードフェーズを実行する
	void runBroadPhase() noexcept
	{
		m_broadPhase.clear();

		for (std::size_t i = 0; i < m_world.colliders().size(); ++i)
		{
			const auto& collider = m_world.colliders()[i];
			const AABBCollider3D aabb = PhysicsWorld3D::computeAABB(collider);
			m_broadPhase.addProxy(collider.bodyId, i, aabb);
		}

		m_broadPhase.sweep();
	}

	/// @brief ナローフェーズを実行し、マニフォールドを生成する
	void runNarrowPhase() noexcept
	{
		m_lastManifolds.clear();

		for (const auto& pair : m_broadPhase.candidatePairs())
		{
			const auto& colA = m_world.colliders()[pair.indexA];
			const auto& colB = m_world.colliders()[pair.indexB];

			NarrowPhaseResult3D result;

			// 形状ペアに応じた特殊化テスト
			if (colA.type == ColliderType3D::Sphere &&
				colB.type == ColliderType3D::Sphere)
			{
				result = NarrowPhase3D::testSphereSphere(colA.sphere, colB.sphere);
			}
			else if (colA.type == ColliderType3D::Sphere &&
					 colB.type == ColliderType3D::AABB)
			{
				result = NarrowPhase3D::testSphereBox(colA.sphere, colB.aabb);
			}
			else if (colA.type == ColliderType3D::AABB &&
					 colB.type == ColliderType3D::Sphere)
			{
				result = NarrowPhase3D::testSphereBox(colB.sphere, colA.aabb);
				result.contact.normal = -result.contact.normal;
			}
			else if (colA.type == ColliderType3D::AABB &&
					 colB.type == ColliderType3D::AABB)
			{
				result = NarrowPhase3D::testBoxBox(colA.aabb, colB.aabb);
			}
			else if (colA.type == ColliderType3D::Sphere &&
					 colB.type == ColliderType3D::Capsule)
			{
				result = NarrowPhase3D::testSphereCapsule(colA.sphere, colB.capsule);
			}
			else if (colA.type == ColliderType3D::Capsule &&
					 colB.type == ColliderType3D::Sphere)
			{
				result = NarrowPhase3D::testSphereCapsule(colB.sphere, colA.capsule);
				result.contact.normal = -result.contact.normal;
			}
			else if (colA.type == ColliderType3D::Capsule &&
					 colB.type == ColliderType3D::Capsule)
			{
				result = NarrowPhase3D::testCapsuleCapsule(colA.capsule, colB.capsule);
			}

			if (result.contact.hasContact)
			{
				ContactManifold3D manifold;
				manifold.bodyIdA = colA.bodyId;
				manifold.bodyIdB = colB.bodyId;
				manifold.point = result.contact.point;
				manifold.normal = result.contact.normal;
				manifold.depth = result.contact.depth;
				m_lastManifolds.push_back(manifold);
			}
		}
	}

	// ── イベント通知 ──────────────────────────────────────────

	/// @brief 衝突コールバックに通知する
	void notifyCollisions()
	{
		if (m_ecsCallbacks.empty()) return;

		for (const auto& manifold : m_lastManifolds)
		{
			CollisionEvent3D event;

			// ボディID → エンティティID の逆引き
			auto itA = m_bodyToEntity.find(manifold.bodyIdA);
			auto itB = m_bodyToEntity.find(manifold.bodyIdB);

			event.entityA = (itA != m_bodyToEntity.end()) ?
				itA->second : scene::INVALID_ENTITY;
			event.entityB = (itB != m_bodyToEntity.end()) ?
				itB->second : scene::INVALID_ENTITY;
			event.point = manifold.point;
			event.normal = manifold.normal;
			event.depth = manifold.depth;

			for (const auto& cb : m_ecsCallbacks)
			{
				cb(event);
			}
		}
	}

	// ── デバッグ描画収集 ──────────────────────────────────────

	/// @brief デバッグ描画データを収集する
	void gatherDebugDraw() noexcept
	{
		m_debugRenderer.clear();

		// コライダーとボディの描画
		for (const auto& collider : m_world.colliders())
		{
			const auto* body = m_world.getBody(collider.bodyId);
			if (body)
			{
				m_debugRenderer.gatherBody(*body, collider);
			}
		}

		// 接触点の描画
		m_debugRenderer.gather(m_world, m_lastManifolds);
	}

	// ── ユーティリティ ────────────────────────────────────────

	/// @brief オイラー角からクォータニオンに変換する
	/// @param euler オイラー角（ラジアン）
	/// @return クォータニオン
	[[nodiscard]] static sgc::Quaternionf eulerToQuaternion(
		const sgc::Vec3f& euler) noexcept
	{
		const float cx = std::cos(euler.x * 0.5f);
		const float sx = std::sin(euler.x * 0.5f);
		const float cy = std::cos(euler.y * 0.5f);
		const float sy = std::sin(euler.y * 0.5f);
		const float cz = std::cos(euler.z * 0.5f);
		const float sz = std::sin(euler.z * 0.5f);

		return sgc::Quaternionf{
			sx * cy * cz - cx * sy * sz,
			cx * sy * cz + sx * cy * sz,
			cx * cy * sz - sx * sy * cz,
			cx * cy * cz + sx * sy * sz
		};
	}

	/// @brief クォータニオンからオイラー角に変換する
	/// @param q クォータニオン
	/// @return オイラー角（ラジアン）
	[[nodiscard]] static sgc::Vec3f quaternionToEuler(
		const sgc::Quaternionf& q) noexcept
	{
		sgc::Vec3f euler;

		// X軸回転（ピッチ）
		const float sinPitch = 2.0f * (q.w * q.x + q.y * q.z);
		const float cosPitch = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
		euler.x = std::atan2(sinPitch, cosPitch);

		// Y軸回転（ヨー）
		const float sinYaw = 2.0f * (q.w * q.y - q.z * q.x);
		if (std::abs(sinYaw) >= 1.0f)
		{
			euler.y = std::copysign(3.14159265f * 0.5f, sinYaw);
		}
		else
		{
			euler.y = std::asin(sinYaw);
		}

		// Z軸回転（ロール）
		const float sinRoll = 2.0f * (q.w * q.z + q.x * q.y);
		const float cosRoll = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
		euler.z = std::atan2(sinRoll, cosRoll);

		return euler;
	}

	// ── メンバ変数 ────────────────────────────────────────────

	PhysicsSystem3DConfig m_config;     ///< システム設定
	PhysicsWorld3D m_world;             ///< 内部物理ワールド
	BroadPhase3D m_broadPhase;          ///< ブロードフェーズ
	NarrowPhase3D m_narrowPhase;        ///< ナローフェーズ
	ContactSolver3D m_contactSolver;    ///< 接触ソルバー

	float m_accumulator{0.0f};          ///< 時間アキュムレータ

	/// @brief エンティティID → ボディID のマッピング
	std::unordered_map<scene::EntityId, BodyId> m_entityToBody;

	/// @brief ボディID → エンティティID の逆引き
	std::unordered_map<BodyId, scene::EntityId> m_bodyToEntity;

	/// @brief 最後のステップで生成されたマニフォールド
	std::vector<ContactManifold3D> m_lastManifolds;

	/// @brief ECSレベルの衝突コールバック
	std::vector<CollisionCallback3DEcs> m_ecsCallbacks;

	/// @brief デバッグレンダラー
	PhysicsDebugRenderer3D m_debugRenderer;
};

} // namespace mitiru::physics3d
