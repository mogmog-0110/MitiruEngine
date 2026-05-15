#pragma once

/// @file ContactSolver3D.hpp
/// @brief 3D逐次インパルス拘束ソルバー
///
/// ペネトレーション解消・摩擦適用・ウォームスタートによる
/// 安定した接触解決を提供する。
///
/// @code
/// mitiru::physics3d::ContactSolver3D solver;
/// solver.configure({.iterations = 8, .baumgarte = 0.2f});
/// solver.prepare(manifolds, bodies);
/// solver.warmStart(bodies);
/// solver.solve(bodies);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

/// @brief ソルバー設定
struct ContactSolverConfig3D
{
	int iterations{8};             ///< ソルバー反復回数
	float baumgarte{0.2f};         ///< Baumgarte安定化係数（0〜1）
	float slop{0.005f};            ///< 許容貫通量（m）
	float restitutionThreshold{1.0f}; ///< 反発適用の最小相対速度
	float warmStartFactor{0.8f};   ///< ウォームスタート係数（0〜1）
};

/// @brief 接触マニフォールド
///
/// 2つのボディ間の接触情報を表す。
/// 接触点・法線・貫通深度に加え、ソルバー用の累積インパルスを保持する。
struct ContactManifold3D
{
	BodyId bodyIdA{INVALID_BODY_ID};  ///< ボディA
	BodyId bodyIdB{INVALID_BODY_ID};  ///< ボディB

	sgc::Vec3f point{};               ///< 接触点（ワールド空間）
	sgc::Vec3f normal{};              ///< 接触法線（AからBへの方向）
	float depth{0.0f};                ///< 貫通深度

	sgc::Vec3f rA{};                  ///< ボディA重心から接触点へのベクトル
	sgc::Vec3f rB{};                  ///< ボディB重心から接触点へのベクトル

	sgc::Vec3f tangent1{};            ///< 摩擦接線方向1
	sgc::Vec3f tangent2{};            ///< 摩擦接線方向2

	float normalMass{0.0f};           ///< 法線方向の有効質量
	float tangentMass1{0.0f};         ///< 接線方向1の有効質量
	float tangentMass2{0.0f};         ///< 接線方向2の有効質量

	float bias{0.0f};                 ///< Baumgarte バイアス

	// ── ウォームスタート用累積インパルス ──
	float accumulatedNormalImpulse{0.0f};    ///< 法線方向の累積インパルス
	float accumulatedTangentImpulse1{0.0f};  ///< 接線方向1の累積インパルス
	float accumulatedTangentImpulse2{0.0f};  ///< 接線方向2の累積インパルス

	float combinedFriction{0.0f};     ///< 合成摩擦係数
	float combinedRestitution{0.0f};  ///< 合成反発係数
};

/// @brief 逐次インパルス拘束ソルバー
///
/// Sequential Impulse (SI) 法で接触拘束を解決する。
///
/// @details 処理フロー:
///   1. prepare(): マニフォールドの前処理（有効質量・バイアス計算）
///   2. warmStart(): 前フレームの累積インパルスを適用（収束加速）
///   3. solve(): 反復的にインパルスを計算・適用
///
/// 各反復で以下を実行:
///   - 法線方向のインパルス（ペネトレーション解消 + 反発）
///   - 接線方向のインパルス（摩擦、クーロン摩擦モデル）
class ContactSolver3D
{
public:
	/// @brief デフォルトコンストラクタ
	ContactSolver3D() = default;

	/// @brief 設定を指定して構築する
	/// @param config ソルバー設定
	explicit ContactSolver3D(const ContactSolverConfig3D& config) noexcept
		: m_config(config)
	{
	}

	/// @brief ソルバー設定を変更する
	/// @param config ソルバー設定
	void configure(const ContactSolverConfig3D& config) noexcept
	{
		m_config = config;
	}

	/// @brief 現在のソルバー設定を取得する
	[[nodiscard]] constexpr const ContactSolverConfig3D& config() const noexcept
	{
		return m_config;
	}

	// ── ソルバー実行 ──────────────────────────────────────────

