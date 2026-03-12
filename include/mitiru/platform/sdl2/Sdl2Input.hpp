#pragma once

/// @file Sdl2Input.hpp
/// @brief SDL2入力ハンドリング
/// @details SDL2のイベントシステムを使用したキーボード・マウス・ゲームパッド入力管理。
///          MITIRU_HAS_SDL2が定義されている場合のみコンパイルされる。

#include <array>
#include <cstdint>

#include <mitiru/input/InputState.hpp>
#include <mitiru/input/KeyCode.hpp>

namespace mitiru
{

/// @brief SDL2入力設定
/// @details SDL2入力処理のオプションパラメータを保持する。
///          実際のSDL2ライブラリに依存せず、テストで使用可能。
struct Sdl2InputConfig
{
	bool enableKeyboard = true;          ///< キーボード入力の有効化
	bool enableMouse = true;             ///< マウス入力の有効化
	bool enableGamepad = true;           ///< ゲームパッド入力の有効化
	int gamepadDeadzone = 8000;          ///< ゲームパッドデッドゾーン（0〜32767）
	bool enableTextInput = false;        ///< テキスト入力モードの有効化
	float mouseSensitivity = 1.0f;       ///< マウス感度スケール

	/// @brief デフォルト設定を取得する
	/// @return 標準的な入力設定
	[[nodiscard]] static Sdl2InputConfig defaults() noexcept
	{
		return Sdl2InputConfig{};
	}
};

/// @brief ゲームパッド状態
/// @details 1つのゲームパッドの軸・ボタン状態を保持する。
struct GamepadState
{
	/// @brief 最大軸数
	static constexpr int MAX_AXES = 6;

	/// @brief 最大ボタン数
	static constexpr int MAX_BUTTONS = 16;

	bool connected = false;                           ///< 接続状態
	std::array<float, MAX_AXES> axes{};               ///< 軸値（-1.0〜1.0に正規化）
	std::array<bool, MAX_BUTTONS> buttons{};          ///< ボタン状態
	std::array<bool, MAX_BUTTONS> prevButtons{};      ///< 前フレームのボタン状態

	/// @brief フレーム開始時処理（前フレーム状態の保存）
	void beginFrame() noexcept
	{
		prevButtons = buttons;
	}

	/// @brief 指定ボタンが今フレームで押されたか（エッジ検出）
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

	/// @brief 指定ボタンが今フレームで離されたか（エッジ検出）
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

/// @brief マウスホイール状態
struct MouseWheelState
{
	float scrollX = 0.0f;  ///< 水平スクロール量
	float scrollY = 0.0f;  ///< 垂直スクロール量

	/// @brief リセットする
	void reset() noexcept
	{
		scrollX = 0.0f;
		scrollY = 0.0f;
	}
};

#ifdef MITIRU_HAS_SDL2

#include <SDL2/SDL.h>

/// @brief SDL2入力ハンドラー
/// @details SDL_Eventからキーボード・マウス・ゲームパッドの状態を更新する。
///          InputStateと統合して使用する。
///
/// @code
/// Sdl2Input input(Sdl2InputConfig::defaults());
/// // メインループ内
/// input.beginFrame();
/// SDL_Event event;
/// while (SDL_PollEvent(&event))
/// {
///     input.processEvent(event);
/// }
/// const auto& state = input.state();
/// if (state.isKeyDown(static_cast<int>(KeyCode::Space)))
/// {
///     // スペースキー処理
/// }
/// @endcode
class Sdl2Input
{
public:
	/// @brief コンストラクタ
	/// @param config 入力設定
	explicit Sdl2Input(const Sdl2InputConfig& config = Sdl2InputConfig::defaults())
		: m_config(config)
	{
		if (config.enableGamepad)
		{
			SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
		}
	}

	/// @brief デストラクタ
	~Sdl2Input()
	{
		if (m_controller)
		{
			SDL_GameControllerClose(m_controller);
			m_controller = nullptr;
		}
	}

	/// コピー禁止
	Sdl2Input(const Sdl2Input&) = delete;
	Sdl2Input& operator=(const Sdl2Input&) = delete;

	/// ムーブ禁止
	Sdl2Input(Sdl2Input&&) = delete;
	Sdl2Input& operator=(Sdl2Input&&) = delete;

	/// @brief フレーム開始処理
	/// @details 前フレーム状態の保存とスクロール量のリセットを行う。
	void beginFrame() noexcept
	{
		m_state.beginFrame();
		m_gamepad.beginFrame();
		m_wheel.reset();
	}

