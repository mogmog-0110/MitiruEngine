#pragma once

/// @file PhysicsWorld2D.hpp
/// @brief 2D物理ワールドの具象実装
///
/// IPhysicsWorld2Dインターフェースの実装。
/// 重力適用、セミインプリシットオイラー積分、N^2ブロードフェーズのAABB衝突検出を行う。
///
/// @code
/// sgc::physics::PhysicsWorld2Df world;
/// world.setGravity({0.0f, 980.0f});
///
/// sgc::physics::RigidBody2Df player;
/// player.position = {50.0f, 0.0f};
/// player.mass = 1.0f;
/// player.restitution = 0.3f;
/// world.addBody(&player, {16.0f, 16.0f});
///
/// sgc::physics::RigidBody2Df ground;
/// ground.position = {50.0f, 300.0f};
/// ground.isStatic = true;
/// world.addBody(&ground, {200.0f, 16.0f});
///
/// world.step(1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

#include "sgc/physics/Collider2D.hpp"
#include "sgc/physics/IPhysicsWorld.hpp"
#include "sgc/physics/AABB2DCollision.hpp"

namespace sgc::physics
{

/// @brief 剛体エントリ（剛体ポインタとAABBハーフサイズのペア）
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct BodyEntry
{
	RigidBody2D<T>* body{nullptr};  ///< 剛体へのポインタ
	Vec2<T> halfSize{};              ///< AABBの半分のサイズ
};

/// @brief 2D物理ワールドの具象実装
/// @tparam T 浮動小数点型
///
/// step()では以下の順序で処理を行う:
/// 1. 全動的剛体に重力を適用
/// 2. 全剛体を積分（位置・速度更新）
/// 3. N^2衝突検出＋衝突応答
template <FloatingPoint T>
class PhysicsWorld2D final : public IPhysicsWorld2D<T>
{
public:
	/// @brief デフォルトコンストラクタ
	PhysicsWorld2D() = default;

	/// @brief 重力を指定して構築する
	/// @param gravity 重力ベクトル
	explicit PhysicsWorld2D(const Vec2<T>& gravity)
		: m_gravity(gravity)
	{
	}

	/// @brief 剛体をワールドに追加する
	/// @param body 追加する剛体へのポインタ
	/// @param halfSize AABBの半分のサイズ
	void addBody(RigidBody2D<T>* body, const Vec2<T>& halfSize) override
	{
		if (!body) return;
		m_bodies.push_back(BodyEntry<T>{body, halfSize});
	}

	/// @brief 剛体をワールドから除去する
	/// @param body 除去する剛体へのポインタ
	void removeBody(RigidBody2D<T>* body) override
	{
		m_bodies.erase(
			std::remove_if(m_bodies.begin(), m_bodies.end(),
				[body](const BodyEntry<T>& entry) { return entry.body == body; }),
			m_bodies.end());
	}

	/// @brief 物理シミュレーションを1ステップ進める
	/// @param dt タイムステップ幅（秒）
	///
	/// 重力適用 → 積分 → 衝突検出＋応答 の順で処理する。
	void step(T dt) override
	{
		// 1. 全動的剛体に重力を適用
		for (auto& entry : m_bodies)
		{
			if (!entry.body->isStatic)
			{
				entry.body->applyForce(m_gravity * entry.body->mass);
			}
		}

		// 2. 全剛体を積分
		for (auto& entry : m_bodies)
		{
			entry.body->integrate(dt);
		}

		// 3. N^2衝突検出＋応答
		const auto count = m_bodies.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			for (std::size_t j = i + 1; j < count; ++j)
			{
				auto& a = m_bodies[i];
				auto& b = m_bodies[j];

				// 両方静的なら衝突判定不要
				if (a.body->isStatic && b.body->isStatic) continue;

				// 衝突フィルターチェック
				if (!shouldCollide(a.body->filter, b.body->filter)) continue;

				const auto aabbA = a.body->bounds(a.halfSize);
				const auto aabbB = b.body->bounds(b.halfSize);

				const auto collision = detectAABBCollision(aabbA, aabbB);
				if (collision.colliding)
				{
					resolveRigidBodyCollision(*a.body, *b.body, collision);
				}
			}
		}
	}

	/// @brief 重力ベクトルを設定する
	/// @param gravity 重力ベクトル
	void setGravity(const Vec2<T>& gravity) override
	{
		m_gravity = gravity;
	}

	/// @brief 現在の重力ベクトルを取得する
	/// @return 重力ベクトル
	[[nodiscard]] Vec2<T> gravity() const override
	{
		return m_gravity;
	}

	/// @brief 登録されている剛体数を取得する
	/// @return 剛体数
	[[nodiscard]] std::size_t bodyCount() const override
	{
		return m_bodies.size();
	}

