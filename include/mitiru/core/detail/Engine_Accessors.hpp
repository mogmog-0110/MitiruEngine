// Detail header for mitiru::Engine - do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── One-liner accessor out-of-class definitions ───────────────────────────
// Pure pass-through getters/setters and trivial helpers extracted from
// Engine.hpp to keep the main class declaration compact. Anything with
// non-trivial logic stays in Engine.hpp itself.

// -- Destructor ------------------------------------------------------------

MITIRU_INLINE mitiru::Engine::~Engine()
{
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

MITIRU_INLINE void mitiru::Engine::setDiffTracker(observe::StructuredDiff* tracker) noexcept
{
	m_diffTracker = tracker;
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