	/// @brief SDL_Eventを処理する
	/// @param event SDL2イベント
	void processEvent(const SDL_Event& event)
	{
		switch (event.type)
		{
		case SDL_KEYDOWN:
			if (m_config.enableKeyboard)
			{
				m_state.setKeyDown(event.key.keysym.scancode, true);
			}
			break;

		case SDL_KEYUP:
			if (m_config.enableKeyboard)
			{
				m_state.setKeyDown(event.key.keysym.scancode, false);
			}
			break;

		case SDL_MOUSEMOTION:
			if (m_config.enableMouse)
			{
				m_state.setMousePosition(
					static_cast<float>(event.motion.x) * m_config.mouseSensitivity,
					static_cast<float>(event.motion.y) * m_config.mouseSensitivity);
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
			if (m_config.enableMouse)
			{
				handleMouseButton(event.button.button, true);
			}
			break;

		case SDL_MOUSEBUTTONUP:
			if (m_config.enableMouse)
			{
				handleMouseButton(event.button.button, false);
			}
			break;

		case SDL_MOUSEWHEEL:
			if (m_config.enableMouse)
			{
				m_wheel.scrollX += static_cast<float>(event.wheel.x);
				m_wheel.scrollY += static_cast<float>(event.wheel.y);
			}
			break;

		case SDL_CONTROLLERDEVICEADDED:
			if (m_config.enableGamepad && !m_controller)
			{
				m_controller = SDL_GameControllerOpen(event.cdevice.which);
				m_gamepad.connected = (m_controller != nullptr);
			}
			break;

		case SDL_CONTROLLERDEVICEREMOVED:
			if (m_controller)
			{
				SDL_GameControllerClose(m_controller);
				m_controller = nullptr;
				m_gamepad.connected = false;
			}
			break;

		case SDL_CONTROLLERBUTTONDOWN:
			if (m_config.enableGamepad)
			{
				m_gamepad.buttons[static_cast<std::size_t>(event.cbutton.button)] = true;
			}
			break;

		case SDL_CONTROLLERBUTTONUP:
			if (m_config.enableGamepad)
			{
				m_gamepad.buttons[static_cast<std::size_t>(event.cbutton.button)] = false;
			}
			break;

		case SDL_CONTROLLERAXISMOTION:
			if (m_config.enableGamepad)
			{
				const auto axis = static_cast<std::size_t>(event.caxis.axis);
				if (axis < GamepadState::MAX_AXES)
				{
					const int raw = event.caxis.value;
					const int dz = m_config.gamepadDeadzone;
					if (raw > -dz && raw < dz)
					{
						m_gamepad.axes[axis] = 0.0f;
					}
					else
					{
						m_gamepad.axes[axis] = static_cast<float>(raw) / 32767.0f;
					}
				}
			}
			break;

		default:
			break;
		}
	}

	/// @brief 入力状態を取得する
	/// @return InputStateへのconst参照
	[[nodiscard]] const InputState& state() const noexcept
	{
		return m_state;
	}

	/// @brief ゲームパッド状態を取得する
	/// @return GamepadStateへのconst参照
	[[nodiscard]] const GamepadState& gamepad() const noexcept
	{
		return m_gamepad;
	}

	/// @brief マウスホイール状態を取得する
	/// @return MouseWheelStateへのconst参照
	[[nodiscard]] const MouseWheelState& wheel() const noexcept
	{
		return m_wheel;
	}

	/// @brief 設定を取得する
	/// @return Sdl2InputConfigへのconst参照
	[[nodiscard]] const Sdl2InputConfig& config() const noexcept
	{
		return m_config;
	}

private:
	/// @brief SDL_MouseButtonEventをMouseButtonに変換して状態を設定する
	/// @param sdlButton SDL2マウスボタンID
	/// @param down 押下状態
	void handleMouseButton(std::uint8_t sdlButton, bool down) noexcept
	{
		switch (sdlButton)
		{
		case SDL_BUTTON_LEFT:
			m_state.setMouseButtonDown(MouseButton::Left, down);
			break;
		case SDL_BUTTON_RIGHT:
			m_state.setMouseButtonDown(MouseButton::Right, down);
			break;
		case SDL_BUTTON_MIDDLE:
			m_state.setMouseButtonDown(MouseButton::Middle, down);
			break;
		default:
			break;
		}
	}

	Sdl2InputConfig m_config;                  ///< 入力設定
	InputState m_state;                        ///< 入力状態
	GamepadState m_gamepad;                    ///< ゲームパッド状態
	MouseWheelState m_wheel;                   ///< マウスホイール状態
	SDL_GameController* m_controller = nullptr; ///< ゲームコントローラーハンドル
};

#endif // MITIRU_HAS_SDL2

} // namespace mitiru
