// mitiru::Engine の detail header - 直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Loader.hpp
/// @brief Engine の module loader 部分の out-of-class 定義 (v0.2.0 step 2-3)
/// @details
/// `Engine::loadModule / unloadModule / reloadModule` の実装と、
/// module 状態の accessor 群、time-travel 用 GameMemory ring 記録 /
/// rewind / branch (ADR 0013 / 0017) を収める。
/// per-frame signal flow は Engine_Module_Adapter.hpp 側。

#include <algorithm>
#include <cstdio>
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
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/SeriesMarkers.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

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

	// DLL が申告した GameMemory サイズを保持 (ADR 0013)。v≤8 DLL は未設定 ⇒ zero-init の 0。
	m_moduleMemorySize = m_moduleApi.memorySize;

	// reflection を申告したのに memorySize=0 だと reflectToJson が bounds 外で全 skip し
	// /api/ai/state が {} を返す。原因が分かりにくいので一度だけ警告する (R-01)。
	// non-POD game でも api->memorySize = sizeof(GameMemory) を申告すれば現フレーム観測は可
	// (reflectToJson は申告した offset のスカラーしか触らない)。ring/diff/branch は flat POD 必須。
	if (m_moduleApi.reflectFieldCount > 0 && m_moduleMemorySize == 0)
	{
		std::fprintf(stderr,
			"[ai] warning: MITIRU_REFLECT で %d field 申告されていますが api->memorySize が 0 です。"
			"/api/ai/state は空 {} になります。api->memorySize = sizeof(GameMemory) を申告してください。\n",
			static_cast<int>(m_moduleApi.reflectFieldCount));
	}

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
	// 旧 layout の GameMemory bytes を破棄する。struct を編集して hot reload すると
	// sizeof が変わり得るので、古い ring 内容への rewind は復元を壊す (ADR 0017)。
	m_moduleMemoryRing.clear();
	return loadModule(modulePath);
}

// ── moduleStateStore accessor ──────────────────────────────────────────────

MITIRU_INLINE mitiru::cef::StateStore* mitiru::Engine::moduleStateStore() noexcept
{
	return m_moduleStateStore.get();
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

MITIRU_INLINE std::uint32_t mitiru::Engine::moduleMemorySize() const noexcept
{
	return m_moduleMemorySize;
}

// ── time-travel: GameMemory ring 記録 + rewind (ADR 0017) ──────────────────

MITIRU_INLINE void mitiru::Engine::recordModuleMemoryFrame()
{
	if (m_moduleMemorySize == 0 || m_moduleMemory == nullptr) { return; }  // 非 flat POD / 未 load
	if (m_moduleMemoryRing.frameSize() != m_moduleMemorySize)
	{
		m_moduleMemoryRing.configure(m_moduleMemorySize, 300);  // 60fps × 5sec
	}
	m_moduleMemoryRing.push(m_moduleMemory, m_moduleMemorySize);
}

MITIRU_INLINE const std::uint8_t*
mitiru::Engine::moduleMemoryRingAt(std::size_t offsetFromNewest) const noexcept
{
	return m_moduleMemoryRing.at(offsetFromNewest);
}

MITIRU_INLINE std::size_t mitiru::Engine::moduleMemoryRingSize() const noexcept
{
	return m_moduleMemoryRing.size();
}

MITIRU_INLINE bool
mitiru::Engine::rewindModuleMemory(const void* bytes, std::uint32_t size) noexcept
{
	if (m_moduleMemory == nullptr || bytes == nullptr) { return false; }
	if (size == 0 || size != m_moduleMemorySize) { return false; }  // size guard (reload 防御)
	std::memcpy(m_moduleMemory, bytes, size);
	return true;
}

MITIRU_INLINE std::string
mitiru::Engine::branchModuleMemory(const module::InputSnapshot* inputs, int frameCount)
{
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0
	    || m_moduleApi.on_update == nullptr || inputs == nullptr || frameCount <= 0)
	{
		return "{}";
	}

	// 現 GameMemory を退避 (試行後に bit-exact 復元する)。
	std::vector<std::uint8_t> saved(m_moduleMemorySize);
	std::memcpy(saved.data(), m_moduleMemory, m_moduleMemorySize);

	// 台本入力で on_update を frameCount 回回す。draw/present/intents drain は一切しない
	// (= sound/state push 等の副作用が外に出ない)。intents は使い捨て (~50KB なので heap)。
	auto intents = std::make_unique<module::FrameIntents>();
	for (int i = 0; i < frameCount; ++i)
	{
		std::memset(intents.get(), 0, sizeof(module::FrameIntents));
		m_moduleApi.on_update(m_moduleMemory, Engine::kFixedDt, &inputs[i], intents.get());
	}

	// 試行後の state を reflected JSON に。
	nlohmann::json state = observe::reflectToJson(
		static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
		m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
		m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);

	// GameMemory を試行前へ復元 (live は何も変わらなかったことになる)。
	std::memcpy(m_moduleMemory, saved.data(), m_moduleMemorySize);
	return state.dump();
}
