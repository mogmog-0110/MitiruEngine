// mitiru::Engine の detail header — 直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <cstdlib>

#include <mitiru/core/InlineMacro.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

// ── run ループ / batch 実行の class 外定義 ───────────────────

MITIRU_INLINE void mitiru::Engine::run(Game& game, const EngineConfig& configIn)
{
	/// 設定永続化が有効なら、起動時に settings.json を読み込んで上書きする
	/// (初回起動でファイルが無ければ既定値を書き出す)
	EngineConfig config = configIn;
	if (config.persistSettings)
	{
		if (!GameSettings::loadInto(config))
		{
			GameSettings::saveFrom(config);
		}
	}

	/// MITIRU_AUTOTEST / MITIRU_AUTOTEST_OUTPUT を反映する。
	/// 明示的に設定された autoTestMode はここで上書きされないため、
	/// CI / capture script は env だけで対象 exe をテストモードにできる。
	config.applyAutoTestEnv();

	initialize(config);

	/// axis 4 — deterministic replay の記録。
	/// MITIRU_RECORD=<path> が設定されていれば InputRecorder を起動し、
	/// 終了時に ~Engine() で saveToFile する。
	if (const char* recPath = std::getenv("MITIRU_RECORD"); recPath && *recPath)
	{
		m_recordOutputPath = recPath;
		m_inputRecorder.beginRecording(config.randomSeed, /*tps=*/60);
	}

	/// axis 4 — deterministic replay の再生。
	/// MITIRU_REPLAY=<path> が設定されていれば ReplayData を読み込んで
	/// InputReplayer に load する。以後 applyInjectedInput が毎フレーム
	/// replayer.getCommandsForFrame(clock.frameNumber()) を inject する。
	if (const char* replayPath = std::getenv("MITIRU_REPLAY"); replayPath && *replayPath)
	{
		try
		{
			auto data = mitiru::ReplayData::loadFromFile(replayPath);
			m_inputReplayer.load(data);
			m_replayActive = true;
		}
		catch (...)
		{
			// file が無い / 壊れている場合: replay を無効のまま通常起動する。
		}
	}

	/// ビルドエラー帯 (mitiru watch): CLI がビルド失敗時に書くエラーファイルを
	/// フレームループ内で poll → 存在する間だけ最前面に帯を描く (Engine_Frame.hpp の
	/// tickRenderPhase 末尾)。path 未設定 (通常起動) なら完全 no-op。
	m_errorBanner.setFile(config.errorBannerFile);

	const auto logicalSize = game.layout(
		m_window->width(), m_window->height());
	m_screen = std::make_unique<Screen>(logicalSize.width, logicalSize.height);

	// sprite(id) 1 行描画の resolver を注入する (ABI v16)。基準 dir は loadModule が
	// DLL 隣接の assets/sprites に設定済み (module 無しは cwd 相対の既定のまま)。
	m_screen->setSpriteResolver(&render::SpriteCache::resolve, &m_spriteCache);

	// 背景色の初期値を config から設定する（以後はゲームの draw() 内 screen->clear() が制御）。
	m_screen->clear(config.backgroundColor);

	// headless (窓なし・NullDevice) では GPU バックバッファが無いので、Screen に
	// ソフトウェアフレームバッファを張る。これで draw() が CPU ラスタライズされ、
	// capture() が中身のあるフレームを返せる (#43: AI 自動回しの画面キャプチャ)。
	if (config.headless)
	{
		m_screen->enableSoftwareFramebuffer();
	}

	m_logicalWidth = logicalSize.width;
	m_logicalHeight = logicalSize.height;

	// バックバッファはウィンドウのクライアントサイズに合わせる。
	// 論理サイズ (layout() が返す固定 1920x1080 等) は投影行列に残し、
	// バックバッファ = 物理ピクセルで 1:1 描画することでストレッチ by DXGI を避ける。
	// -> 縮小ディスプレイでもシャープに描画される。
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(m_window->width(), m_window->height());
	}

	createRenderPipeline(logicalSize.width, logicalSize.height);

	// パイプラインのビューポートを物理バックバッファサイズに設定する
	// (投影行列は論理サイズのまま、ビューポートだけ物理解像度にする)
	if (m_renderPipeline)
	{
		m_renderPipeline->setViewportSize(
			static_cast<float>(m_window->width()),
			static_cast<float>(m_window->height()));
	}

	// TTFフォント自動読み込み・接続
	// (skipDefaultFont = true の場合は SDF アトラス構築をスキップ -> ~15s 短縮)
	if (!config.skipDefaultFont)
	{
		initFont(config.fontPath);
	}

	game.setInputState(&m_inputState);
	game.setEngine(this);
	if (m_device)
	{
	}

	create3DRenderer(logicalSize.width, logicalSize.height);
	if (m_renderer3D)
	{
		game.setRenderer3D(m_renderer3D.get());
		/// 2DオーバーレイScreenを3Dレンダラーに接続する
		/// DX12ではendFrame()でバックバッファ上にHUD/UIが描画される
		/// DX11ではno-op
		m_renderer3D->setOverlayScreen(m_screen.get());
	}

	/// CEF を初期化する (DX12 バックエンド + Win32 のみ。
	/// config.enableCef=false の場合は完全スキップで起動時間短縮可能)
	if (config.enableCef)
	{
		initializeCef(config);
	}

