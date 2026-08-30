// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/BackendInit.hpp>

#include <fstream>

// ── Engine lifecycle (initialize) のクラス外定義 ────────────────

MITIRU_INLINE void mitiru::Engine::initialize(const EngineConfig& config)
{
	m_config = config;
	m_shouldStop.store(false);

	/// 設定された音量を内部状態にコピー (audio engine 生成後に applyVolumes される)
	m_masterVolume = clampVol(config.masterVolume);
	m_bgmVolume    = clampVol(config.bgmVolume);
	m_seVolume     = clampVol(config.seVolume);
	m_voiceVolume  = clampVol(config.voiceVolume);

	/// ウィンドウサイズの解釈: useLogicalWindowSize=true のときは
	/// windowWidth/Height を論理 DIP とみなし、systemDpi を見て物理ピクセルへ拡大する。
	/// それ以外は物理ピクセル扱いで従来通り透過する (後方互換)。
	int winW = config.windowWidth;
	int winH = config.windowHeight;
#ifdef _WIN32
	if (config.useLogicalWindowSize)
	{
		const unsigned dpi = mitiru::Win32Window::systemDpi();
		if (dpi > 0 && dpi != 96)
		{
			winW = static_cast<int>((static_cast<long long>(winW) * dpi + 48) / 96);
			winH = static_cast<int>((static_cast<long long>(winH) * dpi + 48) / 96);
		}
	}
#endif

	/// プラットフォーム生成
	if (config.headless)
	{
		m_platform = std::make_unique<HeadlessPlatform>();
	}
	else
	{
#ifdef _WIN32
		m_platform = std::make_unique<Win32Platform>();
#elif defined(__EMSCRIPTEN__)
		m_platform = std::make_unique<EmscriptenPlatform>();
#else
		/// 非Windows環境ではWindowFactoryでウィンドウ生成
		m_platform = std::make_unique<HeadlessPlatform>();
#endif
	}

	/// ウィンドウ生成
	/// バックエンドに応じて適切なウィンドウ型を選択する
#ifdef _WIN32
	if (config.gfxBackend == gfx::Backend::OpenGL)
	{
		// OpenGL には GlfwWindow（GLFW_NO_API なし）が必要
#ifdef MITIRU_HAS_GLFW
		m_window = std::make_unique<GlfwWindow>(
			config.title, winW, winH,
			GlfwGraphicsMode::OpenGL);
#else
		// GLFW 不在時は黙って変えない。fallback は明示する
		mitiru::debug::warnOnce("gfx.glfw.opengl.fallback",
			"指定 backend OpenGL は GLFW 不在で使用不可、Dx11 に変更");
		m_config.gfxBackend = gfx::Backend::Dx11;
		m_window = m_platform->createWindow(
			config.title, winW, winH);
#endif
	}
	else if (config.gfxBackend == gfx::Backend::Vulkan)
	{
		// Vulkan には GlfwWindow（GLFW_NO_API）が必要
#ifdef MITIRU_HAS_GLFW
		m_window = std::make_unique<GlfwWindow>(
			config.title, winW, winH,
			GlfwGraphicsMode::Vulkan);
#else
		// GLFW 不在時は黙って変えない。fallback は明示する
		mitiru::debug::warnOnce("gfx.glfw.vulkan.fallback",
			"指定 backend Vulkan は GLFW 不在で使用不可、Dx11 に変更");
		m_config.gfxBackend = gfx::Backend::Dx11;
		m_window = m_platform->createWindow(
			config.title, winW, winH);
#endif
	}
	else
	{
		// DX11/DX12/Auto → Win32Window（DisplayMode + resizable 指定可）
		auto* win32Plat = dynamic_cast<Win32Platform*>(m_platform.get());
		if (win32Plat)
		{
			m_window = win32Plat->createWindowExtended(
				config.title, winW, winH,
				config.displayMode,
				config.windowResizable,
				config.windowX, config.windowY);
		}
		else
		{
			m_window = m_platform->createWindow(
				config.title, winW, winH);
		}
	}
#elif defined(__EMSCRIPTEN__)
	m_window = m_platform->createWindow(
		config.title, winW, winH);
#else
	if (config.headless)
	{
		m_window = m_platform->createWindow(
			config.title, winW, winH);
	}
	else
	{
#ifdef MITIRU_HAS_OPENGL
		/// OpenGLバックエンド使用時はSDL_WINDOW_OPENGLフラグ付きで生成する
		m_window = std::make_unique<Sdl2Window>(
			config.title, winW, winH,
			SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
#else
		m_window = createWindow(WindowBackend::Auto,
			config.title, winW, winH);
#endif
	}
#endif
	/// 入力状態とリサイズコールバックをウィンドウに接続する（IWindow仮想メソッド経由）
	m_window->setInputState(&m_inputState);
	m_window->setResizeCallback([this](int w, int h) {
		onWindowResize(w, h);
	});
	/// 最小クライアントサイズを強制 (0 のときは no-op)。文字や panel が潰れて
	/// 読めなくなる極端な縮小を防ぐ resize 安全保証。
	m_window->setMinClientSize(config.minWindowWidth, config.minWindowHeight);
#ifdef _WIN32
	/// Win32Window がある場合、InputInjector を接続してhuman playをキャプチャ可能にする
	if (auto* w32 = dynamic_cast<Win32Window*>(m_window.get()))
	{
		w32->setInputInjector(&m_inputInjector);
	}
#endif

	/// GPUデバイス生成
	if (config.headless || config.gfxBackend == gfx::Backend::Null)
	{
		m_device = std::make_unique<gfx::NullDevice>();
	}
	else
	{
		m_device = gfx::createDevice(
			config.gfxBackend, m_window.get());
	}

	/// クロック生成
	m_clock = std::make_unique<Clock>(config.targetTps, config.deterministic);

	m_initialized = true;
}
