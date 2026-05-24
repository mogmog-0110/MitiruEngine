// Detail header for mitiru::Engine - do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/TracyZones.hpp>

// ── Per-frame loop body and screen capture out-of-class definitions ──────

MITIRU_INLINE std::vector<std::uint8_t> mitiru::Engine::capture() const
{
	if (m_screen && m_screen->hasSoftwareFramebuffer())
	{
		return m_screen->pixels();
	}
	if (!m_device || !m_screen)
	{
		return {};
	}
	// バックバッファはウィンドウクライアントサイズ。論理サイズではない。
	const int bw = m_window ? m_window->width()  : m_screen->width();
	const int bh = m_window ? m_window->height() : m_screen->height();
	return m_device->readPixels(bw, bh);
}

// ── tickOneFrame は per-phase helper の薄いシーケンサとして再構成された ──
// 各 helper はエンジン開発ルールに従い 50 行以内に収めること。
// 早期 return が必要なフェーズ (Emscripten 終了 / autoTestExitAfter) は
// bool を返し、tickOneFrame() が return を担う。

MITIRU_INLINE void mitiru::Engine::tickOneFrame()
{
	MITIRU_ZONE_NAMED("Engine::Frame");
	/// フレームレートキャップ用: 前フレーム開始時刻
	const auto frameStart = std::chrono::steady_clock::now();

	// Host hook — typically `mitiru_host --watch` polls DLL mtime here and
	// triggers Engine::reloadModule() when the source changes. Safe to fire
	// before any per-frame state is touched.
	if (m_loopConfig && m_loopConfig->onFrameStart)
	{
		m_loopConfig->onFrameStart(*this);
	}

	if (!tickInputPollPhase())
	{
		return;
	}
	tickMouseScalingPhase();
	tickFixedUpdatePhase();
	tickRenderPhase();
	tickPresentPhase();
	tickCefComposite();
	if (!tickAutoCaptureAndEndFrame())
	{
		return;
	}
	tickHttpPollAndCap(frameStart);
}

MITIRU_INLINE bool mitiru::Engine::tickInputPollPhase()
{
	MITIRU_ZONE_NAMED("Engine::Input");
#ifdef __EMSCRIPTEN__
	if (m_window->shouldClose() || m_shouldStop.load())
	{
		emscripten_cancel_main_loop();
		return false;
	}
#endif

	/// 重要: ここで m_inputState.beginFrame() を呼んではいけない (ENG-102)。
	/// beginFrame() は prev=curr でエッジを消す。pollEvents が curr を更新する
	/// 「update が走らないレンダーフレーム」(144Hz vsync + 60Hz update など)で
	/// 本メソッドを毎フレーム呼ぶと、KEYDOWN を curr に拾った直後の次フレームで
	/// prev=curr されて just-pressed が消える。prev 維持は while ループ末の
	/// endTick() に任せ、render rate と update rate を独立させる。
	m_window->pollEvents();
	applyInjectedInput();

	// DEBUG: pollEvents直後のマウス座標を保存
	{
		auto [px, py] = m_inputState.mousePosition();
		m_dbgPostPollMx = px;
		m_dbgPostPollMy = py;
	}
	return true;
}

MITIRU_INLINE void mitiru::Engine::tickMouseScalingPhase()
{
	MITIRU_ZONE_NAMED("Engine::MouseScaling");
	/// マウス座標をScreen論理座標に変換する
	/// 重要: Win32Windowが設定したRAW座標を毎フレーム読み取り、
	/// スケーリング済み座標で上書きする。次フレームではWM_MOUSEMOVEが
	/// 来ればRAWに戻るが、来なければ前フレームのスケーリング済み値が
	/// 残っている。そのため、RAW座標をWin32Windowから直接取得する。
	if (!m_screen || !m_window)
	{
		return;
	}

	const float windowW = static_cast<float>(m_window->width());
	const float windowH = static_cast<float>(m_window->height());
	const float screenW = static_cast<float>(m_screen->width());
	const float screenH = static_cast<float>(m_screen->height());

	// Win32Windowが保持するRAW座標を直接使う (スケーリング前の値)
	auto* w32 = dynamic_cast<Win32Window*>(m_window.get());
	const float rawX = w32 ? w32->m_lastMouseX : m_inputState.mousePosition().first;
	const float rawY = w32 ? w32->m_lastMouseY : m_inputState.mousePosition().second;

	m_dbgWindowW = windowW;
	m_dbgWindowH = windowH;
	m_dbgScreenW = screenW;
	m_dbgScreenH = screenH;
	m_dbgRawMx = rawX;
	m_dbgRawMy = rawY;

	if (windowW > 0 && windowH > 0)
	{
		const float scaledX = rawX * screenW / windowW;
		const float scaledY = rawY * screenH / windowH;
		m_inputState.setMousePosition(scaledX, scaledY);
	}

	auto [sx, sy] = m_inputState.mousePosition();
	m_dbgScaledMx = sx;
	m_dbgScaledMy = sy;
}

