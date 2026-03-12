#pragma once

/// @file GlfwInput.hpp
/// @brief GLFW入力ハンドリング
/// @details GLFWのコールバックシステムを使用したキーボード・マウス入力管理。
///          MITIRU_HAS_GLFWが定義されている場合のみコンパイルされる。

#include <array>
#include <cstdint>

#include <mitiru/input/InputState.hpp>
#include <mitiru/input/KeyCode.hpp>

namespace mitiru
{

/// @brief GLFW入力設定
/// @details GLFW入力処理のオプションパラメータを保持する。
///          実際のGLFWライブラリに依存せず、テストで使用可能。
struct GlfwInputConfig
{
	bool enableKeyboard = true;          ///< キーボード入力の有効化
	bool enableMouse = true;             ///< マウス入力の有効化
	bool enableJoystick = true;          ///< ジョイスティック入力の有効化
	bool rawMouseMotion = false;         ///< 生マウスモーション入力の有効化
	bool stickyKeys = false;             ///< スティッキーキーの有効化
	float mouseSensitivity = 1.0f;       ///< マウス感度スケール

	/// @brief デフォルト設定を取得する
	/// @return 標準的な入力設定
	[[nodiscard]] static GlfwInputConfig defaults() noexcept
	{
		return GlfwInputConfig{};
	}

	/// @brief FPS向け設定を取得する（生マウスモーション有効）
	/// @return FPS向け入力設定
	[[nodiscard]] static GlfwInputConfig fps() noexcept
	{
		GlfwInputConfig config;
		config.rawMouseMotion = true;
		config.mouseSensitivity = 0.5f;
		return config;
	}
};

/// @brief GLFWジョイスティック状態
/// @details GLFWで取得した1つのジョイスティックの状態を保持する。
struct GlfwJoystickState
{
	/// @brief 最大軸数
	static constexpr int MAX_AXES = 6;

	/// @brief 最大ボタン数
	static constexpr int MAX_BUTTONS = 16;

	bool connected = false;                           ///< 接続状態
	int joystickId = -1;                              ///< GLFWジョイスティックID
	std::array<float, MAX_AXES> axes{};               ///< 軸値（-1.0〜1.0に正規化）
	std::array<bool, MAX_BUTTONS> buttons{};          ///< ボタン状態
	std::array<bool, MAX_BUTTONS> prevButtons{};      ///< 前フレームのボタン状態

	/// @brief フレーム開始時処理
	void beginFrame() noexcept
	{
		prevButtons = buttons;
	}

