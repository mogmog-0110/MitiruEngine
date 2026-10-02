// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/detail/FixedStepPlan.hpp>
#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/observe/Oracle.hpp>

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

// 以前はここで m_device だけを作り直していたが、2D パイプライン・Renderer3D_DX12・
// MSAA/LoFi・GPU パーティクルは旧デバイスのキューとコマンドリストを持ったまま残り、新しい
// スワップチェーンの RTV を旧デバイスのリストへ渡して nvwgf2umx.dll 内で 0xC0000005 になった (#77)。
// 全部品を作り直す経路は無いうえ、hang した NVIDIA ドライバは再起動まで同じアダプタでは戻らず、
// 作り直すと気づかないうちに iGPU へ切り替わる。なので起動中 (exit 4) と同じく、理由を出して終える。
// bug ring は CPU 側の記録なので、hang 直前の数秒を再生できるよう先に書き出す。
MITIRU_INLINE void mitiru::Engine::tickDeviceLossRecoveryPhase() noexcept
{
#ifdef _WIN32
	if (!gfx::Dx12Device::anyDeviceLost()) { return; }

	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	const bool unresponsive = dx12 != nullptr && dx12->isGpuUnresponsive();
	const bool savedRing = m_config.bugRingSeconds > 0.0f && observe::saveBugRing(this, "gpu_lost_");
	std::fprintf(stderr,
		"[mitiru] フレーム %llu で %s。描画資源をデバイスごと作り直す経路が無いので終了する (exit %d)。"
		"ドライバの再起動 (Win+Ctrl+Shift+B) か PC の再起動のあとで起動し直す%s\n",
		static_cast<unsigned long long>(frameNumber()),
		unresponsive ? "GPU が応答しなくなった" : "D3D12 デバイスが失われた",
		detail::kExitGpuLostDuringPlay,
		savedRing ? "。直前の入力は gpu_lost_<時刻>.mtrr に保存した" : "");
	std::fflush(stdout);
	std::fflush(stderr);
	std::_Exit(detail::kExitGpuLostDuringPlay);
#endif
}

MITIRU_INLINE void mitiru::Engine::tickOneFrame()
{
	MITIRU_ZONE_NAMED("Engine::Frame");
	/// フレームレートキャップ用: 前フレーム開始時刻
	const auto frameStart = std::chrono::steady_clock::now();

	// フレームアリーナ (2-1): このフレームの一時確保をここで丸ごと巻き戻す。
	frameArena().reset();

	// J7: device-lost 復旧はこのフレームの他のどの処理より先に見る (m_device を
	// 使う描画フェーズより前に、作り直すか諦めるかを確定させておく)。
	tickDeviceLossRecoveryPhase();

	// Host hook。通常 `mitiru_host --watch` がここで DLL の書き換えを見て、
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
	flushResizeDeferredFromFrameBody();

	// ここから present までは GPU のフレームを開いている。この間に何かが窓のメッセージを配り、
	// 枠を掴んだ modal ループの tick が同じバックバッファ番号で入れ子のフレームを始めると、
	// 実行中のアロケータと upload ring を Reset してしまう。tick 側はこの印を見て断る (#79)
	struct FrameBodyScope
	{
		bool& active;
		explicit FrameBodyScope(bool& flag) : active(flag) { active = true; }
		~FrameBodyScope() { active = false; }
		FrameBodyScope(const FrameBodyScope&) = delete;
		FrameBodyScope& operator=(const FrameBodyScope&) = delete;
	};
	const FrameBodyScope body(m_frameBodyActive);
	tickMouseScalingPhase();
	tickFixedUpdatePhase();
	tickRenderPhase();
	tickPresentPhase();
	tickUiComposite();
	if (!tickAutoCaptureAndEndFrame())
	{
		return;
	}
	tickHttpPollAndCap(frameStart);
}

