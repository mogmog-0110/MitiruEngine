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
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "mitiru/debug/TracyZones.hpp"
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

	bool isTrigger{false};                     ///< true ならインパルス解決せず衝突コールバックのみ発火
	std::uint32_t layer{0};                    ///< 所属レイヤー（0-31のビットindex）
	std::uint32_t mask{0xFFFFFFFFu};           ///< 衝突対象レイヤーマスク
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
	bool isTrigger{false};  ///< トリガー同士/トリガーと通常体の接触なら true（押し返し無し）
};

/// @brief 衝突コールバック型
using CollisionCallback3DEcs = std::function<void(const CollisionEvent3D&)>;

/// @brief 接触の推移段階
enum class ContactPhase3D
{
	Enter,  ///< このフレームで新たに接触が始まった
	Stay,   ///< 前フレームから引き続き接触している
	Exit    ///< このフレームで接触が終わった
};

/// @brief 接触イベント（Enter/Stay/Exit を区別する版）
///
/// Exit の point/normal/depth は接触が消えた瞬間の値を持たない
/// （形状がもう重なっていない）ため既定値のままになる。
struct ContactEvent3D
{
	scene::EntityId entityA{scene::INVALID_ENTITY};
	scene::EntityId entityB{scene::INVALID_ENTITY};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
	bool isTrigger{false};
	ContactPhase3D phase{ContactPhase3D::Enter};
};

/// @brief 接触イベントコールバック型
using ContactEventCallback3D = std::function<void(const ContactEvent3D&)>;

/// @brief 1フレーム分の接触ペア記録（Enter/Stay/Exit 差分検出用）
struct ContactPairRecord3D
{
	BodyId a{INVALID_BODY_ID};  ///< 常に a <= b になるよう正規化して格納する
	BodyId b{INVALID_BODY_ID};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
	bool isTrigger{false};

