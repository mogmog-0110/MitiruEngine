#pragma once

/// @file RigidBody2D.hpp
/// @brief 2D剛体（物理シミュレーション用）
///
/// 位置・速度・回転を含む2D剛体構造体。
/// セミインプリシットオイラー積分、力・トルク適用、ダンピングを提供する。
/// フレームワーク非依存で、ECSコンポーネントとしても使用可能。
///
/// @code
/// using namespace sgc::physics;
///
/// // ファクトリ関数で動的剛体を作成
/// auto body = RigidBody2D<float>::createDynamic(5.0f, {100.0f, 200.0f});
/// body.linearDamping = 0.01f;
///
/// // 重力を適用
/// body.applyForce({0.0f, 9.8f * body.mass});
///
/// // 物理ステップ更新
/// body.integrate(1.0f / 60.0f);
/// @endcode

#include "sgc/physics/AABB2DCollision.hpp"
#include "sgc/physics/CollisionFilter.hpp"

namespace sgc::physics
{

/// @brief 2D剛体の種類
enum class RigidBody2DType
{
	Static,     ///< 静的（動かない、無限質量として扱う）
	Dynamic,    ///< 動的（力・重力の影響を受ける）
	Kinematic   ///< キネマティック（速度で移動するが力の影響を受けない）
};

/// @brief 2D剛体（物理シミュレーション用）
/// @tparam T 浮動小数点型
///
/// 位置・速度・回転のセミインプリシットオイラー積分を行う。
/// integrate()呼び出し後に力・トルクは自動リセットされる。
template <FloatingPoint T>
struct RigidBody2D
{
	// ── 位置・運動状態 ──
	Vec2<T> position{};             ///< 位置
	Vec2<T> velocity{};             ///< 速度
	Vec2<T> acceleration{};         ///< 加速度（毎ステップリセット）
	Vec2<T> force{};                ///< 蓄積された力（毎ステップリセット）

	// ── 回転 ──
	T rotation{};                   ///< 回転角度（ラジアン）
	T angularVelocity{};            ///< 角速度（rad/s）
	T torque{};                     ///< 蓄積されたトルク（毎ステップリセット）

	// ── 質量と慣性 ──
	T mass{static_cast<T>(1)};      ///< 質量
	T inverseMass{static_cast<T>(1)}; ///< 逆質量（0の場合は無限質量）
	T inertia{static_cast<T>(1)};   ///< 慣性モーメント
	T inverseInertia{static_cast<T>(1)}; ///< 逆慣性モーメント

	// ── ダンピング ──
	T linearDamping{};              ///< 線形ダンピング [0, 1]
	T angularDamping{};             ///< 角ダンピング [0, 1]

	// ── 後方互換性のためのエイリアス ──
	T drag{};                       ///< 空気抵抗係数 [0, 1]（linearDampingの別名）

	// ── 衝突応答パラメータ ──
	T restitution{};                ///< 反発係数 [0, 1]
	T friction{static_cast<T>(0.2)};  ///< 摩擦係数 [0, 1]

	// ── タイプ ──
	RigidBody2DType type{RigidBody2DType::Dynamic}; ///< 剛体の種類
	bool isStatic{false};           ///< 静的オブジェクトか（後方互換性用）
	bool fixedRotation{false};      ///< 回転を固定するか

	CollisionFilter filter{};       ///< 衝突フィルター（レイヤー＋マスク）

	// ── ファクトリ関数 ──

	/// @brief 動的剛体を作成する
	/// @param bodyMass 質量
	/// @param pos 初期位置
	/// @return 動的剛体
	[[nodiscard]] static constexpr RigidBody2D createDynamic(T bodyMass, const Vec2<T>& pos = {}) noexcept
	{
		RigidBody2D body;
		body.position = pos;
		body.mass = bodyMass;
		body.inverseMass = (bodyMass > T{0}) ? T{1} / bodyMass : T{0};
		body.type = RigidBody2DType::Dynamic;
		body.isStatic = false;
		return body;
	}

	/// @brief 静的剛体を作成する
	/// @param pos 初期位置
	/// @return 静的剛体
	[[nodiscard]] static constexpr RigidBody2D createStatic(const Vec2<T>& pos = {}) noexcept
	{
		RigidBody2D body;
		body.position = pos;
		body.mass = T{0};
		body.inverseMass = T{0};
		body.inertia = T{0};
		body.inverseInertia = T{0};
		body.type = RigidBody2DType::Static;
		body.isStatic = true;
		return body;
	}

	/// @brief キネマティック剛体を作成する
	/// @param pos 初期位置
	/// @return キネマティック剛体
	[[nodiscard]] static constexpr RigidBody2D createKinematic(const Vec2<T>& pos = {}) noexcept
	{
		RigidBody2D body;
		body.position = pos;
		body.mass = T{0};
		body.inverseMass = T{0};
		body.inertia = T{0};
		body.inverseInertia = T{0};
		body.type = RigidBody2DType::Kinematic;
		body.isStatic = false;
		return body;
	}

	// ── 力の適用 ──

	/// @brief 力を加える（F = ma に基づく）
	/// @param f 適用する力ベクトル
	///
	/// @code
	/// body.applyForce({0.0f, 9.8f * body.mass}); // 重力
	/// @endcode
	constexpr void applyForce(const Vec2<T>& f) noexcept
	{
		if (isStatic || type == RigidBody2DType::Static ||
			type == RigidBody2DType::Kinematic || mass <= T{0})
		{
			return;
		}
		force = force + f;
		acceleration = acceleration + f / mass;
	}

