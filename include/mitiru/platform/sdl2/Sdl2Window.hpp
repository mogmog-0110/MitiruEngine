#pragma once

/// @file Sdl2Window.hpp
/// @brief SDL2ウィンドウ実装（スタブ）
/// @details SDL2ライブラリを使用したクロスプラットフォームウィンドウ管理。
///          Windows/macOS/Linuxの全プラットフォームで共通のウィンドウAPIを提供する。
///          MITIRU_HAS_SDL2が定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_SDL2

#include <string>
#include <string_view>

#include <SDL2/SDL.h>

#include <mitiru/input/InputState.hpp>
#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief SDL2ウィンドウ実装
/// @details SDL_Windowをラップし、IWindowインターフェースを提供する。
///          Vulkan/OpenGLスワップチェーン生成用にネイティブハンドルを公開する。
///
/// @code
/// auto window = std::make_unique<Sdl2Window>("My Game", 1280, 720);
/// while (!window->shouldClose())
/// {
///     window->pollEvents();
///     // ... 描画 ...
/// }
/// @endcode
class Sdl2Window final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル
	/// @param width クライアント領域の幅
	/// @param height クライアント領域の高さ
	/// @param flags SDL_WindowFlags（デフォルト: SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE）
	explicit Sdl2Window(std::string_view title, int width, int height,
		Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE)
		: m_width(width)
		, m_height(height)
	{
		/// SDL2の初期化（複数回呼び出しても安全）
		if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
		{
			if (SDL_Init(SDL_INIT_VIDEO) < 0)
			{
				throw std::runtime_error(
					std::string("SDL_Init failed: ") + SDL_GetError());
			}
		}

		m_window = SDL_CreateWindow(
			std::string(title).c_str(),
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			width, height,
			flags);

		if (!m_window)
		{
			throw std::runtime_error(
				std::string("SDL_CreateWindow failed: ") + SDL_GetError());
		}
	}

	/// @brief デストラクタ
	~Sdl2Window() override
	{
		if (m_window)
		{
			SDL_DestroyWindow(m_window);
			m_window = nullptr;
		}
	}

	/// コピー禁止
	Sdl2Window(const Sdl2Window&) = delete;
	Sdl2Window& operator=(const Sdl2Window&) = delete;

	/// ムーブ禁止
	Sdl2Window(Sdl2Window&&) = delete;
	Sdl2Window& operator=(Sdl2Window&&) = delete;

	/// @brief ウィンドウが閉じられるべきかどうか
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief 入力状態を接続する
	/// @param inputState 入力状態へのポインタ
	void setInputState(InputState* inputState) noexcept override
	{
		m_inputState = inputState;
	}

	/// @brief SDL2イベントキューをポーリングする
	void pollEvents() override
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				m_shouldClose = true;
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				{
					m_width = event.window.data1;
					m_height = event.window.data2;
				}
				break;

			case SDL_KEYDOWN:
				if (m_inputState)
				{
					m_inputState->setKeyDown(sdlKeyToEngine(event.key.keysym.sym), true);
				}
				break;

			case SDL_KEYUP:
				if (m_inputState)
				{
					m_inputState->setKeyDown(sdlKeyToEngine(event.key.keysym.sym), false);
				}
				break;

			case SDL_MOUSEMOTION:
				if (m_inputState)
				{
					m_inputState->setMousePosition(
						static_cast<float>(event.motion.x),
						static_cast<float>(event.motion.y));
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
				if (m_inputState)
				{
					m_inputState->setMouseButtonDown(
						sdlMouseButtonToEngine(event.button.button), true);
				}
				break;

			case SDL_MOUSEBUTTONUP:
				if (m_inputState)
				{
					m_inputState->setMouseButtonDown(
						sdlMouseButtonToEngine(event.button.button), false);
				}
				break;

			default:
				break;
			}
		}
	}

	/// @brief クライアント領域の幅を取得する
	[[nodiscard]] int width() const override
	{
		return m_width;
	}

	/// @brief クライアント領域の高さを取得する
	[[nodiscard]] int height() const override
	{
		return m_height;
	}

	/// @brief ウィンドウタイトルを設定する
	void setTitle(std::string_view title) override
	{
		if (m_window)
		{
			SDL_SetWindowTitle(m_window, std::string(title).c_str());
		}
	}

	/// @brief ウィンドウの閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

	/// @brief ウィンドウサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(int width, int height)
	{
		if (m_window)
		{
			SDL_SetWindowSize(m_window, width, height);
			m_width = width;
			m_height = height;
		}
	}

	/// @brief フルスクリーンモードを切り替える
	/// @param fullscreen フルスクリーンにする場合true
	/// @param borderless ボーダーレスフルスクリーンを使用する場合true
	void setFullscreen(bool fullscreen, bool borderless = true)
	{
		if (!m_window)
		{
			return;
		}

		Uint32 flags = 0;
		if (fullscreen)
		{
			flags = borderless
				? SDL_WINDOW_FULLSCREEN_DESKTOP
				: SDL_WINDOW_FULLSCREEN;
		}
		SDL_SetWindowFullscreen(m_window, flags);
		m_fullscreen = fullscreen;

		/// フルスクリーン解除時にサイズを更新する
		if (!fullscreen)
		{
			SDL_GetWindowSize(m_window, &m_width, &m_height);
		}
	}

	/// @brief フルスクリーン状態を取得する
	/// @return フルスクリーンならtrue
	[[nodiscard]] bool isFullscreen() const noexcept
	{
		return m_fullscreen;
	}

	/// @brief ネイティブSDL_Windowを取得する
	/// @return SDL_Windowポインタ（Vulkanサーフェス生成に使用）
	[[nodiscard]] SDL_Window* nativeWindow() const noexcept
	{
		return m_window;
	}

