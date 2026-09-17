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
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/TracyZones.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

/// @brief ソルバー設定
struct ContactSolverConfig3D
{
	int iterations{8};             ///< ソルバー反復回数
	float baumgarte{0.7f};         ///< Baumgarte安定化係数（0〜1）。低いと多段スタックの残留貫通が大きい
	float slop{0.005f};            ///< 許容貫通量（m）
	float restitutionThreshold{1.0f}; ///< 反発適用の最小相対速度
	float warmStartFactor{1.0f};   ///< ウォームスタート係数（0〜1）。前フレームの収束値をそのまま初期値にする

	// スリープは既定オフ（重力停止 + 積分スキップを伴うため、opt-in の game 側判断に委ねる）
	bool enableSleeping{false};        ///< スリープ機構を有効にするか
	float sleepLinearThreshold{0.05f}; ///< スリープ判定の並進速度しきい値（m/s）
	float sleepAngularThreshold{0.05f};///< スリープ判定の角速度しきい値（rad/s）
	float sleepTimeToSleep{0.5f};      ///< しきい値を下回り続けてスリープに入るまでの秒数
};

/// @brief 接触マニフォールド
///
/// 2つのボディ間の接触情報を表す。
/// 接触点・法線・貫通深度に加え、ソルバー用の累積インパルスを保持する。
struct ContactManifold3D
{
	BodyId bodyIdA{INVALID_BODY_ID};  ///< ボディA
	BodyId bodyIdB{INVALID_BODY_ID};  ///< ボディB

	RigidBody3D* cachedBodyA{nullptr};  ///< bodyIdA の解決済みポインタ（prepare() が設定、非所有）
	RigidBody3D* cachedBodyB{nullptr};  ///< bodyIdB の解決済みポインタ（prepare() が設定、非所有）

	sgc::Vec3f point{};               ///< 接触点（ワールド空間）
	sgc::Vec3f normal{};              ///< 接触法線（AからBへの方向）
	float depth{0.0f};                ///< 貫通深度

	std::uint32_t featureId{0};       ///< 同一ペア内で複数接触点を区別する安定ID（既定0は単一点ペア）。
	                                   ///< ウォームスタートのキャッシュキーに使い、点ごとに前フレームの
	                                   ///< 累積インパルスを引き継ぐ

	std::uint32_t pairPointCount{1};  ///< 同じ(bodyIdA,bodyIdB)ペアに属するマニフォールド点の総数。
	                                   ///< solvePositions() の直接位置補正は点ごとに独立して深度分を
	                                   ///< 動かすため、複数点マニフォールドではこの数で割らないと
	                                   ///< 同じ深度が点数倍の距離だけ押し出されて積み上げが崩壊する

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

/// @brief 前フレームの累積インパルス（ウォームスタート用キャッシュの値部分）
struct CachedContactImpulse3D
{
	// lo/hi/featureId は impulseCacheKey() が 28+28+8bit にマスクした際の元の実値。
	// マスク衝突で別ペアの値を誤って引き継がないよう、lookup 時にここと照合する
	BodyId lo{INVALID_BODY_ID};
	BodyId hi{INVALID_BODY_ID};
	std::uint32_t featureId{0};

	float normal{0.0f};
	float tangent1{0.0f};
	float tangent2{0.0f};
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
			// unordered_map のハッシュ引きは prepare() のここ1回だけ行い、以降の
			// warmStart/solve/solvePositions は m.cachedBodyA/B のポインタを直接使う
			// （反復 × マニフォールド数ぶんの findBody 呼び出しを排除）
			auto* bodyA = resolveBody(bodies, m.bodyIdA);
			auto* bodyB = resolveBody(bodies, m.bodyIdB);
			m.cachedBodyA = bodyA;
			m.cachedBodyB = bodyB;
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

			// 前フレームの累積インパルスを初期値として引き継ぐ（ウォームスタート）。
			// マニフォールドは毎フレーム新規生成されるため、ここで注入しないと
			// 蓄積インパルスが常に0から再スタートし、多段スタックが収束しない
			if (const CachedContactImpulse3D* cached = findCachedImpulse(m.bodyIdA, m.bodyIdB, m.featureId))
			{
				m.accumulatedNormalImpulse = cached->normal * m_config.warmStartFactor;
				m.accumulatedTangentImpulse1 = cached->tangent1 * m_config.warmStartFactor;
				m.accumulatedTangentImpulse2 = cached->tangent2 * m_config.warmStartFactor;
			}

			// 反発バイアス
			const sgc::Vec3f relVel = computeRelativeVelocity(
				*bodyA, *bodyB, m.rA, m.rB);
			const float velAlongNormal = relVel.dot(m.normal);