/// フレームの途中に終わった modal ループのリサイズは、その場では当てずに (m_pendingResize) ここで当てる (#79)
MITIRU_INLINE void mitiru::Engine::flushResizeDeferredFromFrameBody()
{
#ifdef _WIN32
	if (m_pendingResizeW <= 0 || m_pendingResizeH <= 0) { return; }
	auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get());
	if (win32 == nullptr || win32->inModalLoop()) { return; }
	const int w = m_pendingResizeW;
	const int h = m_pendingResizeH;
	m_pendingResizeW = 0;
	m_pendingResizeH = 0;
	onWindowResize(w, h);
#endif
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
	/// 「update が走らないレンダーフレーム」(144Hz vsync + 60Hz update など) で
	/// 本メソッドを毎フレーム呼ぶと、KEYDOWN を curr に拾った直後の次フレームで
	/// prev=curr されて just-pressed が消える。prev 維持は while ループ末の
	/// endTick() に任せ、render rate と update rate を独立させる。
	m_window->pollEvents();
	applyInjectedInput();
	// headless (自動回し/CI) では物理パッドを開かない。SdlGamepadInput の lazy init が
	// DS4 を占有し、ユーザーが別アプリで使用中の実機入力を横取りしてしまうため。
	// 入力は --input-script / injected input が正であり、実デバイス不要。
	if (!m_config.headless)
	{
#ifdef _WIN32
		m_gamepad.update(); // XInput を毎フレーム 1 回ポーリング (#12, edge 検出は内部 prev/curr)
#endif
		m_sdlGamepad.update(); // SDL_GameController (#32) — DS4/DS5 等。SDL2 無し時は no-op
	}

	// DEBUG: pollEvents 直後のマウス座標を保存
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
	/// マウス座標を Screen 論理座標に変換する
	/// 重要: Win32Window が設定した RAW 座標を毎フレーム読み取り、
	/// スケーリング済み座標で上書きする。次フレームでは WM_MOUSEMOVE が
	/// 来れば RAW に戻るが、来なければ前フレームのスケーリング済み値が
	/// 残っている。そのため、RAW 座標を Win32Window から直接取得する。
	if (!m_screen || !m_window)
	{
		return;
	}

	const float windowW = static_cast<float>(m_window->width());
	const float windowH = static_cast<float>(m_window->height());
	const float screenW = static_cast<float>(m_screen->width());
	const float screenH = static_cast<float>(m_screen->height());

	// Win32Window が保持する RAW 座標を直接使う (スケーリング前の値)。
	// Win32 以外 (web 等) には RAW の持ち主が居ないので InputState の値をそのまま使う。
#ifdef _WIN32
	auto* w32 = dynamic_cast<Win32Window*>(m_window.get());
	const float rawX = w32 ? w32->m_lastMouseX : m_inputState.mousePosition().first;
	const float rawY = w32 ? w32->m_lastMouseY : m_inputState.mousePosition().second;
#else
	const float rawX = m_inputState.mousePosition().first;
	const float rawY = m_inputState.mousePosition().second;