	/// @brief 全剛体をワールドから除去する
	void clear() override
	{
		m_bodies.clear();
	}

	/// @brief 登録済み剛体エントリ一覧を取得する（読み取り専用）
	/// @return 剛体エントリのベクタ参照
	[[nodiscard]] const std::vector<BodyEntry<T>>& bodies() const noexcept
	{
		return m_bodies;
	}

private:
	std::vector<BodyEntry<T>> m_bodies;       ///< 登録済み剛体エントリ
	Vec2<T> m_gravity{T{0}, T{0}};            ///< 重力ベクトル
};

/// @brief float版 PhysicsWorld2D
using PhysicsWorld2Df = PhysicsWorld2D<float>;

// ── ID管理型2D物理ワールド ──────────────────────────────

/// @brief ボディID型（ワールド内のハンドル）
using BodyId2D = uint32_t;

/// @brief 無効なボディID
inline constexpr BodyId2D INVALID_BODY_ID_2D = UINT32_MAX;

/// @brief 2Dコライダーバリアント型
using Collider2DVariant = std::variant<CircleCollider, AABBCollider, OBBCollider, CapsuleCollider>;

/// @brief 2D衝突コールバック型
using CollisionCallback2D = std::function<void(BodyId2D, BodyId2D, const Contact2D&)>;

/// @brief ID管理型2D物理ワールドの内部エントリ
struct ManagedBodyEntry2D
{
	RigidBody2D<float> body;                   ///< 剛体データ
	Collider2DVariant collider;                 ///< コライダー形状
	bool active{true};                          ///< アクティブフラグ
	bool isTrigger{false};                      ///< トリガー（センサー）フラグ
};

/// @brief ID管理型2D物理ワールド
///
/// ボディIDで剛体を管理し、コライダーバリアントによる複数形状の衝突検出、
/// 衝突コールバック、トリガーコールバックを提供する。
///
/// @code
/// using namespace sgc::physics;
///
/// ManagedPhysicsWorld2D world;
/// world.setGravity({0.0f, 980.0f});
///
/// auto body = RigidBody2D<float>::createDynamic(1.0f, {50.0f, 0.0f});
/// CircleCollider collider{{0.0f, 0.0f}, 16.0f};
/// auto id = world.addBody(body, collider);
///
/// world.onCollision([](BodyId2D a, BodyId2D b, const Contact2D& c) {
///     // 衝突処理
/// });
///
/// world.step(1.0f / 60.0f);
/// @endcode
class ManagedPhysicsWorld2D
{
public:
	/// @brief デフォルトコンストラクタ
	ManagedPhysicsWorld2D() = default;

	/// @brief 重力を指定して構築する
	/// @param gravity 重力ベクトル
	explicit ManagedPhysicsWorld2D(const Vec2f& gravity) noexcept
		: m_gravity{gravity}
	{
	}

	// ── 重力 ──

	/// @brief 重力を設定する
	/// @param gravity 重力ベクトル
	void setGravity(const Vec2f& gravity) noexcept { m_gravity = gravity; }

	/// @brief 重力を取得する
	[[nodiscard]] const Vec2f& gravity() const noexcept { return m_gravity; }

	// ── ボディ管理 ──

	/// @brief 剛体をワールドに追加する
	/// @param body 剛体データ
	/// @param collider コライダー形状
	/// @return ボディID
	[[nodiscard]] BodyId2D addBody(
		const RigidBody2D<float>& body,
		const Collider2DVariant& collider)
	{
		const auto id = static_cast<BodyId2D>(m_entries.size());
		m_entries.push_back(ManagedBodyEntry2D{body, collider, true, false});
		return id;
	}

	/// @brief トリガー（センサー）ボディをワールドに追加する
	/// @param body 剛体データ
	/// @param collider コライダー形状
	/// @return ボディID
	[[nodiscard]] BodyId2D addTriggerBody(
		const RigidBody2D<float>& body,
		const Collider2DVariant& collider)
	{
		const auto id = static_cast<BodyId2D>(m_entries.size());
		m_entries.push_back(ManagedBodyEntry2D{body, collider, true, true});
		return id;
	}

	/// @brief 剛体をワールドから除去する（非アクティブ化）
	/// @param id ボディID
	void removeBody(BodyId2D id)
	{
		if (id < m_entries.size())
		{
			m_entries[id].active = false;
		}
	}

	/// @brief 剛体データへのポインタを取得する
	/// @param id ボディID
	/// @return 剛体データへのポインタ（無効な場合はnullptr）
	[[nodiscard]] RigidBody2D<float>* getBody(BodyId2D id)
	{
		if (id < m_entries.size() && m_entries[id].active)
		{
			return &m_entries[id].body;
		}
		return nullptr;
	}

