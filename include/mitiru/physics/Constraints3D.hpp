#pragma once

/// @file Constraints3D.hpp
/// @brief 3D拘束ソルバー
///
/// 距離拘束とヒンジ拘束を提供する。
/// 反復ソルバーによるポジションベースの拘束解決。
///
/// @code
/// mitiru::physics3d::DistanceConstraint dist{&bodyA, &bodyB, 2.0f};
/// dist.solve(1.0f / 60.0f);
///
/// mitiru::physics3d::HingeConstraint hinge{&bodyA, &bodyB, pivot, axis};
/// hinge.solve(1.0f / 60.0f);
/// @endcode

#include <cmath>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

/// @brief 距離拘束
///
/// 2つの剛体間の距離を指定値に維持する。
/// ばね的な挙動を持つソフト拘束にもなる。
class DistanceConstraint
{
public:
	/// @brief コンストラクタ
	/// @param bodyA ボディA（nullptrで固定点として扱う）
	/// @param bodyB ボディB
	/// @param restLength 静止距離
	/// @param stiffness 剛性（0.0～1.0、1.0で完全剛体）
	DistanceConstraint(
		RigidBody3D* bodyA,
		RigidBody3D* bodyB,
		float restLength,
		float stiffness = 1.0f) noexcept
		: m_bodyA(bodyA)
		, m_bodyB(bodyB)
		, m_restLength(restLength)
		, m_stiffness(stiffness)
	{
	}

	/// @brief ローカルアンカー点を設定する
	/// @param anchorA ボディAのローカルアンカー
	/// @param anchorB ボディBのローカルアンカー
	void setAnchors(const sgc::Vec3f& anchorA, const sgc::Vec3f& anchorB) noexcept
	{
		m_localAnchorA = anchorA;
		m_localAnchorB = anchorB;
	}

	/// @brief 拘束を解決する
	/// @param dt タイムステップ（秒）
	void solve([[maybe_unused]] float dt) noexcept
	{
		if (!m_bodyB) return;

		const sgc::Vec3f worldA = getWorldAnchorA();
		const sgc::Vec3f worldB = getWorldAnchorB();

		const sgc::Vec3f diff = worldB - worldA;
		const float dist = diff.length();

		if (dist < 1e-8f) return;

		const float error = dist - m_restLength;
		const sgc::Vec3f correction = diff * (error / dist) * m_stiffness;

		const float invMassA = m_bodyA ? m_bodyA->inverseMass() : 0.0f;
		const float invMassB = m_bodyB->inverseMass();
		const float totalInvMass = invMassA + invMassB;

		if (totalInvMass < 1e-8f) return;

		if (m_bodyA && !m_bodyA->isStatic())
		{
			const sgc::Vec3f deltaA = correction * (invMassA / totalInvMass);
			m_bodyA->setPosition(m_bodyA->position() + deltaA);
		}

		if (!m_bodyB->isStatic())
		{
			const sgc::Vec3f deltaB = correction * (invMassB / totalInvMass);
			m_bodyB->setPosition(m_bodyB->position() - deltaB);
		}
	}

	/// @brief 速度レベルの拘束を解決する
	/// @param dt タイムステップ（秒）
	void solveVelocity([[maybe_unused]] float dt) noexcept
	{
		if (!m_bodyB) return;

		const sgc::Vec3f worldA = getWorldAnchorA();
		const sgc::Vec3f worldB = getWorldAnchorB();

		const sgc::Vec3f diff = worldB - worldA;
		const float dist = diff.length();

		if (dist < 1e-8f) return;

		const sgc::Vec3f normal = diff / dist;

		const sgc::Vec3f velA = m_bodyA ? m_bodyA->linearVelocity() : sgc::Vec3f{};
		const sgc::Vec3f velB = m_bodyB->linearVelocity();
		const float relVel = (velB - velA).dot(normal);

		const float invMassA = m_bodyA ? m_bodyA->inverseMass() : 0.0f;
		const float invMassB = m_bodyB->inverseMass();
		const float totalInvMass = invMassA + invMassB;

		if (totalInvMass < 1e-8f) return;

		const float impulse = -relVel * m_stiffness / totalInvMass;
		const sgc::Vec3f impulseVec = normal * impulse;

		if (m_bodyA && !m_bodyA->isStatic())
		{
			m_bodyA->applyImpulse(-impulseVec);
		}

		if (!m_bodyB->isStatic())
		{
			m_bodyB->applyImpulse(impulseVec);
		}
	}

	/// @brief 静止距離を取得する
	[[nodiscard]] constexpr float restLength() const noexcept { return m_restLength; }

	/// @brief 静止距離を設定する
	void setRestLength(float len) noexcept { m_restLength = len; }

	/// @brief 剛性を取得する
	[[nodiscard]] constexpr float stiffness() const noexcept { return m_stiffness; }

private:
	/// @brief ワールド空間のアンカーA位置を取得する
	[[nodiscard]] sgc::Vec3f getWorldAnchorA() const noexcept
	{
		if (!m_bodyA) return m_localAnchorA;
		return m_bodyA->position() + m_bodyA->rotation().rotate(m_localAnchorA);
	}

	/// @brief ワールド空間のアンカーB位置を取得する
	[[nodiscard]] sgc::Vec3f getWorldAnchorB() const noexcept
	{
		return m_bodyB->position() + m_bodyB->rotation().rotate(m_localAnchorB);
	}

