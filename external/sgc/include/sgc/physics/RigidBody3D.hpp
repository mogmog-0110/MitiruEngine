#pragma once

/// @file RigidBody3D.hpp
/// @brief 3D剛体（簡易物理シミュレーション用）
///
/// 基本的な力・衝撃・ダンピング・反発を扱う軽量な3D剛体構造体。
/// フレームワーク非依存で、ECSコンポーネントとしても使用可能。
///
/// @code
/// using namespace sgc::physics;
///
/// RigidBody3D body;
/// body.position = {0, 10, 0};
/// body.mass = 5.0f;
///
/// // 重力を適用
/// body.applyForce({0, -9.8f * body.mass, 0});
///
/// // 物理ステップ更新
/// body.integrate(1.0f / 60.0f);
/// @endcode

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"

namespace sgc::physics
{

/// @brief 3D剛体（簡易物理シミュレーション用）
///
/// 位置・速度・加速度のセミインプリシットオイラー積分を行う。
/// 角速度によるクォータニオン回転の更新もサポートする。
/// integrate()呼び出し後に加速度は自動リセットされる。
struct RigidBody3D
{
	Vec3f position{};              ///< 位置
	Vec3f velocity{};              ///< 線形速度
	Vec3f acceleration{};          ///< 加速度（毎ステップリセット）
	Vec3f angularVelocity{};       ///< 角速度（ラジアン/秒）
	Quaternionf rotation{};        ///< 回転（クォータニオン）
	float mass = 1.0f;             ///< 質量
	float linearDamping = 0.01f;   ///< 線形ダンピング係数 [0, 1]
	float angularDamping = 0.01f;  ///< 角速度ダンピング係数 [0, 1]
	float restitution = 0.3f;      ///< 反発係数 [0, 1]
	bool isStatic = false;         ///< 静的オブジェクトか

	/// @brief 逆質量を返す
	/// @return 逆質量（静的または質量0の場合は0）
	[[nodiscard]] float inverseMass() const noexcept
	{
		if (isStatic || mass <= 0.0f) return 0.0f;
		return 1.0f / mass;
	}

	/// @brief 力を加える（F = ma に基づく）
	/// @param force 適用する力ベクトル
	void applyForce(const Vec3f& force)
	{
		if (isStatic || mass <= 0.0f) return;
		acceleration = acceleration + force / mass;
	}

	/// @brief 衝撃（瞬間的な速度変化）を加える
	/// @param impulse 衝撃ベクトル
	void applyImpulse(const Vec3f& impulse)
	{
		if (isStatic || mass <= 0.0f) return;
		velocity = velocity + impulse / mass;
	}

	/// @brief 物理ステップを更新する（セミインプリシットオイラー法）
	/// @param dt タイムステップ幅（秒）
	///
	/// 速度に加速度を加算し、ダンピングを適用した後、位置を更新する。
	/// 角速度によるクォータニオン回転の更新も行う。
	/// 加速度は自動的にリセットされる。
	void integrate(float dt)
	{
		if (isStatic) return;

		// 線形運動
		velocity = velocity + acceleration * dt;
		velocity = velocity * (1.0f - linearDamping);
		position = position + velocity * dt;

		// 角速度による回転更新
		if (angularVelocity.lengthSquared() > 0.0f)
		{
			const float angLen = angularVelocity.length();
			const Vec3f axis = angularVelocity / angLen;
			const float halfAngle = angLen * dt * 0.5f;
			const Quaternionf dq{
				axis.x * std::sin(halfAngle),
				axis.y * std::sin(halfAngle),
				axis.z * std::sin(halfAngle),
				std::cos(halfAngle)
			};
			rotation = (dq * rotation).normalized();
		}

		angularVelocity = angularVelocity * (1.0f - angularDamping);

		// 加速度をリセット
		acceleration = {};
	}
};

} // namespace sgc::physics