	/// @brief マニフォールドの前処理を行う
	/// @param manifolds 接触マニフォールド配列
	/// @param bodies ボディIDからボディへのマップ
	///
	/// @details 各マニフォールドについて以下を計算:
	///   - rA, rB（重心から接触点へのベクトル）
	///   - 接線ベクトル（法線に垂直な2方向）
	///   - 有効質量（法線・接線方向）
	///   - Baumgarteバイアス
	///   - 合成摩擦・反発係数
	void prepare(
		std::vector<ContactManifold3D>& manifolds,
		const std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies) noexcept
	{
		for (auto& m : manifolds)
		{
			const auto* bodyA = findBody(bodies, m.bodyIdA);
			const auto* bodyB = findBody(bodies, m.bodyIdB);
			if (!bodyA || !bodyB) continue;

			// 重心から接触点へのベクトル
			m.rA = m.point - bodyA->position();
			m.rB = m.point - bodyB->position();

			// 接線ベクトルを計算（Gram-Schmidt）
			computeTangents(m.normal, m.tangent1, m.tangent2);

			// 有効質量を計算
			m.normalMass = computeEffectiveMass(*bodyA, *bodyB, m.rA, m.rB, m.normal);
			m.tangentMass1 = computeEffectiveMass(*bodyA, *bodyB, m.rA, m.rB, m.tangent1);
			m.tangentMass2 = computeEffectiveMass(*bodyA, *bodyB, m.rA, m.rB, m.tangent2);

			// Baumgarte安定化バイアス
			m.bias = 0.0f;
			if (m.depth > m_config.slop)
			{
				m.bias = -m_config.baumgarte * (m.depth - m_config.slop);
			}

			// 合成材料特性
			m.combinedFriction = std::sqrt(bodyA->friction() * bodyB->friction());
			m.combinedRestitution = std::min(bodyA->restitution(), bodyB->restitution());

			// 反発バイアス
			const sgc::Vec3f relVel = computeRelativeVelocity(
				*bodyA, *bodyB, m.rA, m.rB);
			const float velAlongNormal = relVel.dot(m.normal);

			if (velAlongNormal < -m_config.restitutionThreshold)
			{
				m.bias += -m.combinedRestitution * velAlongNormal;
			}
		}
	}

	/// @brief ウォームスタート：前フレームの累積インパルスを適用する
	/// @param manifolds 接触マニフォールド配列
	/// @param bodies ボディマップ
	///
	/// @details 前フレームで収束したインパルス値を初期値として適用し、
	///          今フレームの収束を大幅に高速化する。
	void warmStart(
		const std::vector<ContactManifold3D>& manifolds,
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies) noexcept
	{
		if (m_config.warmStartFactor <= 0.0f) return;

		for (const auto& m : manifolds)
		{
			auto* bodyA = findBodyMut(bodies, m.bodyIdA);
			auto* bodyB = findBodyMut(bodies, m.bodyIdB);
			if (!bodyA || !bodyB) continue;

			const sgc::Vec3f warmImpulse =
				m.normal * (m.accumulatedNormalImpulse * m_config.warmStartFactor) +
				m.tangent1 * (m.accumulatedTangentImpulse1 * m_config.warmStartFactor) +
				m.tangent2 * (m.accumulatedTangentImpulse2 * m_config.warmStartFactor);

			if (!bodyA->isStatic())
			{
				bodyA->applyImpulseAtPoint(-warmImpulse, m.point);
			}
			if (!bodyB->isStatic())
			{
				bodyB->applyImpulseAtPoint(warmImpulse, m.point);
			}
		}
	}

	/// @brief 逐次インパルス法で接触を解決する
	/// @param manifolds 接触マニフォールド配列
	/// @param bodies ボディマップ
	///
	/// @details 設定された反復回数だけ以下を繰り返す:
	///   1. 法線方向インパルス（分離力）
	///   2. 接線方向インパルス（摩擦力、クーロン摩擦クランプ）
	void solve(
		std::vector<ContactManifold3D>& manifolds,
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies) noexcept
	{
		for (int iter = 0; iter < m_config.iterations; ++iter)
		{
			for (auto& m : manifolds)
			{
				auto* bodyA = findBodyMut(bodies, m.bodyIdA);
				auto* bodyB = findBodyMut(bodies, m.bodyIdB);
				if (!bodyA || !bodyB) continue;

				solveNormalConstraint(m, *bodyA, *bodyB);
				solveFrictionConstraint(m, *bodyA, *bodyB);
			}
		}
	}

