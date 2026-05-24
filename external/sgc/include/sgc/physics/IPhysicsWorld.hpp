#pragma once

/// @file IPhysicsWorld.hpp
/// @brief 2D物理ワールドの抽象インターフェース
///
/// 剛体の追加・削除、重力設定、物理ステップの実行を抽象化する。
/// 具体的な衝突検出・応答アルゴリズムは実装クラスに委ねる。
///
/// @code
/// sgc::physics::PhysicsWorld2Df world;
/// world.setGravity({0.0f, 980.0f});
///
/// sgc::physics::RigidBody2Df body;
/// body.position = {100.0f, 0.0f};
/// body.mass = 1.0f;
/// world.addBody(&body, {16.0f, 16.0f});
///
/// world.step(1.0f / 60.0f);
/// @endcode

#include "sgc/physics/RigidBody2D.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::physics
{

/// @brief 2D物理ワールドインターフェース
/// @tparam T 浮動小数点型
///
/// 剛体の登録・解除、重力制御、物理シミュレーションステップを定義する。
template <FloatingPoint T>
class IPhysicsWorld2D
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IPhysicsWorld2D() = default;

	/// @brief 剛体をワールドに追加する
	/// @param body 追加する剛体へのポインタ（所有権は呼び出し元が保持）
	/// @param halfSize AABBの半分のサイズ
	virtual void addBody(RigidBody2D<T>* body, const Vec2<T>& halfSize) = 0;

	/// @brief 剛体をワールドから除去する
	/// @param body 除去する剛体へのポインタ
	virtual void removeBody(RigidBody2D<T>* body) = 0;

	/// @brief 物理シミュレーションを1ステップ進める
	/// @param dt タイムステップ幅（秒）
	virtual void step(T dt) = 0;

	/// @brief 重力ベクトルを設定する
	/// @param gravity 重力ベクトル
	virtual void setGravity(const Vec2<T>& gravity) = 0;

	/// @brief 現在の重力ベクトルを取得する
	/// @return 重力ベクトル
	[[nodiscard]] virtual Vec2<T> gravity() const = 0;

	/// @brief 登録されている剛体数を取得する
	/// @return 剛体数
	[[nodiscard]] virtual std::size_t bodyCount() const = 0;

	/// @brief 全剛体をワールドから除去する
	virtual void clear() = 0;
};

/// @brief float版 IPhysicsWorld2D
using IPhysicsWorld2Df = IPhysicsWorld2D<float>;

} // namespace sgc::physics
