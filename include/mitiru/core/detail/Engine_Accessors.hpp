// mitiru::Engine の detail header — 直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── 一行 accessor の class 外定義 ───────────────────────────
// 純粋な pass-through な getter/setter と些末な helper を Engine.hpp から
// 抜き出し、本体 class 宣言を簡潔に保つ。non-trivial な logic は Engine.hpp
// 本体に残す。

// -- Destructor ------------------------------------------------------------

MITIRU_INLINE mitiru::Engine::~Engine()
{
	// MITIRU_RECORD が設定されていれば replay 記録を自動保存する。Best-effort:
	// I/O error は握り潰し、output dir が無い / read-only path でも destructor を
	// 巻き込まないようにする (残りの cleanup chain を失うのを防ぐ)。
	if (m_inputRecorder.isRecording() && !m_recordOutputPath.empty())
	{
		try
		{
			auto data = m_inputRecorder.endRecording();
			data.saveToFile(m_recordOutputPath);
		}
		catch (...)
		{
			// 意図的に握り潰す。この時点で logger の保証は無い
		}
	}

	// CEF / HTTP より先に game DLL を破棄する。ModuleHost 内で on_shutdown +
	// FreeLibrary を呼ぶ。m_moduleHost の unique_ptr もこの後に自動破棄され、
	// runModule() を経由しなかった場合の safety net になる。
	unloadModule();

	m_cefContext.shutdown();
	if (m_httpServer)
	{
		m_httpServer->shutdown();
	}
}

// -- CEF -------------------------------------------------------------------

MITIRU_INLINE CefContext* mitiru::Engine::cefContext() noexcept
{
	return &m_cefContext;
}

// -- World / Scene ---------------------------------------------------------

MITIRU_INLINE void mitiru::Engine::setWorld(ecs::MitiruWorld* world) noexcept
{
	m_world = world;
}

MITIRU_INLINE void mitiru::Engine::setSceneManager(scene::MitiruSceneManager* mgr) noexcept
{
	m_sceneManager = mgr;
}

// -- Audio engine getter ---------------------------------------------------

MITIRU_INLINE mitiru::audio::IAudioEngine* mitiru::Engine::audioEngine() noexcept
{
	return m_audioEngine.get();
}

// -- Volume getters --------------------------------------------------------

MITIRU_INLINE float mitiru::Engine::masterVolume() const noexcept
{
	return m_masterVolume;
}

MITIRU_INLINE float mitiru::Engine::bgmVolume() const noexcept
{
	return m_bgmVolume;
}

MITIRU_INLINE float mitiru::Engine::seVolume() const noexcept
{
	return m_seVolume;
}

MITIRU_INLINE float mitiru::Engine::voiceVolume() const noexcept
{
	return m_voiceVolume;
}

// -- Config ----------------------------------------------------------------

MITIRU_INLINE const mitiru::EngineConfig& mitiru::Engine::config() const noexcept
{
	return m_config;
}

MITIRU_INLINE mitiru::EngineConfig& mitiru::Engine::mutableConfig() noexcept
{
	return m_config;
}

// -- Observer / validator setters ------------------------------------------

MITIRU_INLINE void mitiru::Engine::setTemporalChecker(validate::TemporalInvariantChecker* checker) noexcept
{
	m_temporalChecker = checker;
}

MITIRU_INLINE void mitiru::Engine::setCausalChain(observe::CausalChain* chain) noexcept
{
	m_causalChain = chain;
}

// -- PostProcess accessors -------------------------------------------------

MITIRU_INLINE mitiru::render::PostProcessManager* mitiru::Engine::postProcess() noexcept
{
	return m_postProcess.get();
}

MITIRU_INLINE const mitiru::render::PostProcessManager* mitiru::Engine::postProcess() const noexcept
{
	return m_postProcess.get();
}

// -- Loop control ----------------------------------------------------------

MITIRU_INLINE void mitiru::Engine::requestStop() noexcept
{
	m_shouldStop = true;
}

// -- Frame number ----------------------------------------------------------

MITIRU_INLINE std::uint64_t mitiru::Engine::frameNumber() const noexcept
{
	return m_clock ? m_clock->frameNumber() : 0;
}

// -- Input accessors -------------------------------------------------------

MITIRU_INLINE mitiru::InputInjector& mitiru::Engine::inputInjector() noexcept
{
	return m_inputInjector;
}

MITIRU_INLINE mitiru::InputRecorder& mitiru::Engine::inputRecorder() noexcept
{
	return m_inputRecorder;
}

MITIRU_INLINE const mitiru::InputRecorder& mitiru::Engine::inputRecorder() const noexcept
{
	return m_inputRecorder;
}

MITIRU_INLINE mitiru::InputReplayer& mitiru::Engine::inputReplayer() noexcept
{
	return m_inputReplayer;
}

MITIRU_INLINE const mitiru::InputReplayer& mitiru::Engine::inputReplayer() const noexcept
{
	return m_inputReplayer;
}

MITIRU_INLINE const mitiru::InputState& mitiru::Engine::inputState() const noexcept
{
	return m_inputState;
}

// -- Screen accessors ------------------------------------------------------

MITIRU_INLINE const mitiru::Screen* mitiru::Engine::screen() const noexcept
{
	return m_screen.get();
}

MITIRU_INLINE mitiru::Screen* mitiru::Engine::screen() noexcept
{
	return m_screen.get();
}

// -- HTTP server accessor --------------------------------------------------

MITIRU_INLINE mitiru::server::EngineHttpServer* mitiru::Engine::httpServer() noexcept
{
	return m_httpServer.get();
}

MITIRU_INLINE void mitiru::Engine::setCommandSystem(CommandSystem* cmd) noexcept
{
	if (m_httpServer)
	{
		m_httpServer->setCommandSystem(cmd);
	}
}

// -- Clock accessor --------------------------------------------------------

MITIRU_INLINE const mitiru::Clock* mitiru::Engine::clock() const noexcept
{
	return m_clock.get();
}

// -- Game flag accessors ---------------------------------------------------

MITIRU_INLINE void mitiru::Engine::setGameFlag(const std::string& key, const std::string& value)
{
	m_gameFlags[key] = value;
}

MITIRU_INLINE std::string mitiru::Engine::getGameFlag(const std::string& key) const
{
	const auto it = m_gameFlags.find(key);
	return (it != m_gameFlags.end()) ? it->second : std::string{};
}

// -- Private utilities ----------------------------------------------------

MITIRU_INLINE float mitiru::Engine::clampVol(float v) noexcept
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// -- Static image-save helpers --------------------------------------------

MITIRU_INLINE void mitiru::Engine::saveBmp(const std::string& path,
	const std::vector<std::uint8_t>& pixels, int w, int h)
{
	util::saveBmp(path, pixels, w, h);
}

MITIRU_INLINE void mitiru::Engine::savePng(const std::string& path,
	const std::vector<std::uint8_t>& pixels, int w, int h)
{
	util::savePng(path, pixels, w, h);
}
