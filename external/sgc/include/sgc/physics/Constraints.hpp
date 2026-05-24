#pragma once

/// @file Constraints.hpp
/// @brief 物理拘束（距離・ヒンジ・スプリング）
///
/// 剛体間の拘束を定義し、反復ソルバーで解決する。
/// IConstraint インターフェースを実装した各種拘束を提供する。
///
/// @code
/// using namespace sgc::physics;
///
/// RigidBody3D a, b;
/// a.position = {0, 0, 0}; a.mass = 1.0f;
/// b.position = {3, 0, 0}; b.mass = 1.0f;
///
/// // 距離拘束（2.0の距離を維持）
/// DistanceConstraint dc{&a, &b, 2.0f};
///
/// // ソルバーで解決
/// ConstraintSolver solver;
/// solver.addConstraint(&dc);
/// solver.solve(1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/physics/RigidBody3D.hpp"

namespace sgc::physics
{

/// @brief 拘束インターフェース
///
/// solve()で剛体の速度・位置を拘束条件に合わせて補正する。
class IConstraint
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IConstraint() = default;

	/// @brief 拘束を解決する
	/// @param dt タイムステップ幅（秒）
	virtual void solve(float dt) = 0;

	/// @brief 拘束が有効か
	/// @return 有効ならtrue
	[[nodiscard]] virtual bool isActive() const noexcept { return true; }
};

/// @brief 距離拘束（2つの剛体間の距離を一定に保つ）
///
/// 2つの剛体間の距離がtargetDistanceになるように速度を補正する。
/// コンプライアンス値で拘束の「柔らかさ」を制御できる。
class DistanceConstraint final : public IConstraint
{
public:
	/// @brief コンストラクタ
	/// @param bodyA 剛体Aへのポインタ
	/// @param bodyB 剛体Bへのポインタ
	/// @param targetDist 目標距離
	/// @param compliance コンプライアンス（0=完全剛体、大きいほど柔らかい）
	DistanceConstraint(
		RigidBody3D* bodyA,
		RigidBody3D* bodyB,
		float targetDist,
		float compliance = 0.0f) noexcept
		: m_bodyA{bodyA}
		, m_bodyB{bodyB}
		, m_targetDistance{targetDist}
		, m_compliance{compliance}
	{
	}

	/// @brief 拘束を解決する
	/// @param dt タイムステップ幅
	void solve(float dt) override
	{
		if (!m_bodyA || !m_bodyB || dt <= 0.0f) return;

		const Vec3f diff = m_bodyB->position - m_bodyA->position;
		const float dist = diff.length();
		if (dist < 1e-8f) return;

		const Vec3f dir = diff / dist;
		const float error = dist - m_targetDistance;

		const float invMassA = m_bodyA->inverseMass();
		const float invMassB = m_bodyB->inverseMass();
		const float invMassSum = invMassA + invMassB;
		if (invMassSum <= 0.0f) return;

		// XPBD式のコンプライアンス考慮
		const float alpha = m_compliance / (dt * dt);
		const float lambda = -error / (invMassSum + alpha);
		const Vec3f correction = dir * lambda;

		m_bodyA->position = m_bodyA->position - correction * invMassA;
		m_bodyB->position = m_bodyB->position + correction * invMassB;
	}

	/// @brief 目標距離を取得する
	[[nodiscard]] float targetDistance() const noexcept { return m_targetDistance; }

	/// @brief 目標距離を設定する
	void setTargetDistance(float dist) noexcept { m_targetDistance = dist; }

private:
	RigidBody3D* m_bodyA;
	RigidBody3D* m_bodyB;
	float m_targetDistance;
	float m_compliance;
};

/// @brief ヒンジ拘束（1つの軸まわりの回転のみ許可する）
///
/// 2つの剛体を指定した軸まわりにのみ回転可能に接続する。
/// 位置拘束（アンカー点を一致させる）と角度制限を行う。
class HingeConstraint final : public IConstraint
{
public:
	/// @brief コンストラクタ
	/// @param bodyA 剛体A
	/// @param bodyB 剛体B
	/// @param anchor アンカー点（ワールド座標）
	/// @param axis ヒンジ軸（正規化済み）
	HingeConstraint(
		RigidBody3D* bodyA,
		RigidBody3D* bodyB,
		const Vec3f& anchor,
		const Vec3f& axis) noexcept
		: m_bodyA{bodyA}
		, m_bodyB{bodyB}
		, m_anchor{anchor}
		, m_axis{axis.normalized()}
	{
	}

	/// @brief 拘束を解決する
	/// @param dt タイムステップ幅
	void solve(float dt) override
	{
		if (!m_bodyA || !m_bodyB || dt <= 0.0f) return;

		const float invMassA = m_bodyA->inverseMass();
		const float invMassB = m_bodyB->inverseMass();
		const float invMassSum = invMassA + invMassB;
		if (invMassSum <= 0.0f) return;

		// 位置拘束: 両剛体をアンカー点に引き寄せる
		const Vec3f toAnchorA = m_anchor - m_bodyA->position;
		const Vec3f toAnchorB = m_anchor - m_bodyB->position;
		const Vec3f posError = toAnchorA - toAnchorB;

		const Vec3f correction = posError / invMassSum;
		m_bodyA->position = m_bodyA->position + correction * invMassA * m_stiffness;
		m_bodyB->position = m_bodyB->position - correction * invMassB * m_stiffness;

		// 速度拘束: ヒンジ軸以外の角速度成分を除去する
		auto constrainAngular = [this](RigidBody3D& body)
		{
			const Vec3f axisComponent = m_axis * body.angularVelocity.dot(m_axis);
			body.angularVelocity = axisComponent;
		};

		if (!m_bodyA->isStatic) constrainAngular(*m_bodyA);
		if (!m_bodyB->isStatic) constrainAngular(*m_bodyB);
	}

