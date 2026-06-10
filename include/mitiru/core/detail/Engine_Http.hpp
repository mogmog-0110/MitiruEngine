// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/ReflectDiff.hpp>

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

	// AI Lens (ADR 0018): reflected GameMemory を read / diff / what-if する AI 向け面。
	// game に MITIRU_REFLECT が無ければ reflectFieldCount==0 で "{}"/"[]" を返す。
	cb.aiState = [this]() -> std::string {
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleApi.reflectFieldCount <= 0)
		{ return "{}"; }
		return observe::reflectToJson(static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
			m_moduleApi.reflectFields, m_moduleApi.reflectFieldCount,
			m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount).dump();
	};
	cb.aiStateAt = [this](int off) -> std::string {
		if (m_moduleApi.reflectFieldCount <= 0 || off < 0) { return "{}"; }
		const std::uint8_t* p = m_moduleMemoryRing.at(static_cast<std::size_t>(off));
		if (p == nullptr) { return "{}"; }
		return observe::reflectToJson(p, m_moduleMemorySize, m_moduleApi.reflectFields,
			m_moduleApi.reflectFieldCount, m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount).dump();
	};
	cb.aiStateDiff = [this](int from, int to) -> std::string {
		if (m_moduleApi.reflectFieldCount <= 0) { return "[]"; }
		const std::uint8_t* a = m_moduleMemoryRing.at(static_cast<std::size_t>(from < 0 ? 0 : from));
		const std::uint8_t* b = m_moduleMemoryRing.at(static_cast<std::size_t>(to < 0 ? 0 : to));
		if (a == nullptr || b == nullptr) { return "[]"; }
		const auto ja = observe::reflectToJson(a, m_moduleMemorySize, m_moduleApi.reflectFields,
			m_moduleApi.reflectFieldCount, m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);
		const auto jb = observe::reflectToJson(b, m_moduleMemorySize, m_moduleApi.reflectFields,
			m_moduleApi.reflectFieldCount, m_moduleApi.reflectSchemas, m_moduleApi.reflectSchemaCount);
		return observe::reflectDiff(ja, jb).dump();
	};
	cb.aiRingSize = [this]() -> int { return static_cast<int>(m_moduleMemoryRing.size()); };
	cb.aiBranch = [this](const std::string& keysCsv, int frames) -> std::string {
		if (m_moduleApi.reflectFieldCount <= 0 || frames <= 0) { return "{}"; }
		const auto vkOf = [](const std::string& n) -> int {
			if (n == "Left")  { return 0x25; } if (n == "Up")    { return 0x26; }
			if (n == "Right") { return 0x27; } if (n == "Down")  { return 0x28; }
			if (n == "Space") { return 0x20; } if (n == "Enter") { return 0x0D; }
			if (n.size() == 1) {
				char c = n[0];
				if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 32); }
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { return static_cast<int>(c); }
			}
			return -1;
		};
		std::vector<int>   vks;
		std::stringstream  ss(keysCsv);
		std::string        tok;
		while (std::getline(ss, tok, ','))
		{
			const std::size_t a = tok.find_first_not_of(" \t");
			const std::size_t b = tok.find_last_not_of(" \t");
			if (a != std::string::npos) { const int vk = vkOf(tok.substr(a, b - a + 1)); if (vk >= 0) { vks.push_back(vk); } }
		}
		std::vector<module::InputSnapshot> seq(static_cast<std::size_t>(frames));
		for (std::size_t i = 0; i < seq.size(); ++i)
		{
			std::memset(&seq[i], 0, sizeof(module::InputSnapshot));
			seq[i].rngSeed = m_config.randomSeed;
			for (const int vk : vks)
			{
				if (vk >= 0 && vk < 256) { seq[i].keysDown[vk] = 1; if (i == 0) { seq[i].keysJustPressed[vk] = 1; } }
			}
		}
		return branchModuleMemory(seq.data(), frames);
	};

	// AI 音観測 (/api/ai/audio): 適用済み SoundIntent の固定リングを JSON で返す。
	cb.audioLogJson = [this](int max) -> std::string {
		const std::size_t m = max > 0 ? static_cast<std::size_t>(max) : 64;
		return "{\"total\":" + std::to_string(m_audioLog.totalCount()) +
		       ",\"events\":" + m_audioLog.toJson(m) + "}";
	};

	// AI フレーム観測 (/api/ai/frame): Screen の draw log を on/off + JSON 直列化。
	cb.drawLogEnable = [this](bool on) { if (m_screen) { m_screen->setDrawLogEnabled(on); } };
	cb.drawLogJson = [this]() -> std::string {
		if (m_screen == nullptr) { return "[]"; }
		const auto& log = m_screen->drawLog();
		std::string out = "[";
		bool first = true;
		for (const auto& e : log)
		{
			if (!first) { out += ","; }
			first = false;
			out += "{\"call\":\"";
			out += e.call;
			out += "\",\"x\":" + std::to_string(e.x) + ",\"y\":" + std::to_string(e.y)
			     + ",\"w\":" + std::to_string(e.w) + ",\"h\":" + std::to_string(e.h);
			if (e.text[0] != '\0') { out += ",\"text\":\"" + observe::jsonEscape(e.text) + "\""; }
			out += "}";
		}
		out += "]";
		return out;
	};

	m_httpServer->setCallbacks(cb);
	m_httpServer->setInputInjector(&m_inputInjector);
	m_httpServer->setFlags(&m_gameFlags);
	m_httpServer->setConfig(&m_config);

	if (!m_httpServer->init(port))
	{
		m_httpServer.reset();
	}
	else
	{
		// listen 開始の合図 (AI / 自動化が polling をやめて叩き始められる、R-02)。
		std::fprintf(stderr,
			"[ai] HTTP API listening on 127.0.0.1:%d "
			"(/api/status, /api/ai/state, /api/ai/diff, /api/ai/branch)\n",
			port);
	}
}