#ifdef _WIN32
	/// @brief Win32ウィンドウハンドルを取得する（DX11/DX12用）
	/// @return HWND（SDL_SysWMinfo経由で取得）
	[[nodiscard]] void* platformHandle() const
	{
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (SDL_GetWindowWMInfo(m_window, &info))
		{
			return info.info.win.window;
		}
		return nullptr;
	}
#endif

private:
	/// @brief SDLキーコードをエンジン内部キーコードに変換する
	[[nodiscard]] static int sdlKeyToEngine(SDL_Keycode key) noexcept
	{
		// 文字キー (A-Z): SDL は小文字、engine は大文字 (65-90) を使う
		if (key >= SDLK_a && key <= SDLK_z)
		{
			return static_cast<int>(key - SDLK_a) + 65;
		}
		// 数字キー (0-9): SDL / engine とも 48-57 を使う
		if (key >= SDLK_0 && key <= SDLK_9)
		{
			return static_cast<int>(key);
		}
		switch (key)
		{
		case SDLK_RETURN:    return 13;  // Enter
		case SDLK_ESCAPE:    return 27;  // Escape
		case SDLK_BACKSPACE: return 8;   // Backspace
		case SDLK_TAB:       return 9;   // Tab
		case SDLK_SPACE:     return 32;  // Space
		case SDLK_LEFT:      return 37;  // Left
		case SDLK_UP:        return 38;  // Up
		case SDLK_RIGHT:     return 39;  // Right
		case SDLK_DOWN:      return 40;  // Down
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:    return 16;  // Shift
		case SDLK_LCTRL:
		case SDLK_RCTRL:     return 17;  // Ctrl
		case SDLK_LALT:
		case SDLK_RALT:      return 18;  // Alt
		case SDLK_DELETE:    return 46;  // Delete
		case SDLK_INSERT:    return 45;  // Insert
		case SDLK_HOME:      return 36;  // Home
		case SDLK_END:       return 35;  // End
		case SDLK_PAGEUP:    return 33;  // PageUp
		case SDLK_PAGEDOWN:  return 34;  // PageDown
		case SDLK_F1:        return 112;
		case SDLK_F2:        return 113;
		case SDLK_F3:        return 114;
		case SDLK_F4:        return 115;
		case SDLK_F5:        return 116;
		case SDLK_F6:        return 117;
		case SDLK_F7:        return 118;
		case SDLK_F8:        return 119;
		case SDLK_F9:        return 120;
		case SDLK_F10:       return 121;
		case SDLK_F11:       return 122;
		case SDLK_F12:       return 123;
		default:             return 0;
		}
	}

	/// @brief SDLマウスボタンをエンジン内部MouseButtonに変換する
	[[nodiscard]] static MouseButton sdlMouseButtonToEngine(Uint8 button) noexcept
	{
		switch (button)
		{
		case SDL_BUTTON_LEFT:   return MouseButton::Left;
		case SDL_BUTTON_RIGHT:  return MouseButton::Right;
		case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
		default:                return MouseButton::Left;
		}
	}

	SDL_Window* m_window = nullptr;      ///< SDL_Windowハンドル
	int m_width = 0;                     ///< クライアント領域の幅
	int m_height = 0;                    ///< クライアント領域の高さ
	bool m_shouldClose = false;          ///< 閉じ要求フラグ
	bool m_fullscreen = false;           ///< フルスクリーンフラグ
	InputState* m_inputState = nullptr;  ///< 入力状態（外部所有）
};

} // namespace mitiru

#endif // MITIRU_HAS_SDL2
