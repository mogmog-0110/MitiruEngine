#pragma once

/// @file EngineHttpServer.hpp
/// @brief エンジン組み込みHTTP APIサーバー
/// @details 外部ツール（MCPサーバー、エディタ等）からエンジンを制御するための
///          軽量HTTPサーバー。CommandSystemを通じてコマンド実行、スクリーンショット
///          取得、シーン情報の問い合わせなどを提供する。
///          ゲームループの poll() でノンブロッキングに動作する。

#ifdef __EMSCRIPTEN__
// WASM環境ではHTTPサーバーは不要 — スタブのみ提供
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mitiru { class Game; struct EngineConfig; class Clock; class Screen; class CommandSystem; class InputInjector; }
namespace mitiru::server {

struct EngineCallbacks
{
	std::function<std::uint64_t()> getFrameNumber;
	std::function<const Clock*()> getClock;
	std::function<const Screen*()> getScreen;
	std::function<std::vector<std::uint8_t>()> capture;
	std::function<std::string()> getSnapshot;
	std::function<void()> requestStop;
	std::function<std::string()> getSceneJson;
	std::function<int(const std::string&, const std::string&, int)> createNode;
	std::function<bool(int)> deleteNode;
	std::function<bool(int, const std::string&, const std::string&)> updateNodeProperty;
	std::function<bool(int, const std::string&, const std::string&)> addTrait;
	std::function<bool(int, const std::string&)> removeTrait;
	std::function<bool(const std::string&)> saveScene;
	std::function<bool(const std::string&)> loadScene;
	std::function<bool(int)> selectNode;
	std::function<bool(int)> focusNode;
	std::function<std::string()> getErrors;
	std::function<std::string()> getLog;
	std::function<std::string()> getProjectInfo;
	std::function<bool()> runGame;
	std::function<bool()> stopGame;
	std::function<bool(float, float, float, float, float, float)> setEditorCamera;
};

class EngineHttpServer {
public:
	bool init(int) { return false; }
	void shutdown() {}
	void poll() {}
	[[nodiscard]] bool isRunning() const noexcept { return false; }
	void setCommandSystem(CommandSystem*) {}
	void setCallbacks(const EngineCallbacks&) {}
	void setInputInjector(InputInjector*) {}
	void setFlags(std::map<std::string, std::string>*) {}
	void setConfig(const EngineConfig*) {}
};

} // namespace mitiru::server
#else // !__EMSCRIPTEN__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ── 分割済みモジュール ──
#include <mitiru/server/HttpProtocol.hpp>
#include <mitiru/server/PngEncoder.hpp>
#include <mitiru/server/JsonHelper.hpp>

#include <mitiru/core/CommandSystem.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/QueryParser.hpp>
#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Screen.hpp>

namespace mitiru::server
{

/// @brief エンジンアクセス用コールバック群
/// @details Engineクラスへの循環依存を避けるため、コールバック経由でアクセスする。
struct EngineCallbacks
{
	std::function<std::uint64_t()> getFrameNumber;
	std::function<const Clock*()> getClock;
	std::function<const Screen*()> getScreen;
	std::function<std::vector<std::uint8_t>()> capture;
	std::function<std::string()> getSnapshot;
	std::function<void()> requestStop;
	std::function<std::string()> getSceneJson;
	std::function<int(const std::string& name, const std::string& type, int parentId)> createNode;
	std::function<bool(int nodeId)> deleteNode;
	std::function<bool(int nodeId, const std::string& prop, const std::string& value)> updateNodeProperty;
	std::function<bool(int nodeId, const std::string& traitType, const std::string& traitData)> addTrait;
	std::function<bool(int nodeId, const std::string& traitType)> removeTrait;
	std::function<bool(const std::string& path)> saveScene;
	std::function<bool(const std::string& path)> loadScene;
	std::function<bool(int nodeId)> selectNode;
	std::function<bool(int nodeId)> focusNode;
	std::function<std::string()> getErrors;
	std::function<std::string()> getLog;
	std::function<std::string()> getProjectInfo;
	std::function<bool()> runGame;
	std::function<bool()> stopGame;
	std::function<bool(float yaw, float pitch, float distance, float px, float py, float pz)> setEditorCamera;
};

/// @brief エンジン組み込みHTTP APIサーバー
class EngineHttpServer
{
	static constexpr const char* SERVER_VERSION_STR = "0.2.0";

public:
	EngineHttpServer() = default;

	~EngineHttpServer() { shutdown(); }

