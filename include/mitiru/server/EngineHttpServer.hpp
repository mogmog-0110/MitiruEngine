#pragma once

/// @file EngineHttpServer.hpp
/// @brief エンジン組み込み HTTP API サーバー
/// @details 外部ツール（MCP サーバー、エディタ等）からエンジンを制御するための
///          軽量 HTTP サーバー。CommandSystem を通じてコマンド実行、スクリーンショット
///          取得、シーン情報の問い合わせなどを提供する。
///          受信は HttpListener のワーカーが行い、ハンドラはゲームループの poll() の中で動く。
///          ハンドラ実装は server/detail/EngineHttp_*.hpp に分割 (末尾 include)。

// EngineCallbacks は std::function だけの portable な束なので、定義は 1 つに
// まとめて全 platform で共有する。WASM 用の写しを別に持つと、Engine 側で
// フィールドが増えるたびに写しがずれて「no member named...」のエラーになる。
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mitiru { class Game; struct EngineConfig; class Clock; class Screen;
class CommandSystem; class InputInjector; }

namespace mitiru::server
{

/// @brief エンジンアクセス用コールバック群
/// @details Engine クラスへの循環依存を避けるため、コールバック経由でアクセスする。
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

	// ── runtime コントロール ─────────────────────────────
	// `mitiru_console` GUI sub-window / 外部ツールが呼ぶ。各 callable は
	// 設定されていれば host が engine の対応 API へ振り分ける。
	std::function<bool()>           runtimeTogglePause; ///< 戻り値 = toggle 後の paused
	std::function<bool()>           runtimeIsPaused;
	std::function<void()>           runtimeStep;        ///< paused 時に 1 フレーム進める
	std::function<void(float)>      runtimeSetTimeScale;
	std::function<bool(std::uint32_t)> runtimeResim;  ///< k フレーム前へ巻き戻して記録入力で再生
	std::function<float()>          runtimeGetTimeScale;
	std::function<bool()>           runtimeToggleLofi; ///< 戻り値 = toggle 後の lofi enabled
	std::function<bool()>           runtimeIsLofiEnabled;

	// ── AI Lens ─────────────────────────────────────────
	// reflected GameMemory を構造的に read / diff / what-if する AI 向け面。
	std::function<std::string()>                       aiState;     ///< 現フレームの reflected JSON
	std::function<std::string(int)>                    aiStateAt;   ///< ring N フレーム前の reflected JSON
	std::function<std::string(int, int)>               aiStateDiff; ///< reflectDiff(ring.at(from), at(to))
	std::function<std::string(const std::string&, int)> aiBranch;   ///< (keysCsv, frames) → 反実仮想結果
	std::function<int()>                               aiRingSize;  ///< rewind ring の保持フレーム数
	// aiStatePut: {"field": value,...} を書き戻す (3-3)。branch と同じ経路 (現フレームを
	// 複製して書き換え、live には残さない) なので決定論も rewind ring もおかしくならない。
	// statusOut に 200/400(unknown field・kind mismatch)/503(未配線) を書く。
	std::function<std::string(const std::string& fieldsJson, int& statusOut)> aiStatePut;
	// O6 なぜビュー: フィールドを最後に書いた phase (game opt-in) + ring 8 フレームの値推移。
	std::function<std::string(const std::string& fieldPath)> aiWhy;
	// 型の台帳: game が MITIRU_SPAWNER_TYPES_EXPORT で出した「置ける型」の一覧 JSON (HE2 §4)。
	// 未対応 game でも {"supported":false,"types":[]} を返す (「型が無い」と読ませない)。
	std::function<std::string()> aiTypes;
	// O4 分岐候補: {"variants":[{"overrides":{...}},...], "keys":"...", "frames":N} を
	// そのまま渡し、各案を K フレーム進めた結果 JSON をまとめて返す (整形は callback 側)。
	std::function<std::string(const std::string& bodyJson)> aiCandidates;

	// P10「1 フレームの解剖図」(/api/frame/anatomy?frame=N): 入力→書かれたフィールド(blame 付き)
	// →描画コマンド→音を 1 レスポンスで返す。既存の why/diff/drawLog/audioLog を束ねるだけの
	// callback で、新しい状態は持たない (Engine_Http.hpp 側の実装コメント参照)。
	std::function<std::string(int framesAgo)> frameAnatomy;

	// ── 分岐エディタ (ADR 0035): 残す / 捨てる ──────────────────────
	// aiStatePut は毎回「複製→書き換え→読む→復元」で live には残らない (前述コメント参照)。
	// commit は同じ書き換えを復元せずそのまま live GameMemory へ確定する ("残す" = O3 の
	// 「以後の起動・録画はこの新しい状態を正史として扱う」の GameMemory 版。Spawner JSON への
	// 追加の書き戻しは Game 側が `module::writeBackFieldsToJson` を使って自分で行う対象で、
	// host はここでは行わない (Host-Game 境界は signal-only。host はどの JSON ファイルが
	// どの struct を生んだかを知らない))。discard は何もせず現在の live state を返すだけ
	// (ADR 0035 の「捨てる: 何もしない」を明示的な HTTP 応答として揃える)。
	std::function<std::string(const std::string& fieldsJson, int& statusOut)> aiCommit;
	std::function<std::string()>                                             aiDiscard;