#ifdef _WIN32
	/// VSync 設定を SwapChain に反映する (DX12 のみ実装)
	if (auto* dx12Device = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
	{
		if (auto* swap = dynamic_cast<gfx::Dx12SwapChain*>(dx12Device->getSwapChain()))
		{
			swap->setVSync(config.vsync);
		}
	}
#endif

	/// HTTP APIサーバーの初期化 (設定で有効な場合のみ)
	if (config.enableHttpApi)
	{
		initHttpServer(config.httpApiPort, game);
	}

	/// メインループ
	/// m_loopConfig は m_config (member, initialize で copy 済み) を指す。
	/// ローカル config を指すと、設定画面が m_config を書き換えても
	/// 永続化されないバグになるため、必ず member を参照する。
	m_loopGame = &game;
	m_loopConfig = &m_config;

#ifdef _WIN32
	/// Win32 modal resize loop 中 (枠 drag 中) も engine を tick させる。
	/// 詳細は Win32Window::setTickCallback / WM_ENTERSIZEMOVE の comment 参照。
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get()))
	{
		win32->setTickCallback([this] {
			if (!m_window->shouldClose() && !m_shouldStop.load())
			{
				tickOneFrame();
			}
		});

		/// drag 終了時に、modal 中 defer されていた logical/CEF resize を flush
		win32->setModalResizeEndCallback([this] {
			if (m_pendingResizeW > 0 && m_pendingResizeH > 0)
			{
				const int w = m_pendingResizeW;
				const int h = m_pendingResizeH;
				m_pendingResizeW = 0;
				m_pendingResizeH = 0;
				onWindowResize(w, h);
			}
		});
	}
#endif
#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop_arg(
		[](void* arg) {
			static_cast<Engine*>(arg)->tickOneFrame();
		},
		this, 0, 1); // 0 = requestAnimationFrame を使う, 1 = simulate_infinite_loop
#else
	while (!m_window->shouldClose() && !m_shouldStop.load())
	{
		tickOneFrame();
	}
#endif

	/// ループ終了後: ゲームに後処理の機会を与える
	/// (CEF ハンドラーのクリーンアップはここで行う)
	game.onExit();

	/// ループ終了後にHTTPサーバーをシャットダウンする
	if (m_httpServer)
	{
		m_httpServer->shutdown();
	}
}

MITIRU_INLINE void mitiru::Engine::stepFrames(
	Game& game, std::uint64_t frameCount, const EngineConfig& config)
{
	if (!m_initialized)
	{
		auto headlessConfig = config;
		headlessConfig.headless = true;
		initialize(headlessConfig);

		const auto logicalSize = game.layout(
			m_window->width(), m_window->height());
		m_screen = std::make_unique<Screen>(
			logicalSize.width, logicalSize.height);
		m_screen->setSpriteResolver(&render::SpriteCache::resolve, &m_spriteCache);

		/// headlessモードではソフトウェアフレームバッファを自動有効化
		m_screen->enableSoftwareFramebuffer();
	}

	/// ゲームに入力状態を接続する
	game.setInputState(&m_inputState);
	game.setEngine(this);

	for (std::uint64_t i = 0; i < frameCount; ++i)
	{
		(void)m_clock->tick();
		m_inputState.beginFrame();
		applyInjectedInput();

		/// 固定タイムステップで更新 (stepFramesはヘッドレス用なので
		/// 各フレーム = 1固定ステップとして扱う)
		game.update(kFixedDt);

		/// シーンマネージャーが設定されている場合、現在シーンを更新
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onUpdate(kFixedDt);
		}

		m_screen->resetDrawCallCount();
		m_screen->clear();
		game.draw(*m_screen);

		/// シーンマネージャーが設定されている場合、現在シーンを描画
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onDraw(*m_screen);
		}

		/// ソフトウェアフレームバッファ有効時はpresent()でラスタライズ
		if (m_screen->hasSoftwareFramebuffer())
		{
			m_screen->present();
		}
	}
}

MITIRU_INLINE std::vector<std::uint8_t> mitiru::Engine::runAndCapture(
	Game& game, int frameCount, const EngineConfig& config)
{
	initialize(config);
	const auto logicalSize = game.layout(m_window->width(), m_window->height());
	m_screen = std::make_unique<Screen>(logicalSize.width, logicalSize.height);
	m_screen->setSpriteResolver(&render::SpriteCache::resolve, &m_spriteCache);
	createRenderPipeline(logicalSize.width, logicalSize.height);
	game.setInputState(&m_inputState);
	game.setEngine(this);
	if (m_device)
	{
	}
	create3DRenderer(logicalSize.width, logicalSize.height);
	if (m_renderer3D)
	{
		game.setRenderer3D(m_renderer3D.get());
		m_renderer3D->setOverlayScreen(m_screen.get());
	}

	std::vector<std::uint8_t> capturedPixels;

	for (int f = 0; f < frameCount; ++f)
	{
		m_inputState.beginFrame();
		m_window->pollEvents();

		/// 固定タイムステップで更新 (runAndCaptureはキャプチャ用なので
		/// 各フレーム = 1固定ステップとして扱う)
		game.update(kFixedDt);

		m_screen->resetDrawCallCount();
		m_screen->clear();

		const bool renderer3DActive = m_renderer3D && m_renderer3D->isInitialized();
		if (m_device) m_device->beginFrame();
		game.draw(*m_screen);

		// 最後のフレーム: Present前にキャプチャする
		// (Present後はcurrentBackBufferIndexが進むため、描画済みバッファを読めなくなる)
		if (f == frameCount - 1 && m_device)
		{
			m_device->waitForGpu();
			capturedPixels = capture();
		}

		if (m_device)
		{
			if (!renderer3DActive) m_screen->present();
			m_device->endFrame();
		}
	}

	return capturedPixels;
}
