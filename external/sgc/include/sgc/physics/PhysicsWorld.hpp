#pragma once

/// @file PhysicsWorld.hpp
/// @brief 3D物理ワールドシミュレーション
///
/// 3D剛体の管理と物理シミュレーションを行うワールドクラス。
/// 固定タイムステップ蓄積、ブロードフェーズ衝突検出、スリープ状態管理、
/// 衝突イベントコールバックを提供する。
///
/// @code
/// using namespace sgc::physics;
///
/// PhysicsWorld3D world;
/// world.setGravity({0, -9.81f, 0});
///
/// BodyHandle h = world.addBody(RigidBody3D{});
/// world.getBody(h).mass = 5.0f;
/// world.getBody(h).position = {0, 10, 0};
///
/// world.setShape(h, SphereShape{0.5f});
///
/// world.onCollisionEnter([](BodyHandle a, BodyHandle b, const ContactPoint& c) {
///     // 衝突開始
/// });
///
/// world.step(1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/physics/CollisionResponse.hpp"
#include "sgc/physics/CollisionShape3D.hpp"
#include "sgc/physics/RigidBody3D.hpp"

namespace sgc::physics
{

/// @brief 剛体ハンドル（ワールド内のインデックス）
using BodyHandle = uint32_t;

/// @brief 無効なボディハンドル
inline constexpr BodyHandle INVALID_BODY_HANDLE = UINT32_MAX;

/// @brief 3Dワールド内の剛体エントリ
struct BodyEntry3D
{
	RigidBody3D body;                           ///< 剛体
	SphereShape boundingSphere{0.5f};           ///< バウンディング球（ブロードフェーズ用）
	bool active = true;                          ///< アクティブフラグ
	bool sleeping = false;                       ///< スリープ状態
	int lowVelocityFrames = 0;                   ///< 低速度フレーム数（スリープ判定用）
};

/// @brief スリープ判定パラメータ
struct SleepParams
{
	float velocityThreshold = 0.01f;     ///< スリープ遷移の速度閾値
	int framesToSleep = 60;               ///< 閾値以下が何フレーム続いたらスリープか
	bool enabled = true;                  ///< スリープ機能の有効/無効
};

/// @brief 衝突イベントコールバック型
using CollisionCallback = std::function<
	void(BodyHandle, BodyHandle, const ContactPoint&)>;

/// @brief 3D物理ワールド
///
/// 剛体の追加・削除、重力適用、衝突検出・応答、スリープ管理を行う。
/// step()で1物理ステップを実行する。
class PhysicsWorld3D
{
public:
	/// @brief デフォルトコンストラクタ
	PhysicsWorld3D() = default;

	/// @brief 重力を指定して構築する
	/// @param gravity 重力ベクトル
	explicit PhysicsWorld3D(const Vec3f& gravity) noexcept
		: m_gravity{gravity}
	{
	}

	// ── 剛体管理 ──

	/// @brief 剛体を追加する
	/// @param body 追加する剛体
	/// @return ボディハンドル
	[[nodiscard]] BodyHandle addBody(const RigidBody3D& body)
	{
		const auto handle = static_cast<BodyHandle>(m_bodies.size());
		m_bodies.push_back(BodyEntry3D{body, SphereShape{0.5f}, true, false, 0});
		return handle;
	}

	/// @brief 剛体を除去する（非アクティブ化）
	/// @param handle ボディハンドル
	void removeBody(BodyHandle handle)
	{
		if (handle < m_bodies.size())
		{
			m_bodies[handle].active = false;
		}
	}

	/// @brief 剛体への参照を取得する
	/// @param handle ボディハンドル
	/// @return 剛体への参照
	[[nodiscard]] RigidBody3D& getBody(BodyHandle handle)
	{
		return m_bodies[handle].body;
	}

	/// @brief 剛体への参照を取得する（const版）
	/// @param handle ボディハンドル
	/// @return 剛体へのconst参照
	[[nodiscard]] const RigidBody3D& getBody(BodyHandle handle) const
	{
		return m_bodies[handle].body;
	}

