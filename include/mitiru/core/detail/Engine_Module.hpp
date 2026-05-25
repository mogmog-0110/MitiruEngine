// mitiru::Engine の detail header - 直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module.hpp
/// @brief Engine の module loader 部分の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::loadModule / unloadModule / reloadModule / runModule` の実装に
/// 加えて、ADR 0005 (Host-Game C-only signal flow) に準拠した per-frame
/// signal flow:
///   - InputSnapshot 構築 (host が input + action events を POD に詰める)
///   - FrameIntents drain (DLL の要求を host が解釈して engine 操作に変換)
///   - 必要なら StateStore + SharedSnapshot を遅延生成

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include <mitiru/cef/StateStore.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/DebugPrint.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SoundIntentRouter.hpp>
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

}  // namespace mitiru::module::detail

// ── loadModule ─────────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::loadModule(const std::filesystem::path& modulePath)
{
	// すでに module が active なら明示的 unload を要求する。
	if (m_moduleHost && m_moduleHost->isLoaded())
	{
		return false;
	}

	if (!m_moduleHost)
	{
		m_moduleHost = std::make_unique<module::ModuleHost>();
	}

	if (!m_moduleHost->load(modulePath))
	{
		return false;
	}

	m_moduleApi = module::ModuleApi{};
	m_moduleApi.version = module::kCurrentApiVersion;

	const auto loadFn = m_moduleHost->loadFn();
	if (loadFn == nullptr)
	{
		m_moduleHost->unload();
		return false;
	}

	loadFn(&m_moduleApi, &m_moduleMemory);

	// version check。
	if (m_moduleApi.version == 0u || m_moduleApi.version > module::kCurrentApiVersion)
	{
		m_moduleHost->unload();
		m_moduleApi = module::ModuleApi{};
		return false;
	}

	// per-frame signal flow 用の scratch buffer を遅延確保する。
	if (!m_moduleInputSnapshot)
	{
		m_moduleInputSnapshot = std::make_unique<module::InputSnapshot>();
	}
	if (!m_moduleFrameIntents)
	{
		m_moduleFrameIntents = std::make_unique<module::FrameIntents>();
	}
	if (!m_moduleActionEvents)
	{
		m_moduleActionEvents = std::make_unique<ModuleActionEventBuffer>();
	}

	if (m_moduleApi.on_init != nullptr)
	{
		m_moduleApi.on_init(m_moduleMemory);
	}
	return true;
}

// ── unloadModule ───────────────────────────────────────────────────────────

MITIRU_INLINE void mitiru::Engine::unloadModule() noexcept
{
	if (!m_moduleHost || !m_moduleHost->isLoaded())
	{
		return;
	}

	if (m_moduleApi.on_shutdown != nullptr)
	{
		try { m_moduleApi.on_shutdown(m_moduleMemory); }
		catch (...) {}
	}

	if (auto unloadFn = m_moduleHost->unloadFn())
	{
		try { unloadFn(m_moduleMemory); }
		catch (...) {}
	}

	m_moduleApi = module::ModuleApi{};
	m_moduleHost->unload();

	// (今や死んでいる) DLL が所有する state を参照していた pending event は
	// action queue から全て破棄する必要がある。
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		m_moduleActionEvents->events.clear();
	}
}

// ── reloadModule ───────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::reloadModule(const std::filesystem::path& modulePath)
{
	unloadModule();
	return loadModule(modulePath);
}

// ── moduleStateStore accessor ──────────────────────────────────────────────

MITIRU_INLINE mitiru::cef::StateStore* mitiru::Engine::moduleStateStore() noexcept
{
	return m_moduleStateStore.get();
}

// ── runModule (stack-local adapter Game) ───────────────────────────────────