MITIRU_INLINE void mitiru::Engine::tickFixedUpdatePhase()
{
	MITIRU_ZONE_NAMED("Engine::FixedUpdate");
	auto& game = *m_loopGame;

	/// 壁時計dtを取得し、スパイラルオブデス防止でキャップ
	const float rawDt = m_clock->tick();
	const float frameTime = std::min(rawDt, kMaxDelta);
	m_accumulator += frameTime;

	/// 固定タイムステップで更新 (最大スキップ制限付き)
	///
	/// 入力のエッジ管理は完全に `endTick()` で行う。理由:
	///   - render rate と update rate が独立 (144Hz vsync + 60Hz update 等)
	///     のとき、`beginFrame()` を render-frame 頭で呼ぶと「update が走らない
	///     フレーム」で prev=curr されて KEYDOWN エッジが消える (ENG-102)。
	///   - 1 render frame に複数 tick 走るケースでも、各 tick 末で endTick が
	///     prev=curr を進めれば「1 物理入力 = 1 just-pressed observation」を
	///     保証できる。
	int steps = 0;
	while (m_accumulator >= kFixedDt && steps < kMaxFrameSkip)
	{
		game.update(kFixedDt);

		/// シーンマネージャーが設定されている場合、現在シーンを更新
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onUpdate(kFixedDt);
		}

		m_inputState.endTick();
		m_accumulator -= kFixedDt;
		++steps;
	}
}

MITIRU_INLINE void mitiru::Engine::tickRenderPhase()
{
	MITIRU_ZONE_NAMED("Engine::Render");
	auto& game = *m_loopGame;

	// =====================================================================
	// 描画パイプライン (統一パス)
	//
	// 2Dのみのゲーム: game.draw()はScreenに蓄積のみ
	// 3Dゲーム: game.draw()内でRenderer3D::beginFrame()がバックバッファをクリア+描画
	//
	// game.draw()は常にdevice->beginFrame()の後に呼ぶ。
	// Renderer3D::beginFrame()はdevice->beginFrame()の後なら安全。
	// =====================================================================

	m_screen->resetDrawCallCount();
	// config.backgroundColor を渡すことで、host 側で hello_game 等の bg を
	// 統一できる。DLL 内 `screen->clear(...)` は m_clearColor を更新するだけ
	// で device の ClearRenderTargetView には届かない (frame 頭で sync された
	// 値が使われる) ので、ここで明示的に config の値を流す。
	m_screen->clear(m_config.backgroundColor);

	// screen.clear()のクリア色をGPUデバイスに同期する
	if (m_device && m_screen)
	{
		const auto& cc = m_screen->clearColor();
		m_device->setClearColor(cc.r, cc.g, cc.b, cc.a);
	}

	// device->beginFrame(): DX12ではフェンス待機+アロケータリセット
	if (m_renderer3D) m_renderer3D->resetFrameActive();
	if (m_device)
	{
		m_device->beginFrame();

		/// ポストプロセスが有効ならオフスクリーンRTにリダイレクトする
		m_device->beginPostProcess();
	}

	game.draw(*m_screen);

	// デバッグオーバーレイ削除済み (マウス座標問題は解決)

	if (m_sceneManager && m_sceneManager->currentScene())
	{
		m_sceneManager->currentScene()->onDraw(*m_screen);
	}
}

