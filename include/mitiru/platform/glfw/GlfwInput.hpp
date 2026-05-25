#pragma once

/// @file GlfwInput.hpp
/// @brief GLFW入力ハンドリング
/// @details GLFWのコールバックシステムを使用したキーボード・マウス入力管理。
///          MITIRU_HAS_GLFWが定義されている場合のみコンパイルされる。
///
///          カーソルキャプチャ: InputState::setCursorCaptured() の値を毎フレーム
///          applyCursorCapture() が読み取り、エッジ検出で glfwSetInputMode を
///          呼び出す（GLFW_CURSOR_DISABLED ↔ GLFW_CURSOR_NORMAL）。
///          GLFW_RAW_MOUSE_MOTION がサポートされている場合は自動的に有効化する。
///          キャプチャ中の cursorPos コールバックは差分を InputState::setRawMouseDelta()
///          に蓄積する（beginFrame() でリセットされる）。

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

} // namespace mitiru (system include のため一時的に閉じる)

#ifdef MITIRU_HAS_GLFW

#ifdef MITIRU_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif
#include <GLFW/glfw3.h>

namespace mitiru
{

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
	/// @details InputState::isCursorCaptured() の値を読み取り、遷移時に
	///          glfwSetInputMode で GLFW_CURSOR_DISABLED / GLFW_CURSOR_NORMAL を切り替える。
	void beginFrame() noexcept
	{
		m_state.beginFrame();
		m_joystick.beginFrame();
		m_scroll.reset();
		applyCursorCapture();
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
	/// @brief カーソルキャプチャの遷移を GLFW に反映する
	/// @details InputState::isCursorCaptured() の値が前フレームと変わったときに
	///          glfwSetInputMode(GLFW_CURSOR, ...) を呼び出す。
	///          - false → true : GLFW_CURSOR_DISABLED に切り替え、サポートされていれば
	///                           GLFW_RAW_MOUSE_MOTION も有効化する。
	///          - true → false : GLFW_CURSOR_NORMAL に戻し、生マウスモーションを無効化する。
	void applyCursorCapture() noexcept
	{
		if (!m_window) return;

		const bool wantCapture = m_state.isCursorCaptured();
		if (wantCapture == m_captureActive)
		{
			return;
		}

		if (wantCapture)
		{
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			if (glfwRawMouseMotionSupported())
			{
				glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
			}
		}
		else
		{
			if (glfwRawMouseMotionSupported())
			{
				glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
			}
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_haveLastCursorPos = false;
		}
		m_captureActive = wantCapture;
	}

	/// @brief GLFWキーコードをエンジン内部のVKベースキーコードに変換する
	/// @param glfwKey GLFWキーコード
	/// @return VKベースキーコード（変換不可の場合は-1）
	[[nodiscard]] static int glfwKeyToEngine(int glfwKey) noexcept
	{
		/// アルファベット・数字・スペースはGLFWとVKで一致する
		if ((glfwKey >= 'A' && glfwKey <= 'Z') ||
			(glfwKey >= '0' && glfwKey <= '9') ||
			glfwKey == ' ')
		{
			return glfwKey;
		}

		switch (glfwKey)
		{
		/// 制御キー
		case GLFW_KEY_ENTER:       return 13;   // VK_RETURN
		case GLFW_KEY_ESCAPE:      return 27;   // VK_ESCAPE
		case GLFW_KEY_BACKSPACE:   return 8;    // VK_BACK
		case GLFW_KEY_TAB:         return 9;    // VK_TAB
		case GLFW_KEY_LEFT_SHIFT:  return 16;   // VK_SHIFT
		case GLFW_KEY_RIGHT_SHIFT: return 16;
		case GLFW_KEY_LEFT_CONTROL:  return 17; // VK_CONTROL
		case GLFW_KEY_RIGHT_CONTROL: return 17;
		case GLFW_KEY_LEFT_ALT:    return 18;   // VK_MENU
		case GLFW_KEY_RIGHT_ALT:   return 18;
		case GLFW_KEY_CAPS_LOCK:   return 20;   // VK_CAPITAL

		/// 矢印キー
		case GLFW_KEY_LEFT:        return 37;   // VK_LEFT
		case GLFW_KEY_UP:          return 38;   // VK_UP
		case GLFW_KEY_RIGHT:       return 39;   // VK_RIGHT
		case GLFW_KEY_DOWN:        return 40;   // VK_DOWN

		/// ナビゲーション
		case GLFW_KEY_INSERT:      return 45;   // VK_INSERT
		case GLFW_KEY_DELETE:      return 46;   // VK_DELETE
		case GLFW_KEY_HOME:        return 36;   // VK_HOME
		case GLFW_KEY_END:         return 35;   // VK_END
		case GLFW_KEY_PAGE_UP:     return 33;   // VK_PRIOR
		case GLFW_KEY_PAGE_DOWN:   return 34;   // VK_NEXT

		/// ファンクションキー
		case GLFW_KEY_F1:          return 112;  // VK_F1
		case GLFW_KEY_F2:          return 113;
		case GLFW_KEY_F3:          return 114;
		case GLFW_KEY_F4:          return 115;
		case GLFW_KEY_F5:          return 116;
		case GLFW_KEY_F6:          return 117;
		case GLFW_KEY_F7:          return 118;
		case GLFW_KEY_F8:          return 119;
		case GLFW_KEY_F9:          return 120;
		case GLFW_KEY_F10:         return 121;
		case GLFW_KEY_F11:         return 122;
		case GLFW_KEY_F12:         return 123;

		/// OEMキー（ScriptDemo等で使用）
		case GLFW_KEY_EQUAL:       return 0xBB; // VK_OEM_PLUS (+/=)
		case GLFW_KEY_MINUS:       return 0xBD; // VK_OEM_MINUS (-/_)
		case GLFW_KEY_SLASH:       return 0xBF; // VK_OEM_2 (/)
		case GLFW_KEY_PERIOD:      return 0xBE; // VK_OEM_PERIOD (.)
		case GLFW_KEY_SEMICOLON:   return 0xBA; // VK_OEM_1 (;)

		default:
			return -1;
		}
	}

	/// @brief キーコールバック
	static void keyCallback(GLFWwindow* window, int key,
		int /*scancode*/, int action, int /*mods*/)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		const int engineKey = glfwKeyToEngine(key);
		if (engineKey < 0) return;

		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			self->m_state.setKeyDown(engineKey, true);
		}
		else if (action == GLFW_RELEASE)
		{
			self->m_state.setKeyDown(engineKey, false);
		}
	}

	/// @brief カーソル位置コールバック
	/// @details 通常モードでは位置をそのまま InputState に転送する。
	///          カーソルキャプチャ中は前回位置との差分を setRawMouseDelta に蓄積する。
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(window));
		if (!self) return;

		const float sx = static_cast<float>(xpos) * self->m_config.mouseSensitivity;
		const float sy = static_cast<float>(ypos) * self->m_config.mouseSensitivity;
		self->m_state.setMousePosition(sx, sy);

		if (self->m_captureActive)
		{
			if (self->m_haveLastCursorPos)
			{
				self->m_state.setRawMouseDelta(
					sx - self->m_lastCursorX,
					sy - self->m_lastCursorY);
			}
			self->m_lastCursorX = sx;
			self->m_lastCursorY = sy;
			self->m_haveLastCursorPos = true;
		}
		else
		{
			self->m_haveLastCursorPos = false;
		}
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
	bool m_captureActive = false;       ///< 前フレームのカーソルキャプチャ状態（遷移検出用）
	bool m_haveLastCursorPos = false;   ///< m_lastCursorX/Y が有効か
	float m_lastCursorX = 0.0f;         ///< キャプチャ中の前回カーソル位置 X（差分計算用）
	float m_lastCursorY = 0.0f;         ///< キャプチャ中の前回カーソル位置 Y（差分計算用）
};

} // namespace mitiru

#endif // MITIRU_HAS_GLFW
