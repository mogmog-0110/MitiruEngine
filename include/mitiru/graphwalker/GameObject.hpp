#pragma once

/// @file GameObject.hpp
/// @brief GraphWalkerゲームオブジェクトの基底インターフェースと共通型
///
/// IGameObjectはすべてのゲームオブジェクトが実装する仮想インターフェース。
/// update / 位置 / 範囲 / アクティブ状態を統一的に扱う。
///
/// @code
/// auto obj = factory.createStaticPlatform({100, 200}, {200, 20});
/// obj->update(dt);
/// auto bounds = obj->getBounds();
/// @endcode

#include <cstdint>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Rect.hpp"
#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief ゲームオブジェクトの基底インターフェース
///
/// すべてのゲームオブジェクトはこのインターフェースを実装する。
/// 位置・更新・範囲・アクティブ状態を統一的に管理する。
class IGameObject
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IGameObject() = default;

	/// @brief フレーム更新
	/// @param dt デルタタイム（秒）
	virtual void update(float dt) = 0;

	/// @brief 位置を取得する
	/// @return 現在の位置（ワールド座標）
	[[nodiscard]] virtual sgc::Vec2f getPosition() const = 0;

	/// @brief 位置を設定する
	/// @param pos 新しい位置
	virtual void setPosition(sgc::Vec2f pos) = 0;

	/// @brief オブジェクト種別を取得する
	/// @return ObjectType列挙値
	[[nodiscard]] virtual ObjectType getType() const = 0;

	/// @brief バウンディング矩形を取得する
	/// @return 衝突判定用AABB
	[[nodiscard]] virtual sgc::Rectf getBounds() const = 0;

	/// @brief アクティブ状態を取得する
	/// @return アクティブならtrue
	[[nodiscard]] virtual bool isActive() const = 0;

	/// @brief アクティブ状態を設定する
	/// @param active 新しいアクティブ状態
	virtual void setActive(bool active) = 0;

protected:
	IGameObject() = default;
	IGameObject(const IGameObject&) = default;
	IGameObject& operator=(const IGameObject&) = default;
	IGameObject(IGameObject&&) = default;
	IGameObject& operator=(IGameObject&&) = default;
};

} // namespace mitiru::gw