	/// @brief ヒンジ軸を取得する
	[[nodiscard]] const Vec3f& axis() const noexcept { return m_axis; }

	/// @brief 剛性を設定する（0〜1、デフォルト0.5）
	void setStiffness(float s) noexcept { m_stiffness = std::clamp(s, 0.0f, 1.0f); }

	/// @brief 剛性を取得する
	[[nodiscard]] float stiffness() const noexcept { return m_stiffness; }

private:
	RigidBody3D* m_bodyA;
	RigidBody3D* m_bodyB;
	Vec3f m_anchor;
	Vec3f m_axis;
	float m_stiffness = 0.5f;
};

/// @brief スプリング拘束（フックの法則 + ダンピング）
///
/// 2つの剛体間にバネとダンパーを接続する。
/// 自然長からの変位に比例した力と、相対速度に比例した減衰力を適用する。
class SpringConstraint final : public IConstraint
{
public:
	/// @brief コンストラクタ
	/// @param bodyA 剛体A
	/// @param bodyB 剛体B
	/// @param restLength バネの自然長
	/// @param stiffness バネ定数 k
	/// @param damping ダンピング係数 c
	SpringConstraint(
		RigidBody3D* bodyA,
		RigidBody3D* bodyB,
		float restLength,
		float stiffness = 10.0f,
		float damping = 0.5f) noexcept
		: m_bodyA{bodyA}
		, m_bodyB{bodyB}
		, m_restLength{restLength}
		, m_stiffness{stiffness}
		, m_damping{damping}
	{
	}

	/// @brief 拘束を解決する（力ベースでインパルスを適用）
	/// @param dt タイムステップ幅
	void solve(float dt) override
	{
		if (!m_bodyA || !m_bodyB || dt <= 0.0f) return;

		const Vec3f diff = m_bodyB->position - m_bodyA->position;
		const float dist = diff.length();
		if (dist < 1e-8f) return;

		const Vec3f dir = diff / dist;

		// フックの法則: F = -k * (x - x0)
		const float displacement = dist - m_restLength;
		const float springForce = m_stiffness * displacement;

		// ダンピング: F_d = -c * v_rel
		const Vec3f relVelocity = m_bodyB->velocity - m_bodyA->velocity;
		const float dampingForce = m_damping * relVelocity.dot(dir);

		// 合計力
		const float totalForce = springForce + dampingForce;
		const Vec3f forceVec = dir * totalForce;

		// インパルスとして適用（F * dt）
		m_bodyA->applyImpulse(forceVec * dt);
		m_bodyB->applyImpulse(forceVec * (-dt));
	}

	/// @brief 自然長を取得する
	[[nodiscard]] float restLength() const noexcept { return m_restLength; }

	/// @brief 自然長を設定する
	void setRestLength(float len) noexcept { m_restLength = len; }

	/// @brief バネ定数を取得する
	[[nodiscard]] float stiffness() const noexcept { return m_stiffness; }

	/// @brief バネ定数を設定する
	void setStiffness(float k) noexcept { m_stiffness = k; }

	/// @brief ダンピング係数を取得する
	[[nodiscard]] float damping() const noexcept { return m_damping; }

	/// @brief ダンピング係数を設定する
	void setDamping(float c) noexcept { m_damping = c; }

private:
	RigidBody3D* m_bodyA;
	RigidBody3D* m_bodyB;
	float m_restLength;
	float m_stiffness;
	float m_damping;
};

/// @brief 拘束ソルバー
///
/// 登録された拘束を反復的に解決する。
/// 反復回数を増やすほど精度が上がるが、計算コストも増える。
class ConstraintSolver
{
public:
	/// @brief 拘束を追加する（所有権は呼び出し元が保持）
	/// @param constraint 追加する拘束へのポインタ
	void addConstraint(IConstraint* constraint)
	{
		if (constraint)
		{
			m_constraints.push_back(constraint);
		}
	}

	/// @brief 拘束を除去する
	/// @param constraint 除去する拘束へのポインタ
	void removeConstraint(IConstraint* constraint)
	{
		m_constraints.erase(
			std::remove(m_constraints.begin(), m_constraints.end(), constraint),
			m_constraints.end());
	}

	/// @brief 全拘束を除去する
	void clear() { m_constraints.clear(); }

	/// @brief 全拘束を反復的に解決する
	/// @param dt タイムステップ幅
	void solve(float dt)
	{
		for (int iter = 0; iter < m_iterations; ++iter)
		{
			for (auto* c : m_constraints)
			{
				if (c && c->isActive())
				{
					c->solve(dt);
				}
			}
		}
	}

	/// @brief 反復回数を設定する
	/// @param iterations 反復回数（1以上）
	void setIterations(int iterations) noexcept
	{
		m_iterations = std::max(1, iterations);
	}

	/// @brief 反復回数を取得する
	[[nodiscard]] int iterations() const noexcept { return m_iterations; }

	/// @brief 登録済み拘束数を取得する
	[[nodiscard]] std::size_t constraintCount() const noexcept
	{
		return m_constraints.size();
	}

private:
	std::vector<IConstraint*> m_constraints;  ///< 拘束リスト（非所有）
	int m_iterations = 4;                      ///< 反復回数
};

} // namespace sgc::physics
