#pragma once
// EngineHttpServer の runtime 制御 / コマンド / フラグ / 設定 / 入力 / デバッグ系ハンドラ実装。
// server/EngineHttpServer.hpp から末尾 include される (単体 include も親経由で自己完結)。

#include <mitiru/server/EngineHttpServer.hpp>

#ifndef __EMSCRIPTEN__

#include <string>

#include <mitiru/core/CommandSystem.hpp>
#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/server/JsonHelper.hpp>

// ── ステータスエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleStatus(const HttpRequest&, HttpResponse& resp)
{
	std::string json = "{\"running\":true";
	if (m_callbacks.getFrameNumber)
	{
		json += ",\"frameNumber\":" + std::to_string(m_callbacks.getFrameNumber());
		const auto* clk = m_callbacks.getClock();
		if (clk)
		{
			const float elapsed = clk->elapsed();
			const float fps = (elapsed > 0.0f)
				? static_cast<float>(m_callbacks.getFrameNumber()) / elapsed : 0.0f;
			json += ",\"fps\":" + std::to_string(fps);
			json += ",\"elapsed\":" + std::to_string(elapsed);
		}
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

// ── コントロールパネル HTML (ADR 0011 phase 2) ─────────────────
// engine 自身が console.html を serve する。`mitiru_host --http-port N` 起動後、
// ブラウザで http://127.0.0.1:N/ を開けば pause/step/scale/screenshot ボタンが
// 並ぶ UI が出る。phase 3 で --console flag による自動ブラウザ起動を予定。

inline void mitiru::server::EngineHttpServer::handleConsoleHtml(const HttpRequest&, HttpResponse& resp)
{
	static constexpr const char* kHtml =
R"HTML(<!DOCTYPE html>
<html lang="ja">
<head><meta charset="utf-8"><title>Mitiru Control</title>
<style>
 body{background:#1b1d22;color:#dee2e6;font-family:system-ui,sans-serif;margin:0;padding:18px;}
 h1{margin:0 0 12px;font-size:16px;font-weight:600;letter-spacing:.04em;}
 #s{background:#272a31;border:1px solid #3a3f47;border-radius:6px;padding:10px 12px;
    margin-bottom:14px;font-family:monospace;font-size:12px;}
 .row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px;}
 button{background:#3a78c2;border:0;color:#fff;padding:8px 14px;border-radius:5px;
        font-size:13px;cursor:pointer;}
 button:hover{background:#4a8fdc;}
 button.alt{background:#444a55;}
 button.alt:hover{background:#525866;}
 .scale{background:#4a623e;}
 .scale:hover{background:#5a754d;}
</style></head>
<body>
<h1>Mitiru Control</h1>
<div id="s">loading...</div>
<div class="row">
  <button onclick="post('/api/runtime/pause')">pause / play</button>
  <button class="alt" onclick="post('/api/runtime/step')">step 1 frame</button>
</div>
<div class="row">
  <button class="scale" onclick="setScale(0.25)">0.25x</button>
  <button class="scale" onclick="setScale(0.5)">0.5x</button>
  <button class="scale" onclick="setScale(1)">1x</button>
  <button class="scale" onclick="setScale(2)">2x</button>
  <button class="scale" onclick="setScale(4)">4x</button>
</div>
<div class="row">
  <button class="alt" onclick="post('/api/runtime/lofi')">lo-fi toggle</button>
  <button class="alt" onclick="window.open('/api/screenshot','_blank')">screenshot (open PNG)</button>
  <button class="alt" style="background:#7a3b3b" onclick="post('/api/runtime/quit')">quit game</button>
</div>
<script>
const $s = document.getElementById('s');
async function post(path, body){
  try { await fetch(path,{method:'POST',body:body?JSON.stringify(body):''}); }
  catch(e){ console.error(e); }
  await refresh();
}
async function setScale(v){ await post('/api/runtime/timescale', {value: String(v)}); }
async function refresh(){
  try {
    const r = await fetch('/api/runtime/status'); const j = await r.json();
    $s.textContent = `paused: ${j.paused} | time-scale: ${j.timeScale.toFixed(2)}x | frame: ${j.frame}`;
  } catch(e){ $s.textContent = '(engine not responding)'; }
}
refresh(); setInterval(refresh, 500);
</script>
</body></html>
)HTML";
	resp.status = 200;
	resp.contentType = "text/html; charset=utf-8";
	resp.setBody(kHtml);
}

// ── runtime コントロール (ADR 0011) ──────────────────────────

inline void mitiru::server::EngineHttpServer::handleRuntimeStatus(const HttpRequest&, HttpResponse& resp)
{
	const bool paused = m_callbacks.runtimeIsPaused ? m_callbacks.runtimeIsPaused() : false;
	const float scale = m_callbacks.runtimeGetTimeScale ? m_callbacks.runtimeGetTimeScale() : 1.0f;
	const std::uint64_t frame = m_callbacks.getFrameNumber ? m_callbacks.getFrameNumber() : 0;
	std::string json = "{\"paused\":";
	json += (paused ? "true" : "false");
	json += ",\"timeScale\":" + std::to_string(scale);
	json += ",\"frame\":" + std::to_string(frame);
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRuntimePause(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.runtimeTogglePause)
	{
		resp.status = 503;
		resp.setBody(R"({"success":false,"message":"runtime control not wired"})");
		return;
	}
	const bool newPaused = m_callbacks.runtimeTogglePause();
	std::string json = "{\"success\":true,\"paused\":";
	json += (newPaused ? "true" : "false");
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRuntimeStep(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.runtimeStep)
	{
		resp.status = 503;
		resp.setBody(R"({"success":false,"message":"runtime control not wired"})");
		return;
	}
	m_callbacks.runtimeStep();
	resp.status = 200;
	resp.setBody(R"({"success":true})");
}

inline void mitiru::server::EngineHttpServer::handleRuntimeTimeScale(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.runtimeSetTimeScale)
	{
		resp.status = 503;
		resp.setBody(R"({"success":false,"message":"runtime control not wired"})");
		return;
	}
	const auto raw = detail::extractJsonString(req.body, "value");
	float v = 1.0f;
	try { v = std::stof(raw); } catch (...) { resp.status = 400; resp.setBody(R"({"success":false,"message":"value must be a number"})"); return; }
	if (v < 0.0f) { resp.status = 400; resp.setBody(R"({"success":false,"message":"value must be >= 0"})"); return; }
	m_callbacks.runtimeSetTimeScale(v);
	std::string json = "{\"success\":true,\"timeScale\":" + std::to_string(v) + "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRuntimeLofi(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.runtimeToggleLofi)
	{
		resp.status = 503;
		resp.setBody(R"({"success":false,"message":"runtime control not wired"})");
		return;
	}
	const bool nowEnabled = m_callbacks.runtimeToggleLofi();
	std::string json = "{\"success\":true,\"lofi\":";
	json += (nowEnabled ? "true" : "false");
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRuntimeQuit(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.requestStop)
	{
		resp.status = 503;
		resp.setBody(R"({"success":false,"message":"requestStop not wired"})");
		return;
	}
	m_callbacks.requestStop();
	resp.status = 200;
	resp.setBody(R"({"success":true})");
}

// ── コマンドエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleCommand(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_commandSystem) { resp.status = 500; resp.setBody(R"({"success":false,"message":"command system not available"})"); return; }
	if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }

	const auto command = detail::extractJsonString(req.body, "command");
	if (command.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'command' field"})"); return; }

	const auto result = m_commandSystem->executeString(command);
	std::string json = "{\"success\":" + std::string(result.success ? "true" : "false");
	json += ",\"message\":\"" + observe::jsonEscape(result.message) + "\"";
	if (!result.output.empty())
	{
		json += ",\"output\":[";
		for (std::size_t i = 0; i < result.output.size(); ++i)
		{
			if (i > 0) { json += ","; }
			json += "\"" + observe::jsonEscape(result.output[i]) + "\"";
		}
		json += "]";
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleCommands(const HttpRequest&, HttpResponse& resp)
{
	if (!m_commandSystem) { resp.status = 200; resp.setBody(R"({"commands":[],"count":0})"); return; }

	const auto& commands = m_commandSystem->commands();
	std::string json = "{\"commands\":[";
	for (std::size_t i = 0; i < commands.size(); ++i)
	{
		if (i > 0) { json += ","; }
		json += commandDefToJson(commands[i]);
	}
	json += "],\"count\":" + std::to_string(commands.size());

	const auto cats = m_commandSystem->categories();
	json += ",\"categories\":[";
	for (std::size_t i = 0; i < cats.size(); ++i)
	{
		if (i > 0) { json += ","; }
		json += "\"" + observe::jsonEscape(cats[i]) + "\"";
	}
	json += "]}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleCommandsByCategory(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_commandSystem) { resp.status = 200; resp.setBody(R"({"commands":[],"count":0})"); return; }

	const auto category = req.path.substr(14);
	const auto commands = m_commandSystem->commandsInCategory(category);
	std::string json = "{\"category\":\"" + observe::jsonEscape(category) + "\"";
	json += ",\"commands\":[";
	for (std::size_t i = 0; i < commands.size(); ++i)
	{
		if (i > 0) { json += ","; }
		json += commandDefToJson(*commands[i]);
	}
	json += "],\"count\":" + std::to_string(commands.size()) + "}";
	resp.status = 200;
	resp.setBody(json);
}

// ── フラグエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleGetFlags(const HttpRequest&, HttpResponse& resp)
{
	std::string json = "{\"flags\":{";
	if (m_flags)
	{
		bool first = true;
		for (const auto& [key, value] : *m_flags)
		{
			if (!first) { json += ","; }
			json += "\"" + observe::jsonEscape(key) + "\":";
			bool isNumber = false;
			if (!value.empty())
			{
				try { std::size_t idx = 0; (void)std::stod(value, &idx); isNumber = (idx == value.size()); } catch (...) {}
			}
			if (isNumber) { json += value; }
			else if (value == "true" || value == "false") { json += value; }
			else { json += "\"" + observe::jsonEscape(value) + "\""; }
			first = false;
		}
	}
	json += "}}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleSetFlag(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_flags) { resp.status = 500; resp.setBody(R"({"success":false,"message":"flag store not available"})"); return; }
	if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }
	const auto key = detail::extractJsonString(req.body, "key");
	const auto value = detail::extractJsonString(req.body, "value");
	if (key.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'key' field"})"); return; }
	(*m_flags)[key] = value;
	resp.status = 200;
	resp.setBody(R"({"success":true,"key":")" + observe::jsonEscape(key) + R"(","value":")" + observe::jsonEscape(value) + "\"}");
}

