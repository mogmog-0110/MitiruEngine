// mitiru::Engine の detail header - 直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Adapter.hpp
/// @brief Engine の module per-frame signal flow の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::runModule` (stack-local ModuleAdapter) と、ADR 0005
/// (Host-Game C-only signal flow) に準拠した per-frame signal flow:
///   - InputSnapshot 構築 (host が input + action events を POD に詰める)
///   - FrameIntents drain (DLL の要求を host が解釈して engine 操作に変換)
///   - 必要なら StateStore + SharedSnapshot を遅延生成
/// load / unload / ring 記録は Engine_Module_Loader.hpp 側。

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include <mitiru/cef/StateStore.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/DebugPrint.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/GameMemorySave.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SoundIntentRouter.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/SeriesMarkers.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

// ── Free helper 群 (file-local、Engine の method ではない) ──────────────────
namespace mitiru::module::detail
{

/// @brief `src` を固定長 buffer に copy する。source が buffer より長くても
///        UB を起こさず null 終端 + 切り詰めする。
template <std::size_t N>
inline void copyBounded(char (&dst)[N], const std::string& src) noexcept
{
	const std::size_t cap = N > 0 ? N - 1 : 0;
	const std::size_t n   = src.size() < cap ? src.size() : cap;
	if (n > 0) { std::memcpy(dst, src.data(), n); }
	if (N > 0) { dst[n] = '\0'; }
}

/// @brief 固定長 buffer の bounded strlen。DLL からの wire buffer は null 終端を
///        信頼しない (exportedInspectables と同基準の境界防御)。
template <std::size_t N>
[[nodiscard]] inline std::size_t boundedLen(const char (&s)[N]) noexcept
{
	std::size_t l = 0;
	while (l < N && s[l] != '\0') { ++l; }
	return l;
}

}  // namespace mitiru::module::detail

// ── runModule (stack-local adapter Game) ───────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::runModule(
	const std::filesystem::path& modulePath, const EngineConfig& configIn)
{
	if (!loadModule(modulePath))
	{
		// 黙って return すると「窓が出ず exit 0」で原因不明になる (#hello-game)。
		// 理由を明示し false を返す → host は非ゼロ終了 → ランチャー .bat が pause する。
		std::fprintf(stderr,
			"mitiru: ゲームモジュールの読み込みに失敗しました: %s\n"
			"  理由: %s\n"
			"  ヒント: その DLL に MITIRU_GAME(YourType) の入口がありますか? 旧 mitiru::Game 継承＋\n"
			"  自前 main() の Mode-A ゲームは現行 host (DLL モジュール方式) では動きません。\n",
			modulePath.string().c_str(),
			m_moduleHost ? m_moduleHost->lastError().c_str() : "module host 未生成");
		return false;
	}

	// stack-local な Game adapter。既存の engine main loop を ADR 0005 の
	// signal flow へ橋渡しする:
	//   - update(): input を snapshot、intents を zero、on_update、intents を drain
	//   - draw():   Screen pointer をそのまま on_draw へ渡す
	class ModuleAdapter : public Game
	{
	public:
		explicit ModuleAdapter(Engine* engine) noexcept : m_engine(engine) {}

		void update(float dt) override
		{
			m_engine->ensureModuleCefBindings();
			// 過去フレームで静止 (scrub-hold): 別窓のバーで過去を選んでいる間は、その
			// フレームを毎フレーム復元して止める。ゲームを前進させず記録もしない。
			if (m_engine->applyScrubHold()) { return; }
			// 実効 dt (pause/hitStop gating) も snapshot 構築時に書き込む (v21、H-3)。
			m_engine->buildModuleInputSnapshot(dt);
			m_engine->applyResimInputOverride();  // resim 中は記録入力で上書き (ADR 0021)
			m_engine->zeroModuleFrameIntents();

			const auto& api  = m_engine->moduleApi();
			const auto* snap = m_engine->m_moduleInputSnapshot.get();
			if (api.on_update != nullptr && snap != nullptr)
			{
				// dt は snapshot の値を渡す (v21、H-3)。live は build 時の実効値、
				// replay / resim は override が再投入した記録値 — dt gating も記録系の
				// 内側になり、GUI 録画 (F8 pause / hitStop 込み) → headless 再生が
				// bit-exact に成立する。
				api.on_update(m_engine->moduleMemory(), snap->effectiveDt, snap,
				              m_engine->m_moduleFrameIntents.get());
			}

			// restart (§8-4) は ring 記録より前に適用する — ring のフレーム N = 次フレームの
			// memory_in が成立する。intent は (GameMemory, InputSnapshot) の純関数出力なので、
			// replay / resim では update が同フレームで再発行し bit-exact に再現される。
			m_engine->applyModuleRestartIntent();

			// on_update 後の確定 GameMemory を time-travel ring に記録 (ADR 0017)。
			// replay の state slot と同一 bytes。観測 (probe 系列) と rewind の単一源。
			m_engine->recordModuleMemoryFrame();
			m_engine->recordModuleInputFrame();  // 入力も同じ窓で ring 保持 (ADR 0021)

			m_engine->drainModuleFrameIntents();

			// Replay record hook (axis 4): このフレームの input + 結果の intents を
			// host に渡し、.mtrr へ追記できるようにする (mitiru run --record)。
			if (m_engine->m_config.onModuleFrameRecorded
			    && m_engine->m_moduleInputSnapshot && m_engine->m_moduleFrameIntents)
			{
				m_engine->m_config.onModuleFrameRecorded(
					*m_engine->m_moduleInputSnapshot,
					*m_engine->m_moduleFrameIntents);
			}
		}

		void draw(Screen& screen) override
		{
			const auto& fx = m_engine->m_moduleVisualFx;

			// Shake (kind=4): game 描画全体を frame index ベースの決定的オフセットで
			// 平行移動する (乱数なし — リプレイ bit-exact)。
			const bool shaking = fx.shakeActive();
			if (shaking)
			{
				const auto off = fx.shakeOffset(m_engine->frameNumber());
				screen.pushTransform(off.dx, off.dy);
			}

			const auto& api = m_engine->moduleApi();
			if (api.on_draw != nullptr)
			{
				api.on_draw(m_engine->moduleMemory(), &screen);
			}

			if (shaking) { screen.popTransform(); }

			// Letterbox (kind=6): 上下黒帯。transform 外なので shake 非影響。
			// fade 覆いより先に描く = 帯の上に fade が乗る。
			const float lb = fx.letterboxAmount();
			if (lb > 0.0f)
			{
				const float w    = static_cast<float>(screen.width());
				const float h    = static_cast<float>(screen.height());
				const float band = h * 0.12f * lb;  // 上下それぞれの帯高さ (px)
				const sgc::Colorf black{0.0f, 0.0f, 0.0f, 1.0f};
				screen.drawRect(sgc::Rectf{0.0f, 0.0f, w, band}, black);
				screen.drawRect(sgc::Rectf{0.0f, h - band, w, band}, black);
			}

			// FadeOut/FadeIn (kind=2/3) の覆い。transform の外で描くので shake に
			// 影響されず、fadeIn が来るまで全画面を覆い続ける。
			const auto ov = fx.overlay();
			if (ov.a > 0.0f)
			{
				screen.drawRect(sgc::Rectf{0.0f, 0.0f,
				                           static_cast<float>(screen.width()),
				                           static_cast<float>(screen.height())},
				                sgc::Colorf{ov.r, ov.g, ov.b, ov.a});
			}
		}

		Size layout(int outsideW, int outsideH) override
		{
			return {outsideW, outsideH};
		}

	private:
		Engine* m_engine;
	};

	ModuleAdapter adapter(this);
	run(adapter, configIn);
	unloadModule();
	return true;
}

