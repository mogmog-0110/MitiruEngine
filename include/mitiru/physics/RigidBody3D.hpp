#pragma once

/// @file RigidBody3D.hpp
/// @brief 3D剛体シミュレーション
///
/// 質量、慣性テンソル、線形/角速度、力/トルク蓄積、
/// 半暗黙的オイラー積分を提供する。
///
/// @code
/// mitiru::physics3d::RigidBody3D body;
/// body.setMass(2.0f);
/// body.applyForce({0, -9.81f * 2.0f, 0});
/// body.integrate(1.0f / 60.0f);
/// @endcode

#include <cmath>
#include <cstdint>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Mat3.hpp"

namespace mitiru::physics3d
{

/// @brief 剛体ID型
using BodyId = uint32_t;

/// @brief 無効なボディID
inline constexpr BodyId INVALID_BODY_ID = 0;

/// @brief 3D剛体
///
/// 線形運動と角運動をシミュレートする。
/// 半暗黙的オイラー積分（symplectic Euler）を使用。
class RigidBody3D
{
public:
	/// @brief デフォルトコンストラクタ（質量1kgの動的ボディ）
	RigidBody3D() noexcept
	{
		setMass(1.0f);
	}

	/// @brief 質量を指定して構築する
	/// @param mass 質量（kg）。0以下で静的ボディ。
	explicit RigidBody3D(float mass) noexcept
	{
		setMass(mass);
	}

	// ── 位置・回転 ────────────────────────────────────────────

	/// @brief 位置を取得する
	[[nodiscard]] constexpr const sgc::Vec3f& position() const noexcept { return m_position; }

	/// @brief 位置を設定する
	void setPosition(const sgc::Vec3f& pos) noexcept { m_position = pos; }

	/// @brief 回転を取得する
	[[nodiscard]] constexpr const sgc::Quaternionf& rotation() const noexcept { return m_rotation; }

	/// @brief 回転を設定する
	void setRotation(const sgc::Quaternionf& rot) noexcept { m_rotation = rot.normalized(); }

	// ── 質量・慣性 ────────────────────────────────────────────

	/// @brief 質量を取得する
	[[nodiscard]] constexpr float mass() const noexcept { return m_mass; }

	/// @brief 逆質量を取得する
	[[nodiscard]] constexpr float inverseMass() const noexcept { return m_inverseMass; }

	/// @brief 質量を設定する
	/// @param mass 質量（kg）。0以下で静的ボディ。
	void setMass(float mass) noexcept
	{
		m_mass = mass;
		m_inverseMass = (mass > 0.0f) ? (1.0f / mass) : 0.0f;

		// デフォルトの慣性テンソル（球の近似: 2/5 * m * r^2, r=1）
		if (mass > 0.0f)
		{
			const float inertia = 0.4f * mass;
			setInertiaTensor(sgc::Mat3f{
				inertia, 0.0f, 0.0f,
				0.0f, inertia, 0.0f,
				0.0f, 0.0f, inertia
			});
		}
		else
		{
			m_inertiaTensor = sgc::Mat3f{};
			m_inverseInertiaTensor = sgc::Mat3f{};
		}
	}

	/// @brief 慣性テンソル（ローカル空間）を取得する
	[[nodiscard]] constexpr const sgc::Mat3f& inertiaTensor() const noexcept
	{
		return m_inertiaTensor;
	}

	/// @brief 逆慣性テンソル（ローカル空間）を取得する
	[[nodiscard]] constexpr const sgc::Mat3f& inverseInertiaTensor() const noexcept
	{
		return m_inverseInertiaTensor;
	}

	/// @brief 慣性テンソルを設定する
	/// @param tensor 慣性テンソル（3x3対角行列を推奨）
	void setInertiaTensor(const sgc::Mat3f& tensor) noexcept
	{
		m_inertiaTensor = tensor;
		m_inverseInertiaTensor = tensor.inversed();
	}