	// ── Inspector 観測 ─────────────────────────────────
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

}  // namespace mitiru::server

#ifdef __EMSCRIPTEN__
// WASM 環境では HTTP サーバーは不要。スタブのみ提供
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mitiru { class Game; struct EngineConfig; class Clock; class Screen; class CommandSystem; class InputInjector; }
namespace mitiru::server {


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

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

// ── 分割済みモジュール ──
#include <mitiru/server/HttpListener.hpp>
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


/// @brief エンジン組み込み HTTP API サーバー
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

	/// @brief 127.0.0.1:port で待ち受けを始める
	/// @param port 0 なら空いているポートを使う (実際の番号は port() で読む)
	bool init(int port = 8090)
	{
		return m_listener.start(port);
	}

	void setCallbacks(const EngineCallbacks& callbacks) { m_callbacks = callbacks; }
	void setCommandSystem(CommandSystem* cmd) noexcept { m_commandSystem = cmd; }
	void setInputInjector(InputInjector* injector) noexcept { m_inputInjector = injector; }
	void setFlags(std::map<std::string, std::string>* flags) noexcept { m_flags = flags; }
	void setConfig(const EngineConfig* config) noexcept { m_config = config; }

	/// @brief 毎フレーム呼び出し、届いているリクエストをこのスレッドで処理する
	void poll()
	{
		m_listener.serve(kMaxRequestsPerPoll, [this](const HttpRequest& req, HttpResponse& resp)
		{
			if (req.method == "OPTIONS")
			{
				resp.status = 204;
				resp.contentType = "text/plain";
				return;
			}
			handleRequest(req, resp);
		});
	}

	void shutdown() noexcept { m_listener.stop(); }

	[[nodiscard]] bool isRunning() const noexcept { return m_listener.isRunning(); }
	[[nodiscard]] int port() const noexcept { return m_listener.port(); }

private:
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
			if (path == "/api/ai/why")            { handleAiWhy(req, resp); return; }
			if (path == "/api/ai/types")          { handleAiTypes(req, resp); return; }
			if (path == "/api/frame/anatomy")     { handleFrameAnatomy(req, resp); return; }

			// ── Inspector / 観測 ──────────────────────────────────
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
			if (path == "/api/runtime/resim") { handleRuntimeResim(req, resp); return; }
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
			if (path == "/api/ai/commit")            { handleAiCommit(req, resp); return; }
			if (path == "/api/ai/discard")           { handleAiDiscard(req, resp); return; }
			if (path == "/api/ai/candidates")        { handleAiCandidates(req, resp); return; }

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
			if (path == "/api/ai/state")         { handleAiStatePut(req, resp); return; }
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
	void handleRuntimeResim(const HttpRequest& req, HttpResponse& resp);
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

	// AI Lens / Inspector 観測 / AI フレーム・音観測
	// → detail/EngineHttp_AiHandlers.hpp
	void handleAiState(const HttpRequest& req, HttpResponse& resp);
	void handleAiDiff(const HttpRequest& req, HttpResponse& resp);
	void handleAiRingSize(const HttpRequest&, HttpResponse& resp);
	void handleAiBranch(const HttpRequest& req, HttpResponse& resp);
	void handleAiStatePut(const HttpRequest& req, HttpResponse& resp);
	void handleAiCommit(const HttpRequest& req, HttpResponse& resp);
	void handleAiDiscard(const HttpRequest& req, HttpResponse& resp);
	void handleAiWhy(const HttpRequest& req, HttpResponse& resp);
	void handleAiTypes(const HttpRequest& req, HttpResponse& resp);
	void handleAiCandidates(const HttpRequest& req, HttpResponse& resp);
	void handleFrameAnatomy(const HttpRequest& req, HttpResponse& resp);
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

	HttpListener m_listener;
	EngineCallbacks m_callbacks;
	bool m_drawLogActive = false; ///< /api/ai/frame 初回呼び出しで draw log を有効化済みか
	CommandSystem* m_commandSystem = nullptr;
	InputInjector* m_inputInjector = nullptr;
	std::map<std::string, std::string>* m_flags = nullptr;
	const EngineConfig* m_config = nullptr;
};

} // namespace mitiru::server

// ── ハンドラ実装 (分割 detail) ──
#include <mitiru/server/detail/EngineHttp_RuntimeHandlers.hpp>
#include <mitiru/server/detail/EngineHttp_AiHandlers.hpp>
#include <mitiru/server/detail/EngineHttp_SceneHandlers.hpp>

#endif // !__EMSCRIPTEN__