// ── Per-frame signal flow helper 群 (private; ModuleAdapter が呼ぶ) ────────
// 以下は inline で追加する member fn。inline-friend 宣言の方が綺麗だが、
// Engine.hpp が既に素の private として公開しており、ModuleAdapter は
// `m_engine->m_*` access 経由で暗黙に friend 扱いになる。
// 明確さのため Engine.hpp の class body に private member fn として宣言する…
// が、その header を整然と保つため、ここでは adapter が呼ぶ free namespace 内に
// 定義する。より単純には、ModuleAdapter が runModule の本体内で定義され、
// 囲うスコープへ access できる local class となるため friend 扱いとなり、
// m_engine の private member を直接呼べる。
//
// 補足: C++ の local class は friend にしない限り囲う関数の `this` の private
// member へ access できない。そこで helper を PRIVATE member function として
// 公開し、下記マークで friend にする。最も綺麗なのは Engine の private member に
// して ModuleAdapter から `m_engine->fooBar()` で呼ぶ方法。そのためには
// Engine.hpp の private section に宣言する必要がある。
//
// ここで採った実装方針: helper は Engine* を取り、public accessor + buffer 用に
// 新たに公開した少数の accessor (Engine.hpp に追加) 経由で access する file-scope
// の free function とする。

namespace mitiru::module::detail
{
// (ここに helper は不要 — Engine が自前の member fn を持つ)
}  // namespace mitiru::module::detail

// ── Engine member helper の定義 (ModuleAdapter から呼ばれる) ──────────────

MITIRU_INLINE void mitiru::Engine::ensureModuleCefBindings()
{
	// Inspector / perf / mixer のツール窓が読む SharedSnapshot の producer は、
	// CEF の有無に関係なく必ず起動する。これより下は CEF が準備でき次第 early-return
	// するので、ここで先に作っておかないと --no-cef のとき producer が永久に立たず、
	// 独立ウィンドウが「waiting for producer」のままになる (ADR 0014)。
	if (!m_moduleInspectorSnapshot)
	{
		m_moduleInspectorSnapshot = std::make_unique<observe::SharedSnapshot>();
	}

	if (m_moduleStateStore)
	{
		return;  // 接続済み
	}
	if (!m_cefContext.isInitialized())
	{
		// Headless / CEF 無効時 (例: `mitiru replay --test`): それでも no-op sink で
		// StateStore を生成し、game が push する view.* state を観察/assertion 用に
		// map へ取り込む。CEF 有効時は準備完了まで待つ。
		if (!m_config.enableCef && !m_moduleStateStore)
		{
			m_moduleStateStore = std::make_shared<cef::StateStore>(
				[](const std::string&) {},
				[](const std::string&, cef::StateStore::HandlerFn) {});
		}
		return;  // CEF はまだ準備未完 — 次フレームで再試行 (または上で store 生成済み)
	}

	auto* cef = &m_cefContext;
	m_moduleStateStore = std::make_shared<cef::StateStore>(
		[cef](const std::string& code) { cef->executeJavaScript(code); },
		[cef](const std::string& name, cef::StateStore::HandlerFn fn) {
			cef->registerHandler(name, std::move(fn));
		});

	// ページが (再)読み込みされたら保持済み state を全て再送する。これで
	// ページ読込前に push された値の取りこぼしが無くなり、game 側の heartbeat
	// 再 push が不要になる (hot reload 後も即座に最新状態が出る)。
	// weak 捕捉 — store 破棄後 (CEF shutdown ポンプ中等) にロード完了が
	// 来ても no-op (H-19: 生ポインタ捕捉による UAF を構造で排除)。
	{
		std::weak_ptr<cef::StateStore> weak = m_moduleStateStore;
		cef->setLoadEndCallback([weak](std::string_view)
		{
			if (auto s = weak.lock()) { s->replayRetainedState(); }
		});
	}

	// engine 所有の action handler — capture が reload 時に dangle する (ADR 0005,
	// F3) ため DLL 側には置けない。
	m_moduleStateStore->onAction("inspector.open",
		[](const cef::json& payload) -> cef::json
		{
			const std::string name = payload.value("name", "");
			if (name.empty())
			{
				return cef::json{{"ok", false}, {"reason", "missing name"}};
			}
			const bool ok = mitiru::debug::openInspectable(name);
			return cef::json{{"ok", ok}};
		});

	// その他の action 全てを受ける catch-all forwarder — DLL が次フレームで
	// 処理できるよう ActionEvent として queue する。
	auto* buffer = m_moduleActionEvents.get();
	m_moduleStateStore->onActionFallback(
		[buffer](std::string_view action, const cef::json& payload) -> cef::json
		{
			if (buffer == nullptr)
			{
				return cef::json{{"ok", false}, {"reason", "no event buffer"}};
			}
			std::lock_guard lock(buffer->mu);
			if (buffer->events.size() < 64)  // queue を bound する
			{
				buffer->events.emplace_back(std::string(action), payload.dump());
			}
			return cef::json{{"ok", true}, {"queued", true}};
		});

	// (Inspector SharedSnapshot の producer は関数先頭で CEF 非依存に作成済み)
}