	/// @brief 箱の慣性テンソルを設定する
	/// @param halfExtents 半径サイズ
	void setBoxInertiaTensor(const sgc::Vec3f& halfExtents) noexcept
	{
		if (m_mass <= 0.0f) return;
		const float w2 = 4.0f * halfExtents.x * halfExtents.x;
		const float h2 = 4.0f * halfExtents.y * halfExtents.y;
		const float d2 = 4.0f * halfExtents.z * halfExtents.z;
		const float factor = m_mass / 12.0f;
		setInertiaTensor(sgc::Mat3f{
			factor * (h2 + d2), 0.0f, 0.0f,
			0.0f, factor * (w2 + d2), 0.0f,
			0.0f, 0.0f, factor * (w2 + h2)
		});
	}

	/// @brief 球の慣性テンソルを設定する
	/// @param radius 半径
	void setSphereInertiaTensor(float radius) noexcept
	{
		if (m_mass <= 0.0f) return;
		const float inertia = 0.4f * m_mass * radius * radius;
		setInertiaTensor(sgc::Mat3f{
			inertia, 0.0f, 0.0f,
			0.0f, inertia, 0.0f,
			0.0f, 0.0f, inertia
		});
	}

	/// @brief 静的ボディかどうかを返す
	[[nodiscard]] constexpr bool isStatic() const noexcept { return m_inverseMass == 0.0f; }

	// ── 速度 ──────────────────────────────────────────────────

	/// @brief 線形速度を取得する
	[[nodiscard]] constexpr const sgc::Vec3f& linearVelocity() const noexcept { return m_linearVelocity; }

	/// @brief 線形速度を設定する
	void setLinearVelocity(const sgc::Vec3f& vel) noexcept { m_linearVelocity = vel; }

	/// @brief 角速度を取得する
	[[nodiscard]] constexpr const sgc::Vec3f& angularVelocity() const noexcept { return m_angularVelocity; }

	/// @brief 角速度を設定する
	void setAngularVelocity(const sgc::Vec3f& vel) noexcept { m_angularVelocity = vel; }

	// ── 減衰 ──────────────────────────────────────────────────

	/// @brief 線形減衰率を取得する
	[[nodiscard]] constexpr float linearDamping() const noexcept { return m_linearDamping; }

	/// @brief 線形減衰率を設定する
	void setLinearDamping(float damping) noexcept { m_linearDamping = damping; }

	/// @brief 角速度減衰率を取得する
	[[nodiscard]] constexpr float angularDamping() const noexcept { return m_angularDamping; }

	/// @brief 角速度減衰率を設定する
	void setAngularDamping(float damping) noexcept { m_angularDamping = damping; }

	// ── 反発・摩擦 ────────────────────────────────────────────

	/// @brief 反発係数を取得する
	[[nodiscard]] constexpr float restitution() const noexcept { return m_restitution; }

	/// @brief 反発係数を設定する
	void setRestitution(float e) noexcept { m_restitution = e; }

	/// @brief 摩擦係数を取得する
	[[nodiscard]] constexpr float friction() const noexcept { return m_friction; }

	/// @brief 摩擦係数を設定する
	void setFriction(float f) noexcept { m_friction = f; }

	// ── 力・トルク ────────────────────────────────────────────

	/// @brief 力を加える（ワールド空間、重心に適用）
	/// @param force 力ベクトル（N）
	void applyForce(const sgc::Vec3f& force) noexcept
	{
		m_accumulatedForce += force;
	}

	/// @brief トルクを加える
	/// @param torque トルクベクトル（N*m）
	void applyTorque(const sgc::Vec3f& torque) noexcept
	{
		m_accumulatedTorque += torque;
	}

	/// @brief 特定点に力を加える（トルクも発生する）
	/// @param force 力ベクトル（N）
	/// @param worldPoint 力の作用点（ワールド座標）
	void applyForceAtPoint(const sgc::Vec3f& force, const sgc::Vec3f& worldPoint) noexcept
	{
		m_accumulatedForce += force;
		const sgc::Vec3f r = worldPoint - m_position;
		m_accumulatedTorque += r.cross(force);
	}