	/// @brief 衝撃（瞬間的な速度変化）を加える
	/// @param impulse 衝撃ベクトル
	///
	/// @code
	/// body.applyImpulse({0.0f, -500.0f}); // ジャンプ
	/// @endcode
	constexpr void applyImpulse(const Vec2<T>& impulse) noexcept
	{
		if (isStatic || type == RigidBody2DType::Static ||
			type == RigidBody2DType::Kinematic || mass <= T{0})
		{
			return;
		}
		velocity = velocity + impulse * inverseMass;
	}

	/// @brief トルクを加える
	/// @param t 適用するトルク値
	constexpr void applyTorque(T t) noexcept
	{
		if (isStatic || type == RigidBody2DType::Static ||
			type == RigidBody2DType::Kinematic || fixedRotation)
		{
			return;
		}
		torque = torque + t;
	}

	/// @brief 物理ステップを更新する（セミインプリシットオイラー法）
	/// @param dt タイムステップ幅（秒）
	///
	/// 速度に加速度を加算し、ダンピングを適用した後、位置を更新する。
	/// 力・トルク・加速度は自動的にリセットされる。
	constexpr void integrate(T dt) noexcept
	{
		if (isStatic || type == RigidBody2DType::Static) return;

		if (type != RigidBody2DType::Kinematic)
		{
			// 線形運動
			velocity = velocity + acceleration * dt;
		}

		// ダンピング適用（dragとlinearDampingの両方を考慮）
		const T effectiveDamping = (linearDamping > drag) ? linearDamping : drag;
		velocity = velocity * (T{1} - effectiveDamping);
		position = position + velocity * dt;

		if (type != RigidBody2DType::Kinematic && !fixedRotation)
		{
			// 角運動
			angularVelocity = angularVelocity + torque * inverseInertia * dt;
			angularVelocity = angularVelocity * (T{1} - angularDamping);
			rotation = rotation + angularVelocity * dt;
		}

		// 力・トルク・加速度をリセット
		acceleration = {};
		force = {};
		torque = {};
	}

	/// @brief AABBを取得する（位置中心、指定ハーフサイズ）
	/// @param halfSize AABBの半分のサイズ
	/// @return 位置を中心としたAABB
	[[nodiscard]] constexpr AABB2<T> bounds(const Vec2<T>& halfSize) const noexcept
	{
		return {position - halfSize, position + halfSize};
	}
};

/// @brief 2つの剛体間の衝突応答を計算する
///
/// 質量比に基づいて分離し、反発係数を用いた衝撃を適用する。
/// 静的オブジェクトは移動しない（質量無限大として扱う）。
///
/// @tparam T 浮動小数点型
/// @param a 1つ目の剛体
/// @param b 2つ目の剛体
/// @param collision detectAABBCollisionの結果
///
/// @code
/// auto collision = detectAABBCollision(a.bounds(halfA), b.bounds(halfB));
/// if (collision.colliding)
/// {
///     resolveRigidBodyCollision(a, b, collision);
/// }
/// @endcode
template <FloatingPoint T>
constexpr void resolveRigidBodyCollision(
	RigidBody2D<T>& a, RigidBody2D<T>& b,
	const CollisionResult2D<T>& collision) noexcept
{
	if (!collision.colliding) return;

	// 両方静的なら何もしない
	if (a.isStatic && b.isStatic) return;

	const Vec2<T>& normal = collision.normal;
	const T penetration = collision.penetration;

	// ── 位置分離（質量比に基づく） ──
	if (a.isStatic)
	{
		// Aが静的 → Bだけ移動
		b.position = b.position + normal * penetration;
	}
	else if (b.isStatic)
	{
		// Bが静的 → Aだけ移動
		a.position = a.position - normal * penetration;
	}
	else
	{
		// 両方動的 → 質量比で分離
		const T totalMass = a.mass + b.mass;
		const T ratioA = b.mass / totalMass;
		const T ratioB = a.mass / totalMass;
		a.position = a.position - normal * (penetration * ratioA);
		b.position = b.position + normal * (penetration * ratioB);
	}

	// ── 速度応答（反発係数ベースの衝撃） ──
	const Vec2<T> relativeVelocity = a.velocity - b.velocity;
	const T velocityAlongNormal = relativeVelocity.dot(normal);

	// 離れる方向に動いていたら衝撃不要
	if (velocityAlongNormal < T{0}) return;

	// 反発係数（小さい方を採用）
	const T e = (a.restitution < b.restitution) ? a.restitution : b.restitution;

	// 逆質量の計算（静的は逆質量0）
	const T invMassA = a.isStatic ? T{0} : T{1} / a.mass;
	const T invMassB = b.isStatic ? T{0} : T{1} / b.mass;
	const T invMassSum = invMassA + invMassB;

	if (invMassSum <= T{0}) return;

	// 衝撃の大きさ
	const T j = -(T{1} + e) * velocityAlongNormal / invMassSum;

	// 衝撃を適用
	const Vec2<T> impulse = normal * j;
	if (!a.isStatic) a.velocity = a.velocity + impulse * invMassA;
	if (!b.isStatic) b.velocity = b.velocity - impulse * invMassB;
}

using RigidBody2Df = RigidBody2D<float>;

} // namespace sgc::physics