MITIRU_INLINE void mitiru::Engine::buildModuleInputSnapshot(float dt)
{
	auto* snap = m_moduleInputSnapshot.get();
	if (snap == nullptr) { return; }

	// ── 実効 dt / pause (ABI v21、H-3) ─────────────────────────────────
	// pause / hitStop の dt gating は simulation 入力なので snapshot に載せる。
	// 末尾の moduleInputOverride (replay) / applyResimInputOverride (resim) が
	// snapshot 全体を記録値で置換するため、再生時は記録された実効 dt が再投入される。
	// paused 中 stepFrames>0 なら 1 フレームだけ通常 dt で進める (従来意味論のまま)。
	{
		float        effectiveDt = dt;
		std::uint8_t paused      = m_config.paused ? 1u : 0u;
		if (m_config.paused)
		{
			if (m_config.stepFrames > 0) { --m_config.stepFrames; }
			else                         { effectiveDt = 0.0f; }
		}
		// HitStop (kind=5): 残量がある間 module へ渡す dt を 0 にする (update は
		// 呼び続ける)。intent は決定論的な module 出力なので replay でも同じ
		// フレームで発火する。fade/shake もここで実時間 (固定ステップ) で進める —
		// 演出は engine 側状態であり GameMemory には入れない (観測対象外)。
		if (m_moduleVisualFx.hitStopActive()) { effectiveDt = 0.0f; }
		m_moduleVisualFx.advance(dt);
		snap->effectiveDt = effectiveDt;
		snap->paused      = paused;
	}

	// ── 論理解像度 (ABI v21、§8-5) ──────────────────────────────────────
	// Screen の logical size を毎フレーム供給。game は kScreenW/kScreenH の自前
	// constexpr を持たなくてよい。replay 時は記録値が再投入される (resize も記録系の内側)。
	{
		const int w = (m_screen != nullptr) ? m_screen->width()  : 0;
		const int h = (m_screen != nullptr) ? m_screen->height() : 0;
		snap->logicalW = static_cast<std::uint16_t>(std::clamp(w, 0, 65535));
		snap->logicalH = static_cast<std::uint16_t>(std::clamp(h, 0, 65535));
	}

	// 決定論 seed を供給 (ADR 0012)。replay 時は末尾の moduleInputOverride が
	// snapshot 全体を記録値で置換するので、ここで入れた値は再生時に記録 seed に戻る。
	snap->rngSeed = m_config.randomSeed;

	// 音声クロック (ABI v13)。host の audio backend の再生サンプル位置を供給。replay 時は
	// 末尾の moduleInputOverride が記録値で上書きするので bit-exact 性は保たれる。
	// audio master clock (ABI v13)。契約 (R-03, oscar-rythm): 0 = backend 未準備
	// (game は dt 積算へフォールバックする)。非ゼロになった後は **単調非減少** を
	// engine が保証する — backend がチャンク供給の谷で一瞬小さい値を返しても、
	// game の同期ロジック (snap/lerp) が拍を巻き戻さないようにここで clamp する。
	{
		const double raw = (m_audioEngine != nullptr) ? m_audioEngine->masterTimeSec() : 0.0;
		if (raw > m_lastAudioTimeSec) { m_lastAudioTimeSec = raw; }
		snap->audioTimeSec = m_lastAudioTimeSec;
	}

	// 音声出力レイテンシ (ABI v19)。device 固定値なので毎フレーム同じ。判定を耳基準へ
	// 補正したいリズムゲームが earTime = audioTimeSec - audioLatencySec で使う。0 = 不明。
	// replay 時は moduleInputOverride が記録値で上書きするので再現する。
	snap->audioLatencySec = (m_audioEngine != nullptr) ? m_audioEngine->outputLatencySec() : 0.0;

	// Keys (256 VK codes)。internal を覗かず InputState API を使う —
	// InputState の engine refactor の自由度を保つため。
	for (int vk = 0; vk < 256; ++vk)
	{
		const auto key = static_cast<KeyCode>(vk);
		snap->keysDown[vk]         = m_inputState.isKeyDown(key)         ? 1u : 0u;
		snap->keysJustPressed[vk]  = m_inputState.isKeyJustPressed(key)  ? 1u : 0u;
		snap->keysJustReleased[vk] = m_inputState.isKeyJustReleased(key) ? 1u : 0u;
	}

	// Mouse — 3 button (L/R/M)。
	auto [mx, my] = m_inputState.mousePosition();
	snap->mouseX = mx;
	snap->mouseY = my;
	for (int i = 0; i < 3; ++i)
	{
		const auto btn = static_cast<MouseButton>(i);
		snap->mouseButtonsDown[i]            = m_inputState.isMouseButtonDown(btn)         ? 1u : 0u;
		snap->mouseButtonsJustPressed[i]     = m_inputState.isMouseButtonJustPressed(btn)  ? 1u : 0u;
		snap->mouseButtonsJustReleased[i]    = m_inputState.isMouseButtonJustReleased(btn) ? 1u : 0u;
	}

	// Gamepad — ABI v5 (#12) + #32: XInput と SDL_GameController を並走、ボタン OR、
	// axes は XInput 接続時優先 / 切断時 SDL を採用。snapshot 永続バッファなので毎フレーム全 field 必書。
	{
		std::uint32_t down = 0, pressed = 0, released = 0;
		bool xinputConn = false;
		for (int i = 0; i < 6; ++i) { snap->gamepadAxes[i] = 0.0f; }
#ifdef _WIN32
		{
			constexpr int P = 0;
			xinputConn = m_gamepad.isConnected(P);
			const GamepadButton kBtns[] = {
				GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft,
				GamepadButton::DPadRight, GamepadButton::Start, GamepadButton::Back,
				GamepadButton::LS, GamepadButton::RS, GamepadButton::LB, GamepadButton::RB,
				GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y };
			for (auto b : kBtns)
			{
				const auto bit = static_cast<std::uint32_t>(b);
				if (m_gamepad.isButtonDown(P, b))     down     |= bit;
				if (m_gamepad.isButtonPressed(P, b))  pressed  |= bit;
				if (m_gamepad.isButtonReleased(P, b)) released |= bit;
			}
			if (xinputConn)
			{
				snap->gamepadAxes[0] = m_gamepad.getAxis(P, GamepadAxis::LeftStickX);
				snap->gamepadAxes[1] = m_gamepad.getAxis(P, GamepadAxis::LeftStickY);
				snap->gamepadAxes[2] = m_gamepad.getAxis(P, GamepadAxis::RightStickX);
				snap->gamepadAxes[3] = m_gamepad.getAxis(P, GamepadAxis::RightStickY);
				snap->gamepadAxes[4] = m_gamepad.getAxis(P, GamepadAxis::LeftTrigger);
				snap->gamepadAxes[5] = m_gamepad.getAxis(P, GamepadAxis::RightTrigger);
			}
		}
#endif
		// SDL_GameController を OR で重ねる (DS4/DS5 等)。同一デバイス重複でもビット OR は冪等。
		const bool sdlConn = m_sdlGamepad.connected();
		if (sdlConn)
		{
			down     |= m_sdlGamepad.buttonsDown();
			pressed  |= m_sdlGamepad.buttonsJustPressed();
			released |= m_sdlGamepad.buttonsJustReleased();
			if (!xinputConn)  // XInput が axes を埋めてなければ SDL axes を採用
			{
				snap->gamepadAxes[0] = m_sdlGamepad.axis(mitiru::module::gamepad::LeftStickX);
				snap->gamepadAxes[1] = m_sdlGamepad.axis(mitiru::module::gamepad::LeftStickY);
				snap->gamepadAxes[2] = m_sdlGamepad.axis(mitiru::module::gamepad::RightStickX);
				snap->gamepadAxes[3] = m_sdlGamepad.axis(mitiru::module::gamepad::RightStickY);
				snap->gamepadAxes[4] = m_sdlGamepad.axis(mitiru::module::gamepad::LeftTrigger);
				snap->gamepadAxes[5] = m_sdlGamepad.axis(mitiru::module::gamepad::RightTrigger);
			}
		}
		snap->gamepadConnected           = (xinputConn || sdlConn) ? 1 : 0;
		snap->gamepadButtonsDown         = down;
		snap->gamepadButtonsJustPressed  = pressed;
		snap->gamepadButtonsJustReleased = released;
	}

	// queue 済み action event (CEF UI thread 由来) を POD buffer へ drain する。
	// wire 上限 (name 64B / payload 256B) を超える event は **切り詰めず破棄** する —
	// 半端に切れた JSON を game に渡すと parse 失敗が game 側の謎バグに化けるため
	// (warnOnce で通知、R-01)。
	snap->actionEventCount = 0;
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		const std::size_t slotCap =
			sizeof(snap->actionEvents) / sizeof(snap->actionEvents[0]);
		std::size_t  taken   = 0;
		std::int32_t emitted = 0;
		for (; taken < m_moduleActionEvents->events.size()
		       && emitted < static_cast<std::int32_t>(slotCap); ++taken)
		{
			const auto& [name, payloadJson] = m_moduleActionEvents->events[taken];
			if (name.size() >= sizeof(snap->actionEvents[0].name)
			    || payloadJson.size() >= sizeof(snap->actionEvents[0].payloadJson))
			{
				mitiru::debug::warnOnce("action.event.oversize",
					"CEF action '" + name.substr(0, 32) + "' の name/payload が wire 上限 "
					"(64/256B) を超過 — event を破棄 (payload を小さくするか分割する)");
				continue;
			}
			module::detail::copyBounded(snap->actionEvents[emitted].name, name);
			module::detail::copyBounded(snap->actionEvents[emitted].payloadJson, payloadJson);
			++emitted;
		}
		snap->actionEventCount = emitted;
		// 消費 (破棄含む) した event を取り除く。溢れた分は次フレームに残す。
		if (taken > 0)
		{
			m_moduleActionEvents->events.erase(
				m_moduleActionEvents->events.begin(),
				m_moduleActionEvents->events.begin() + static_cast<std::ptrdiff_t>(taken));
		}
	}

	// Replay inject hook (axis 4): headless な `mitiru replay --test` は live 構築
	// した snapshot を記録済み byte で上書きし、on_update が記録通りの input stream を
	// 再実行できるようにする (DLL は input に関して stateless ゆえ ADR 0005、これで
	// run を bit-exact に再現する)。
	if (m_config.moduleInputOverride) { m_config.moduleInputOverride(*snap); }
}

