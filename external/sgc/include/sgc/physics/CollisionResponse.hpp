#pragma once

/// @file CollisionResponse.hpp
/// @brief 3D衝突応答（インパルスベースの衝突解決）
///
/// 接触点情報に基づいて衝突インパルスを計算し、剛体を分離する。
/// 反発係数・摩擦モデル・Baumgarte安定化を含む。
///
/// @code
/// using namespace sgc::physics;
///
/// RigidBody3D a, b;
/// a.position = {0, 0, 0}; a.mass = 1.0f;
/// b.position = {0.8f, 0, 0}; b.mass = 2.0f;
///
/// ContactPoint contact;
/// contact.position = {0.4f, 0, 0};
/// contact.normal = {1.0f, 0, 0};
/// contact.penetrationDepth = 0.2f;
///
/// auto impulse = resolveCollision(a, b, contact);
/// // a, b の速度が更新される
/// separateBodies(a, b, contact);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/physics/RigidBody3D.hpp"

namespace sgc::physics
{

/// @brief 接触点情報
struct ContactPoint
{
	Vec3f position{};            ///< 接触位置（ワールド座標）
	Vec3f normal{};              ///< 接触法線（AからBへ向かう方向、正規化済み）
	float penetrationDepth = 0.0f; ///< めり込み深さ（正の値）
};

/// @brief 衝突ペア情報
struct CollisionPair
{
	uint32_t bodyIdA = 0;                ///< 剛体AのID
	uint32_t bodyIdB = 0;                ///< 剛体BのID
	std::vector<ContactPoint> contacts;  ///< 接触点リスト
};

/// @brief 摩擦パラメータ
struct FrictionParams
{
	float staticFriction = 0.4f;   ///< 静摩擦係数
	float dynamicFriction = 0.3f;  ///< 動摩擦係数
};

/// @brief Baumgarte安定化パラメータ
struct BaumgarteParams
{
	float factor = 0.2f;    ///< 補正係数（0.1〜0.3が一般的）
	float slop = 0.01f;     ///< 許容めり込み量（これ以下は補正しない）
};

/// @brief 衝突インパルスを計算して両剛体の速度を更新する
///
/// 反発係数は両剛体のrestitutionの小さい方を使用する。
/// 法線方向のインパルスに加え、摩擦によるタンジェントインパルスも適用する。
///
/// @param bodyA 剛体A（速度が更新される）
/// @param bodyB 剛体B（速度が更新される）
/// @param contact 接触点情報
/// @param friction 摩擦パラメータ
/// @return 法線方向のインパルスベクトル
[[nodiscard]] inline Vec3f resolveCollision(
	RigidBody3D& bodyA,
	RigidBody3D& bodyB,
	const ContactPoint& contact,
	const FrictionParams& friction = {})
{
	const float invMassA = bodyA.inverseMass();
	const float invMassB = bodyB.inverseMass();
	const float invMassSum = invMassA + invMassB;

	// 両方静的の場合は何もしない
	if (invMassSum <= 0.0f)
	{
		return Vec3f::zero();
	}

	// 相対速度（BからAへ）
	const Vec3f relativeVelocity = bodyA.velocity - bodyB.velocity;
	const float relVelAlongNormal = relativeVelocity.dot(contact.normal);

	// 分離方向に移動中なら衝突解決不要
	// （法線はA→B方向なので、relVelAlongNormal < 0 は分離中を意味する）
	if (relVelAlongNormal < 0.0f)
	{
		return Vec3f::zero();
	}

	// 反発係数（小さい方を採用）
	const float restitution = std::min(bodyA.restitution, bodyB.restitution);

	// 法線方向のインパルス量
	const float j = -(1.0f + restitution) * relVelAlongNormal / invMassSum;
	const Vec3f impulse = contact.normal * j;

	// 速度を更新
	bodyA.velocity = bodyA.velocity + impulse * invMassA;
	bodyB.velocity = bodyB.velocity - impulse * invMassB;

	// ── 摩擦インパルス ──
	// 法線方向成分を除いたタンジェント方向を計算
	const Vec3f relVelAfter = bodyA.velocity - bodyB.velocity;
	const Vec3f tangent = relVelAfter - contact.normal * relVelAfter.dot(contact.normal);
	const float tangentLenSq = tangent.lengthSquared();

	if (tangentLenSq > 1e-8f)
	{
		const Vec3f tangentDir = tangent / std::sqrt(tangentLenSq);
		const float tangentSpeed = relVelAfter.dot(tangentDir);

		// クーロン摩擦モデル
		const float frictionImpulse = -tangentSpeed / invMassSum;
		const float absJ = std::abs(j);

		Vec3f frictionVec;
		if (std::abs(frictionImpulse) < absJ * friction.staticFriction)
		{
			// 静摩擦（完全に停止）
			frictionVec = tangentDir * frictionImpulse;
		}
		else
		{
			// 動摩擦
			frictionVec = tangentDir * (-absJ * friction.dynamicFriction);
		}

		bodyA.velocity = bodyA.velocity + frictionVec * invMassA;
		bodyB.velocity = bodyB.velocity - frictionVec * invMassB;
	}

	return impulse;
}

/// @brief Baumgarte安定化による位置補正で剛体を分離する
///
/// めり込み量がslopを超えた分だけ、質量比に応じて両剛体を法線方向に押し出す。
///
/// @param bodyA 剛体A（位置が更新される）
/// @param bodyB 剛体B（位置が更新される）
/// @param contact 接触点情報
/// @param params Baumgarteパラメータ
inline void separateBodies(
	RigidBody3D& bodyA,
	RigidBody3D& bodyB,
	const ContactPoint& contact,
	const BaumgarteParams& params = {})
{
	const float invMassA = bodyA.inverseMass();
	const float invMassB = bodyB.inverseMass();
	const float invMassSum = invMassA + invMassB;

	if (invMassSum <= 0.0f) return;

	const float correction = std::max(contact.penetrationDepth - params.slop, 0.0f)
		* params.factor / invMassSum;
	const Vec3f correctionVec = contact.normal * correction;

	bodyA.position = bodyA.position - correctionVec * invMassA;
	bodyB.position = bodyB.position + correctionVec * invMassB;
}

/// @brief 接触点とインパルスを同時に解決する便利関数
///
/// resolveCollision + separateBodies を一括で呼び出す。
///
/// @param bodyA 剛体A
/// @param bodyB 剛体B
/// @param contact 接触点情報
/// @param friction 摩擦パラメータ
/// @param baumgarte Baumgarteパラメータ
/// @return 法線方向のインパルスベクトル
[[nodiscard]] inline Vec3f resolveAndSeparate(
	RigidBody3D& bodyA,
	RigidBody3D& bodyB,
	const ContactPoint& contact,
	const FrictionParams& friction = {},
	const BaumgarteParams& baumgarte = {})
{
	const Vec3f impulse = resolveCollision(bodyA, bodyB, contact, friction);
	separateBodies(bodyA, bodyB, contact, baumgarte);
	return impulse;
}

} // namespace sgc::physics