// ── 設定/レンダリングエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleConfig(const HttpRequest&, HttpResponse& resp)
{
	std::string json = "{";
	if (m_config)
	{
		json += "\"title\":\"" + observe::jsonEscape(m_config->title) + "\"";
		json += ",\"windowWidth\":" + std::to_string(m_config->windowWidth);
		json += ",\"windowHeight\":" + std::to_string(m_config->windowHeight);
		json += ",\"headless\":" + std::string(m_config->headless ? "true" : "false");
		json += ",\"deterministic\":" + std::string(m_config->deterministic ? "true" : "false");
		json += ",\"targetTps\":" + std::to_string(m_config->targetTps);
		json += ",\"enableObserver\":" + std::string(m_config->enableObserver ? "true" : "false");
		json += ",\"enableDiffTracking\":" + std::string(m_config->enableDiffTracking ? "true" : "false");
		json += ",\"enableCausalTracking\":" + std::string(m_config->enableCausalTracking ? "true" : "false");
		json += ",\"enableTemporalValidation\":" + std::string(m_config->enableTemporalValidation ? "true" : "false");
		json += ",\"enableUIValidation\":" + std::string(m_config->enableUIValidation ? "true" : "false");
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRenderStats(const HttpRequest&, HttpResponse& resp)
{
	std::string json = "{";
	if (m_callbacks.getFrameNumber)
	{
		const auto* screen = m_callbacks.getScreen();
		if (screen)
		{
			json += "\"drawCalls\":" + std::to_string(screen->drawCallCount());
			json += ",\"screenWidth\":" + std::to_string(screen->width());
			json += ",\"screenHeight\":" + std::to_string(screen->height());
		}
		const auto* clk = m_callbacks.getClock();
		if (clk)
		{
			const float elapsed = clk->elapsed();
			const std::uint64_t frame = m_callbacks.getFrameNumber();
			const float fps = (elapsed > 0.0f) ? static_cast<float>(frame) / elapsed : 0.0f;
			json += ",\"fps\":" + std::to_string(fps);
			json += ",\"frameNumber\":" + std::to_string(frame);
		}
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

// ── 入力シミュレーションエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleInputSimulate(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_inputInjector) { resp.status = 500; resp.setBody(R"({"success":false,"message":"input injector not available"})"); return; }
	if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }

	const auto inputType = detail::extractJsonString(req.body, "type", "key");

	if (inputType == "mouse_move")
	{
		const float x = detail::extractJsonFloat(req.body, "x", 0.0f);
		const float y = detail::extractJsonFloat(req.body, "y", 0.0f);
		InputCommand cmd; cmd.type = InputCommandType::MouseMove; cmd.mouseX = x; cmd.mouseY = y;
		m_inputInjector->inject(cmd);
		resp.status = 200;
		resp.setBody(R"({"success":true,"type":"mouse_move","x":)" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");
		return;
	}

	if (inputType == "mouse_click" || inputType == "mouse_down" || inputType == "mouse_up")
	{
		const float x = detail::extractJsonFloat(req.body, "x", -1.0f);
		const float y = detail::extractJsonFloat(req.body, "y", -1.0f);
		const int button = detail::extractJsonInt(req.body, "button", 0);

		if (x >= 0.0f && y >= 0.0f)
		{
			InputCommand move; move.type = InputCommandType::MouseMove; move.mouseX = x; move.mouseY = y;
			m_inputInjector->inject(move);
		}

		if (inputType == "mouse_click")
		{
			InputCommand down; down.type = InputCommandType::MouseDown; down.mouseButton = button; m_inputInjector->inject(down);
			InputCommand up; up.type = InputCommandType::MouseUp; up.mouseButton = button; m_inputInjector->inject(up);
		}
		else if (inputType == "mouse_down")
		{
			InputCommand cmd; cmd.type = InputCommandType::MouseDown; cmd.mouseButton = button; m_inputInjector->inject(cmd);
		}
		else
		{
			InputCommand cmd; cmd.type = InputCommandType::MouseUp; cmd.mouseButton = button; m_inputInjector->inject(cmd);
		}

		resp.status = 200;
		resp.setBody(R"({"success":true,"type":")" + inputType + "\"}");
		return;
	}

	const auto key = detail::extractJsonString(req.body, "key");
	const auto action = detail::extractJsonString(req.body, "action", "press");
	if (key.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'key' field"})"); return; }
	const int keyCode = resolveKeyCode(key);
	if (keyCode < 0) { resp.status = 400; resp.setBody(R"({"success":false,"message":"unknown key: )" + observe::jsonEscape(key) + "\"}"); return; }

	if (action == "press")
	{
		InputCommand down; down.type = InputCommandType::KeyDown; down.keyCode = keyCode; m_inputInjector->inject(down);
		InputCommand up; up.type = InputCommandType::KeyUp; up.keyCode = keyCode; m_inputInjector->inject(up);
	}
	else if (action == "down")
	{
		InputCommand cmd; cmd.type = InputCommandType::KeyDown; cmd.keyCode = keyCode; m_inputInjector->inject(cmd);
	}
	else if (action == "up")
	{
		InputCommand cmd; cmd.type = InputCommandType::KeyUp; cmd.keyCode = keyCode; m_inputInjector->inject(cmd);
	}
	else
	{
		resp.status = 400;
		resp.setBody(R"({"success":false,"message":"unknown action: )" + observe::jsonEscape(action) + "\"}");
		return;
	}

	resp.status = 200;
	resp.setBody(R"({"success":true,"key":")" + observe::jsonEscape(key) + R"(","action":")" + observe::jsonEscape(action) + "\"}");
}