MITIRU_INLINE void mitiru::Engine::zeroModuleFrameIntents()
{
	if (m_moduleFrameIntents)
	{
		m_moduleFrameIntents->reset();
	}
}

// restart intent (§8-4): GameMemory を unload せず初期状態から fresh 再構築する。
// ring 記録前に適用するので ring frame N = 再構築後 bytes = 次フレーム memory_in が
// 保たれる (単一 timeline のまま — ring は破棄しない。restart 前への rewind も正当)。
// memset 0 で padding byte まで決定論化し、on_init (MITIRU_GAME の gameInit) が
// static な既定値イメージを memcpy して NSDMI 既定値へ戻す。
MITIRU_INLINE void mitiru::Engine::applyModuleRestartIntent()
{
	auto* intents = m_moduleFrameIntents.get();
	if (intents == nullptr || intents->restartRequest == 0) { return; }
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleApi.on_init == nullptr)
	{
		mitiru::debug::warnOnce("restart.unavailable",
			"hud.requestRestart: GameMemory 未申告 (memorySize=0) か on_init 不在のため無視");
		return;
	}
	std::memset(m_moduleMemory, 0, m_moduleMemorySize);
	m_moduleApi.on_init(m_moduleMemory);
}

MITIRU_INLINE void mitiru::Engine::drainModuleFrameIntents()
{
	auto* intents = m_moduleFrameIntents.get();
	if (intents == nullptr) { return; }

	// 上限到達の検知 (R-01 級・初回のみ)。DLL 側 helper は満杯時に黙って drop するため、
	// count == 容量 を「超過分が落ちた可能性あり」として一度だけ知らせる。
	// int 比較 3 つだけなので hot path への影響は無視できる (warnOnce は到達時のみ呼ぶ)。
	{
		constexpr std::int32_t kPushCap = static_cast<std::int32_t>(
			sizeof(intents->statePushes) / sizeof(intents->statePushes[0]));
		constexpr std::int32_t kWatchCap = static_cast<std::int32_t>(
			sizeof(intents->exportedInspectables) / sizeof(intents->exportedInspectables[0]));
		constexpr std::int32_t kSoundCap = static_cast<std::int32_t>(
			sizeof(intents->soundIntents) / sizeof(intents->soundIntents[0]));
		if (intents->statePushCount >= kPushCap)
		{
			mitiru::debug::warnOnce("intents.statePush.cap",
				"HUD 更新が 1 フレーム " + std::to_string(kPushCap)
				+ " 件の上限に到達 — 超過分は落ちている");
		}
		if (intents->exportedInspectableCount >= kWatchCap)
		{
			mitiru::debug::warnOnce("intents.watch.cap",
				"watch が 1 フレーム " + std::to_string(kWatchCap)
				+ " 件の上限に到達 — 超過分は落ちている");
		}
		if (intents->soundIntentCount >= kSoundCap)
		{
			mitiru::debug::warnOnce("intents.sound.cap",
				"sound 再生要求が 1 フレーム " + std::to_string(kSoundCap)
				+ " 件の上限に到達 — 超過分は落ちている");
		}
	}

	// 単純な flag 群
	if (intents->requestStop) { requestStop(); }

	// Screenshot — host が capture + filename を処理する。
	if (intents->requestScreenshot)
	{
		if (m_screen != nullptr)
		{
			const int w = m_screen->width();
			const int h = m_screen->height();
			if (w > 0 && h > 0)
			{
				auto pixels = capture();
				if (!pixels.empty())
				{
					(void)mitiru::render::saveTimestampedFrameToPng(
						pixels.data(), w, h, "screenshots", "module_game");
				}
			}
		}
	}

	// セーブ/ロード intent — セーブ = GameMemory bytes の memcpy (ADR 0020、v17)。
	// save: GameMemory → save/<slot>.msav (cwd 基準、tmp→rename の atomic 書き)。
	// load: ファイル → GameMemory memcpy + ring clear (rewind と同一機構)。
	if (intents->saveRequest != 0)
	{
		const std::string slot = module::save::sanitizeSlot(intents->saveSlot);
		if (slot.empty())
		{
			mitiru::debug::warnOnce("save.slot.empty",
				"hud.save: slot 名が不正です (使える文字: a-zA-Z0-9_-) — 無視");
		}
		else if (m_moduleMemory == nullptr || m_moduleMemorySize == 0)
		{
			mitiru::debug::warnOnce("save.no-memory",
				"hud.save: GameMemory が未申告 (memorySize=0) のためセーブできません");
		}
		else
		{
			// layout hash (MITIRU_REFLECT 由来) を header に格納 — ロード時にサイズ照合を
			// 素通りする「同サイズの field 並べ替え / 型変更」を拒否できる (ADR 0024 追記)。
			const auto path = std::filesystem::path("save") / (slot + ".msav");
			if (!module::save::saveGameMemory(path, m_moduleMemory, m_moduleMemorySize,
			                                  module::kWireApiVersion,
			                                  module::moduleLayoutHash(m_moduleApi)))
			{
				mitiru::debug::warnOnce("save.write." + slot,
					"hud.save: 書き込みに失敗しました: " + path.string());
			}
		}
	}
	if (intents->loadRequest != 0)
	{
		const std::string slot = module::save::sanitizeSlot(intents->loadSlot);
		if (slot.empty())
		{
			mitiru::debug::warnOnce("load.slot.empty",
				"hud.load: slot 名が不正です (使える文字: a-zA-Z0-9_-) — 無視");
		}
		else
		{
			// replay 代用フック (ADR 0020 の核心): override が true を返したら記録済み
			// state blob を適用済みなのでファイルは読まない — セーブファイルが録画後に
			// 上書きされていても bit-exact が構造保証される。
			const bool substituted = m_saveLoadOverride && m_saveLoadOverride(slot.c_str());
			bool       applied     = substituted;
			if (!substituted)
			{
				const auto path  = std::filesystem::path("save") / (slot + ".msav");
				const auto bytes = module::save::loadGameMemory(
					path, m_moduleMemorySize, module::moduleLayoutHash(m_moduleApi));
				if (bytes.has_value()
				    && rewindModuleMemory(bytes->data(),
				                          static_cast<std::uint32_t>(bytes->size())))
				{
					applied = true;
				}
				else
				{
					// 不在 / 形式不正 / サイズ・layout 不一致 (struct 変更後の旧セーブ) は
					// 拒否 — 化けた state を黙って流し込まない (ADR 0020 失敗モード表)。
					mitiru::debug::warnOnce("load.reject." + slot,
						"hud.load: ロード拒否 (ファイル不在 / 形式不正 / GameMemory サイズ・layout 不一致): "
						+ path.string());
				}
			}
			// 適用成功時は time-travel ring を破棄する。load 前の履歴は別時間軸の bytes で、
			// そこへの rewind は復元を壊す (reloadModule の ring clear と同じ理由、ADR 0017)。
			if (applied) { m_moduleMemoryRing.clear(); }
		}
	}

	// Tool window spawn 要求 — DLL → host → 別 exe を spawn する (ADR 0014)。
	// game は Engine* を持てない (ADR 0005) ので「このツール窓を開いて」と intent で頼み、
	// host が mitiru_<tool>.exe を別窓で起動する (必要なときだけ・pulled UI)。exe が
	// 見つからなければ無害に no-op。inspector へは host 自身の pid を渡し、game が
	// exportedInspectables に出した state をそのまま観測させる (SharedSnapshot 経由)。
	if (!m_suppressToolWindows && intents->toolRequestCount > 0)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->toolRequestCount,
			static_cast<std::int32_t>(sizeof(intents->toolRequests) /
			                          sizeof(intents->toolRequests[0])));
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& req = intents->toolRequests[i];
			if (req.tool[0] == '\0') { continue; }
			// 重複 spawn 防止: hud.open(Tool::X) を毎フレーム update で呼んでも窓は 1 回だけ
			// 開く。これで「どこに置くか」を気にせず、開きたい所で呼べる (pulled UI のまま)。
			std::string key = std::string{req.tool} + '|' + std::string{req.args};
			if (!m_spawnedToolKeys.insert(std::move(key)).second) { continue; }
			// setToolWindowPos が指定されていれば spawn 引数へ --window-pos を足す (録画で実画面に出さない)。
			std::string spawnArgs{req.args};
			if (m_toolWinX != (-2147483647 - 1))
			{
				spawnArgs += " --window-pos " + std::to_string(m_toolWinX) + " " + std::to_string(m_toolWinY);
			}
			(void)mitiru::debug::spawnTool(std::string{req.tool}, 0, spawnArgs);
		}
	}

	// Palette の表示状態 — engine 所有の flag を CEF へ push する。
	if (intents->paletteToggle && m_moduleStateStore)
	{
		const auto cur = m_moduleStateStore->get<bool>("view.palette.visible");
		const bool next = !cur.value_or(false);
		m_moduleStateStore->set("view.palette.visible", next);
	}

	// State push — DLL → host → StateStore → CEF JS。
	if (m_moduleStateStore && intents->statePushCount > 0)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->statePushCount,
			static_cast<std::int32_t>(sizeof(intents->statePushes) /
			                          sizeof(intents->statePushes[0])));
		// この frame の全 push を溜めて 1 回の executeJavaScript に畳む (per-key IPC 削減)。
		// key / strVal は bounded 読み — DLL からの wire buffer は null 終端を信頼しない
		// (exportedInspectables の boundedLen と同基準の境界防御)。
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& item = intents->statePushes[i];
			const std::string_view key{item.key, module::detail::boundedLen(item.key)};
			switch (item.kind)
			{
			case 1: m_moduleStateStore->setBatched(key, item.intVal); break;
			case 2: m_moduleStateStore->setBatched(key, item.floatVal); break;
			case 3: m_moduleStateStore->setBatched(key, static_cast<bool>(item.intVal)); break;
			case 4: m_moduleStateStore->setBatched(key,
				std::string{item.strVal, module::detail::boundedLen(item.strVal)}); break;
			default: break;  // kind=0 (null) は今は意図的に no-op
			}
		}
		m_moduleStateStore->flushBatch();
	}

	// Exported inspectable — DLL が毎フレーム埋める。engine が SharedSnapshot +
	// view.palette.items へ sync し、F12 palette + inspector sub-window が
	// DLL 側 state を拾えるようにする。
	if (intents->exportedInspectableCount > 0 && m_moduleInspectorSnapshot)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->exportedInspectableCount,
			static_cast<std::int32_t>(sizeof(intents->exportedInspectables) /
			                          sizeof(intents->exportedInspectables[0])));

		// 変化検知: export 内容 (name + json) を FNV-1a で畳み、前回と同一なら
		// parse+rebuild+disk-write を丸ごと省く。inspector は同じ内容を読み続けるので
		// skip しても観測結果は変わらず、毎フレームの temp-file 書き込みを避けられる。
		std::uint64_t digest = 1469598103934665603ull;
		const auto fold = [&digest](const char* p, std::size_t len)
		{
			for (std::size_t k = 0; k < len; ++k)
			{
				digest ^= static_cast<unsigned char>(p[k]);
				digest *= 1099511628211ull;
			}
		};
		// jsonLen も game 申告値 — buffer サイズへ clamp してから読む (境界防御)。
		const auto clampedJsonLen = [](const module::InspectableExport& e) noexcept
		{
			const auto cap = static_cast<std::int32_t>(sizeof(e.json));
			return (e.jsonLen < 0) ? 0 : (e.jsonLen > cap ? cap : e.jsonLen);
		};
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& exp = intents->exportedInspectables[i];
			fold(exp.name, module::detail::boundedLen(exp.name));
			const std::int32_t jl = clampedJsonLen(exp);
			if (jl > 0) { fold(exp.json, static_cast<std::size_t>(jl)); }
		}

		if (digest != m_lastInspectorDigest)
		{
			m_lastInspectorDigest = digest;
			// mitiru_inspector.exe が期待する JSON map を構築する:
			//   { name: { title, state }, ... }
			cef::json out = cef::json::object();
			cef::json palette = cef::json::array();
			for (std::int32_t i = 0; i < n; ++i)
			{
				const auto& exp = intents->exportedInspectables[i];
				const std::string name{exp.name, module::detail::boundedLen(exp.name)};
				const std::string title{exp.title, module::detail::boundedLen(exp.title)};
				const std::int32_t jl = clampedJsonLen(exp);
				cef::json state;
				try
				{
					state = jl > 0
						? cef::json::parse(std::string{exp.json,
						                               static_cast<std::size_t>(jl)})
						: cef::json::object();
				}
				catch (...)
				{
					state = cef::json{{"error", "invalid inspectable JSON"}};
				}
				out[name] = cef::json{{"title", title}, {"state", state}};
				palette.push_back({{"name", name}, {"title", title}});
			}
			// 即時 write せずキャッシュ。perf/audio 併記と throttle write は下の
			// host-owned 観察ブロックが担う (game export 無しでも perf が動くように)。
			m_lastInspectorOut = std::move(out);
			m_inspectorDirty   = true;
			if (m_moduleStateStore)
			{
				m_moduleStateStore->set("view.palette.items", palette);
			}
		}
	}

	// host 所有の観察 (perf / audio) を ~10Hz で snapshot に併記する。game inspectable
	// とは別 cadence — 常時変化するので digest gate に乗せず wall-clock で計測して書く。
	// ツール窓 (mitiru_perf / mitiru_mixer) が同じ SharedSnapshot を読む (ADR 0014)。
	if (m_moduleInspectorSnapshot)
	{
		const auto now = std::chrono::steady_clock::now();
		if (m_havePerfTp)
		{
			const float dtMs =
				std::chrono::duration<float, std::milli>(now - m_lastPerfTp).count();
			if (dtMs > 0.0f)
			{
				const float fps = 1000.0f / dtMs;
				m_emaFps      = m_emaFps > 0.0f ? (m_emaFps * 0.9f + fps * 0.1f) : fps;
				m_lastFrameMs = dtMs;
			}
		}
		m_lastPerfTp = now;
		m_havePerfTp = true;

		if (m_inspectorDirty || ++m_toolWriteAccum >= 6)   // 即時 or ~10Hz
		{
			m_inspectorDirty = false;
			m_toolWriteAccum = 0;

			cef::json out = m_lastInspectorOut.is_object()
				? m_lastInspectorOut : cef::json::object();
			out["perf"] = cef::json{
				{"title", "Performance"},
				{"state", cef::json{{"fps", static_cast<int>(m_emaFps + 0.5f)},
				                    {"frameMs", m_lastFrameMs}}}};
			// 再生中チャンネルのメーター (任意)。列挙非対応の audio engine は空配列。
			cef::json channels = cef::json::array();
			if (m_audioEngine)
			{
				for (const auto& m : m_audioEngine->meterChannels())
				{
					channels.push_back(cef::json{{"kind", m.kind}, {"level", m.level}});
				}
			}
			out["audio"] = cef::json{
				{"title", "Audio"},
				{"state", cef::json{{"masterVolume", masterVolume()},
				                    {"engine", m_audioEngine ? "active" : "none"},
				                    {"channels", std::move(channels)}}}};

			// 過去フレームの記録: GameMemory ring があれば、別窓のシークバーで過去へ戻せる。
			// probe を宣言していれば値の履歴も一緒に送る (任意)。全フレームを送るので、
			// バーの位置がそのまま「何フレーム前か」に 1:1 で対応する。
			if (m_moduleMemoryRing.size() >= 2 && m_moduleMemorySize > 0)
			{
				const std::size_t frames = m_moduleMemoryRing.size();
				const std::int32_t probeCap = static_cast<std::int32_t>(
					sizeof(m_moduleApi.seriesProbes) / sizeof(m_moduleApi.seriesProbes[0]));
				const std::int32_t pc = std::min(m_moduleApi.seriesProbeCount, probeCap);

				cef::json ttState;
				ttState["capacity"] = static_cast<int>(frames);
				cef::json markersJson = cef::json::array();
				bool markersDone = false;

				for (std::int32_t p = 0; p < pc; ++p)
				{
					const auto& probe = m_moduleApi.seriesProbes[p];
					if (probe.accessor == nullptr || probe.name[0] == '\0') { continue; }

					// ring を oldest → newest に走査し probe を適用 (graph 左端=最古)。
					std::vector<double> series;
					series.reserve(frames);
					for (std::size_t k = 0; k < frames; ++k)
					{
						const std::uint8_t* bytes = m_moduleMemoryRing.at(frames - 1 - k);
						if (bytes != nullptr) { series.push_back(probe.accessor(bytes)); }
					}

					cef::json arr = cef::json::array();
					for (const double v : series) { arr.push_back(v); }
					// html は /History$/ のキーを channel として検出する (例 "hpHistory")。
					ttState[std::string{probe.name} + "History"] = std::move(arr);

					// 最初の probe から節目 (edge + danger 閾値跨ぎ) を marker にする。
					if (!markersDone)
					{
						observe::MarkerOpts opts;
						opts.wantEdges  = true;
						opts.epsilon    = 0.5;
						opts.maxMarkers = 24;
						if (probe.hasThreshold)
						{
							opts.hasThreshold = true;
							opts.threshold    = probe.threshold;
						}
						for (const auto& m : observe::extractMarkers(series, opts))
						{
							markersJson.push_back(cef::json{
								{"o", m.offsetFromNewest},
								{"v", m.value},
								{"k", static_cast<int>(m.kind)}});
						}
						markersDone = true;
					}
				}
				ttState["markers"] = std::move(markersJson);
				out["rewind"] = cef::json{{"title", "巻き戻し"}, {"state", std::move(ttState)}};
			}

			// AI Lens: GameMemory 全フィールドを reflection で構造化 (ADR 0018)。
			// game が MITIRU_REFLECT を宣言してれば、AI が窓を開かず全状態を構造的に読める。
			if (m_moduleApi.reflectFieldCount > 0 && m_moduleMemory != nullptr && m_moduleMemorySize > 0)
			{
				// フィールド名を宣言順 (MITIRU_REFLECT の並び) で列挙して渡す。JSON object は key を
				// ソートしてしまうので、観測窓が「コード順」で表示できるよう順序を配列で別に添える。
				cef::json fieldOrder = cef::json::array();
				for (std::uint32_t fi = 0; fi < m_moduleApi.reflectFieldCount; ++fi)
				{
					fieldOrder.push_back(m_moduleApi.reflectFields[fi].name);
				}
				out["gameMemory"] = cef::json{
					{"title", "Game memory"},
					{"order", std::move(fieldOrder)},
					{"state", observe::reflectToJson(
						static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
						m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
						m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount)}};
			}

			m_moduleInspectorSnapshot->write(out);
		}
	}

	// 生の JS 実行 (例: hot reload toast の trigger)。
	if (intents->jsToExecuteLen > 0 && m_cefContext.isInitialized())
	{
		const std::int32_t cap = static_cast<std::int32_t>(
			sizeof(intents->jsToExecute) / sizeof(intents->jsToExecute[0]));
		const std::int32_t n = std::min(intents->jsToExecuteLen, cap - 1);
		if (n > 0)
		{
			m_cefContext.executeJavaScript(
				std::string{intents->jsToExecute,
				            static_cast<std::size_t>(n)});
		}
	}

	// Sound 再生要求 — DLL → host → audio engine (ADR 0008)。game は mixer
	// pointer を持たず (ADR 0005)、sound 名を指定するだけ。audio engine 未設定時
	// (graceful degradation) や v3 module では無音 no-op となる
	// (soundIntentCount は 0 のまま — on_update 前に毎フレーム zero される)。
	if (intents->soundIntentCount > 0 && m_audioEngine)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->soundIntentCount,
			static_cast<std::int32_t>(sizeof(intents->soundIntents) /
			                          sizeof(intents->soundIntents[0])));
		// category / stop / loop / volume の解釈は applySoundIntent に集約 (ADR 0008)。
		// BGM の同 id 連打は router が冪等化する (毎フレーム hud.music("bgm") を許容)。
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& si = intents->soundIntents[i];
			if (!m_soundIntentRouter.apply(*m_audioEngine, si)) { continue; }  // dedupe skip
			// AI 観測ログ (/api/ai/audio): 適用済み intent をそのまま記録する。
			m_audioLog.push(frameNumber(), si.id, si.category, si.loop, si.stop,
			                si.volume, si.pitchScale);
		}
	}

	// 毎フレームの audio 定期掃除 (#51): 終了 SE voice 回収 + fade-out music 解放。
	// 再生有無に関わらず呼ぶ (静かな区間でも ended voice が滞留しないように)。
	if (m_audioEngine) { m_audioEngine->update(); }

	// VisualIntent (#33、v7): kind=1 (Tint) は Screen::pushTint へ、kind 2-6
	// (FadeOut/FadeIn/Shake/HitStop/Letterbox) は VisualIntentFx へ流す。kind=0 は no-op。
	// FX の適用 (覆い描画・shake transform・dt=0 gating) は ModuleAdapter が行う。
	if (intents->visualIntentCount > 0 && m_screen)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->visualIntentCount,
			static_cast<std::int32_t>(sizeof(intents->visualIntents) /
			                          sizeof(intents->visualIntents[0])));
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& vi = intents->visualIntents[i];
			if (vi.kind == mitiru::module::kVisualIntentTint)
			{
				if (vi.durSec > 0.0f)
				{
					m_screen->pushTint(sgc::Colorf{vi.r, vi.g, vi.b, vi.a}, vi.durSec);
				}
			}
			else
			{
				(void)m_moduleVisualFx.applyIntent(vi);  // kind 2-6 (それ以外は no-op)
			}
		}
	}
}
