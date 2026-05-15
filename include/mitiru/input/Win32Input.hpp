#pragma once

/// @file Win32Input.hpp
/// @brief Win32キーボード・マウス入力ハンドラ
/// @details Win32メッセージループから入力を受け取り、
///          フレーム単位のキー状態追跡（押下・押し始め・離し）を行う。
///          WndProcから processMessage() を呼び出して使用する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <cstdint>

#include <mitiru/input/KeyCode.hpp>
#include <mitiru/input/InputState.hpp>

namespace mitiru
{

/// @brief マウスホイール方向
enum class MouseWheelAxis : std::uint8_t
{
	Vertical = 0,   ///< 垂直スクロール
	Horizontal = 1   ///< 水平スクロール
};

/// @brief Win32キーボード・マウス入力ハンドラ
/// @details WndProcから受信したWin32メッセージを解析し、
///          フレーム単位でキー状態を管理する。
///          current/previousフレームの比較によりエッジ検出（pressed/released）を行う。
///
/// @code
/// mitiru::Win32Input input;
///
/// // WndProc内で呼ぶ
/// case WM_KEYDOWN:
///     input.processMessage(msg, wParam, lParam);
///     break;
///
/// // 毎フレーム呼ぶ
/// input.update();
/// if (input.isKeyPressed(mitiru::KeyCode::Space))
///     jump();
/// @endcode
class Win32Input
{
public:
	/// @brief デフォルトコンストラクタ（全入力なし状態）
	Win32Input() noexcept
	{
		m_currentKeys.fill(false);
		m_previousKeys.fill(false);
		m_currentMouse.fill(false);
		m_previousMouse.fill(false);
	}

	/// @brief Win32メッセージを処理する
	/// @param msg Win32メッセージID
	/// @param wParam メッセージのWPARAM
	/// @param lParam メッセージのLPARAM
	/// @return メッセージを処理した場合 true
	/// @details WndProcから呼び出す。キーボード・マウス関連のメッセージを処理する。
	bool processMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept
	{
		switch (msg)
		{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			const int vk = static_cast<int>(wParam);
			if (vk >= 0 && vk < MAX_KEYS)
			{
				m_currentKeys[static_cast<std::size_t>(vk)] = true;
			}
			return true;
		}

		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			const int vk = static_cast<int>(wParam);
			if (vk >= 0 && vk < MAX_KEYS)
			{
				m_currentKeys[static_cast<std::size_t>(vk)] = false;
			}
			return true;
		}

		case WM_MOUSEMOVE:
			m_mouseX = GET_X_LPARAM(lParam);
			m_mouseY = GET_Y_LPARAM(lParam);
			return true;