MITIRU_INLINE void mitiru::Engine::tickPresentPhase()
{
	MITIRU_ZONE_NAMED("Engine::Present");
	if (!m_device)
	{
		return;
	}

	// 3D描画が行われたフレームの処理
	const bool renderer3DUsed = m_renderer3D
		&& m_renderer3D->isFrameActive();
	if (!renderer3DUsed)
	{
		if (m_renderer3D && m_renderer3D->isInitialized())
		{
			m_device->resetRenderTargetFor2D();
		}
	}
	// 2D描画を常にGPU送信する
	// (3D使用時でもHUD/UIオーバーレイが必要)
	if (!renderer3DUsed || !m_renderer3D->hasOverlaySupport())
	{
		if (renderer3DUsed)
		{
			m_device->resetRenderTargetFor2D();
		}
		m_screen->present();
	}

	/// ポストプロセスチェーンを実行し、バックバッファに出力する
	m_device->endPostProcess();

	/// 3Dレンダラーのコマンドリストを閉じて実行する
	if (m_renderer3D && m_renderer3D->isFrameActive())
	{
		m_renderer3D->finalizeFrame();
	}
}

MITIRU_INLINE void mitiru::Engine::tickCefComposite()
{
	MITIRU_ZONE_NAMED("Engine::CefComposite");
	if (!m_device || !m_cefContext.isInitialized())
	{
		return;
	}

	/// CEF UI レイヤーをバックバッファに重ねて描画する
	/// (2D/3D/PostFX の後、present の前)
	m_cefContext.doMessageLoopWork();
	m_cefContext.handleInput(m_inputState);
	if (m_cefContext.hasDirtyFrame())
	{
		m_cefContext.upload();
	}
#if defined(_WIN32) && defined(MITIRU_HAS_CEF)
	if (auto* dx12Dev = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
	{
		m_cefContext.composite(
			*dx12Dev,
			m_window->width(), m_window->height());
	}
#endif
}

MITIRU_INLINE bool mitiru::Engine::tickAutoCaptureAndEndFrame()
{
	MITIRU_ZONE_NAMED("Engine::AutoCapture");
	const auto& config = *m_loopConfig;

	if (m_device)
	{
		/// 自律テストモード: present 前にキャプチャする (present 後は
		/// バックバッファがフリップして空のバッファを読んでしまう)
		if (config.autoTestMode
			&& m_autoTestFrameCount + 1 >= config.autoTestFrames)
		{
			m_device->waitForGpu();
			saveAutoTestScreenshot(config.autoTestOutputDir);
			saveAutoTestReport(config.autoTestOutputDir, config);
			m_autoTestCaptured = true;
		}

		m_device->endFrame();
	}

	/// 自律テストモード: 指定フレーム後にレポート出力+終了
	if (config.autoTestMode)
	{
		++m_autoTestFrameCount;
		if (m_autoTestCaptured && config.autoTestExitAfter)
		{
#ifdef __EMSCRIPTEN__
			emscripten_cancel_main_loop();
			return false;
#else
			m_shouldStop = true;
			return false;
#endif
		}
	}
	return true;
}

MITIRU_INLINE void mitiru::Engine::tickHttpPollAndCap(
	const std::chrono::steady_clock::time_point& frameStart)
{
	MITIRU_ZONE_NAMED("Engine::HttpPoll");
	const auto& config = *m_loopConfig;

	/// HTTP APIサーバーのポーリング (リクエスト処理)
	if (m_httpServer && m_httpServer->isRunning())
	{
		m_httpServer->poll();
	}

	/// フレームレートキャップ (vsync OFF + targetFps>0 のときのみ)
	/// vsync ON 時は SwapChain の Present(1) でブロックされるので不要
	if (!config.vsync && config.targetFps > 0)
	{
		const auto target = std::chrono::microseconds(
			1000000 / config.targetFps);
		const auto elapsed = std::chrono::steady_clock::now() - frameStart;
		if (elapsed < target)
		{
			std::this_thread::sleep_for(target - elapsed);
		}
	}
}