			// lambda = -(velAlongNormal + bias) より、解いた後の velAlongNormal は -bias に収束する。
			// 反発で欲しい分離速度 "+e*|v0|" を作るには bias に "e*v0"（v0 は負）を足す
			if (velAlongNormal < -m_config.restitutionThreshold)
			{
				m.bias += m.combinedRestitution * velAlongNormal;
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
			auto* bodyA = m.cachedBodyA;
			auto* bodyB = m.cachedBodyB;
			if (!bodyA || !bodyB) continue;

			// warmStartFactor は prepare() で m.accumulatedXxxImpulse に既に織り込み済み
			// （ここで二重に掛けると次の solve() のクランプ基準とずれる）
			const sgc::Vec3f warmImpulse =
				m.normal * m.accumulatedNormalImpulse +
				m.tangent1 * m.accumulatedTangentImpulse1 +
				m.tangent2 * m.accumulatedTangentImpulse2;

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
		MITIRU_ZONE_PHYSICS("ContactSolver3D::solve");

		for (int iter = 0; iter < m_config.iterations; ++iter)
		{
			for (auto& m : manifolds)
			{
				auto* bodyA = m.cachedBodyA;
				auto* bodyB = m.cachedBodyB;
				if (!bodyA || !bodyB) continue;

				solveNormalConstraint(m, *bodyA, *bodyB);
				solveFrictionConstraint(m, *bodyA, *bodyB);
			}
		}

		commitImpulseCache(manifolds);
	}

