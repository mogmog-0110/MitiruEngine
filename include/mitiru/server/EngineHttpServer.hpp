#pragma once

/// @file EngineHttpServer.hpp
/// @brief エンジン組み込みHTTP APIサーバー
/// @details 外部ツール（MCPサーバー、エディタ等）からエンジンを制御するための
///          軽量HTTPサーバー。CommandSystemを通じてコマンド実行、スクリーンショット
///          取得、シーン情報の問い合わせなどを提供する。
///          ゲームループの poll() でノンブロッキングに動作する。
///          ハンドラ実装は server/detail/EngineHttp_*.hpp に分割 (末尾 include)。

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
#include <utility>
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
#include <mitiru/observe/SnapshotSchema.hpp>
#include <mitiru/util/Base64.hpp>
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

	// ── runtime コントロール (ADR 0011) ─────────────────────────────
	// `mitiru_console` GUI sub-window / 外部ツールが叩く。各 callable は
	// 設定されてれば host が engine の対応 API へ振り分ける。
	std::function<bool()>           runtimeTogglePause; ///< 戻り値 = toggle 後の paused
	std::function<bool()>           runtimeIsPaused;
	std::function<void()>           runtimeStep;        ///< paused 時に 1 フレーム進める
	std::function<void(float)>      runtimeSetTimeScale;
	std::function<float()>          runtimeGetTimeScale;
	std::function<bool()>           runtimeToggleLofi; ///< 戻り値 = toggle 後の lofi enabled
	std::function<bool()>           runtimeIsLofiEnabled;

	// ── AI Lens (ADR 0018) ─────────────────────────────────────────
	// reflected GameMemory を構造的に read / diff / what-if する AI 向け面。
	std::function<std::string()>                       aiState;     ///< 現フレームの reflected JSON
	std::function<std::string(int)>                    aiStateAt;   ///< ring N フレーム前の reflected JSON
	std::function<std::string(int, int)>               aiStateDiff; ///< reflectDiff(ring.at(from), at(to))
	std::function<std::string(const std::string&, int)> aiBranch;   ///< (keysCsv, frames) → 反実仮想結果
	std::function<int()>                               aiRingSize;  ///< time-travel ring の保持フレーム数

	// ── Inspector 観測 (ADR 0019) ─────────────────────────────────
	// Inspector key-value ストアへの read-only アクセス。
	// nullptr = Inspector 未配線 → 503 を返す。
	std::function<std::string(const std::string&)> inspectorQuery;   ///< prefix でフィルタ (空=全件)
	std::function<std::string(std::size_t)>        inspectorAt;      ///< back=N の過去スナップショット
	std::function<std::size_t()>                   inspectorDepth;   ///< 現在の履歴エントリ数
	std::function<std::size_t()>                   inspectorCapacity; ///< 履歴の最大容量

	// ── AI フレーム観測 (/api/ai/frame) ──────────────────────────
	// Screen の draw log (何をどこに描いたか) への read-only アクセス。
	std::function<void(bool)>    drawLogEnable; ///< draw log 記録の on/off
	std::function<std::string()> drawLogJson;   ///< 当フレームの draw log JSON 配列

	// ── AI 音観測 (/api/ai/audio) ────────────────────────────────
	std::function<std::string(int)> audioLogJson; ///< 最新 max 件の音イベント JSON

	// ── capture() の実寸 ─────────────────────────────────────────
	// 論理 Screen サイズ ≠ ウィンドウ実寸のゲームで screenshot の stride ズレを防ぐ。
	std::function<std::pair<int, int>()> captureDims; ///< capture() が返す pixel buffer の (幅, 高さ)
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
			if (path == "/console.html" || path == "/" || path == "/console")
			{ handleConsoleHtml(req, resp); return; }
			if (path == "/api/runtime/status")    { handleRuntimeStatus(req, resp); return; }
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
			if (path == "/api/ai/state")          { handleAiState(req, resp); return; }
			if (path == "/api/ai/diff")           { handleAiDiff(req, resp); return; }
			if (path == "/api/ai/ringsize")       { handleAiRingSize(req, resp); return; }

			// ── Inspector / 観測 (ADR 0019) ──────────────────────────────────
			if (path == "/api/ai/frame")            { handleAiFrame(req, resp); return; }
			if (path == "/api/ai/audio")            { handleAiAudio(req, resp); return; }
			if (path == "/api/health")              { handleHealth(req, resp); return; }
			if (path == "/api/observe/schema")      { handleObserveSchema(req, resp); return; }
			if (path == "/api/observe/inspect")     { handleObserveInspect(req, resp); return; }
			if (path == "/api/observe/inspect/at")  { handleObserveInspectAt(req, resp); return; }
			if (path == "/api/observe/inspect/depth") { handleObserveInspectDepth(req, resp); return; }

			if (path.rfind("/api/commands/", 0) == 0 && path.size() > 14)
			{
				handleCommandsByCategory(req, resp);
				return;
			}
		}

		if (req.method == "POST")
		{
			if (path == "/api/runtime/pause")     { handleRuntimePause(req, resp); return; }
			if (path == "/api/runtime/step")      { handleRuntimeStep(req, resp); return; }
			if (path == "/api/runtime/timescale") { handleRuntimeTimeScale(req, resp); return; }
			if (path == "/api/runtime/lofi")      { handleRuntimeLofi(req, resp); return; }
			if (path == "/api/runtime/quit")      { handleRuntimeQuit(req, resp); return; }
			if (path == "/api/command")              { handleCommand(req, resp); return; }
			if (path == "/api/flag")                 { handleSetFlag(req, resp); return; }
			if (path == "/api/input/simulate")       { handleInputSimulate(req, resp); return; }
			if (path == "/api/scene/create-node")    { handleCreateNode(req, resp); return; }
			if (path == "/api/scene/save")           { handleSaveScene(req, resp); return; }
			if (path == "/api/scene/load")           { handleLoadScene(req, resp); return; }
			if (path == "/api/editor/focus")         { handleFocusNode(req, resp); return; }
			if (path == "/api/game/run")             { handleRunGame(req, resp); return; }
			if (path == "/api/game/stop")            { handleStopGame(req, resp); return; }
			if (path == "/api/ai/branch")            { handleAiBranch(req, resp); return; }

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

	// ── ハンドラ宣言 (実装は server/detail/EngineHttp_*.hpp) ──────────

	// status / console / runtime / コマンド / フラグ / 設定 / 入力 / デバッグ
	// → detail/EngineHttp_RuntimeHandlers.hpp
	void handleStatus(const HttpRequest&, HttpResponse& resp);
	void handleConsoleHtml(const HttpRequest&, HttpResponse& resp);
	void handleRuntimeStatus(const HttpRequest&, HttpResponse& resp);
	void handleRuntimePause(const HttpRequest&, HttpResponse& resp);
	void handleRuntimeStep(const HttpRequest&, HttpResponse& resp);
	void handleRuntimeTimeScale(const HttpRequest& req, HttpResponse& resp);
	void handleRuntimeLofi(const HttpRequest&, HttpResponse& resp);
	void handleRuntimeQuit(const HttpRequest&, HttpResponse& resp);
	void handleCommand(const HttpRequest& req, HttpResponse& resp);
	void handleCommands(const HttpRequest&, HttpResponse& resp);
	void handleCommandsByCategory(const HttpRequest& req, HttpResponse& resp);
	void handleGetFlags(const HttpRequest&, HttpResponse& resp);
	void handleSetFlag(const HttpRequest& req, HttpResponse& resp);
	void handleConfig(const HttpRequest&, HttpResponse& resp);
	void handleRenderStats(const HttpRequest&, HttpResponse& resp);
	void handleInputSimulate(const HttpRequest& req, HttpResponse& resp);
	void handleGetErrors(const HttpRequest&, HttpResponse& resp);
	void handleGetLog(const HttpRequest&, HttpResponse& resp);
	void handleProjectInfo(const HttpRequest&, HttpResponse& resp);
	void handleRunGame(const HttpRequest&, HttpResponse& resp);
	void handleStopGame(const HttpRequest&, HttpResponse& resp);

	// AI Lens (ADR 0018) / Inspector 観測 (ADR 0019) / AI フレーム・音観測
	// → detail/EngineHttp_AiHandlers.hpp
	void handleAiState(const HttpRequest& req, HttpResponse& resp);
	void handleAiDiff(const HttpRequest& req, HttpResponse& resp);
	void handleAiRingSize(const HttpRequest&, HttpResponse& resp);
	void handleAiBranch(const HttpRequest& req, HttpResponse& resp);
	void handleHealth(const HttpRequest&, HttpResponse& resp);
	void handleObserveSchema(const HttpRequest&, HttpResponse& resp);
	void handleObserveInspect(const HttpRequest& req, HttpResponse& resp);
	void handleObserveInspectAt(const HttpRequest& req, HttpResponse& resp);
	void handleObserveInspectDepth(const HttpRequest&, HttpResponse& resp);
	[[nodiscard]] std::pair<int, int> captureSourceDims() const;
	[[nodiscard]] std::string buildScreenshotJson(int srcW, int srcH, int reqW, int reqH);
	void handleAiFrame(const HttpRequest& req, HttpResponse& resp);
	void handleAiAudio(const HttpRequest& req, HttpResponse& resp);

	// screenshot / シーン操作 / エディタ
	// → detail/EngineHttp_SceneHandlers.hpp
	void handleScreenshot(const HttpRequest& req, HttpResponse& resp);
	void handleScene(const HttpRequest&, HttpResponse& resp);
	void handleSceneTree(const HttpRequest&, HttpResponse& resp);
	void handleCreateNode(const HttpRequest& req, HttpResponse& resp);
	void handleDeleteNode(const HttpRequest& req, HttpResponse& resp);
	void handleUpdateNode(const HttpRequest& req, HttpResponse& resp);
	void handleAddTrait(const HttpRequest& req, HttpResponse& resp);
	void handleRemoveTrait(const HttpRequest& req, HttpResponse& resp);
	void handleSaveScene(const HttpRequest& req, HttpResponse& resp);
	void handleLoadScene(const HttpRequest& req, HttpResponse& resp);
	void handleEditorState(const HttpRequest&, HttpResponse& resp);
	void handleSetCamera(const HttpRequest& req, HttpResponse& resp);
	void handleSelectNode(const HttpRequest& req, HttpResponse& resp);
	void handleFocusNode(const HttpRequest& req, HttpResponse& resp);

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
	bool m_drawLogActive = false; ///< /api/ai/frame 初回呼び出しで draw log を有効化済みか
	CommandSystem* m_commandSystem = nullptr;
	InputInjector* m_inputInjector = nullptr;
	std::map<std::string, std::string>* m_flags = nullptr;
	const EngineConfig* m_config = nullptr;

#ifdef _WIN32
	bool m_wsaInitialized = false;
#endif
};

} // namespace mitiru::server

// ── ハンドラ実装 (分割 detail) ──
#include <mitiru/server/detail/EngineHttp_RuntimeHandlers.hpp>
#include <mitiru/server/detail/EngineHttp_AiHandlers.hpp>
#include <mitiru/server/detail/EngineHttp_SceneHandlers.hpp>

#endif // !__EMSCRIPTEN__