	/// @brief バウンディング球を設定する
	/// @param handle ボディハンドル
	/// @param shape バウンディング球形状
	void setShape(BodyHandle handle, const SphereShape& shape)
	{
		if (handle < m_bodies.size())
		{
			m_bodies[handle].boundingSphere = shape;
		}
	}

	/// @brief アクティブな剛体数を取得する
	[[nodiscard]] std::size_t activeBodyCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& entry : m_bodies)
		{
			if (entry.active) ++count;
		}
		return count;
	}

	/// @brief 全剛体エントリ数（非アクティブ含む）
	[[nodiscard]] std::size_t totalBodyCount() const noexcept
	{
		return m_bodies.size();
	}

	// ── 重力 ──

	/// @brief 重力を設定する
	/// @param gravity 重力ベクトル
	void setGravity(const Vec3f& gravity) noexcept { m_gravity = gravity; }

	/// @brief 重力を取得する
	[[nodiscard]] const Vec3f& gravity() const noexcept { return m_gravity; }

	// ── スリープ ──

	/// @brief スリープパラメータを設定する
	void setSleepParams(const SleepParams& params) noexcept { m_sleepParams = params; }

	/// @brief スリープパラメータを取得する
	[[nodiscard]] const SleepParams& sleepParams() const noexcept { return m_sleepParams; }

	/// @brief 指定ボディがスリープ中か
	[[nodiscard]] bool isSleeping(BodyHandle handle) const
	{
		if (handle < m_bodies.size())
		{
			return m_bodies[handle].sleeping;
		}
		return false;
	}

	/// @brief 指定ボディを起こす
	void wakeUp(BodyHandle handle)
	{
		if (handle < m_bodies.size())
		{
			m_bodies[handle].sleeping = false;
			m_bodies[handle].lowVelocityFrames = 0;
		}
	}

	// ── コールバック ──

	/// @brief 衝突開始コールバックを設定する
	void onCollisionEnter(CollisionCallback callback)
	{
		m_onCollisionEnter = std::move(callback);
	}

	/// @brief 衝突終了コールバックを設定する
	void onCollisionExit(CollisionCallback callback)
	{
		m_onCollisionExit = std::move(callback);
	}

	// ── シミュレーション ──

	/// @brief 物理ステップを1回実行する
	/// @param dt タイムステップ幅（秒）
	///
	/// 処理順序:
	/// 1. 重力適用
	/// 2. 積分（速度・位置更新）
	/// 3. ブロードフェーズ（バウンディング球による候補ペア生成）
	/// 4. ナローフェーズ（球同士の正確な衝突判定）
	/// 5. 衝突応答（インパルス＋位置補正）
	/// 6. スリープ判定
	void step(float dt)
	{
		if (dt <= 0.0f) return;

		// 1. 重力適用 + 2. 積分
		for (auto& entry : m_bodies)
		{
			if (!entry.active || entry.body.isStatic || entry.sleeping) continue;
			entry.body.applyForce(m_gravity * entry.body.mass);
			entry.body.integrate(dt);
		}

		// 3-5. 衝突検出＋応答
		// 現フレームの衝突ペアを収集
		std::vector<std::pair<BodyHandle, BodyHandle>> currentPairs;

		const auto count = static_cast<BodyHandle>(m_bodies.size());
		for (BodyHandle i = 0; i < count; ++i)
		{
			if (!m_bodies[i].active) continue;

			for (BodyHandle j = i + 1; j < count; ++j)
			{
				if (!m_bodies[j].active) continue;

				auto& a = m_bodies[i];
				auto& b = m_bodies[j];

				// 両方静的またはスリープ中はスキップ
				if ((a.body.isStatic || a.sleeping) &&
					(b.body.isStatic || b.sleeping))
				{
					continue;
				}

				// ブロードフェーズ: バウンディング球テスト
				const float radSum = a.boundingSphere.radius + b.boundingSphere.radius;
				const Vec3f diff = b.body.position - a.body.position;
				if (diff.lengthSquared() > radSum * radSum) continue;

				// ナローフェーズ: 球同士の衝突判定
				const auto result = testSphereSphere(
					a.body.position, a.boundingSphere,
					b.body.position, b.boundingSphere);

				if (result.collided)
				{
					ContactPoint contact;
					contact.position = result.contactPoint;
					contact.normal = result.contactNormal;
					contact.penetrationDepth = result.penetrationDepth;

					// 衝突応答
					(void)resolveCollision(a.body, b.body, contact, m_friction);
					separateBodies(a.body, b.body, contact, m_baumgarte);

					// スリープ中のボディを起こす
					if (a.sleeping) { a.sleeping = false; a.lowVelocityFrames = 0; }
					if (b.sleeping) { b.sleeping = false; b.lowVelocityFrames = 0; }

					currentPairs.emplace_back(i, j);

					// コールバック: 新規衝突か判定
					if (m_onCollisionEnter)
					{
						const bool wasColliding = isPairInSet(m_previousPairs, i, j);
						if (!wasColliding)
						{
							m_onCollisionEnter(i, j, contact);
						}
					}
				}
			}
		}

		// コールバック: 衝突終了判定
		if (m_onCollisionExit)
		{
			for (const auto& [pA, pB] : m_previousPairs)
			{
				if (!isPairInSet(currentPairs, pA, pB))
				{
					ContactPoint empty;
					m_onCollisionExit(pA, pB, empty);
				}
			}
		}

		m_previousPairs = std::move(currentPairs);

		// 6. スリープ判定
		if (m_sleepParams.enabled)
		{
			updateSleepStates();
		}
	}

	/// @brief 摩擦パラメータを設定する
	void setFriction(const FrictionParams& friction) noexcept { m_friction = friction; }

	/// @brief Baumgarteパラメータを設定する
	void setBaumgarte(const BaumgarteParams& params) noexcept { m_baumgarte = params; }

	/// @brief 全剛体を除去する
	void clear()
	{
		m_bodies.clear();
		m_previousPairs.clear();
	}

