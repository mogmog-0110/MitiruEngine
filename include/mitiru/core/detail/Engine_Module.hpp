// Detail header for mitiru::Engine - do not include directly; included via core/Engine.hpp
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
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

// ── Free helpers (file-local, not Engine methods) ──────────────────────────
namespace mitiru::module::detail
{

/// @brief Copy `src` into a fixed-size buffer, null-terminating + truncating
///        without UB if the source is longer than the buffer.
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

	// Version check.
	if (m_moduleApi.version == 0u || m_moduleApi.version > module::kCurrentApiVersion)
	{
		m_moduleHost->unload();
		m_moduleApi = module::ModuleApi{};
		return false;
	}

	// Lazy-allocate scratch buffers for per-frame signal flow.
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

	// Action queue must drop any pending events that referenced state owned
	// by the (now-dead) DLL.
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

	// Stack-local Game adapter. Bridges existing engine main loop into the
	// ADR 0005 signal flow:
	//   - update(): snapshot input, zero intents, on_update, drain intents
	//   - draw():   pass Screen pointer through to on_draw
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

// ── Per-frame signal flow helpers (private; called by ModuleAdapter) ──────
// These are member fns added inline below; declared as inline-friend would be
// cleaner but Engine.hpp already exposes them through plain private status —
// ModuleAdapter is friended implicitly via `m_engine->m_*` access.
// For clarity we declare them as private member fns in Engine.hpp's class
// body... but to keep that header tidy, define them here in a free namespace
// that the adapter calls. Simpler: just call into m_engine's private members
// directly since ModuleAdapter is defined inside runModule's body, making it
// a friend by virtue of being a local class with access to enclosing scope.
//
// Adjustment: C++ local classes do NOT have access to the enclosing function's
// `this` private members unless friended. So we expose the helpers as
// PRIVATE member functions but make them friends via mark below. Cleanest:
// just make them private members of Engine and ModuleAdapter calls via
// `m_engine->fooBar()`. To do so, we need to declare them in Engine.hpp's
// private section.
//
// Implementation strategy used here: helpers are file-scope free functions
// that take Engine* and access via public accessors + a small set of newly-
// exposed accessors for the buffers (added to Engine.hpp).

namespace mitiru::module::detail
{
// (no helpers needed here — Engine has its own member fns)
}  // namespace mitiru::module::detail

// ── Engine member helper definitions (called from ModuleAdapter) ──────────

MITIRU_INLINE void mitiru::Engine::ensureModuleCefBindings()
{
	if (m_moduleStateStore)
	{
		return;  // already wired
	}
	if (!m_cefContext.isInitialized())
	{
		return;  // CEF not ready yet — try again next frame
	}

	auto* cef = &m_cefContext;
	m_moduleStateStore = std::make_unique<cef::StateStore>(
		[cef](const std::string& code) { cef->executeJavaScript(code); },
		[cef](const std::string& name, cef::StateStore::HandlerFn fn) {
			cef->registerHandler(name, std::move(fn));
		});

	// Engine-owned action handlers — these can't live in the DLL because
	// their captures would dangle on reload (ADR 0005, F3).
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

	// Catch-all forwarder for any other action — queue as an ActionEvent for
	// the DLL to process next frame.
	auto* buffer = m_moduleActionEvents.get();
	m_moduleStateStore->onActionFallback(
		[buffer](std::string_view action, const cef::json& payload) -> cef::json
		{
			if (buffer == nullptr)
			{
				return cef::json{{"ok", false}, {"reason", "no event buffer"}};
			}
			std::lock_guard lock(buffer->mu);
			if (buffer->events.size() < 64)  // bound the queue
			{
				buffer->events.emplace_back(std::string(action), payload.dump());
			}
			return cef::json{{"ok", true}, {"queued", true}};
		});

	// Inspector SharedSnapshot — writes %TEMP%\mitiru_inspector_<pid>.json
	// that mitiru_inspector.exe sub-windows poll.
	if (!m_moduleInspectorSnapshot)
	{
		m_moduleInspectorSnapshot = std::make_unique<observe::SharedSnapshot>();
	}
}

MITIRU_INLINE void mitiru::Engine::buildModuleInputSnapshot()
{
	auto* snap = m_moduleInputSnapshot.get();
	if (snap == nullptr) { return; }

	// Keys (256 VK codes). Use the InputState API rather than peeking at
	// internals — keeps engine refactor freedom in InputState.
	for (int vk = 0; vk < 256; ++vk)
	{
		const auto key = static_cast<KeyCode>(vk);
		snap->keysDown[vk]         = m_inputState.isKeyDown(key)         ? 1u : 0u;
		snap->keysJustPressed[vk]  = m_inputState.isKeyJustPressed(key)  ? 1u : 0u;
		snap->keysJustReleased[vk] = m_inputState.isKeyJustReleased(key) ? 1u : 0u;
	}

	// Mouse — 3 buttons (L/R/M).
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

	// Drain queued action events (from CEF UI thread) into the POD buffer.
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
		// Drop the consumed events; any overflow stays for next frame.
		if (take > 0)
		{
			m_moduleActionEvents->events.erase(
				m_moduleActionEvents->events.begin(),
				m_moduleActionEvents->events.begin() + static_cast<std::ptrdiff_t>(take));
		}
	}
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

	// Simple flags
	if (intents->requestStop) { requestStop(); }

	// Screenshot — host handles capture + filename.
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

	// Palette visibility — engine-owned flag pushed to CEF.
	if (intents->paletteToggle && m_moduleStateStore)
	{
		const auto cur = m_moduleStateStore->get<bool>("view.palette.visible");
		const bool next = !cur.value_or(false);
		m_moduleStateStore->set("view.palette.visible", next);
	}

	// State pushes — DLL → host → StateStore → CEF JS.
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
			default: break;  // kind=0 (null) intentionally no-op for now
			}
		}
	}

	// Exported inspectables — DLL fills these each frame; engine syncs to
	// SharedSnapshot + view.palette.items so the F12 palette + inspector
	// sub-windows pick up DLL-side state.
	if (intents->exportedInspectableCount > 0 && m_moduleInspectorSnapshot)
	{
		const std::int32_t n = std::min<std::int32_t>(
			intents->exportedInspectableCount,
			static_cast<std::int32_t>(sizeof(intents->exportedInspectables) /
			                          sizeof(intents->exportedInspectables[0])));
		// Build the JSON map that mitiru_inspector.exe expects:
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

	// Raw JS execution (e.g. hot reload toast trigger).
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
}