	EngineHttpServer(const EngineHttpServer&) = delete;
	EngineHttpServer& operator=(const EngineHttpServer&) = delete;
	EngineHttpServer(EngineHttpServer&&) = delete;
	EngineHttpServer& operator=(EngineHttpServer&&) = delete;

	/// @brief サーバーを初期化して開始する
	bool init(int port = 8090)
	{
		if (m_running.load()) { return false; }

#ifdef _WIN32
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { return false; }
		m_wsaInitialized = true;
#endif

		m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == kInvalidSocket) { return false; }

		int opt = 1;
#ifdef _WIN32
		setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
		           reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
		setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

		setNonBlocking(m_listenSocket);

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		addr.sin_port = htons(static_cast<std::uint16_t>(port));

		if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = kInvalidSocket;
			return false;
		}
		if (::listen(m_listenSocket, 8) < 0)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = kInvalidSocket;
			return false;
		}

		m_port = port;
		m_running.store(true);
		return true;
	}

	void setCallbacks(const EngineCallbacks& callbacks) { m_callbacks = callbacks; }
	void setCommandSystem(CommandSystem* cmd) noexcept { m_commandSystem = cmd; }
	void setInputInjector(InputInjector* injector) noexcept { m_inputInjector = injector; }
	void setFlags(std::map<std::string, std::string>* flags) noexcept { m_flags = flags; }
	void setConfig(const EngineConfig* config) noexcept { m_config = config; }

	/// @brief 毎フレーム呼び出してリクエストを処理する
	void poll()
	{
		if (!m_running.load() || m_listenSocket == kInvalidSocket) { return; }

		for (int i = 0; i < kMaxRequestsPerPoll; ++i)
		{
			const SocketHandle client = ::accept(m_listenSocket, nullptr, nullptr);
			if (client == kInvalidSocket) { break; }
			handleConnection(client);
		}
	}

	void shutdown() noexcept
	{
		m_running.store(false);
		if (m_listenSocket != kInvalidSocket)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = kInvalidSocket;
		}
#ifdef _WIN32
		if (m_wsaInitialized)
		{
			WSACleanup();
			m_wsaInitialized = false;
		}
#endif
	}

	[[nodiscard]] bool isRunning() const noexcept { return m_running.load(); }
	[[nodiscard]] int port() const noexcept { return m_port; }

