#pragma once

/// @file IInputProvider.hpp
/// @brief 抽象入力プロバイダーインターフェース
///
/// フレームワーク非依存の入力APIを定義する。
/// Siv3D、DxLib等の具体的な入力実装はこのインターフェースを実装する。
///
/// @code
/// class MyInput : public sgc::IInputProvider {
/// public:
///     void pollPressedKeys(std::vector<int>& out) const override { /* ... */ }
///     Vec2f mousePosition() const override { return {0, 0}; }
///     Vec2f mouseDelta() const override { return {0, 0}; }
/// };
/// @endcode

#include <vector>

#include "sgc/math/Vec2.hpp"

namespace sgc
{

/// @brief 抽象入力プロバイダーインターフェース
///
/// 現在押されているキーとマウス状態を取得する。
/// ActionMapと組み合わせてフレームワーク非依存の入力処理を実現する。
class IInputProvider
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IInputProvider() = default;

	/// @brief マウスボタン定数: 左ボタン
	static constexpr int MOUSE_LEFT = 0;

	/// @brief マウスボタン定数: 右ボタン
	static constexpr int MOUSE_RIGHT = 1;

	/// @brief マウスボタン定数: 中ボタン
	static constexpr int MOUSE_MIDDLE = 2;

	/// @brief 現在押されているキーコードを収集する
	/// @param[out] outPressedKeys 押されているキーコードを格納するベクタ
	virtual void pollPressedKeys(std::vector<int>& outPressedKeys) const = 0;

	/// @brief マウスカーソルの現在座標を取得する
	/// @return マウス座標（ピクセル）
	[[nodiscard]] virtual Vec2f mousePosition() const = 0;

	/// @brief マウスカーソルの前フレームからの移動量を取得する
	/// @return マウス移動量（ピクセル）
	[[nodiscard]] virtual Vec2f mouseDelta() const = 0;

	/// @brief マウスボタンが押下中か
	/// @param button MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
	/// @return 押下中ならtrue
	[[nodiscard]] virtual bool isMouseButtonDown(int /*button*/) const { return false; }

	/// @brief マウスボタンがこのフレームで押されたか
	/// @param button MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
	/// @return このフレームで押されたならtrue
	[[nodiscard]] virtual bool isMouseButtonPressed(int /*button*/) const { return false; }

	/// @brief マウスボタンがこのフレームで離されたか
	/// @param button MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
	/// @return このフレームで離されたならtrue
	[[nodiscard]] virtual bool isMouseButtonReleased(int /*button*/) const { return false; }

	/// @brief 指定キーが押下中か
	/// @param keyCode キーコード（addKey等で登録したsgcキーコード）
	/// @return 押下中ならtrue
	[[nodiscard]] virtual bool isKeyDown(int /*keyCode*/) const { return false; }

	/// @brief 指定キーがこのフレームで押されたか
	/// @param keyCode キーコード（addKey等で登録したsgcキーコード）
	/// @return このフレームで新たに押されたならtrue
	[[nodiscard]] virtual bool isKeyJustPressed(int /*keyCode*/) const { return false; }
};

} // namespace sgc