MITIRU_INLINE void mitiru::Engine::runModule(
	const std::filesystem::path& modulePath, const EngineConfig& configIn)
{
	if (!loadModule(modulePath))
	{
		return;
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
			m_engine->buildModuleInputSnapshot();
			m_engine->zeroModuleFrameIntents();

			const auto& api = m_engine->moduleApi();
			if (api.on_update != nullptr)
			{
				api.on_update(m_engine->moduleMemory(), dt,
				              m_engine->m_moduleInputSnapshot.get(),
				              m_engine->m_moduleFrameIntents.get());
			}

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
			const auto& api = m_engine->moduleApi();
			if (api.on_draw != nullptr)
			{
				api.on_draw(m_engine->moduleMemory(), &screen);
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
}

// ── Accessors ──────────────────────────────────────────────────────────────

MITIRU_INLINE bool mitiru::Engine::hasModule() const noexcept
{
	return m_moduleHost && m_moduleHost->isLoaded();
}

MITIRU_INLINE const mitiru::module::ModuleApi& mitiru::Engine::moduleApi() const noexcept
{
	return m_moduleApi;
}

MITIRU_INLINE void* mitiru::Engine::moduleMemory() const noexcept
{
	return m_moduleMemory;
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
			m_moduleStateStore = std::make_unique<cef::StateStore>(
				[](const std::string&) {},
				[](const std::string&, cef::StateStore::HandlerFn) {});
		}
		return;  // CEF はまだ準備未完 — 次フレームで再試行 (または上で store 生成済み)
	}

	auto* cef = &m_cefContext;
	m_moduleStateStore = std::make_unique<cef::StateStore>(
		[cef](const std::string& code) { cef->executeJavaScript(code); },
		[cef](const std::string& name, cef::StateStore::HandlerFn fn) {
			cef->registerHandler(name, std::move(fn));
		});

	// ページが (再)読み込みされたら保持済み state を全て再送する。これで
	// ページ読込前に push された値の取りこぼしが無くなり、game 側の heartbeat
	// 再 push が不要になる (hot reload 後も即座に最新状態が出る)。
	{
		auto* store = m_moduleStateStore.get();
		cef->setLoadEndCallback([store](std::string_view) { store->replayRetainedState(); });
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

	// Inspector SharedSnapshot — mitiru_inspector.exe の sub-window が poll する
	// %TEMP%\mitiru_inspector_<pid>.json を書き出す。
	if (!m_moduleInspectorSnapshot)
	{
		m_moduleInspectorSnapshot = std::make_unique<observe::SharedSnapshot>();
	}
}

MITIRU_INLINE void mitiru::Engine::buildModuleInputSnapshot()
{
	auto* snap = m_moduleInputSnapshot.get();
	if (snap == nullptr) { return; }

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

	// queue 済み action event (CEF UI thread 由来) を POD buffer へ drain する。
	snap->actionEventCount = 0;
	if (m_moduleActionEvents)
	{
		std::lock_guard lock(m_moduleActionEvents->mu);
		const auto take = std::min<std::size_t>(
			m_moduleActionEvents->events.size(),
			sizeof(snap->actionEvents) / sizeof(snap->actionEvents[0]));
		for (std::size_t i = 0; i < take; ++i)
		{
			const auto& [name, payloadJson] = m_moduleActionEvents->events[i];
			module::detail::copyBounded(snap->actionEvents[i].name, name);
			module::detail::copyBounded(snap->actionEvents[i].payloadJson, payloadJson);
		}
		snap->actionEventCount = static_cast<std::int32_t>(take);
		// 消費した event を破棄する。溢れた分は次フレームに残す。
		if (take > 0)
		{
			m_moduleActionEvents->events.erase(
				m_moduleActionEvents->events.begin(),
				m_moduleActionEvents->events.begin() + static_cast<std::ptrdiff_t>(take));
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
		*m_moduleFrameIntents = module::FrameIntents{};
	}
}

MITIRU_INLINE void mitiru::Engine::drainModuleFrameIntents()
{
	auto* intents = m_moduleFrameIntents.get();
	if (intents == nullptr) { return; }

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
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& item = intents->statePushes[i];
			const std::string key{item.key};
			switch (item.kind)
			{
			case 1: m_moduleStateStore->set(key, item.intVal); break;
			case 2: m_moduleStateStore->set(key, item.floatVal); break;
			case 3: m_moduleStateStore->set(key, static_cast<bool>(item.intVal)); break;
			case 4: m_moduleStateStore->set(key, std::string{item.strVal}); break;
			default: break;  // kind=0 (null) は今は意図的に no-op
			}
		}
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
		// mitiru_inspector.exe が期待する JSON map を構築する:
		//   { name: { title, state }, ... }
		cef::json out = cef::json::object();
		cef::json palette = cef::json::array();
		for (std::int32_t i = 0; i < n; ++i)
		{
			const auto& exp = intents->exportedInspectables[i];
			const std::string name{exp.name};
			const std::string title{exp.title};
			cef::json state;
			try
			{
				state = exp.jsonLen > 0
					? cef::json::parse(std::string{exp.json,
					                               static_cast<std::size_t>(exp.jsonLen)})
					: cef::json::object();
			}
			catch (...)
			{
				state = cef::json{{"error", "DLL produced invalid JSON"}};
			}
			out[name] = cef::json{{"title", title}, {"state", state}};
			palette.push_back({{"name", name}, {"title", title}});
		}
		m_moduleInspectorSnapshot->write(out);
		if (m_moduleStateStore)
		{
			m_moduleStateStore->set("view.palette.items", palette);
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
		for (std::int32_t i = 0; i < n; ++i)
		{
			mitiru::module::applySoundIntent(*m_audioEngine, intents->soundIntents[i]);
		}
	}
}
