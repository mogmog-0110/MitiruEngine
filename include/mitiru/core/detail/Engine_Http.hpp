// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── HTTP server bridge のクラス外定義 ───────────────────────────

MITIRU_INLINE void mitiru::Engine::initHttpServer(int port, Game& game)
{
	m_httpServer = std::make_unique<server::EngineHttpServer>();

	server::EngineBridgeContext ctx;
	ctx.getFrameNumber = [this]() -> std::uint64_t { return frameNumber(); };
	ctx.getClock       = [this]() -> const Clock* { return clock(); };
	ctx.getScreen      = [this]() -> const Screen* { return screen(); };
	ctx.capture        = [this]() -> std::vector<std::uint8_t> { return capture(); };
	ctx.getSnapshot    = [this]() -> std::string { return snapshot(); };
	ctx.requestStop    = [this]() { requestStop(); };
	ctx.gameFlags      = &m_gameFlags;
	ctx.config         = &m_config;

	server::EngineCallbacks cb;
	server::initEngineHttpCallbacks(cb, ctx, game);

	// runtime コントロール (ADR 0011): `mitiru_console` / 外部ツールから叩く。
	cb.runtimeTogglePause   = [this]() -> bool { togglePaused(); return isPaused(); };
	cb.runtimeIsPaused      = [this]() -> bool { return isPaused(); };
	cb.runtimeStep          = [this]() { stepOneFrame(); };
	cb.runtimeSetTimeScale  = [this](float s) { setTimeScale(s); };
	cb.runtimeGetTimeScale  = [this]() -> float { return timeScale(); };
	cb.runtimeToggleLofi    = [this]() -> bool { toggleLofi(); return isLofiEnabled(); };
	cb.runtimeIsLofiEnabled = [this]() -> bool { return isLofiEnabled(); };

	m_httpServer->setCallbacks(cb);
	m_httpServer->setInputInjector(&m_inputInjector);
	m_httpServer->setFlags(&m_gameFlags);
	m_httpServer->setConfig(&m_config);

	if (!m_httpServer->init(port))
	{
		m_httpServer.reset();
	}
}