	RigidBody3D* m_bodyA{nullptr};
	RigidBody3D* m_bodyB{nullptr};
	sgc::Vec3f m_localAnchorA{};
	sgc::Vec3f m_localAnchorB{};
	float m_restLength{1.0f};
	float m_stiffness{1.0f};
};

/// @brief ヒンジ拘束（1自由度回転）
///
/// 2つの剛体をピボット点で接続し、
/// 指定された軸周りの回転のみ許可する。
class HingeConstraint
{
public:
	/// @brief コンストラクタ
	/// @param bodyA ボディA
	/// @param bodyB ボディB
	/// @param worldPivot ワールド空間のピボット点
	/// @param worldAxis ワールド空間の回転軸（正規化推奨）
	HingeConstraint(
		RigidBody3D* bodyA,
		RigidBody3D* bodyB,
		const sgc::Vec3f& worldPivot,
		const sgc::Vec3f& worldAxis) noexcept
		: m_bodyA(bodyA)
		, m_bodyB(bodyB)
		, m_worldAxis(worldAxis.normalized())
	{
		// ピボットをローカル空間に変換
		if (m_bodyA)
		{
			m_localPivotA = m_bodyA->rotation().conjugate().rotate(worldPivot - m_bodyA->position());
			m_localAxisA = m_bodyA->rotation().conjugate().rotate(m_worldAxis);
		}
		else
		{
			m_localPivotA = worldPivot;
			m_localAxisA = m_worldAxis;
		}

		if (m_bodyB)
		{
			m_localPivotB = m_bodyB->rotation().conjugate().rotate(worldPivot - m_bodyB->position());
			m_localAxisB = m_bodyB->rotation().conjugate().rotate(m_worldAxis);
		}
	}

	/// @brief 角度制限を設定する
	/// @param minAngle 最小角度（ラジアン）
	/// @param maxAngle 最大角度（ラジアン）
	void setLimits(float minAngle, float maxAngle) noexcept
	{
		m_minAngle = minAngle;
		m_maxAngle = maxAngle;
		m_hasLimits = true;
	}

	/// @brief 角度制限の有無を返す
	[[nodiscard]] constexpr bool hasLimits() const noexcept { return m_hasLimits; }

	/// @brief 拘束を解決する（位置レベル）
	/// @param dt タイムステップ（秒）
	void solve([[maybe_unused]] float dt) noexcept
	{
		if (!m_bodyA || !m_bodyB) return;

		// ピボット点の位置拘束
		const sgc::Vec3f worldPivotA = m_bodyA->position()
			+ m_bodyA->rotation().rotate(m_localPivotA);
		const sgc::Vec3f worldPivotB = m_bodyB->position()
			+ m_bodyB->rotation().rotate(m_localPivotB);

		const sgc::Vec3f error = worldPivotB - worldPivotA;
		const float errorLen = error.length();

		if (errorLen > 1e-6f)
		{
			const float invMassA = m_bodyA->inverseMass();
			const float invMassB = m_bodyB->inverseMass();
			const float totalInvMass = invMassA + invMassB;

			if (totalInvMass > 1e-8f)
			{
				const sgc::Vec3f correction = error * 0.5f;

				if (!m_bodyA->isStatic())
				{
					m_bodyA->setPosition(
						m_bodyA->position() + correction * (invMassA / totalInvMass));
				}

				if (!m_bodyB->isStatic())
				{
					m_bodyB->setPosition(
						m_bodyB->position() - correction * (invMassB / totalInvMass));
				}
			}
		}

		// 軸整合拘束
		solveAxisAlignment();
	}

	/// @brief 回転軸を取得する
	[[nodiscard]] const sgc::Vec3f& axis() const noexcept { return m_worldAxis; }

private:
	/// @brief 軸の整合を拘束する
	void solveAxisAlignment() noexcept
	{
		if (!m_bodyA || !m_bodyB) return;

		const sgc::Vec3f axisA = m_bodyA->rotation().rotate(m_localAxisA);
		const sgc::Vec3f axisB = m_bodyB->rotation().rotate(m_localAxisB);

		// 2つの軸ベクトルのクロス積がゼロになるべき
		const sgc::Vec3f crossAB = axisA.cross(axisB);
		const float sinAngle = crossAB.length();

		if (sinAngle < 1e-6f) return;

		const sgc::Vec3f correctionAxis = crossAB / sinAngle;
		const float angle = std::asin(std::min(sinAngle, 1.0f));

		// 角速度ベースの補正
		const float correctionMag = angle * 0.3f;

		if (!m_bodyA->isStatic())
		{
			m_angularCorrection = correctionAxis * correctionMag * 0.5f;
			m_bodyA->setAngularVelocity(
				m_bodyA->angularVelocity() + m_angularCorrection);
		}

		if (!m_bodyB->isStatic())
		{
			m_bodyB->setAngularVelocity(
				m_bodyB->angularVelocity() - m_angularCorrection);
		}
	}

	RigidBody3D* m_bodyA{nullptr};
	RigidBody3D* m_bodyB{nullptr};

	sgc::Vec3f m_localPivotA{};
	sgc::Vec3f m_localPivotB{};
	sgc::Vec3f m_localAxisA{};
	sgc::Vec3f m_localAxisB{};
	sgc::Vec3f m_worldAxis{};

	float m_minAngle{0.0f};
	float m_maxAngle{0.0f};
	bool m_hasLimits{false};

	sgc::Vec3f m_angularCorrection{};
};

} // namespace mitiru::physics3d