#endif

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

	/// 壁時計 dt を取得し、スパイラルオブデス防止でキャップ
	const float rawDt = m_clock->tick();
	const float frameTime = std::min(rawDt, kMaxDelta);
	m_accumulator += frameTime * m_config.timeScale;   // timeScale はステップ数を増減 (dt は固定)

	/// 固定タイムステップで更新 (最大スキップ制限付き)
	///
	/// 入力のエッジ管理は完全に `endTick()` で行う。理由は次のとおり。
	///   - render rate と update rate が独立 (144Hz vsync + 60Hz update 等)
	///     のとき、`beginFrame()` を render-frame 頭で呼ぶと「update が走らない
	///     フレーム」で prev=curr されて KEYDOWN エッジが消える (ENG-102)。
	///   - 1 render frame に複数 tick 走るケースでも、各 tick 末で endTick が
	///     prev=curr を進めれば「1 物理入力 = 1 just-pressed observation」を
	///     保証できる。
	const int stepCap = m_config.deterministic
	                        ? detail::deterministicStepCap(kMaxFrameSkip, m_config.timeScale)
	                        : kMaxFrameSkip;
	const int planned = detail::planFixedSteps(m_accumulator, kFixedDt, stepCap);
	for (int steps = 0; steps < planned; ++steps)
	{
		// Listener フック (1-6): host が独自に呼んでいた dispatchBeforeUpdate/After を
		// ここへ一本化した (二重発火防止)。fixed-step ごと (= game.update() 1 回ごと) に発火する。
		dispatchBeforeUpdate();

		game.update(kFixedDt);

		/// シーンマネージャーが設定されている場合、現在シーンを更新
		if (m_sceneManager && m_sceneManager->currentScene())
		{
			m_sceneManager->currentScene()->onUpdate(kFixedDt);
		}

		dispatchAfterUpdate();

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
	}
	// 上限に当たって残った時間はスローモーションとして perf に出す (2-4)。非決定論のときは
	// 残りを捨てて追いかけをやめる (決定論では cap が timeScale+2 以上なので通常ここに来ない。
	// 来ても捨てると replay の入力列とずれるので数えるだけ)。
	m_droppedFixedSteps = detail::countDroppedFixedSteps(m_accumulator, kFixedDt);
	if (m_droppedFixedSteps > 0 && !m_config.deterministic)
	{
		m_accumulator -= static_cast<float>(m_droppedFixedSteps) * kFixedDt;
	}

	// N1/P1/P14: 組込オラクル評価 + 常時バグリング更新。GameMemory が flat POD (reflect 申告済み)
	// のときだけ意味を持つ。私有メンバ (m_moduleMemoryRing 等) へのアクセスが要るためこの関数の
	// 中に直接書く。フレームをまたぐ状態 (ring / 直近ハッシュ等) は observe/Oracle.hpp 側の
	// Engine* キー map に持たせ、Engine.hpp のクラス宣言そのものは変更しない (D11 の this キー
	// map と同じ手法)。
	if (m_config.oracleEnabled && m_moduleMemorySize > 0 && m_moduleMemory != nullptr)
	{
		const auto frameNo = static_cast<std::uint32_t>(m_clock->frameNumber());
		observe::OracleRing& ring = observe::oracleRingFor(this);
		ring.setMachineLogEnabled(m_config.oracleMachineLog);

		observe::checkFieldsOracle(m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize, frameNo, ring,
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());

		observe::OracleTimeState& oracleState = observe::oracleStateFor(this);
		observe::checkFrameTimeSpikeOracle(rawDt * 1000.0f, frameNo, oracleState, ring);

		// 部分状態の game (MITIRU_GAME_OBJECTS) は場面の中身が GameMemory の外で動く。進行データが
		// 何秒も変わらないのは正常なので、bytes の停滞を「update が止まっている」とは読まない。
		if (m_moduleInputSnapshot && !modulePartialState())
		{
			observe::checkStagnationOracle(
				static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
				reinterpret_cast<const std::uint8_t*>(m_moduleInputSnapshot.get()),
				static_cast<std::uint32_t>(sizeof(module::InputSnapshot)),
				frameNo, kFixedDt, m_config.oracleStagnantSeconds, oracleState, ring);
		}

		if (m_moduleHost)
		{
			std::int32_t invCount = 0;
			if (auto invFn = m_moduleHost->invariantsFn())
			{
				// 不変条件の述語も game のコード。落ちたら live の callback と同じく止める。
				guardModuleCallback("invariants", [&] {
					const module::InvariantDescriptor* invs = invFn(&invCount);
					observe::checkInvariantsOracle(invs, invCount, m_moduleMemory, frameNo, ring);
				});
			}
		}

		// 決定論オラクル (e/P14、既定 OFF): K フレームごとに、K フレーム前の GameMemory +
		// その後の記録済み入力で再シミュレーションし現在の GameMemory と memcmp する
		// (既存の resim ring = m_moduleMemoryRing/m_moduleInputRing をそのまま流用する)。
		if (m_config.oracleDeterminism)
		{
			static std::unordered_map<const void*, std::uint32_t> s_detTick;  // D11 と同じ手法
			std::uint32_t& tick = s_detTick[this];
			++tick;
			const std::uint32_t k = (m_config.oracleDeterminismEveryFrames > 0)
				? m_config.oracleDeterminismEveryFrames : 120;
			const std::size_t memFrames = m_moduleMemoryRing.size();
			const std::size_t inFrames  = m_moduleInputRing.size();
			if (tick >= k && k > 0 && k < memFrames && k <= inFrames)
			{
				tick = 0;
				const std::uint8_t* past = m_moduleMemoryRing.at(k);
				std::vector<module::InputSnapshot> pastInputs(k);
				bool haveInputs = (past != nullptr);
				for (std::uint32_t i = 0; haveInputs && i < k; ++i)
				{
					const std::uint8_t* snap = m_moduleInputRing.at(k - 1 - i);
					if (snap == nullptr) { haveInputs = false; break; }
					std::memcpy(&pastInputs[i], snap, sizeof(module::InputSnapshot));
				}
				if (haveInputs)
				{
					std::vector<std::uint8_t> scratchFallback;
					auto* scratch = static_cast<std::uint8_t*>(frameArena().alloc(m_moduleMemorySize));
					if (scratch == nullptr)
					{
						scratchFallback.resize(m_moduleMemorySize);
						scratch = scratchFallback.data();
					}
					// 再シミュレーションで落ちたら、書きかけの live bytes は停止の処理が直前の記録へ戻す。
					guardModuleCallback("on_update (determinism oracle)", [&] {
						observe::checkDeterminismOracle(m_moduleApi,
							static_cast<std::uint8_t*>(m_moduleMemory), m_moduleMemorySize, past,
							pastInputs.data(), static_cast<int>(k), scratch, frameNo, ring,
							[this](std::uint32_t off) { return queryModuleWriteBlame(off); },
							m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount());
					});
				}
			}
		}
	}

	// 常時バグリング (P1): 短い ring に毎フレーム積み、host が要求したら
	// bug_<timestamp>.mtrr として保存する。oracle の on/off とは独立に回す。
	if (m_config.bugRingSeconds > 0.0f)
	{
		const auto bugFrames = static_cast<std::uint32_t>(m_config.bugRingSeconds / kFixedDt) + 1;
		observe::pushBugRingFrame(this, m_moduleMemory, m_moduleMemorySize,
			m_moduleInputSnapshot.get(), static_cast<std::uint32_t>(sizeof(module::InputSnapshot)),
			bugFrames);
	}
	if (m_config.bugRingSaveRequested)
	{
		m_config.bugRingSaveRequested = false;
		if (!observe::saveBugRing(this))
		{
			debug::warnOnce("oracle.bugring.save_failed",
				"bug ring の .mtrr 保存に失敗した (出力先ディレクトリの書き込み権限か、まだ記録が無い)");
		}
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
	// 2D のみのゲーム: game.draw() は Screen に蓄積のみ
	// 3D ゲーム: game.draw() 内で Renderer3D::beginFrame() がバックバッファをクリア+描画
	//
	// game.draw() は常に device->beginFrame() の後に呼ぶ。
	// Renderer3D::beginFrame() は device->beginFrame() の後なら安全。
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

	// device->beginFrame(): DX12 ではフェンス待機+アロケータリセット
	if (m_renderer3D) m_renderer3D->resetFrameActive();
	if (m_device)
	{
		m_device->beginFrame();

		/// ポストプロセスが有効ならオフスクリーン RT にリダイレクトする
		m_device->beginPostProcess();
	}

#ifdef _WIN32
	// ローファイ・ポスト FX: 有効時はゲーム描画を低解像オフスクリーン RT へ向ける。
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

#ifdef _WIN32
	// 2D MSAA: pure-2D フレームのとき、ゲーム描画を MSAA 中間 RT へ向け、
	// present 後に実バックバッファへ resolve する。回転した塗り図形・三角形・多角形・
	// 線・円・スプライトの輪郭を平滑化する (サンプル数は Dx12MsaaTarget::kSampleCount)。
	//
	// override は必ず game.draw() の *前* に張る: Screen は switchToTexture /
	// pushClipRect / setBlendMode で draw() 中に textured/sprite バッチを即 flush する
	// ため、present 時点で override を張ると draw() 中フラッシュ分が実バックバッファへ落ち、
	// 後段の resolve に上書きされて消える (実スワップチェーンでスプライトが全消しになる回帰)。
	//
	// gate: config + !lo-fi + !postprocess + DX12 + 「前フレームが 3D 未使用」。当フレームの
	// 3D 使用可否は draw() 前には未知なので前フレームで代理判定する。万一この frame で 3D が
	// 使われても、renderer3D->endFrame() の前 (下記) で override を外し resolve を諦める安全策で
	// 守る。MITIRU_MSAA2D=0 で実行時に無効化 (トラブルシュート / 明示 1x)。
	m_msaa2dActiveThisFrame = false;
	static const bool s_msaa2dEnvOff = [] {
		const char* e = std::getenv("MITIRU_MSAA2D");
		return e != nullptr && e[0] == '0' && e[1] == '\0';
	}();
	if (m_config.antialiasing2D && !s_msaa2dEnvOff && !m_prevFrame3DUsed
		&& !m_config.loFi.enabled
		&& (!m_postProcess || !m_postProcess->isEnabled()) && m_window && m_screen)
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			if (!m_msaaTarget) { m_msaaTarget = std::make_unique<gfx::Dx12MsaaTarget>(); }
			const int bw = m_window->width();
			const int bh = m_window->height();
			if (m_msaaTarget->ensure(dx12->nativeDevice(), dx12->commandQueue(), bw, bh))
			{
				const auto& cc = m_screen->clearColor();
				const float clear[4] = { cc.r, cc.g, cc.b, cc.a };
				m_msaaTarget->beginFrame(dx12->getSwapChain(), clear);
				m_msaa2dActiveThisFrame = true;
			}
		}
	}