	/// @brief 位置補正（ペネトレーション解消）を直接適用する
	/// @param manifolds 接触マニフォールド配列
	/// @param bodies ボディマップ
	///
	/// @details Baumgarte安定化が不十分な場合のフォールバック。
	///          直接位置を調整して貫通を解消する。
	void solvePositions(
		const std::vector<ContactManifold3D>& manifolds,
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies) noexcept
	{
		for (const auto& m : manifolds)
		{
			auto* bodyA = findBodyMut(bodies, m.bodyIdA);
			auto* bodyB = findBodyMut(bodies, m.bodyIdB);
			if (!bodyA || !bodyB) continue;

			if (m.depth <= m_config.slop) continue;

			const float invMassA = bodyA->inverseMass();
			const float invMassB = bodyB->inverseMass();
			const float totalInvMass = invMassA + invMassB;

			if (totalInvMass < 1e-8f) continue;

			const float correctionMag = (m.depth - m_config.slop) *
				m_config.baumgarte / totalInvMass;
			const sgc::Vec3f correction = m.normal * correctionMag;

			if (!bodyA->isStatic())
			{
				bodyA->setPosition(bodyA->position() - correction * invMassA);
			}
			if (!bodyB->isStatic())
			{
				bodyB->setPosition(bodyB->position() + correction * invMassB);
			}
		}
	}

	/// @brief マニフォールド数を返す（統計用）
	/// @param manifolds マニフォールド配列
	/// @return マニフォールド数
	[[nodiscard]] static std::size_t manifoldCount(
		const std::vector<ContactManifold3D>& manifolds) noexcept
	{
		return manifolds.size();
	}

private:
	// ── 法線拘束の解決 ────────────────────────────────────────

	/// @brief 法線方向の拘束を解決する
	static void solveNormalConstraint(
		ContactManifold3D& m, RigidBody3D& bodyA, RigidBody3D& bodyB) noexcept
	{
		if (m.normalMass <= 0.0f) return;

		const sgc::Vec3f relVel = computeRelativeVelocity(bodyA, bodyB, m.rA, m.rB);
		const float velAlongNormal = relVel.dot(m.normal);

		// インパルス量
		float lambda = -(velAlongNormal + m.bias) / m.normalMass;

		// 累積インパルスのクランプ（分離方向のみ）
		const float oldAccumulated = m.accumulatedNormalImpulse;
		m.accumulatedNormalImpulse = std::max(oldAccumulated + lambda, 0.0f);
		lambda = m.accumulatedNormalImpulse - oldAccumulated;

		const sgc::Vec3f impulse = m.normal * lambda;

		if (!bodyA.isStatic())
		{
			bodyA.applyImpulseAtPoint(-impulse, bodyA.position() + m.rA);
		}
		if (!bodyB.isStatic())
		{
			bodyB.applyImpulseAtPoint(impulse, bodyB.position() + m.rB);
		}
	}

	// ── 摩擦拘束の解決 ────────────────────────────────────────

	/// @brief 摩擦方向の拘束を解決する（クーロン摩擦モデル）
	static void solveFrictionConstraint(
		ContactManifold3D& m, RigidBody3D& bodyA, RigidBody3D& bodyB) noexcept
	{
		const float frictionLimit = m.combinedFriction * m.accumulatedNormalImpulse;

		// 接線方向1
		if (m.tangentMass1 > 0.0f)
		{
			const sgc::Vec3f relVel = computeRelativeVelocity(bodyA, bodyB, m.rA, m.rB);
			const float velAlongTangent1 = relVel.dot(m.tangent1);

			float lambda1 = -velAlongTangent1 / m.tangentMass1;

			const float oldAccumulated1 = m.accumulatedTangentImpulse1;
			m.accumulatedTangentImpulse1 = std::clamp(
				oldAccumulated1 + lambda1, -frictionLimit, frictionLimit);
			lambda1 = m.accumulatedTangentImpulse1 - oldAccumulated1;

			const sgc::Vec3f impulse1 = m.tangent1 * lambda1;

			if (!bodyA.isStatic())
			{
				bodyA.applyImpulseAtPoint(-impulse1, bodyA.position() + m.rA);
			}
			if (!bodyB.isStatic())
			{
				bodyB.applyImpulseAtPoint(impulse1, bodyB.position() + m.rB);
			}
		}

		// 接線方向2
		if (m.tangentMass2 > 0.0f)
		{
			const sgc::Vec3f relVel = computeRelativeVelocity(bodyA, bodyB, m.rA, m.rB);
			const float velAlongTangent2 = relVel.dot(m.tangent2);

			float lambda2 = -velAlongTangent2 / m.tangentMass2;

			const float oldAccumulated2 = m.accumulatedTangentImpulse2;
			m.accumulatedTangentImpulse2 = std::clamp(
				oldAccumulated2 + lambda2, -frictionLimit, frictionLimit);
			lambda2 = m.accumulatedTangentImpulse2 - oldAccumulated2;

			const sgc::Vec3f impulse2 = m.tangent2 * lambda2;

			if (!bodyA.isStatic())
			{
				bodyA.applyImpulseAtPoint(-impulse2, bodyA.position() + m.rA);
			}
			if (!bodyB.isStatic())
			{
				bodyB.applyImpulseAtPoint(impulse2, bodyB.position() + m.rB);
			}
		}
	}