private:
	/// @brief ペアセット内に指定ペアが存在するか
	[[nodiscard]] static bool isPairInSet(
		const std::vector<std::pair<BodyHandle, BodyHandle>>& pairs,
		BodyHandle a, BodyHandle b) noexcept
	{
		for (const auto& [pA, pB] : pairs)
		{
			if ((pA == a && pB == b) || (pA == b && pB == a)) return true;
		}
		return false;
	}

	/// @brief スリープ状態を更新する
	void updateSleepStates()
	{
		for (auto& entry : m_bodies)
		{
			if (!entry.active || entry.body.isStatic) continue;

			const float speedSq = entry.body.velocity.lengthSquared()
				+ entry.body.angularVelocity.lengthSquared();
			const float threshold = m_sleepParams.velocityThreshold;

			if (speedSq < threshold * threshold)
			{
				++entry.lowVelocityFrames;
				if (entry.lowVelocityFrames >= m_sleepParams.framesToSleep)
				{
					entry.sleeping = true;
					entry.body.velocity = Vec3f::zero();
					entry.body.angularVelocity = Vec3f::zero();
				}
			}
			else
			{
				entry.lowVelocityFrames = 0;
				entry.sleeping = false;
			}
		}
	}

	std::vector<BodyEntry3D> m_bodies;     ///< 剛体エントリ
	Vec3f m_gravity{0.0f, -9.81f, 0.0f};  ///< 重力ベクトル
	FrictionParams m_friction;              ///< 摩擦パラメータ
	BaumgarteParams m_baumgarte;            ///< Baumgarteパラメータ
	SleepParams m_sleepParams;              ///< スリープパラメータ
	CollisionCallback m_onCollisionEnter;   ///< 衝突開始コールバック
	CollisionCallback m_onCollisionExit;    ///< 衝突終了コールバック

	/// @brief 前フレームの衝突ペア（Enter/Exit判定用）
	std::vector<std::pair<BodyHandle, BodyHandle>> m_previousPairs;
};

} // namespace sgc::physics