#endif

	game.draw(*m_screen);

	// ゴーストリプレイ (10-1): live の直後に半透明で重ねる。drawGhost は ghost 専用の
	// 別 Screen へ焼いてから合成するため、live 側の全画面クリアに上書きされない。
	// stepGhost 自体は host が onFrameStart で駆動する (Engine はファイル I/O を持たない)。
	// live 側はゴーストの存在を知らない。
	if (m_ghostMemory != nullptr)
	{
		drawGhost(*m_screen);
	}

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

	if (m_moduleFaulted)
	{
		debug::drawNoticeCard(*m_screen, "ゲームが停止しました", m_moduleFaultLines,
		                      "コードを直して DLL をビルドし直すと、読み込み直してこのフレームから再開します");
	}

#ifdef _WIN32
	// 2D→3D 遷移フレーム安全策: 前フレームが 2D で MSAA override を張った直後に、この
	// フレームで 3D が使われた場合、renderer3D->endFrame() の resolve が backBuffer() を
	// dest にする。override が張られたままだと 4x MSAA RT を resolve dest にして失敗する。
	// override を実バックバッファへ戻し、この frame の 2D resolve は諦める (2D は次フレーム
	// から prev3D=true で MSAA を張らず 1x で正しく描く)。稀な遷移 1 フレームのみ。
	if (m_msaa2dActiveThisFrame && m_renderer3D && m_renderer3D->isFrameActive())
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			if (auto* sc = dx12->getSwapChain()) { sc->clearBackBufferOverride(); }
		}
		m_msaa2dActiveThisFrame = false;
	}