		case WM_LBUTTONDOWN:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Left)] = true;
			return true;
		case WM_LBUTTONUP:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Left)] = false;
			return true;

		case WM_RBUTTONDOWN:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Right)] = true;
			return true;
		case WM_RBUTTONUP:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Right)] = false;
			return true;

		case WM_MBUTTONDOWN:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Middle)] = true;
			return true;
		case WM_MBUTTONUP:
			m_currentMouse[static_cast<std::size_t>(MouseButton::Middle)] = false;
			return true;

		case WM_MOUSEWHEEL:
			m_wheelDelta += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			return true;

		case WM_MOUSEHWHEEL:
			m_hWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			return true;

		default:
			return false;
		}
	}

	/// @brief フレーム開始時に呼び出す
	/// @details 現在の状態を前フレーム状態にコピーし、ホイールデルタをリセットする。
	///          ゲームループの先頭で毎フレーム呼ぶこと。
	void update() noexcept
	{
		m_previousKeys = m_currentKeys;
		m_previousMouse = m_currentMouse;
		m_wheelDelta = 0;
		m_hWheelDelta = 0;
	}

	// ── キーボードクエリ ────────────────────

	/// @brief 指定キーが押されているか
	/// @param code キーコード
	/// @return 押されていれば true
	[[nodiscard]] bool isKeyDown(KeyCode code) const noexcept
	{
		const auto idx = static_cast<int>(code);
		if (idx < 0 || idx >= MAX_KEYS) return false;
		return m_currentKeys[static_cast<std::size_t>(idx)];
	}

	/// @brief 指定キーが今フレームで押されたか（エッジ検出）
	/// @param code キーコード
	/// @return 今フレーム押下 かつ 前フレーム非押下なら true
	[[nodiscard]] bool isKeyPressed(KeyCode code) const noexcept
	{
		const auto idx = static_cast<int>(code);
		if (idx < 0 || idx >= MAX_KEYS) return false;
		const auto i = static_cast<std::size_t>(idx);
		return m_currentKeys[i] && !m_previousKeys[i];
	}

	/// @brief 指定キーが今フレームで離されたか（エッジ検出）
	/// @param code キーコード
	/// @return 今フレーム非押下 かつ 前フレーム押下なら true
	[[nodiscard]] bool isKeyReleased(KeyCode code) const noexcept
	{
		const auto idx = static_cast<int>(code);
		if (idx < 0 || idx >= MAX_KEYS) return false;
		const auto i = static_cast<std::size_t>(idx);
		return !m_currentKeys[i] && m_previousKeys[i];
	}

	/// @brief 仮想キーコード（int）でキーが押されているか
	/// @param vk Win32仮想キーコード（0 ~ MAX_KEYS-1）
	/// @return 押されていれば true
	[[nodiscard]] bool isKeyDownRaw(int vk) const noexcept
	{
		if (vk < 0 || vk >= MAX_KEYS) return false;
		return m_currentKeys[static_cast<std::size_t>(vk)];
	}

	// ── マウスクエリ ────────────────────

	/// @brief マウス座標を取得する
	/// @return {x, y} のペア（クライアント座標）
	[[nodiscard]] std::pair<int, int> getMousePosition() const noexcept
	{
		return { m_mouseX, m_mouseY };
	}

	/// @brief マウスX座標を取得する
	[[nodiscard]] int mouseX() const noexcept { return m_mouseX; }

	/// @brief マウスY座標を取得する
	[[nodiscard]] int mouseY() const noexcept { return m_mouseY; }

	/// @brief マウスボタンが押されているか
	/// @param button マウスボタン
	/// @return 押されていれば true
	[[nodiscard]] bool isMouseButtonDown(MouseButton button) const noexcept
	{
		const auto idx = static_cast<std::size_t>(button);
		if (idx >= MAX_MOUSE_BUTTONS) return false;
		return m_currentMouse[idx];
	}

	/// @brief マウスボタンが今フレームで押されたか
	/// @param button マウスボタン
	/// @return 今フレーム押下 かつ 前フレーム非押下なら true
	[[nodiscard]] bool isMouseButtonPressed(MouseButton button) const noexcept
	{
		const auto idx = static_cast<std::size_t>(button);
		if (idx >= MAX_MOUSE_BUTTONS) return false;
		return m_currentMouse[idx] && !m_previousMouse[idx];
	}

	/// @brief マウスボタンが今フレームで離されたか
	/// @param button マウスボタン
	/// @return 今フレーム非押下 かつ 前フレーム押下なら true
	[[nodiscard]] bool isMouseButtonReleased(MouseButton button) const noexcept
	{
		const auto idx = static_cast<std::size_t>(button);
		if (idx >= MAX_MOUSE_BUTTONS) return false;
		return !m_currentMouse[idx] && m_previousMouse[idx];
	}

	/// @brief マウスホイールの垂直スクロール量を取得する
	/// @return 上方向が正、下方向が負
	[[nodiscard]] int getMouseWheel() const noexcept
	{
		return m_wheelDelta;
	}

	/// @brief マウスホイールの水平スクロール量を取得する
	/// @return 右方向が正、左方向が負
	[[nodiscard]] int getMouseHWheel() const noexcept
	{
		return m_hWheelDelta;
	}

	// ── InputState連携 ────────────────────

	/// @brief 現在の状態をInputStateに転写する
	/// @param[out] state 転写先のInputState
	/// @details 既存のInputState連携コードとの互換用。
	void fillInputState(InputState& state) const noexcept
	{
		for (int i = 0; i < MAX_KEYS; ++i)
		{
			state.setKeyDown(i, m_currentKeys[static_cast<std::size_t>(i)]);
		}
		state.setMousePosition(
			static_cast<float>(m_mouseX),
			static_cast<float>(m_mouseY));
		state.setMouseButtonDown(MouseButton::Left,
			m_currentMouse[static_cast<std::size_t>(MouseButton::Left)]);
		state.setMouseButtonDown(MouseButton::Right,
			m_currentMouse[static_cast<std::size_t>(MouseButton::Right)]);
		state.setMouseButtonDown(MouseButton::Middle,
			m_currentMouse[static_cast<std::size_t>(MouseButton::Middle)]);
	}

private:
	/// @brief キーの最大数
	static constexpr int MAX_KEYS = 256;

	/// @brief マウスボタンの最大数
	static constexpr int MAX_MOUSE_BUTTONS = 3;

	std::array<bool, MAX_KEYS> m_currentKeys{};        ///< 現在のキー状態
	std::array<bool, MAX_KEYS> m_previousKeys{};       ///< 前フレームのキー状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_currentMouse{};  ///< 現在のマウスボタン状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_previousMouse{}; ///< 前フレームのマウスボタン状態
	int m_mouseX = 0;   ///< マウスX座標
	int m_mouseY = 0;   ///< マウスY座標
	int m_wheelDelta = 0;  ///< 垂直ホイールデルタ
	int m_hWheelDelta = 0; ///< 水平ホイールデルタ
};

} // namespace mitiru

#endif // _WIN32
