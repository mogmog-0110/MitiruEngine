// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── HTTP server bridge out-of-class definitions ───────────────────────────

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

	m_httpServer->setCallbacks(cb);
	m_httpServer->setInputInjector(&m_inputInjector);
	m_httpServer->setFlags(&m_gameFlags);
	m_httpServer->setConfig(&m_config);

	if (!m_httpServer->init(port))
	{
		m_httpServer.reset();
	}
}