private:
	// ── 接続処理 ────────────────────────────────

	void handleConnection(SocketHandle clientSocket)
	{
		setRecvTimeout(clientSocket, kRecvTimeoutMs);

		std::string rawRequest(kMaxRequestSize, '\0');
		const auto bytesRead = recv(clientSocket, rawRequest.data(),
		                            static_cast<int>(rawRequest.size()) - 1, 0);
		if (bytesRead <= 0)
		{
			closeSocket(clientSocket);
			return;
		}
		rawRequest.resize(static_cast<std::size_t>(bytesRead));

		const auto request = parseRequest(rawRequest);
		HttpResponse response;

		if (request.method == "OPTIONS")
		{
			response.status = 204;
			response.contentType = "text/plain";
		}
		else
		{
			handleRequest(request, response);
		}

		const auto rawResponse = buildResponse(response);
		send(clientSocket, reinterpret_cast<const char*>(rawResponse.data()),
		     static_cast<int>(rawResponse.size()), 0);
		closeSocket(clientSocket);
	}

	// ── HTTPパース ──────────────────────────────

	[[nodiscard]] static HttpRequest parseRequest(const std::string& raw)
	{
		HttpRequest req;
		const auto lineEnd = raw.find("\r\n");
		if (lineEnd == std::string::npos) { return req; }

		const auto requestLine = std::string_view(raw).substr(0, lineEnd);
		const auto sp1 = requestLine.find(' ');
		if (sp1 == std::string_view::npos) { return req; }
		req.method = std::string(requestLine.substr(0, sp1));
		const auto sp2 = requestLine.find(' ', sp1 + 1);
		req.rawPath = (sp2 == std::string_view::npos)
			? std::string(requestLine.substr(sp1 + 1))
			: std::string(requestLine.substr(sp1 + 1, sp2 - sp1 - 1));

		const auto qpos = req.rawPath.find('?');
		if (qpos != std::string::npos)
		{
			req.path = req.rawPath.substr(0, qpos);
			req.params = observe::parseQuery(req.rawPath);
		}
		else
		{
			req.path = req.rawPath;
		}

		auto pos = lineEnd + 2;
		while (pos < raw.size())
		{
			const auto end = raw.find("\r\n", pos);
			if (end == std::string::npos || end == pos)
			{
				pos = (end == std::string::npos) ? raw.size() : end + 2;
				break;
			}
			const auto line = std::string_view(raw).substr(pos, end - pos);
			const auto col = line.find(':');
			if (col != std::string_view::npos)
			{
				auto val = line.substr(col + 1);
				if (!val.empty() && val[0] == ' ') { val = val.substr(1); }
				req.headers[std::string(line.substr(0, col))] = std::string(val);
			}
			pos = end + 2;
		}

		if (pos < raw.size()) { req.body = raw.substr(pos); }

		return req;
	}

	// ── ルーティング ───────────────────────────────

	void handleRequest(const HttpRequest& req, HttpResponse& resp)
	{
		const auto& path = req.path;

		if (req.method == "GET")
		{
			if (path == "/api/status")           { handleStatus(req, resp); return; }
			if (path == "/api/commands")          { handleCommands(req, resp); return; }
			if (path == "/api/screenshot")        { handleScreenshot(req, resp); return; }
			if (path == "/api/scene")             { handleScene(req, resp); return; }
			if (path == "/api/scene/tree")        { handleSceneTree(req, resp); return; }
			if (path == "/api/flags")             { handleGetFlags(req, resp); return; }
			if (path == "/api/config")            { handleConfig(req, resp); return; }
			if (path == "/api/render/stats")      { handleRenderStats(req, resp); return; }
			if (path == "/api/editor/state")      { handleEditorState(req, resp); return; }
			if (path == "/api/editor/screenshot") { handleScreenshot(req, resp); return; }
			if (path == "/api/debug/errors")      { handleGetErrors(req, resp); return; }
			if (path == "/api/debug/log")         { handleGetLog(req, resp); return; }
			if (path == "/api/project/info")      { handleProjectInfo(req, resp); return; }

			if (path.rfind("/api/commands/", 0) == 0 && path.size() > 14)
			{
				handleCommandsByCategory(req, resp);
				return;
			}
		}

		if (req.method == "POST")
		{
			if (path == "/api/command")              { handleCommand(req, resp); return; }
			if (path == "/api/flag")                 { handleSetFlag(req, resp); return; }
			if (path == "/api/input/simulate")       { handleInputSimulate(req, resp); return; }
			if (path == "/api/scene/create-node")    { handleCreateNode(req, resp); return; }
			if (path == "/api/scene/save")           { handleSaveScene(req, resp); return; }
			if (path == "/api/scene/load")           { handleLoadScene(req, resp); return; }
			if (path == "/api/editor/focus")         { handleFocusNode(req, resp); return; }
			if (path == "/api/game/run")             { handleRunGame(req, resp); return; }
			if (path == "/api/game/stop")            { handleStopGame(req, resp); return; }

			if (path.rfind("/api/scene/node/", 0) == 0 && path.find("/trait") != std::string::npos
				&& path.find("/trait/") == std::string::npos)
			{
				handleAddTrait(req, resp);
				return;
			}
		}

		if (req.method == "PUT")
		{
			if (path.rfind("/api/scene/node/", 0) == 0 && path.size() > 16
				&& path.find("/trait") == std::string::npos)
			{
				handleUpdateNode(req, resp);
				return;
			}
			if (path == "/api/editor/camera")   { handleSetCamera(req, resp); return; }
			if (path == "/api/editor/select")    { handleSelectNode(req, resp); return; }
		}

		if (req.method == "DELETE")
		{
			if (path.rfind("/api/scene/node/", 0) == 0 && path.size() > 16
				&& path.find("/trait/") == std::string::npos)
			{
				handleDeleteNode(req, resp);
				return;
			}
			if (path.rfind("/api/scene/node/", 0) == 0 && path.find("/trait/") != std::string::npos)
			{
				handleRemoveTrait(req, resp);
				return;
			}
		}

		resp.status = 404;
		resp.setBody(R"({"error":"not found","path":")" + observe::jsonEscape(path) + "\"}");
	}

	// ── ステータスエンドポイント ──────────────────────────────

	void handleStatus(const HttpRequest&, HttpResponse& resp)
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

	// ── コマンドエンドポイント ──────────────────────────────

	void handleCommand(const HttpRequest& req, HttpResponse& resp)
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

	void handleCommands(const HttpRequest&, HttpResponse& resp)
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

	void handleCommandsByCategory(const HttpRequest& req, HttpResponse& resp)
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

	// ── スクリーンショットエンドポイント ──────────────────────────────

	void handleScreenshot(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.getFrameNumber) { resp.status = 500; resp.setBody(R"({"error":"engine not available"})"); return; }

		const auto pixels = m_callbacks.capture();
		if (pixels.empty()) { resp.status = 404; resp.setBody(R"({"error":"no screenshot available"})"); return; }

		const auto* screen = m_callbacks.getScreen();
		const int w = screen ? screen->width() : 0;
		const int h = screen ? screen->height() : 0;
		if (w <= 0 || h <= 0) { resp.status = 500; resp.setBody(R"({"error":"invalid screen dimensions"})"); return; }

		const auto wParam = observe::getParam(req.params, "width");
		const auto hParam = observe::getParam(req.params, "height");

		std::vector<std::uint8_t> srcPixels = pixels;
		int outW = w;
		int outH = h;

		if (wParam.has_value() || hParam.has_value())
		{
			int reqW = 0, reqH = 0;
			try { if (wParam.has_value()) { reqW = std::stoi(*wParam); } } catch (...) {}
			try { if (hParam.has_value()) { reqH = std::stoi(*hParam); } } catch (...) {}

			if (reqW > 0 && reqH > 0 && (reqW != w || reqH != h))
			{
				srcPixels = detail::resizePixels(pixels, w, h, reqW, reqH);
				outW = reqW; outH = reqH;
			}
			else if (reqW > 0 && reqH <= 0)
			{
				const int calcH = (reqW * h) / w;
				if (reqW != w || calcH != h) { srcPixels = detail::resizePixels(pixels, w, h, reqW, calcH); outW = reqW; outH = calcH; }
			}
			else if (reqH > 0 && reqW <= 0)
			{
				const int calcW = (reqH * w) / h;
				if (calcW != w || reqH != h) { srcPixels = detail::resizePixels(pixels, w, h, calcW, reqH); outW = calcW; outH = reqH; }
			}
		}

		const auto png = detail::encodePng(srcPixels.data(), outW, outH);
		if (png.empty()) { resp.status = 500; resp.setBody(R"({"error":"PNG encoding failed"})"); return; }

		resp.status = 200;
		resp.contentType = "image/png";
		resp.body = png;
	}

	// ── シーンエンドポイント ──────────────────────────────

	void handleScene(const HttpRequest&, HttpResponse& resp)
	{
		if (!m_callbacks.getFrameNumber) { resp.status = 200; resp.setBody(R"({"nodes":[]})"); return; }
		resp.status = 200;
		resp.setBody(m_callbacks.getSnapshot());
	}

	void handleSceneTree(const HttpRequest&, HttpResponse& resp)
	{
		if (!m_callbacks.getSceneJson) { resp.status = 200; resp.setBody(R"({"nodes":[]})"); return; }
		resp.status = 200;
		resp.setBody(m_callbacks.getSceneJson());
	}

	void handleCreateNode(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.createNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }

		const auto name = detail::extractJsonString(req.body, "name", "Node");
		const auto type = detail::extractJsonString(req.body, "type", "empty");
		const int parentId = detail::extractJsonInt(req.body, "parentId", -1);
		const int newId = m_callbacks.createNode(name, type, parentId);
		if (newId < 0) { resp.status = 500; resp.setBody(R"({"success":false,"message":"failed to create node"})"); return; }
		resp.status = 200;
		resp.setBody("{\"success\":true,\"nodeId\":" + std::to_string(newId) + "}");
	}

	void handleDeleteNode(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.deleteNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		const int nodeId = extractNodeIdFromPath(req.path, "/api/scene/node/");
		if (nodeId < 0) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid node id"})"); return; }
		const bool ok = m_callbacks.deleteNode(nodeId);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleUpdateNode(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.updateNodeProperty) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }
		const int nodeId = extractNodeIdFromPath(req.path, "/api/scene/node/");
		if (nodeId < 0) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid node id"})"); return; }
		const auto prop = detail::extractJsonString(req.body, "property");
		const auto value = detail::extractJsonString(req.body, "value");
		if (prop.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'property' field"})"); return; }
		const bool ok = m_callbacks.updateNodeProperty(nodeId, prop, value);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleAddTrait(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.addTrait) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }
		const auto traitPos = req.path.find("/trait");
		const auto prefix = std::string("/api/scene/node/");
		const int nodeId = extractNodeIdFromPath(req.path.substr(0, traitPos), prefix);
		if (nodeId < 0) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid node id"})"); return; }
		const auto traitType = detail::extractJsonString(req.body, "traitType");
		if (traitType.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'traitType' field"})"); return; }
		const bool ok = m_callbacks.addTrait(nodeId, traitType, req.body);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleRemoveTrait(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.removeTrait) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		const auto traitSlash = req.path.find("/trait/");
		if (traitSlash == std::string::npos) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid path"})"); return; }
		const auto prefix = std::string("/api/scene/node/");
		const auto nodeIdStr = req.path.substr(prefix.size(), traitSlash - prefix.size());
		const auto traitType = req.path.substr(traitSlash + 7);
		int nodeId = -1;
		try { nodeId = std::stoi(nodeIdStr); } catch (...) {}
		if (nodeId < 0 || traitType.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid node id or trait type"})"); return; }
		const bool ok = m_callbacks.removeTrait(nodeId, traitType);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleSaveScene(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.saveScene) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		const auto filePath = detail::extractJsonString(req.body, "filePath");
		if (filePath.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'filePath'"})"); return; }
		const bool ok = m_callbacks.saveScene(filePath);
		resp.status = ok ? 200 : 500;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleLoadScene(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.loadScene) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
		const auto filePath = detail::extractJsonString(req.body, "filePath");
		if (filePath.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'filePath'"})"); return; }
		const bool ok = m_callbacks.loadScene(filePath);
		resp.status = ok ? 200 : 500;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	// ── フラグエンドポイント ──────────────────────────────

	void handleGetFlags(const HttpRequest&, HttpResponse& resp)
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

	void handleSetFlag(const HttpRequest& req, HttpResponse& resp)
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

	void handleConfig(const HttpRequest&, HttpResponse& resp)
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

	void handleRenderStats(const HttpRequest&, HttpResponse& resp)
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

	void handleInputSimulate(const HttpRequest& req, HttpResponse& resp)
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

	// ── エディタエンドポイント ──────────────────────────────

	void handleEditorState(const HttpRequest&, HttpResponse& resp)
	{
		resp.status = 200;
		resp.setBody("{\"editor\":{}}");
	}

	void handleSetCamera(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.setEditorCamera) { resp.status = 500; resp.setBody(R"({"success":false,"message":"editor camera not available"})"); return; }
		if (req.body.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"empty body"})"); return; }

		const float yaw = detail::extractJsonFloat(req.body, "yaw", 0.0f);
		const float pitch = detail::extractJsonFloat(req.body, "pitch", 0.0f);
		const float distance = detail::extractJsonFloat(req.body, "distance", 10.0f);
		const float pivotX = detail::extractJsonFloat(req.body, "pivotX", 0.0f);
		const float pivotY = detail::extractJsonFloat(req.body, "pivotY", 0.0f);
		const float pivotZ = detail::extractJsonFloat(req.body, "pivotZ", 0.0f);

		const auto preset = detail::extractJsonString(req.body, "preset");
		float finalYaw = yaw, finalPitch = pitch;
		if (!preset.empty())
		{
			if (preset == "front")       { finalYaw = 0.0f;    finalPitch = 0.0f; }
			else if (preset == "back")   { finalYaw = 180.0f;  finalPitch = 0.0f; }
			else if (preset == "left")   { finalYaw = -90.0f;  finalPitch = 0.0f; }
			else if (preset == "right")  { finalYaw = 90.0f;   finalPitch = 0.0f; }
			else if (preset == "top")    { finalYaw = 0.0f;    finalPitch = 90.0f; }
			else if (preset == "bottom") { finalYaw = 0.0f;    finalPitch = -90.0f; }
		}

		const bool ok = m_callbacks.setEditorCamera(finalYaw, finalPitch, distance, pivotX, pivotY, pivotZ);
		resp.status = ok ? 200 : 500;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleSelectNode(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.selectNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"selection not available"})"); return; }
		const int nodeId = detail::extractJsonInt(req.body, "nodeId", -1);
		const bool ok = m_callbacks.selectNode(nodeId);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleFocusNode(const HttpRequest& req, HttpResponse& resp)
	{
		if (!m_callbacks.focusNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"focus not available"})"); return; }
		const int nodeId = detail::extractJsonInt(req.body, "nodeId", -1);
		const bool ok = m_callbacks.focusNode(nodeId);
		resp.status = ok ? 200 : 404;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	// ── デバッグ/ゲーム制御エンドポイント ──────────────────────────────

	void handleGetErrors(const HttpRequest&, HttpResponse& resp)
	{
		if (!m_callbacks.getErrors) { resp.status = 200; resp.setBody(R"({"errors":[]})"); return; }
		resp.status = 200;
		resp.setBody(m_callbacks.getErrors());
	}

	void handleGetLog(const HttpRequest&, HttpResponse& resp)
	{
		if (!m_callbacks.getLog) { resp.status = 200; resp.setBody(R"({"log":[]})"); return; }
		resp.status = 200;
		resp.setBody(m_callbacks.getLog());
	}

	void handleProjectInfo(const HttpRequest&, HttpResponse& resp)
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

	void handleRunGame(const HttpRequest&, HttpResponse& resp)
	{
		if (!m_callbacks.runGame) { resp.status = 500; resp.setBody(R"({"success":false,"message":"run not available"})"); return; }
		const bool ok = m_callbacks.runGame();
		resp.status = ok ? 200 : 500;
		resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
	}

	void handleStopGame(const HttpRequest&, HttpResponse& resp)
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

	// ── ユーティリティ ────────────────────────────────

	[[nodiscard]] static int extractNodeIdFromPath(const std::string& path, const std::string& prefix)
	{
		if (path.size() <= prefix.size()) { return -1; }
		const auto idStr = path.substr(prefix.size());
		try { return std::stoi(idStr); }
		catch (...) { return -1; }
	}

	[[nodiscard]] static std::string commandDefToJson(const CommandDef& def)
	{
		std::string json = "{\"name\":\"" + observe::jsonEscape(def.name) + "\"";
		json += ",\"category\":\"" + observe::jsonEscape(def.category) + "\"";
		json += ",\"description\":\"" + observe::jsonEscape(def.description) + "\"";
		json += ",\"usage\":\"" + observe::jsonEscape(def.usage) + "\"";
		json += ",\"args\":[";
		for (std::size_t i = 0; i < def.argNames.size(); ++i)
		{
			if (i > 0) { json += ","; }
			json += "{\"name\":\"" + observe::jsonEscape(def.argNames[i]) + "\"";
			if (i < def.argTypes.size()) { json += ",\"type\":\"" + observe::jsonEscape(def.argTypes[i]) + "\""; }
			if (i < def.argRequired.size()) { json += ",\"required\":" + std::string(def.argRequired[i] ? "true" : "false"); }
			json += "}";
		}
		json += "]}";
		return json;
	}

	[[nodiscard]] static int resolveKeyCode(const std::string& name)
	{
		if (name.size() == 1)
		{
			const char c = name[0];
			if (c >= 'a' && c <= 'z') { return static_cast<int>(c - 'a') + 0x41; }
			if (c >= 'A' && c <= 'Z') { return static_cast<int>(c); }
			if (c >= '0' && c <= '9') { return static_cast<int>(c); }
		}

		if (name == "space")  { return 0x20; }
		if (name == "enter" || name == "return") { return 0x0D; }
		if (name == "escape" || name == "esc")   { return 0x1B; }
		if (name == "tab")    { return 0x09; }
		if (name == "backspace") { return 0x08; }
		if (name == "delete" || name == "del")   { return 0x2E; }
		if (name == "left")   { return 0x25; }
		if (name == "up")     { return 0x26; }
		if (name == "right")  { return 0x27; }
		if (name == "down")   { return 0x28; }
		if (name == "shift")  { return 0x10; }
		if (name == "ctrl" || name == "control") { return 0x11; }
		if (name == "alt")    { return 0x12; }

		if (name.size() >= 2 && name[0] == 'f')
		{
			try { const int n = std::stoi(name.substr(1)); if (n >= 1 && n <= 12) { return 0x70 + n - 1; } } catch (...) {}
		}

		try { const int code = std::stoi(name); if (code >= 0 && code <= 255) { return code; } } catch (...) {}

		return -1;
	}

	// ── メンバ変数 ─────────────────────────────────

	static constexpr int kMaxRequestsPerPoll = 4;
	static constexpr int kRecvTimeoutMs = 2000;
	static constexpr int kMaxRequestSize = 65536;

	SocketHandle m_listenSocket = kInvalidSocket;
	std::atomic<bool> m_running{false};
	int m_port = 0;

	EngineCallbacks m_callbacks;
	CommandSystem* m_commandSystem = nullptr;
	InputInjector* m_inputInjector = nullptr;
	std::map<std::string, std::string>* m_flags = nullptr;
	const EngineConfig* m_config = nullptr;

#ifdef _WIN32
	bool m_wsaInitialized = false;
#endif
};

} // namespace mitiru::server

#endif // !__EMSCRIPTEN__