	// ── ユーティリティ ────────────────────────────────────────

	/// @brief 接触点での相対速度を計算する
	/// @param bodyA ボディA
	/// @param bodyB ボディB
	/// @param rA 重心Aから接触点へのベクトル
	/// @param rB 重心Bから接触点へのベクトル
	/// @return 相対速度（B - A）
	[[nodiscard]] static sgc::Vec3f computeRelativeVelocity(
		const RigidBody3D& bodyA, const RigidBody3D& bodyB,
		const sgc::Vec3f& rA, const sgc::Vec3f& rB) noexcept
	{
		const sgc::Vec3f velA = bodyA.linearVelocity() + bodyA.angularVelocity().cross(rA);
		const sgc::Vec3f velB = bodyB.linearVelocity() + bodyB.angularVelocity().cross(rB);
		return velB - velA;
	}

	/// @brief 有効質量を計算する
	/// @param bodyA ボディA
	/// @param bodyB ボディB
	/// @param rA 重心Aから接触点へのベクトル
	/// @param rB 重心Bから接触点へのベクトル
	/// @param direction 拘束方向
	/// @return 有効質量の逆数の逆数（有効質量）
	[[nodiscard]] static float computeEffectiveMass(
		const RigidBody3D& bodyA, const RigidBody3D& bodyB,
		const sgc::Vec3f& rA, const sgc::Vec3f& rB,
		const sgc::Vec3f& direction) noexcept
	{
		const float invMassA = bodyA.inverseMass();
		const float invMassB = bodyB.inverseMass();

		const sgc::Vec3f rAxN = rA.cross(direction);
		const sgc::Vec3f rBxN = rB.cross(direction);

		const float angularA = (bodyA.inverseInertiaTensor() * rAxN).dot(rAxN);
		const float angularB = (bodyB.inverseInertiaTensor() * rBxN).dot(rBxN);

		const float effectiveInvMass = invMassA + invMassB + angularA + angularB;

		if (effectiveInvMass < 1e-8f) return 0.0f;
		return 1.0f / effectiveInvMass;
	}

	/// @brief 法線から接線ベクトル2本を計算する（Gram-Schmidt）
	/// @param normal 法線ベクトル
	/// @param tangent1 接線ベクトル1（出力）
	/// @param tangent2 接線ベクトル2（出力）
	static void computeTangents(
		const sgc::Vec3f& normal,
		sgc::Vec3f& tangent1,
		sgc::Vec3f& tangent2) noexcept
	{
		// Frisvad の方法で法線に垂直な2ベクトルを生成
		if (normal.y < -0.9999f)
		{
			tangent1 = sgc::Vec3f{0.0f, 0.0f, -1.0f};
			tangent2 = sgc::Vec3f{-1.0f, 0.0f, 0.0f};
			return;
		}

		const float a = 1.0f / (1.0f + normal.y);
		const float b = -normal.x * normal.z * a;

		tangent1 = sgc::Vec3f{1.0f - normal.x * normal.x * a, -normal.x, b};
		tangent2 = sgc::Vec3f{b, -normal.z, 1.0f - normal.z * normal.z * a};
	}

	/// @brief ボディマップからボディを検索する（const版）
	[[nodiscard]] static const RigidBody3D* findBody(
		const std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies,
		BodyId id) noexcept
	{
		const auto it = bodies.find(id);
		return (it != bodies.end()) ? it->second.get() : nullptr;
	}

	/// @brief ボディマップからボディを検索する（非const版）
	[[nodiscard]] static RigidBody3D* findBodyMut(
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies,
		BodyId id) noexcept
	{
		auto it = bodies.find(id);
		return (it != bodies.end()) ? it->second.get() : nullptr;
	}

	ContactSolverConfig3D m_config;  ///< ソルバー設定
};

} // namespace mitiru::physics3d
