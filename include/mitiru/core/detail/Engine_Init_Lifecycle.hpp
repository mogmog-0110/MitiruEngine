// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/BackendInit.hpp>

#include <cstdlib>
#include <fstream>

// ── Engine lifecycle (initialize) のクラス外定義 ────────────────

MITIRU_INLINE void mitiru::Engine::initialize(const EngineConfig& config)
{
	m_config = config;
	// G1: 撮影中は実時間 dt を使わない。以降 m_config.deterministic を見る箇所
	// (Clock 構築・tickFixedUpdatePhase の stepCap 判定) すべてに一括で反映させる。
	if (m_config.captureActive) { m_config.deterministic = true; }
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
		/// 非 Windows 環境では WindowFactory でウィンドウ生成
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
		// GLFW 不在時は知らせないまま変えない。fallback は明示する
		mitiru::debug::warnOnceFix("gfx.glfw.opengl.fallback",
			"指定 backend OpenGL は GLFW 不在で使用不可、Dx11 に変更",
			"ビルド構成に MITIRU_HAS_GLFW が定義されていない",
			"GLFW を find_package できる構成で再 configure するか、Dx11 backend を明示指定する");
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
		// GLFW 不在時は知らせないまま変えない。fallback は明示する
		mitiru::debug::warnOnceFix("gfx.glfw.vulkan.fallback",
			"指定 backend Vulkan は GLFW 不在で使用不可、Dx11 に変更",
			"ビルド構成に MITIRU_HAS_GLFW が定義されていない",
			"GLFW を find_package できる構成で再 configure するか、Dx11 backend を明示指定する");
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
		/// OpenGL バックエンド使用時は SDL_WINDOW_OPENGL フラグ付きで生成する
		m_window = std::make_unique<Sdl2Window>(
			config.title, winW, winH,
			SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
#else
		m_window = createWindow(WindowBackend::Auto,
			config.title, winW, winH);
#endif
	}
#endif
	/// 入力状態とリサイズコールバックをウィンドウに接続する（IWindow 仮想メソッド経由）
	m_window->setInputState(&m_inputState);
	m_window->setResizeCallback([this](int w, int h) {
		onWindowResize(w, h);
	});
	/// 最小クライアントサイズを強制 (0 のときは no-op)。文字や panel が潰れて
	/// 読めなくなる極端な縮小を防ぐ resize 安全保証。
	m_window->setMinClientSize(config.minWindowWidth, config.minWindowHeight);
#ifdef _WIN32
	/// Win32Window がある場合、InputInjector を接続して human play をキャプチャできるようにする
	if (auto* w32 = dynamic_cast<Win32Window*>(m_window.get()))
	{
		w32->setInputInjector(&m_inputInjector);
	}
#endif

	/// GPU デバイス生成。
	/// G2: `--headless` は既定で最速の `NullDevice` (2D は SW ラスタ経由で別途描く)。
	/// 環境変数 `MITIRU_HEADLESS_GPU3D` (opt-in) が立っている時だけ、ウィンドウ無しでも
	/// 実 GPU デバイスを windowless に作って 3D を描き `readPixels()` で読み戻せるようにする
	/// (`--capture-dir` で 3D シーンの PNG を撮る用途)。無条件に切り替えないのは、既存の
	/// ctest 群 (determinism/e2e/replay_golden 等) が `--headless` の NullDevice 前提で
	/// 大量に走っており、既定動作を変えると全て巻き添えで失敗するため。
	const bool headlessGpu3D = config.headless
		&& std::getenv("MITIRU_HEADLESS_GPU3D") != nullptr
		&& config.gfxBackend != gfx::Backend::Null;
	if (headlessGpu3D)
	{
		m_device = gfx::createWindowlessDevice3D(
			config.gfxBackend, winW, winH);
	}
	else if (config.headless || config.gfxBackend == gfx::Backend::Null)
	{
		m_device = std::make_unique<gfx::NullDevice>();
	}
	else
	{
		m_device = gfx::createDevice(
			config.gfxBackend, m_window.get());
	}

	/// クロック生成
	m_clock = std::make_unique<Clock>(config.targetTps, m_config.deterministic);

	m_initialized = true;
}