// ── デバッグ/ゲーム制御エンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleGetErrors(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.getErrors) { resp.status = 200; resp.setBody(R"({"errors":[]})"); return; }
	resp.status = 200;
	resp.setBody(m_callbacks.getErrors());
}

inline void mitiru::server::EngineHttpServer::handleGetLog(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.getLog) { resp.status = 200; resp.setBody(R"({"log":[]})"); return; }
	resp.status = 200;
	resp.setBody(m_callbacks.getLog());
}

inline void mitiru::server::EngineHttpServer::handleProjectInfo(const HttpRequest&, HttpResponse& resp)
{
	std::string json = "{";
	json += "\"engine\":\"MitiruEngine\"";
	json += ",\"version\":\"" + std::string(SERVER_VERSION_STR) + "\"";
	json += ",\"port\":" + std::to_string(m_port);
	if (m_config)
	{
		json += ",\"title\":\"" + observe::jsonEscape(m_config->title) + "\"";
		json += ",\"windowWidth\":" + std::to_string(m_config->windowWidth);
		json += ",\"windowHeight\":" + std::to_string(m_config->windowHeight);
		json += ",\"headless\":" + std::string(m_config->headless ? "true" : "false");
	}
	if (m_callbacks.getProjectInfo)
	{
		const auto extra = m_callbacks.getProjectInfo();
		if (!extra.empty()) { json += ",\"extra\":" + extra; }
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

inline void mitiru::server::EngineHttpServer::handleRunGame(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.runGame) { resp.status = 500; resp.setBody(R"({"success":false,"message":"run not available"})"); return; }
	const bool ok = m_callbacks.runGame();
	resp.status = ok ? 200 : 500;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

inline void mitiru::server::EngineHttpServer::handleStopGame(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.stopGame)
	{
		if (m_callbacks.requestStop) { m_callbacks.requestStop(); resp.status = 200; resp.setBody(R"({"success":true})"); return; }
		resp.status = 500; resp.setBody(R"({"success":false,"message":"stop not available"})"); return;
	}
	const bool ok = m_callbacks.stopGame();
	resp.status = ok ? 200 : 500;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

#endif // !__EMSCRIPTEN__