	/// @brief (a, b, isTrigger) の組でソートするための比較関数
	/// @details isTrigger も鍵に含める。複合ボディ (1 body に複数 collider) で trigger 用と
	///          solid 用の collider が同じ相手ボディに同時に触れると、(a,b) だけでは
	///          notifyContactEvents() の多重集合マージが両者を区別できず、trigger 側が
	///          Exit した時に solid 側の record を代わりに消費してしまう (isTrigger の値が
	///          入れ替わって通知される) ことがあった。
	[[nodiscard]] static bool less(const ContactPairRecord3D& lhs, const ContactPairRecord3D& rhs) noexcept
	{
		if (lhs.a != rhs.a) return lhs.a < rhs.a;
		if (lhs.b != rhs.b) return lhs.b < rhs.b;
		return lhs.isTrigger < rhs.isTrigger;
	}
};

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
	/// @details ISystem::name() の戻り値は std::string 固定（SystemRunner.hpp、担当外ファイル）
	///          なので string_view 化はできない。代わりに static キャッシュにして、
	///          呼び出しごとの `const char*` → `std::string` 変換（strlen 走査）を省く
	[[nodiscard]] std::string name() const override
	{
		static const std::string kName{"PhysicsSystem3D"};
		return kName;
	}

	/// @brief 物理更新を実行する
	/// @param gameWorld ゲームワールド
	/// @param dt デルタタイム（秒）
	void update(scene::GameWorld& gameWorld, float dt) override
	{
		MITIRU_ZONE_PHYSICS("PhysicsSystem3D::update");

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
		notifyContactEvents();

		if (m_config.enableDebugDraw)
		{
			gatherDebugDraw();
		}
	}

	/// @brief 更新フェーズを明示する（既定値と同じ Sim だが、物理は Sim 固定である
	/// ことを SystemRunner 側の意図に依存させず自己文書化するため override する）
	[[nodiscard]] scene::UpdatePhase defaultPhase() const noexcept override
	{
		return scene::UpdatePhase::Sim;
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

	/// @brief Enter/Stay/Exit を区別する接触イベントコールバックを登録する
	/// @param callback 接触の推移段階が変わるたび呼ばれるコールバック
	void registerContactEventCallback(ContactEventCallback3D callback)
	{
		m_contactEventCallbacks.push_back(std::move(callback));
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
	/// @param mask 対象レイヤーマスク
	/// @return 最も近いヒット結果
	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist = 1e6f, std::uint32_t mask = 0xFFFFFFFFu) const noexcept
	{
		return m_world.raycast(ray, maxDist, mask);
	}

	/// @brief レイと交差する全コライダーを収集する
	/// @param ray レイ
	/// @param maxDist 最大検出距離
	/// @param mask 対象レイヤーマスク
	/// @param outHits ヒット結果の追加先（呼び出し側が確保する）
	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept
	{
		m_world.raycastAll(ray, maxDist, mask, outHits);
	}

	/// @brief 球とのオーバーラップ問い合わせ
	/// @param center 球の中心
	/// @param radius 球の半径
	/// @param mask 対象レイヤーマスク
	/// @param outResults ヒット結果の追加先（呼び出し側が確保する）
	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept
	{
		m_world.overlapSphere(center, radius, mask, outResults);
	}

	/// @brief AABBとのオーバーラップ問い合わせ
	/// @param aabb 問い合わせ範囲
	/// @param mask 対象レイヤーマスク
	/// @param outResults ヒット結果の追加先（呼び出し側が確保する）
	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept
	{
		m_world.overlapBox(aabb, mask, outResults);
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
			m_world.addSphereCollider(bodyId, sphere, rb.isTrigger, rb.layer, rb.mask);

			body.setSphereInertiaTensor(rb.colliderRadius);
			break;
		}
		case ColliderType3D::AABB:
		{
			const AABBCollider3D aabb = AABBCollider3D::fromCenterExtents(
				transform.position, rb.colliderHalfExtents);
			m_world.addAABBCollider(bodyId, aabb, rb.isTrigger, rb.layer, rb.mask);

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
			m_world.addCapsuleCollider(bodyId, capsule, rb.isTrigger, rb.layer, rb.mask);
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

		// トリガー/レイヤー/マスクは実行時に変わりうるため毎フレーム同期する
		for (auto& collider : m_world.colliders())
		{
			if (collider.bodyId != bodyId) continue;
			collider.isTrigger = rb.isTrigger;
			collider.layer = rb.layer;
			collider.mask = rb.mask;
		}
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
		MITIRU_ZONE_PHYSICS("PhysicsSystem3D::runBroadPhase");

		m_broadPhase.clear();

		// コライダー構成（追加/削除）が変わった時だけキャッシュを作り直す。
		// 中身が変わらない添字再利用はここで一掃されるので、古い AABB を誤って使い回すことはない
		const std::uint64_t topology = m_world.topologyVersion();
		if (topology != m_staticAabbCacheVersion)
		{
			m_staticAabbCache.assign(m_world.colliders().size(), AABBCollider3D{});
			m_staticAabbCacheComputed.assign(m_world.colliders().size(), false);
			m_staticAabbCacheVersion = topology;
		}

		for (std::size_t i = 0; i < m_world.colliders().size(); ++i)
		{
			const auto& collider = m_world.colliders()[i];
			const auto* body = m_world.getBody(collider.bodyId);
			const bool isStatic = body != nullptr && body->isStatic();

			AABBCollider3D aabb;
			if (isStatic)
			{
				// 静的コライダーは毎フレーム再計算せず、初回だけ計算してキャッシュを使い回す。
				// このキャッシュは topologyVersion (add/removeCollider) でしか無効化されないため、
				// 生成後に isStatic() な body の setPosition() を呼んで動かすと、ブロードフェーズは
				// 古い位置の AABB を使い続ける (現状そのような呼び出し元はリポジトリ内に無い)。
				// 動く床のような静的質量の可動体を作る場合は、この前提が崩れることに注意する。
				if (!m_staticAabbCacheComputed[i])
				{
					m_staticAabbCache[i] = PhysicsWorld3D::computeAABB(collider);
					m_staticAabbCacheComputed[i] = true;
				}
				aabb = m_staticAabbCache[i];
			}
			else
			{
				aabb = PhysicsWorld3D::computeAABB(collider);
			}

			m_broadPhase.addProxy(collider.bodyId, i, aabb, isStatic);
		}

		m_broadPhase.sweep();
	}

	/// @brief 単一接触点をマニフォールドとして積み、Enter/Stay/Exit 用に記録する
	void pushManifold(const BodyCollider& colA, const BodyCollider& colB,
		const ContactInfo3D& contact) noexcept
	{
		const bool isTrigger = colA.isTrigger || colB.isTrigger;

		ContactManifold3D manifold;
		manifold.bodyIdA = colA.bodyId;
		manifold.bodyIdB = colB.bodyId;
		manifold.point = contact.point;
		manifold.normal = contact.normal;
		manifold.depth = contact.depth;

		if (isTrigger)
		{
			// トリガーは押し返さない。ContactSolver には渡さずコールバックのみ発火する
			m_lastTriggerContacts.push_back(manifold);
		}
		else
		{
			m_lastManifolds.push_back(manifold);
		}

		recordContactPair(colA.bodyId, colB.bodyId, manifold.point, manifold.normal,
			manifold.depth, isTrigger);
	}

	/// @brief box-box ペアの複数接触点マニフォールドを積む
	/// @details Enter/Stay/Exit の記録は (bodyA,bodyB) キー1つに1件が前提
	///          （ContactPairRecord3D::less）なので、点ごとには記録せずペア単位で1回だけ呼ぶ
	void processBoxBoxPair(const BodyCollider& colA, const BodyCollider& colB) noexcept
	{
		const auto manifold = NarrowPhase3D::testBoxBoxManifold(colA.aabb, colB.aabb);
		if (!manifold.hasContact) return;

		const bool isTrigger = colA.isTrigger || colB.isTrigger;

		for (std::size_t i = 0; i < manifold.count; ++i)
		{
			ContactManifold3D m;
			m.bodyIdA = colA.bodyId;
			m.bodyIdB = colB.bodyId;
			m.point = manifold.points[i].point;
			m.normal = manifold.normal;
			m.depth = manifold.points[i].depth;
			m.featureId = manifold.points[i].featureId;
			m.pairPointCount = static_cast<std::uint32_t>(manifold.count);

			if (isTrigger) m_lastTriggerContacts.push_back(m);
			else m_lastManifolds.push_back(m);
		}

		recordContactPair(colA.bodyId, colB.bodyId,
			manifold.points[0].point, manifold.normal, manifold.points[0].depth, isTrigger);
	}

	/// @brief ナローフェーズを実行し、マニフォールドを生成する
	void runNarrowPhase() noexcept
	{
		m_lastManifolds.clear();
		m_lastTriggerContacts.clear();
		m_currentPairRecords.clear();

		for (const auto& pair : m_broadPhase.candidatePairs())
		{
			const auto& colA = m_world.colliders()[pair.indexA];
			const auto& colB = m_world.colliders()[pair.indexB];

			// レイヤー/マスクで対象外ならナローフェーズにすら渡さない
			if (!layersCollide(colA.layer, colA.mask, colB.layer, colB.mask)) continue;

			// box-box だけは複数接触点を持てるため専用パスへ分ける
			if (colA.type == ColliderType3D::AABB && colB.type == ColliderType3D::AABB)
			{
				processBoxBoxPair(colA, colB);
				continue;
			}

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
				pushManifold(colA, colB, result.contact);
			}
		}
	}

	/// @brief このフレームの接触ペアを記録する（Enter/Stay/Exit 判定用）
	void recordContactPair(BodyId bodyA, BodyId bodyB,
		const sgc::Vec3f& point, const sgc::Vec3f& normal, float depth, bool isTrigger) noexcept
	{
		ContactPairRecord3D record;
		record.a = std::min(bodyA, bodyB);
		record.b = std::max(bodyA, bodyB);
		record.point = point;
		record.normal = normal;
		record.depth = depth;
		record.isTrigger = isTrigger;
		m_currentPairRecords.push_back(record);
	}

	// ── イベント通知 ──────────────────────────────────────────

	/// @brief 衝突コールバックに通知する（通常接触 + トリガー接触の両方）
	void notifyCollisions()
	{
		if (m_ecsCallbacks.empty()) return;

		notifyCollisionList(m_lastManifolds, false);
		notifyCollisionList(m_lastTriggerContacts, true);
	}

	/// @brief マニフォールド列を CollisionEvent3D に変換してコールバックへ渡す
	void notifyCollisionList(const std::vector<ContactManifold3D>& manifolds, bool isTrigger)
	{
		for (const auto& manifold : manifolds)
		{
			CollisionEvent3D event;
			event.entityA = entityForBody(manifold.bodyIdA);
			event.entityB = entityForBody(manifold.bodyIdB);
			event.point = manifold.point;
			event.normal = manifold.normal;
			event.depth = manifold.depth;
			event.isTrigger = isTrigger;

			for (const auto& cb : m_ecsCallbacks)
			{
				cb(event);
			}
		}
	}

	/// @brief 前フレームとの接触ペア差分から Enter/Stay/Exit を通知する
	///
	/// @details m_currentPairRecords / m_previousPairRecords はどちらも
	///          bodyId の組でソート済みの状態で保持し、毎フレーム swap して使い回す
	///          （2本のバッファを行き来させるだけで allocation を避ける）。
	void notifyContactEvents()
	{
		std::sort(m_currentPairRecords.begin(), m_currentPairRecords.end(), ContactPairRecord3D::less);
		// 下のマージは多重集合なので、同じ (a, b, isTrigger) を複数の manifold が積むと Enter を件数分
		// 出し、件数が減っただけで Exit を出す。鍵ごとに 1 件へ潰してから比較する。
		m_currentPairRecords.erase(
			std::unique(m_currentPairRecords.begin(), m_currentPairRecords.end(),
				[](const ContactPairRecord3D& x, const ContactPairRecord3D& y) noexcept {
					return !ContactPairRecord3D::less(x, y) && !ContactPairRecord3D::less(y, x);
				}),
			m_currentPairRecords.end());

		if (!m_contactEventCallbacks.empty())
		{
			std::size_t i = 0, j = 0;
			while (i < m_previousPairRecords.size() || j < m_currentPairRecords.size())
			{
				const bool hasPrev = i < m_previousPairRecords.size();
				const bool hasCurr = j < m_currentPairRecords.size();

				if (hasPrev && (!hasCurr ||
					ContactPairRecord3D::less(m_previousPairRecords[i], m_currentPairRecords[j])))
				{
					emitContactEvent(m_previousPairRecords[i], ContactPhase3D::Exit);
					++i;
				}
				else if (hasCurr && (!hasPrev ||
					ContactPairRecord3D::less(m_currentPairRecords[j], m_previousPairRecords[i])))
				{
					emitContactEvent(m_currentPairRecords[j], ContactPhase3D::Enter);
					++j;
				}
				else
				{
					emitContactEvent(m_currentPairRecords[j], ContactPhase3D::Stay);
					++i;
					++j;
				}
			}
		}

		std::swap(m_previousPairRecords, m_currentPairRecords);
		m_currentPairRecords.clear();
	}

	/// @brief 1件の接触ペア記録をコールバックへ渡す
	void emitContactEvent(const ContactPairRecord3D& record, ContactPhase3D phase)
	{
		ContactEvent3D event;
		event.entityA = entityForBody(record.a);
		event.entityB = entityForBody(record.b);
		event.point = record.point;
		event.normal = record.normal;
		event.depth = record.depth;
		event.isTrigger = record.isTrigger;
		event.phase = phase;

		for (const auto& cb : m_contactEventCallbacks)
		{
			cb(event);
		}
	}

	/// @brief ボディID からエンティティID を逆引きする（見つからなければ INVALID_ENTITY）
	[[nodiscard]] scene::EntityId entityForBody(BodyId bodyId) const noexcept
	{
		const auto it = m_bodyToEntity.find(bodyId);
		return (it != m_bodyToEntity.end()) ? it->second : scene::INVALID_ENTITY;
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

	// ── 静的コライダーAABBキャッシュ（runBroadPhase 用） ──────────
	// 静的コライダーは形状・姿勢が変わらない前提で AABB を使い回す。
	// m_world.topologyVersion()（コライダー追加/削除でのみ増加）が変わったときだけ全体を作り直す
	std::vector<AABBCollider3D> m_staticAabbCache;
	std::vector<bool> m_staticAabbCacheComputed;
	std::uint64_t m_staticAabbCacheVersion{static_cast<std::uint64_t>(-1)};

	/// @brief 最後のステップで生成されたマニフォールド（トリガー除く、ソルバー入力）
	std::vector<ContactManifold3D> m_lastManifolds;

	/// @brief 最後のステップで生成されたトリガー接触（ソルバーには渡さない）
	std::vector<ContactManifold3D> m_lastTriggerContacts;

	/// @brief ECSレベルの衝突コールバック
	std::vector<CollisionCallback3DEcs> m_ecsCallbacks;

	/// @brief Enter/Stay/Exit を区別する接触イベントコールバック
	std::vector<ContactEventCallback3D> m_contactEventCallbacks;

	/// @brief 今フレームの接触ペア（notifyContactEvents() で前フレームと比較後、swapして使い回す）
	std::vector<ContactPairRecord3D> m_currentPairRecords;

	/// @brief 前フレームの接触ペア
	std::vector<ContactPairRecord3D> m_previousPairRecords;

	/// @brief デバッグレンダラー
	PhysicsDebugRenderer3D m_debugRenderer;
};

} // namespace mitiru::physics3d
