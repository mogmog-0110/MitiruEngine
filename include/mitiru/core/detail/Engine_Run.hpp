// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <cstdio>
#include <cstdlib>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

// ── run ループ / batch 実行の class 外定義 ───────────────────

namespace mitiru::detail
{

/// mitiru_host の終了コード 1〜3 (引数・module 読み込み) と区別する
inline constexpr int kExitGpuLostAtStartup = 4;
inline constexpr int kExitGpuLostDuringPlay = 5;

/// 起動中にデバイスが lost になったら、理由を 1 行出して描画を始める前に終える (#75)。
/// この時点の復旧は描画資源ごと作り直すことになるが、その経路は無い。後始末の destructor も
/// hang 中のドライバ呼び出しで止まりうるので、unwind せずに抜ける。
inline void exitIfDeviceLostAtStartup(gfx::IDevice* device, const char* phase)
{
#ifdef _WIN32
	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(device);
	if (dx12 == nullptr || !dx12->isDeviceLost()) { return; }
	console::noticef("起動の途中 (%s) で%sため、ゲームを始められません。"
		"Win+Ctrl+Shift+B でグラフィックスドライバを再起動するか、PC を再起動してから起動してください。%s%s%s",
		phase, dx12->isGpuUnresponsive() ? " GPU が応答しなくなった" : "グラフィックスデバイスが使えなくなった",
		gfx::dred::reportPath().empty() ? "" : "GPU の状態の記録は ", gfx::dred::reportPath().c_str(),
		gfx::dred::reportPath().empty() ? "" : " にあります。");
	std::fflush(stdout);
	std::fflush(stderr);
	std::_Exit(kExitGpuLostAtStartup);
#else
	(void)device;
	(void)phase;
#endif
}

} // namespace mitiru::detail