#endif

	// 3D facade (s.drawMesh / 直接 renderer3D 経路) が使われたフレームは、2D の蓄積が
	// 終わったここで endFrame する (MSAA resolve + tonemap + 2D Screen を overlay 合成)。
	// drawMesh が遅延 beginFrame するだけなので、フレームを閉じるのは engine の役目。
	// 閉じて GPU 実行する finalizeFrame は tickPresentPhase が呼ぶ。
	if (m_renderer3D && m_renderer3D->isFrameActive())
	{
		m_renderer3D->endFrame();
	}
}

MITIRU_INLINE void mitiru::Engine::tickPresentPhase()
{
	MITIRU_ZONE_NAMED("Engine::Present");
	if (!m_device)
	{
		return;
	}

	// 3D 描画が行われたフレームの処理
	const bool renderer3DUsed = m_renderer3D
		&& m_renderer3D->isFrameActive();

	// 次フレームの MSAA gate 用に「このフレームが 3D を使ったか」を控える。
	// (MSAA override は draw() より前に張る必要があり、その時点で当フレームの 3D
	//  使用可否は未知のため、前フレームの結果で判定する。tickRenderPhase 参照)
	m_prevFrame3DUsed = renderer3DUsed;

	if (!renderer3DUsed)
	{
		if (m_renderer3D && m_renderer3D->isInitialized())
		{
			m_device->resetRenderTargetFor2D();
		}
	}
	// 2D 描画を常に GPU 送信する
	// (3D 使用時でも HUD/UI オーバーレイが必要)
	if (!renderer3DUsed || !m_renderer3D->hasOverlaySupport())
	{
		if (renderer3DUsed)
		{
			m_device->resetRenderTargetFor2D();
		}
		m_screen->present();
	}

#ifdef _WIN32
	// 2D MSAA resolve: override を外し、4x MSAA → 実バックバッファへ解決する。
	// この後の UI 合成 / present は resolve 済みバックバッファに正しく乗る
	// (順序: 2D → resolve → UI 合成 → present)。override は draw() 前 (tickRenderPhase)
	// に張ってあるので、draw() 中に flush された textured/sprite バッチも MSAA RT に入り、
	// この resolve に含まれる (実スワップチェーンでスプライトが消える回帰の根治)。
	if (m_msaa2dActiveThisFrame && m_msaaTarget)
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			m_msaaTarget->resolve(dx12->getSwapChain());
		}
		m_msaa2dActiveThisFrame = false;
	}