	/// @brief 指定ボタンが今フレームで押されたか
	/// @param index ボタンインデックス
	/// @return 今フレーム押下かつ前フレーム非押下ならtrue
	[[nodiscard]] bool isButtonJustPressed(int index) const noexcept
	{
		if (index < 0 || index >= MAX_BUTTONS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(index);
		return buttons[idx] && !prevButtons[idx];
	}

	/// @brief 指定ボタンが今フレームで離されたか
	/// @param index ボタンインデックス
	/// @return 今フレーム非押下かつ前フレーム押下ならtrue
	[[nodiscard]] bool isButtonJustReleased(int index) const noexcept
	{
		if (index < 0 || index >= MAX_BUTTONS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(index);
		return !buttons[idx] && prevButtons[idx];
	}
};

/// @brief マウススクロール状態
struct GlfwScrollState
{
	double xOffset = 0.0;  ///< 水平スクロールオフセット
	double yOffset = 0.0;  ///< 垂直スクロールオフセット

	/// @brief リセットする
	void reset() noexcept
	{
		xOffset = 0.0;
		yOffset = 0.0;
	}
};

#ifdef MITIRU_HAS_GLFW

#include <GLFW/glfw3.h>

/// @brief GLFW入力ハンドラー
/// @details GLFWウィンドウにコールバックを登録してキーボード・マウス入力を管理する。
///
/// @code
/// GlfwInput input(glfwWindow, GlfwInputConfig::defaults());
/// // メインループ内
/// input.beginFrame();
/// glfwPollEvents();
/// input.pollJoysticks();
/// const auto& state = input.state();
/// @endcode
class GlfwInput
{
public:
	/// @brief コンストラクタ
	/// @param window GLFWウィンドウハンドル
	/// @param config 入力設定
	explicit GlfwInput(GLFWwindow* window,
		const GlfwInputConfig& config = GlfwInputConfig::defaults())
		: m_window(window)
		, m_config(config)
	{
		/// ユーザーポインタを設定してコールバックからアクセスできるようにする
		glfwSetWindowUserPointer(m_window, this);

		if (config.enableKeyboard)
		{
			glfwSetKeyCallback(m_window, keyCallback);
		}
		if (config.enableMouse)
		{
			glfwSetCursorPosCallback(m_window, cursorPosCallback);
			glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
			glfwSetScrollCallback(m_window, scrollCallback);

			if (config.rawMouseMotion &&
				glfwRawMouseMotionSupported())
			{
				glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
			}
		}
		if (config.stickyKeys)
		{
			glfwSetInputMode(m_window, GLFW_STICKY_KEYS, GLFW_TRUE);
		}
	}

	/// @brief デストラクタ
	~GlfwInput()
	{
		if (m_window)
		{
			glfwSetKeyCallback(m_window, nullptr);
			glfwSetCursorPosCallback(m_window, nullptr);
			glfwSetMouseButtonCallback(m_window, nullptr);
			glfwSetScrollCallback(m_window, nullptr);
		}
	}

	/// コピー禁止
	GlfwInput(const GlfwInput&) = delete;
	GlfwInput& operator=(const GlfwInput&) = delete;

	/// ムーブ禁止
	GlfwInput(GlfwInput&&) = delete;
	GlfwInput& operator=(GlfwInput&&) = delete;

	/// @brief フレーム開始処理
	void beginFrame() noexcept
	{
		m_state.beginFrame();
		m_joystick.beginFrame();
		m_scroll.reset();
	}

	/// @brief ジョイスティック状態をポーリングする
	/// @details GLFWのジョイスティックAPIを使用して状態を更新する。
	void pollJoysticks()
	{
		if (!m_config.enableJoystick)
		{
			return;
		}

		/// 最初に検出されたジョイスティックを使用する
		for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
		{
			if (glfwJoystickPresent(jid))
			{
				m_joystick.connected = true;
				m_joystick.joystickId = jid;

				int axisCount = 0;
				const float* axes = glfwGetJoystickAxes(jid, &axisCount);
				for (int i = 0; i < axisCount && i < GlfwJoystickState::MAX_AXES; ++i)
				{
					m_joystick.axes[static_cast<std::size_t>(i)] = axes[i];
				}

				int buttonCount = 0;
				const unsigned char* buttons = glfwGetJoystickButtons(jid, &buttonCount);
				for (int i = 0; i < buttonCount && i < GlfwJoystickState::MAX_BUTTONS; ++i)
				{
					m_joystick.buttons[static_cast<std::size_t>(i)] =
						(buttons[i] == GLFW_PRESS);
				}
				return;
			}
		}

		m_joystick.connected = false;
	}

	/// @brief 入力状態を取得する
	[[nodiscard]] const InputState& state() const noexcept
	{
		return m_state;
	}

	/// @brief ジョイスティック状態を取得する
	[[nodiscard]] const GlfwJoystickState& joystick() const noexcept
	{
		return m_joystick;
	}

	/// @brief スクロール状態を取得する
	[[nodiscard]] const GlfwScrollState& scroll() const noexcept
	{
		return m_scroll;
	}

	/// @brief 設定を取得する
	[[nodiscard]] const GlfwInputConfig& config() const noexcept
	{
		return m_config;
	}

private:
	/// @brief キーコールバック
	static void keyCallback(GLFWwindow* window, int key,
		int /*scancode*/, int action, int /*mods*/)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			self->m_state.setKeyDown(key, true);
		}
		else if (action == GLFW_RELEASE)
		{
			self->m_state.setKeyDown(key, false);
		}
	}

	/// @brief カーソル位置コールバック
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		self->m_state.setMousePosition(
			static_cast<float>(xpos) * self->m_config.mouseSensitivity,
			static_cast<float>(ypos) * self->m_config.mouseSensitivity);
	}

	/// @brief マウスボタンコールバック
	static void mouseButtonCallback(GLFWwindow* window, int button,
		int action, int /*mods*/)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		const bool down = (action == GLFW_PRESS);
		switch (button)
		{
		case GLFW_MOUSE_BUTTON_LEFT:
			self->m_state.setMouseButtonDown(MouseButton::Left, down);
			break;
		case GLFW_MOUSE_BUTTON_RIGHT:
			self->m_state.setMouseButtonDown(MouseButton::Right, down);
			break;
		case GLFW_MOUSE_BUTTON_MIDDLE:
			self->m_state.setMouseButtonDown(MouseButton::Middle, down);
			break;
		default:
			break;
		}
	}

	/// @brief スクロールコールバック
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		self->m_scroll.xOffset += xoffset;
		self->m_scroll.yOffset += yoffset;
	}

	GLFWwindow* m_window;              ///< GLFWウィンドウハンドル
	GlfwInputConfig m_config;           ///< 入力設定
	InputState m_state;                 ///< 入力状態
	GlfwJoystickState m_joystick;       ///< ジョイスティック状態
	GlfwScrollState m_scroll;           ///< スクロール状態
};

#endif // MITIRU_HAS_GLFW

} // namespace mitiru