MITIRU_INLINE void mitiru::Engine::run(Game& game, const EngineConfig& configIn)
{
	EngineConfig config = configIn;

	/// MITIRU_AUTOTEST / MITIRU_AUTOTEST_OUTPUT を反映する。
	/// 明示的に設定された autoTestMode はここで上書きされないため、
	/// CI / capture script は env だけで対象 exe をテストモードにできる。
	config.applyAutoTestEnv();

	initialize(config);

	/// ビルドエラー帯 (mitiru watch): CLI がビルド失敗時に書くエラーファイルを
	/// フレームループ内で poll → 存在する間だけ最前面に帯を描く (Engine_Frame.hpp の
	/// tickRenderPhase 末尾)。path 未設定 (通常起動) なら完全 no-op。
	m_errorBanner.setFile(config.errorBannerFile);

	const auto logicalSize = game.layout(
		m_window->width(), m_window->height());
	m_screen = std::make_unique<Screen>(logicalSize.width, logicalSize.height);
	m_screen->applyExpectedSprites(config.expectedSprites);

	// sprite(id) 1 行描画の resolver を注入する (ABI v16)。基準 dir は loadModule が
	// DLL 隣接の assets/sprites に設定済み (module 無しは cwd 相対の既定のまま)。
	m_screen->setSpriteResolver(&render::SpriteCache::resolve, &m_spriteCache);

	// 背景色の初期値を config から設定する（以後はゲームの draw() 内 screen->clear() が制御）。
	m_screen->clear(config.backgroundColor);

	// headless (窓なし・NullDevice) では GPU バックバッファが無いので、Screen に
	// ソフトウェアフレームバッファを張る。これで draw() が CPU ラスタライズされ、
	// capture() が中身のあるフレームを返せる (#43: AI 自動回しの画面キャプチャ)。
	// G2: ただし headless でも実 GPU device (windowless 3D, `--headless-3d`) を
	// 持っている時は SW ラスタライズを張らない。張ると Engine::capture() が
	// (SW framebuffer 優先の分岐で) 3D を積んだ GPU オフスクリーンではなく
	// 空の CPU framebuffer を返してしまう。
	if (config.headless && (!m_device || m_device->backend() == gfx::Backend::Null))
	{
		m_screen->enableSoftwareFramebuffer();
	}

	m_logicalWidth = logicalSize.width;
	m_logicalHeight = logicalSize.height;

	// バックバッファはウィンドウのクライアントサイズに合わせる。
	// 論理サイズ (layout() が返す固定 1920x1080 等) は投影行列に残し、
	// バックバッファ = 物理ピクセルで 1:1 描画することで DXGI によるストレッチを避ける。
	// -> 縮小ディスプレイでもシャープに描画される。
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(m_window->width(), m_window->height());
	}
	detail::exitIfDeviceLostAtStartup(m_device.get(), "バックバッファの初期化");

	createRenderPipeline(logicalSize.width, logicalSize.height);

	// パイプラインのビューポートを物理バックバッファサイズに設定する
	// (投影行列は論理サイズのまま、ビューポートだけ物理解像度にする)
	if (m_renderPipeline)
	{
		m_renderPipeline->setViewportSize(
			static_cast<float>(m_window->width()),
			static_cast<float>(m_window->height()));
	}

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
		/// Screen の 3D facade (s.drawMesh) も同じレンダラーへ繋ぐ。
		/// これで MITIRU_GAME の draw(Screen&) から直接 3D が描ける。
		m_screen->setRenderer3D(m_renderer3D.get());
	}

	initializeRmlUi(config);

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

	detail::exitIfDeviceLostAtStartup(m_device.get(), "描画パイプラインの初期化");

	/// HTTP API サーバーの初期化 (設定で有効な場合のみ)
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
			if (m_frameBodyActive)
			{
				debug::verboseOnce("engine.tick.nested",
					"フレームを描いている途中に窓の移動かリサイズの通知が来たので、その通知での描画を飛ばしました。");
				return;
			}
			if (!m_window->shouldClose() && !m_shouldStop.load())
			{
				tickOneFrame();
			}
		});

		/// drag 終了時に、modal 中 defer されていた logical resize を flush
		win32->setModalResizeEndCallback([this] {
			// フレームの途中ならバックバッファを差し替えない。次のフレームの頭で flushResizeDeferredFromFrameBody が適用する
			if (m_frameBodyActive) { return; }
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

	// 最後に present したフレームを GPU が描き終えてから後始末に入る (#78)
	if (m_device)
	{
		m_device->waitForGpu();
	}

	/// ループ終了後: ゲームに後処理の機会を与える
	game.onExit();

	/// ループ終了後に HTTP サーバーをシャットダウンする
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
		m_screen->applyExpectedSprites(config.expectedSprites);
		m_screen->setSpriteResolver(&render::SpriteCache::resolve, &m_spriteCache);

		/// headless モードではソフトウェアフレームバッファを自動有効化
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

		/// 固定タイムステップで更新 (stepFrames はヘッドレス用なので
		/// 各フレーム = 1 固定ステップとして扱う)。Listener フック (1-6) は
		/// tickFixedUpdatePhase (Engine_Frame.hpp) と同じく 1 固定ステップごとに発火する。
		dispatchBeforeUpdate();
		game.update(kFixedDt);

		/// シーンマネージャーが設定されている場合、現在シーンを更新
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onUpdate(kFixedDt);
		}

		dispatchAfterUpdate();

		m_screen->resetDrawCallCount();
		m_screen->clear();
		game.draw(*m_screen);

		/// シーンマネージャーが設定されている場合、現在シーンを描画
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onDraw(*m_screen);
		}

		/// ソフトウェアフレームバッファ有効時は present() でラスタライズ
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
	m_screen->applyExpectedSprites(config.expectedSprites);
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
		m_screen->setRenderer3D(m_renderer3D.get());
	}

	std::vector<std::uint8_t> capturedPixels;

	for (int f = 0; f < frameCount; ++f)
	{
		m_inputState.beginFrame();
		m_window->pollEvents();

		/// 固定タイムステップで更新 (runAndCapture はキャプチャ用なので
		/// 各フレーム = 1 固定ステップとして扱う)
		game.update(kFixedDt);

		m_screen->resetDrawCallCount();
		m_screen->clear();

		const bool renderer3DActive = m_renderer3D && m_renderer3D->isInitialized();
		if (m_device) m_device->beginFrame();
		game.draw(*m_screen);

		// 最後のフレーム: Present 前にキャプチャする
		// (Present 後は currentBackBufferIndex が進むため、描画済みバッファを読めなくなる)
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