#endif

	/// ポストプロセスチェーンを実行し、バックバッファに出力する
	m_device->endPostProcess();

	/// 3D レンダラーのコマンドリストを閉じて実行する
	if (m_renderer3D && m_renderer3D->isFrameActive())
	{
		m_renderer3D->finalizeFrame();
	}

	/// 3D フレームの HUD/2D は蓄積してあるのでここで replay する。
	/// lo-fi 有効時は resolve より前 = 低解像 RT の中へ描く (HUD も世界と同じ粒になる)。
	const bool overlayPending =
		(renderer3DUsed && m_renderer3D->hasOverlaySupport() && m_screen);
	bool overlayDone = false;
#ifdef _WIN32
	if (overlayPending && m_config.loFi.enabled && m_loFiTarget && m_loFiTarget->ready())
	{
		m_screen->present3DOverlay();
		overlayDone = true;
	}

	// ローファイ・ポスト FX: 低解像 RT を量子化+Bayer ディザしながら実バックバッファへ拡大。
	// 3D コマンドが lofi RT (override 先) に落ちてから resolve する必要があるためこの位置。
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
			p.soft = m_config.loFi.softUpscale ? 1.0f : 0.0f;
			p.viFilter = m_config.loFi.viFilter ? 1.0f : 0.0f;
			p.gamma = m_config.loFi.gamma;
			const int fullW = m_window->width(), fullH = m_window->height();
			m_loFiTarget->resolve(dx12->getSwapChain(), fullW, fullH, p);
			if (m_renderPipeline) m_renderPipeline->setViewportSize(fullW, fullH); // viewport を戻す
		}
	}
#endif

	if (overlayPending && !overlayDone)
	{
		m_screen->present3DOverlay();
	}
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

		// ニューラル現像 (M3): present 直前 = backbuffer に最新フレームがあり PRESENT 状態。
		// 要求があれば直前フレームを readback → ONNX+DirectML で 2D 化する (auto-test
		// キャプチャと同じ安全境界)。要求が無ければ内部で即 return するので毎フレーム無害。
		if (m_renderer3D)
		{
			m_renderer3D->tickDevelop();
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

	/// HTTP API サーバーのポーリング (リクエスト処理)
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