	/// @brief 剛体データへのポインタを取得する（const版）
	/// @param id ボディID
	/// @return 剛体データへのconstポインタ（無効な場合はnullptr）
	[[nodiscard]] const RigidBody2D<float>* getBody(BodyId2D id) const
	{
		if (id < m_entries.size() && m_entries[id].active)
		{
			return &m_entries[id].body;
		}
		return nullptr;
	}

	/// @brief コライダーを変更する
	/// @param id ボディID
	/// @param collider 新しいコライダー
	void setCollider(BodyId2D id, const Collider2DVariant& collider)
	{
		if (id < m_entries.size() && m_entries[id].active)
		{
			m_entries[id].collider = collider;
		}
	}

	/// @brief ボディの位置を設定する
	/// @param id ボディID
	/// @param pos 新しい位置
	void setBodyPosition(BodyId2D id, const Vec2f& pos)
	{
		if (id < m_entries.size() && m_entries[id].active)
		{
			m_entries[id].body.position = pos;
		}
	}

	/// @brief ボディの速度を設定する
	/// @param id ボディID
	/// @param vel 新しい速度
	void setBodyVelocity(BodyId2D id, const Vec2f& vel)
	{
		if (id < m_entries.size() && m_entries[id].active)
		{
			m_entries[id].body.velocity = vel;
		}
	}

	/// @brief アクティブなボディ数を取得する
	[[nodiscard]] std::size_t bodyCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& entry : m_entries)
		{
			if (entry.active) ++count;
		}
		return count;
	}

	// ── コールバック ──

	/// @brief 衝突コールバックを設定する
	/// @param callback 衝突時に呼ばれるコールバック
	void onCollision(CollisionCallback2D callback)
	{
		m_onCollision = std::move(callback);
	}

	/// @brief トリガーコールバックを設定する
	/// @param callback トリガー衝突時に呼ばれるコールバック
	void onTrigger(CollisionCallback2D callback)
	{
		m_onTrigger = std::move(callback);
	}

	// ── シミュレーション ──

	/// @brief 物理ステップを1回実行する
	/// @param dt タイムステップ幅（秒）
	///
	/// 処理順序:
	/// 1. 動的剛体に重力を適用
	/// 2. 全剛体を積分
	/// 3. 衝突検出＋応答＋コールバック
	void step(float dt)
	{
		if (dt <= 0.0f) return;

		// 1. 重力適用 + 2. 積分
		for (auto& entry : m_entries)
		{
			if (!entry.active) continue;
			auto& body = entry.body;

			if (body.type == RigidBody2DType::Dynamic && !body.isStatic)
			{
				body.applyForce(m_gravity * body.mass);
			}

			body.integrate(dt);
		}

		// 3. 衝突検出＋応答
		const auto count = static_cast<BodyId2D>(m_entries.size());
		for (BodyId2D i = 0; i < count; ++i)
		{
			if (!m_entries[i].active) continue;

			for (BodyId2D j = i + 1; j < count; ++j)
			{
				if (!m_entries[j].active) continue;

				auto& entryA = m_entries[i];
				auto& entryB = m_entries[j];

				// 両方静的/キネマティックならスキップ
				const bool aStatic = entryA.body.isStatic ||
					entryA.body.type == RigidBody2DType::Static;
				const bool bStatic = entryB.body.isStatic ||
					entryB.body.type == RigidBody2DType::Static;
				if (aStatic && bStatic) continue;

				// 衝突判定
				const auto contact = testColliderPair(entryA, entryB);
				if (!contact) continue;

				// トリガーの場合はコールバックのみ（物理応答なし）
				if (entryA.isTrigger || entryB.isTrigger)
				{
					if (m_onTrigger)
					{
						m_onTrigger(i, j, *contact);
					}
					continue;
				}

				// 衝突応答
				resolveContact(entryA.body, entryB.body, *contact);

				// コールバック
				if (m_onCollision)
				{
					m_onCollision(i, j, *contact);
				}
			}
		}
	}

	/// @brief 全エントリを除去する
	void clear()
	{
		m_entries.clear();
	}