	/// @brief インパルスを適用する（速度に直接加算）
	/// @param impulse インパルスベクトル（N*s）
	void applyImpulse(const sgc::Vec3f& impulse) noexcept
	{
		m_linearVelocity += impulse * m_inverseMass;
	}

	/// @brief 特定点にインパルスを適用する
	/// @param impulse インパルスベクトル（N*s）
	/// @param worldPoint 作用点（ワールド座標）
	void applyImpulseAtPoint(const sgc::Vec3f& impulse, const sgc::Vec3f& worldPoint) noexcept
	{
		m_linearVelocity += impulse * m_inverseMass;
		const sgc::Vec3f r = worldPoint - m_position;
		m_angularVelocity += m_inverseInertiaTensor * r.cross(impulse);
	}

	/// @brief 蓄積された力・トルクをクリアする
	void clearForces() noexcept
	{
		m_accumulatedForce = {};
		m_accumulatedTorque = {};
	}

	// ── 積分 ──────────────────────────────────────────────────

	/// @brief 半暗黙的オイラー積分を実行する
	/// @param dt タイムステップ（秒）
	///
	/// @details 処理順序:
	///   1. 力 → 線形加速度 → 速度更新
	///   2. トルク → 角加速度 → 角速度更新
	///   3. 減衰適用
	///   4. 速度 → 位置更新
	///   5. 角速度 → 回転更新
	///   6. 力・トルクをクリア
	void integrate(float dt) noexcept
	{
		if (isStatic()) return;

		// 線形運動
		const sgc::Vec3f linearAcceleration = m_accumulatedForce * m_inverseMass;
		m_linearVelocity += linearAcceleration * dt;

		// 角運動
		const sgc::Vec3f angularAcceleration = m_inverseInertiaTensor * m_accumulatedTorque;
		m_angularVelocity += angularAcceleration * dt;

		// 減衰
		m_linearVelocity *= std::pow(1.0f - m_linearDamping, dt);
		m_angularVelocity *= std::pow(1.0f - m_angularDamping, dt);

		// 位置更新
		m_position += m_linearVelocity * dt;

		// 回転更新（クォータニオン微分）
		const sgc::Quaternionf spin{
			m_angularVelocity.x * 0.5f,
			m_angularVelocity.y * 0.5f,
			m_angularVelocity.z * 0.5f,
			0.0f
		};
		const auto dq = spin * m_rotation;
		m_rotation = sgc::Quaternionf{
			m_rotation.x + dq.x * dt,
			m_rotation.y + dq.y * dt,
			m_rotation.z + dq.z * dt,
			m_rotation.w + dq.w * dt
		}.normalized();

		clearForces();
	}

	// ── ユーザーデータ ────────────────────────────────────────

	/// @brief ボディIDを取得する
	[[nodiscard]] constexpr BodyId id() const noexcept { return m_id; }

	/// @brief ボディIDを設定する
	void setId(BodyId id) noexcept { m_id = id; }

private:
	sgc::Vec3f m_position{};
	sgc::Quaternionf m_rotation{};

	float m_mass{1.0f};
	float m_inverseMass{1.0f};
	sgc::Mat3f m_inertiaTensor{};
	sgc::Mat3f m_inverseInertiaTensor{};

	sgc::Vec3f m_linearVelocity{};
	sgc::Vec3f m_angularVelocity{};

	sgc::Vec3f m_accumulatedForce{};
	sgc::Vec3f m_accumulatedTorque{};

	float m_linearDamping{0.01f};
	float m_angularDamping{0.05f};
	float m_restitution{0.3f};
	float m_friction{0.5f};

	BodyId m_id{INVALID_BODY_ID};
};

} // namespace mitiru::physics3d
