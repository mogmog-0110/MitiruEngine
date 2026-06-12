// mitiru::Engine の detail header — 直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/TracyZones.hpp>

// ── per-frame ループ本体と screen capture の class 外定義 ──────

MITIRU_INLINE std::vector<std::uint8_t> mitiru::Engine::capture() const
{
	if (m_screen && m_screen->hasSoftwareFramebuffer())
	{
		// gating 中 (#53) に未ラスタライズのフレームを読まれたら、次フレームの
		// ラスタライズを要求しておく (連続 polling 消費者は 1 フレーム遅れで追従)。
		if (!m_screen->softwareFbActive())
		{
			m_screen->requestSwRasterizeNext();
		}
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

	// Host hook — 通常 `mitiru_host --watch` がここで DLL の mtime を polling し、
	// source 変更時に Engine::reloadModule() を起こす。per-frame state に触れる前
	// なので安全に発火できる。
	if (m_loopConfig && m_loopConfig->onFrameStart)
	{
		m_loopConfig->onFrameStart(*this);
	}

	// sprite ホットリロード: ~0.5 秒に 1 回 (30 frame 毎) mtime を poll する。
	// 毎フレーム stat しない。常時有効 (watch モード限定にしない。stat は数個で害なし)。
	if (++m_spritePollCounter % 30 == 0)
	{
		m_spriteCache.pollReload();
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
#ifdef _WIN32
	m_gamepad.update(); // XInput を毎フレーム 1 回ポーリング (#12, edge 検出は内部 prev/curr)
#endif
	m_sdlGamepad.update(); // SDL_GameController (#32) — DS4/DS5 等。SDL2 無し時は no-op

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
		// gamepad の edge (prev/curr) も fixed tick で前進させ、keyboard と cadence を揃える。
		// これで render rate と update rate が独立でも just-pressed の取りこぼし/多重消費が起きない。
#ifdef _WIN32
		m_gamepad.endTick();
#endif
		m_sdlGamepad.endTick();
		// tint 残量を fixed step で減衰 (#31)。決定論的に動く。
		if (m_screen) { m_screen->advanceTint(kFixedDt); }
		m_accumulator -= kFixedDt;
		++steps;
	}
}

MITIRU_INLINE void mitiru::Engine::tickRenderPhase()
{
	MITIRU_ZONE_NAMED("Engine::Render");
	auto& game = *m_loopGame;

	// SW-FB 観測フレーム gating (#53): capture が読むフレームだけ CPU ラスタライズする。
	// host の --capture-every N は onFrameStart (描画前) で前フレームを読むため、
	// 「次の host frame で読まれる」描画フレーム = frameNumber() % N == 0 が観測対象。
	// (frameNumber は直前の tickFixedUpdatePhase の tick() で +1 済み = 描画フレーム+1)
	if (m_screen && m_screen->hasSoftwareFramebuffer())
	{
		const int every = m_config.swRasterizeEvery;
		bool active = true;
		if (every != 1)
		{
			const bool wanted = m_screen->consumeSwRasterizeRequest();
			active = wanted ||
				(every > 1 && (m_clock->frameNumber() % static_cast<std::uint64_t>(every)) == 0);
		}
		m_screen->setSoftwareFbActive(active);
	}

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
	// device の ClearRenderTargetView は frame 頭で screen->clearColor() を sync して使う。
	// DLL の draw() 内 `screen->clear(色)` は clearColor を更新し、それが「次フレーム頭」で
	// device に反映される (1 フレーム遅れだが体感できない)。clearColor の初期値は
	// EngineConfig::backgroundColor で、Engine::run の起動時に一度だけ設定する。
	// → ゲームが clear() を呼べばその色が背景になり、呼ばなければ config の既定が残る。
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

#ifdef _WIN32
	// ローファイ・ポストFX: 有効時はゲーム描画を低解像オフスクリーン RT へ向ける。
	if (m_config.loFi.enabled && m_renderPipeline)
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			if (!m_loFiTarget) m_loFiTarget = std::make_unique<gfx::Dx12LoFiTarget>();
			if (m_loFiTarget->ensure(dx12->nativeDevice(), dx12->commandQueue(),
				m_config.loFi.internalWidth, m_config.loFi.internalHeight))
			{
				const auto& cc = m_screen->clearColor();
				const float clear[4] = { cc.r, cc.g, cc.b, cc.a };
				m_loFiTarget->beginFrame(dx12->getSwapChain(), clear);
				m_renderPipeline->setViewportSize(
					m_config.loFi.internalWidth, m_config.loFi.internalHeight);
			}
		}
	}
#endif

	game.draw(*m_screen);

	// debug overlay 削除済み (マウス座標問題は解決)

	if (m_sceneManager && m_sceneManager->currentScene())
	{
		m_sceneManager->currentScene()->onDraw(*m_screen);
	}

	// 全画面 tint オーバーレイ (#31)。被弾点滅 / フラッシュ / グレー化等。
	// game/scene draw の末尾で重ねるので 2D / 3D どちらの上にも乗る。
	m_screen->renderTint();

	// ビルドエラー帯 (mitiru watch): tint / 演出 FX より後 = 最前面。CLI がビルド
	// 失敗時に書くエラーファイルを ~0.5 秒毎に poll し、存在する間だけ上部に帯を
	// 描く。直して保存 → ビルド成功でファイルが消え、次の poll で帯も消える。
	if (m_errorBanner.enabled())
	{
		const double nowSec = std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		m_errorBanner.poll(nowSec);
		m_errorBanner.drawTo(*m_screen);
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

#ifdef _WIN32
	// ローファイ・ポストFX: 低解像 RT を量子化+Bayerディザしながら実バックバッファへ拡大。
	if (m_config.loFi.enabled && m_loFiTarget && m_loFiTarget->ready() && m_window)
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			render::lofi::LoFiParamsCB p;
			p.texW = static_cast<float>(m_config.loFi.internalWidth);
			p.texH = static_cast<float>(m_config.loFi.internalHeight);
			p.bitsR = static_cast<float>(m_config.loFi.colorBitsR);
			p.bitsG = static_cast<float>(m_config.loFi.colorBitsG);
			p.bitsB = static_cast<float>(m_config.loFi.colorBitsB);
			p.ditherStrength = m_config.loFi.ditherStrength;
			p.doQuantize = m_config.loFi.quantize ? 1.0f : 0.0f;
			p.doDither = m_config.loFi.dither ? 1.0f : 0.0f;
			const int fullW = m_window->width(), fullH = m_window->height();
			m_loFiTarget->resolve(dx12->getSwapChain(), fullW, fullH, p);
			if (m_renderPipeline) m_renderPipeline->setViewportSize(fullW, fullH); // viewport を戻す
		}
	}
#endif

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
		const auto& cc = m_screen->clearColor();
		const float clearRGBA[4] = { cc.r, cc.g, cc.b, cc.a };
		m_cefContext.composite(
			*dx12Dev,
			m_window->width(), m_window->height(), clearRGBA);
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