	/// @brief 静止している動的ボディをスリープさせる（config.enableSleeping 時のみ有効）
	/// @param bodies ボディマップ
	/// @param dt タイムステップ（スリープ経過時間の計測用）
	///
	/// @details 接触に伴う重力・インパルスは PhysicsWorld3D::applyGravity / solve が
	///          isAsleep() を見て素通りする前提。ここではしきい値判定と入眠のみ行い、
	///          起床は「起きているボディと接触した」タイミングで wakeTouching() が行う
	void updateSleepStates(
		std::vector<ContactManifold3D>& manifolds,
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies,
		float dt) noexcept
	{
		if (!m_config.enableSleeping) return;

		wakeTouching(manifolds, bodies);

		const float linSq = m_config.sleepLinearThreshold * m_config.sleepLinearThreshold;
		const float angSq = m_config.sleepAngularThreshold * m_config.sleepAngularThreshold;

		for (auto& [id, body] : bodies)
		{
			if (body->isStatic()) continue;
			body->updateSleepTimer(dt, linSq, angSq, m_config.sleepTimeToSleep);
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
			auto* bodyA = m.cachedBodyA;
			auto* bodyB = m.cachedBodyB;
			if (!bodyA || !bodyB) continue;

			if (m.depth <= m_config.slop) continue;

			const float invMassA = bodyA->inverseMass();
			const float invMassB = bodyB->inverseMass();
			const float totalInvMass = invMassA + invMassB;

			if (totalInvMass < 1e-8f) continue;

			const float pointCount = static_cast<float>(std::max<std::uint32_t>(m.pairPointCount, 1));
			const float correctionMag = (m.depth - m_config.slop) *
				m_config.baumgarte / totalInvMass / pointCount;
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

		// インパルス量。normalMass は computeEffectiveMass() が返す「有効質量」そのものなので
		// 逆数化されたインパルス空間の量ではなく実質量として掛ける（割ると質量比の分だけ暴走する）
		float lambda = -(velAlongNormal + m.bias) * m.normalMass;

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

			float lambda1 = -velAlongTangent1 * m.tangentMass1;

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

			float lambda2 = -velAlongTangent2 * m.tangentMass2;

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

	/// @brief ボディマップからボディを検索する。unique_ptr::get() は const 呼び出しでも
	///        非const ポインタを返すため、bodies が const 参照でも可変ポインタを得られる
	///        （prepare() でキャッシュし、以降 solve 系はこのポインタを直接使う）
	[[nodiscard]] static RigidBody3D* resolveBody(
		const std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies,
		BodyId id) noexcept
	{
		const auto it = bodies.find(id);
		return (it != bodies.end()) ? it->second.get() : nullptr;
	}

	// ── ウォームスタート用インパルスキャッシュ ────────────────────

	/// @brief キャッシュキーを作る（ペアの順序に依存しない）
	/// @details A/B の割り当ては broadphase 候補ペアの並びで決まり毎フレーム入れ替わり得るが、
	///          法線は常に A→B で定義されるため、キー正規化のために順序を入れ替えても
	///          スカラー値（法線・接線インパルスの大きさ）はそのまま再利用してよい。
	///          featureId（下位8bit）で同一ペア内の複数接触点を区別し、点ごとに
	///          ウォームスタートを引き継ぐ（28+28+8bit、BodyId は index 由来で十分小さい前提）。
	///          1ワールド累積で BodyId が 2^28 を超えて発行されると異なるペアがこのキーへ
	///          衝突しうるが、findCachedImpulse() が実値 (lo/hi/featureId) を再照合して
	///          弾くため、衝突時は誤った値の再利用ではなく単なるキャッシュミス（ウォーム
	///          スタート無し）に縮退する
	[[nodiscard]] static std::uint64_t impulseCacheKey(BodyId a, BodyId b, std::uint32_t featureId) noexcept
	{
		const BodyId lo = std::min(a, b);
		const BodyId hi = std::max(a, b);
		return (static_cast<std::uint64_t>(lo & 0x0FFFFFFFu) << 36) |
			(static_cast<std::uint64_t>(hi & 0x0FFFFFFFu) << 8) |
			static_cast<std::uint64_t>(featureId & 0xFFu);
	}

	/// @brief 前フレームのキャッシュから該当ペア・接触点の累積インパルスを探す
	/// @details キーは 28bit マスクのため異なるペアが衝突しうる。lo/hi/featureId の実値を
	///          照合し、一致しなければ衝突とみなしてキャッシュミス扱い（nullptr）にする
	[[nodiscard]] const CachedContactImpulse3D* findCachedImpulse(
		BodyId a, BodyId b, std::uint32_t featureId) const noexcept
	{
		const BodyId lo = std::min(a, b);
		const BodyId hi = std::max(a, b);
		const std::uint64_t key = impulseCacheKey(a, b, featureId);
		const auto it = std::lower_bound(
			m_impulseCachePrev.begin(), m_impulseCachePrev.end(), key,
			[](const auto& entry, std::uint64_t k) { return entry.first < k; });
		if (it == m_impulseCachePrev.end() || it->first != key) return nullptr;

		const CachedContactImpulse3D& cached = it->second;
		if (cached.lo != lo || cached.hi != hi || cached.featureId != featureId) return nullptr;
		return &cached;
	}

	/// @brief 今フレームで収束した累積インパルスを次フレーム用キャッシュへ確定する
	/// @details 固定長の 2 本の vector を使い回す（clear() は容量を解放しないため、
	///          定常状態ではホットパスでの allocation が発生しない）
	void commitImpulseCache(const std::vector<ContactManifold3D>& manifolds)
	{
		m_impulseCacheNext.clear();
		for (const auto& m : manifolds)
		{
			const BodyId lo = std::min(m.bodyIdA, m.bodyIdB);
			const BodyId hi = std::max(m.bodyIdA, m.bodyIdB);
			m_impulseCacheNext.emplace_back(
				impulseCacheKey(m.bodyIdA, m.bodyIdB, m.featureId),
				CachedContactImpulse3D{
					lo, hi, m.featureId,
					m.accumulatedNormalImpulse,
					m.accumulatedTangentImpulse1,
					m.accumulatedTangentImpulse2});
		}
		std::sort(m_impulseCacheNext.begin(), m_impulseCacheNext.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });
		std::swap(m_impulseCacheNext, m_impulseCachePrev);
	}

	// ── スリープ ──────────────────────────────────────────────

	/// @brief 接触ペアの片方だけが起きていたら、寝ている側を起こす
	static void wakeTouching(
		const std::vector<ContactManifold3D>& manifolds,
		std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies) noexcept
	{
		for (const auto& m : manifolds)
		{
			auto* bodyA = m.cachedBodyA;
			auto* bodyB = m.cachedBodyB;
			if (!bodyA || !bodyB) continue;

			const bool awakeA = bodyA->isStatic() || !bodyA->isAsleep();
			const bool awakeB = bodyB->isStatic() || !bodyB->isAsleep();
			if (awakeA && !awakeB && !bodyB->isStatic()) bodyB->wake();
			if (awakeB && !awakeA && !bodyA->isStatic()) bodyA->wake();
		}
	}

	ContactSolverConfig3D m_config;  ///< ソルバー設定

	std::vector<std::pair<std::uint64_t, CachedContactImpulse3D>> m_impulseCachePrev;  ///< 前フレーム分（key昇順）
	std::vector<std::pair<std::uint64_t, CachedContactImpulse3D>> m_impulseCacheNext;  ///< 今フレーム構築用
};

} // namespace mitiru::physics3d