private:
	/// @brief 2つのエントリ間の衝突判定を行う
	/// @param a エントリA
	/// @param b エントリB
	/// @return 衝突している場合は接触情報
	[[nodiscard]] static std::optional<Contact2D> testColliderPair(
		const ManagedBodyEntry2D& a, const ManagedBodyEntry2D& b)
	{
		// コライダーをワールド位置にオフセットして判定
		const auto colliderA = offsetCollider(a.collider, a.body.position);
		const auto colliderB = offsetCollider(b.collider, b.body.position);

		return std::visit([](const auto& ca, const auto& cb) -> std::optional<Contact2D>
		{
			return testPair(ca, cb);
		}, colliderA, colliderB);
	}

	/// @brief コライダーをワールド位置にオフセットする
	static Collider2DVariant offsetCollider(const Collider2DVariant& collider, const Vec2f& pos)
	{
		return std::visit([&pos](const auto& c) -> Collider2DVariant
		{
			return offsetColliderImpl(c, pos);
		}, collider);
	}

	static Collider2DVariant offsetColliderImpl(const CircleCollider& c, const Vec2f& pos)
	{
		return CircleCollider{c.center + pos, c.radius};
	}

	static Collider2DVariant offsetColliderImpl(const AABBCollider& c, const Vec2f& pos)
	{
		return AABBCollider{c.min + pos, c.max + pos};
	}

	static Collider2DVariant offsetColliderImpl(const OBBCollider& c, const Vec2f& pos)
	{
		return OBBCollider{c.center + pos, c.halfExtents, c.rotation};
	}

	static Collider2DVariant offsetColliderImpl(const CapsuleCollider& c, const Vec2f& pos)
	{
		return CapsuleCollider{c.pointA + pos, c.pointB + pos, c.radius};
	}

	// ── 衝突判定ディスパッチ ──

	static std::optional<Contact2D> testPair(const CircleCollider& a, const CircleCollider& b)
	{
		return testCircleCircle(a, b);
	}

	static std::optional<Contact2D> testPair(const CircleCollider& a, const AABBCollider& b)
	{
		return testCircleAABB(a, b);
	}

	static std::optional<Contact2D> testPair(const AABBCollider& a, const CircleCollider& b)
	{
		auto result = testCircleAABB(b, a);
		if (result)
		{
			result->normal = -result->normal;
		}
		return result;
	}

	static std::optional<Contact2D> testPair(const AABBCollider& a, const AABBCollider& b)
	{
		return testAABBAABB(a, b);
	}

	static std::optional<Contact2D> testPair(const CircleCollider& a, const OBBCollider& b)
	{
		return testCircleOBB(a, b);
	}

	static std::optional<Contact2D> testPair(const OBBCollider& a, const CircleCollider& b)
	{
		auto result = testCircleOBB(b, a);
		if (result)
		{
			result->normal = -result->normal;
		}
		return result;
	}

	// 未実装の組み合わせはnulloptを返す
	template <typename A, typename B>
	static std::optional<Contact2D> testPair(const A&, const B&)
	{
		return std::nullopt;
	}

	/// @brief 衝突応答（位置補正＋速度反射）
	static void resolveContact(
		RigidBody2D<float>& bodyA,
		RigidBody2D<float>& bodyB,
		const Contact2D& contact)
	{
		const bool aStatic = bodyA.isStatic || bodyA.type == RigidBody2DType::Static;
		const bool bStatic = bodyB.isStatic || bodyB.type == RigidBody2DType::Static;
		const bool aKinematic = bodyA.type == RigidBody2DType::Kinematic;
		const bool bKinematic = bodyB.type == RigidBody2DType::Kinematic;

		// 逆質量（静的・キネマティックは0）
		const float invMassA = (aStatic || aKinematic) ? 0.0f : bodyA.inverseMass;
		const float invMassB = (bStatic || bKinematic) ? 0.0f : bodyB.inverseMass;
		const float invMassSum = invMassA + invMassB;

		if (invMassSum <= 0.0f) return;

		// 位置補正
		const float correctionFactor = 0.2f;
		const float slop = 0.01f;
		const float correction = std::max(contact.penetration - slop, 0.0f)
			* correctionFactor / invMassSum;
		const Vec2f correctionVec = contact.normal * correction;

		bodyA.position = bodyA.position - correctionVec * invMassA;
		bodyB.position = bodyB.position + correctionVec * invMassB;

		// 速度応答
		const Vec2f relVel = bodyA.velocity - bodyB.velocity;
		const float velAlongNormal = relVel.dot(contact.normal);

		// 分離方向なら何もしない
		if (velAlongNormal < 0.0f) return;

		// 反発係数
		const float e = std::min(bodyA.restitution, bodyB.restitution);
		const float j = -(1.0f + e) * velAlongNormal / invMassSum;
		const Vec2f impulse = contact.normal * j;

		bodyA.velocity = bodyA.velocity + impulse * invMassA;
		bodyB.velocity = bodyB.velocity - impulse * invMassB;
	}

	std::vector<ManagedBodyEntry2D> m_entries;  ///< ボディエントリ
	Vec2f m_gravity{0.0f, 0.0f};                ///< 重力ベクトル
	CollisionCallback2D m_onCollision;           ///< 衝突コールバック
	CollisionCallback2D m_onTrigger;             ///< トリガーコールバック
};

} // namespace sgc::physics
