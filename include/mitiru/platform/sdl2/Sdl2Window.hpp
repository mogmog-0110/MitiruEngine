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
	SDL_Window* m_window = nullptr;   ///< SDL_Windowハンドル
	int m_width = 0;                  ///< クライアント領域の幅
	int m_height = 0;                 ///< クライアント領域の高さ
	bool m_shouldClose = false;       ///< 閉じ要求フラグ
};

} // namespace mitiru

#endif // MITIRU_HAS_SDL2
