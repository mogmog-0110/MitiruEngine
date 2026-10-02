#pragma once

/// @file InputAdapter.hpp
/// @brief mitiru::InputState → sgc::IInputProvider アダプター
/// @details InputState の入力 API を sgc の IInputProvider インターフェースに適合させる。
///          これにより sgc の ActionMap、UI ウィジェット入力等が利用可能になる。

#include <vector>

#include <sgc/input/IInputProvider.hpp>
#include <sgc/math/Vec2.hpp>
#include <mitiru/input/InputState.hpp>

namespace mitiru::adapter
{

/// @brief mitiru::InputState を sgc の IInputProvider として使用するアダプター
/// @details InputState の全入力状態を sgc の IInputProvider インターフェースに変換する。
///          mouseDelta()は前回呼び出しからの差分を自動追跡する。
class InputAdapter : public sgc::IInputProvider
{
public:
	/// @brief コンストラクタ
	/// @param state 入力状態（非所有）
	explicit InputAdapter(const InputState& state) noexcept
		: m_state(state)
	{
	}

	/// @brief 現在押されているキーのコードを収集する
	/// @param[out] outPressedKeys 押されているキーのコードを格納するベクタ
	void pollPressedKeys(std::vector<int>& outPressedKeys) const override
	{
		outPressedKeys.clear();
		for (int i = 0; i < InputState::MAX_KEYS; ++i)
		{
			if (m_state.isKeyDown(i))
			{
				outPressedKeys.push_back(i);
			}
		}
	}

	/// @brief マウスカーソルの現在座標を取得する
	/// @return マウス座標（ピクセル）
	[[nodiscard]] sgc::Vec2f mousePosition() const override
	{
		const auto [mx, my] = m_state.mousePosition();
		return {mx, my};
	}

	/// @brief マウスカーソルの前回呼び出しからの移動量を取得する
	/// @return マウス移動量（ピクセル）
	/// @note InputState には delta API が無いため、前回位置との差分を自動計算する
	[[nodiscard]] sgc::Vec2f mouseDelta() const override
	{
		const auto [mx, my] = m_state.mousePosition();
		const sgc::Vec2f delta{mx - m_prevMouseX, my - m_prevMouseY};
		m_prevMouseX = mx;
		m_prevMouseY = my;
		return delta;
	}

	/// @brief マウスボタンが押されているか
	/// @param button ボタン番号（0=左, 1=右, 2=中）
	/// @return 押されている場合は true
	[[nodiscard]] bool isMouseButtonDown(int button) const override
	{
		return m_state.isMouseButtonDown(static_cast<MouseButton>(button));
	}

	/// @brief マウスボタンがこのフレームで押されたか
	/// @param button ボタン番号（0=左, 1=右, 2=中）
	/// @return このフレームで押された場合は true
	[[nodiscard]] bool isMouseButtonPressed(int button) const override
	{
		return m_state.isMouseButtonJustPressed(static_cast<MouseButton>(button));
	}

	/// @brief マウスボタンがこのフレームで離されたか
	/// @param button ボタン番号（0=左, 1=右, 2=中）
	/// @return このフレームで離された場合は true
	[[nodiscard]] bool isMouseButtonReleased(int button) const override
	{
		return m_state.isMouseButtonJustReleased(static_cast<MouseButton>(button));
	}

	/// @brief 指定したキーが押されているか
	/// @param keyCode キーコード
	/// @return 押されている場合は true
	[[nodiscard]] bool isKeyDown(int keyCode) const override
	{
		return m_state.isKeyDown(keyCode);
	}

	/// @brief 指定したキーがこのフレームで押されたか
	/// @param keyCode キーコード
	/// @return このフレームで新たに押された場合は true
	[[nodiscard]] bool isKeyJustPressed(int keyCode) const override
	{
		return m_state.isKeyJustPressed(keyCode);
	}

private:
	const InputState& m_state;      ///< 入力状態（非所有）
	mutable float m_prevMouseX = 0.0f;  ///< 前回マウスX座標
	mutable float m_prevMouseY = 0.0f;  ///< 前回マウスY座標
};

} // namespace mitiru::adapter
