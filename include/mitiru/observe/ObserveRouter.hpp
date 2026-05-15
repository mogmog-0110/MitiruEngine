#pragma once

/// @file ObserveRouter.hpp
/// @brief 観測サーバーのHTTPルーティング

#include <mutex>
#include <string>

#include <mitiru/core/Engine.hpp>
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/observe/HttpServer.hpp>
#include <mitiru/observe/Inspector.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/QueryParser.hpp>
#include <mitiru/observe/SnapshotSchema.hpp>
#include <mitiru/observe/StructuredDiff.hpp>
#include <mitiru/observe/CausalChain.hpp>

namespace mitiru::observe
{

/// @brief 観測サーバーのルーター（スレッドセーフ）
class ObserveRouter
{
public:
	/// @brief コンストラクタ
	ObserveRouter(Engine& engine, Inspector& inspector, InputInjector& inputInjector)
		: m_engine(engine), m_inspector(inspector), m_inputInjector(inputInjector) {}

	/// @brief 全ルートをHTTPサーバーに登録する
	void setupRoutes(HttpServer& server)
	{
		server.registerRoute("GET", "/snapshot",
			[this](const HttpRequest&) { return handleSnapshot(); });
		server.registerRoute("GET", "/screenshot",
			[this](const HttpRequest&) { return handleScreenshot(); });
		server.registerRoute("GET", "/health",
			[this](const HttpRequest&) { return handleHealth(); });
		server.registerRoute("GET", "/inspect",
			[this](const HttpRequest& req) { return handleInspect(req); });
		server.registerRoute("POST", "/input",
			[this](const HttpRequest& req) { return handleInput(req); });
		server.registerRoute("POST", "/command",
			[this](const HttpRequest& req) { return handleCommand(req); });
		server.registerRoute("GET", "/schema",
			[this](const HttpRequest&) { return handleSchema(); });
		server.registerRoute("POST", "/step",
			[this](const HttpRequest& req) { return handleStep(req); });
		server.registerRoute("GET", "/diff",
			[this](const HttpRequest&) { return handleDiff(); });
		server.registerRoute("GET", "/causal",
			[this](const HttpRequest&) { return handleCausal(); });
		server.registerRoute("GET", "/ui",
			[this](const HttpRequest&) { return handleUI(); });
		server.registerRoute("GET", "/inspect/at",
			[this](const HttpRequest& req) { return handleInspectAt(req); });
		server.registerRoute("GET", "/inspect/depth",
			[this](const HttpRequest&) { return handleInspectDepth(); });
	}

private:
	/// @brief JSONレスポンスを構築するヘルパー
	[[nodiscard]] static HttpResponse jsonResp(int code, std::string body)
	{
		HttpResponse resp;
		resp.statusCode = code;
		resp.contentType = "application/json";
		resp.body = std::move(body);
		return resp;
	}

	/// @brief GET /snapshot
	[[nodiscard]] HttpResponse handleSnapshot()
	{
		const std::lock_guard<std::mutex> lock(m_engineMutex);
		return jsonResp(200, m_engine.snapshot());
	}

	/// @brief GET /screenshot - RGBAバイト列を返す
	[[nodiscard]] HttpResponse handleScreenshot()
	{
		const std::lock_guard<std::mutex> lock(m_engineMutex);
		const auto pixels = m_engine.capture();
		if (pixels.empty())
		{
			return jsonResp(404, R"json({"error":"no screenshot available"})json");
		}
		const auto* screen = m_engine.screen();
		HttpResponse resp;
		resp.statusCode = 200;
		resp.contentType = "application/octet-stream";
		resp.body.assign(reinterpret_cast<const char*>(pixels.data()), pixels.size());
		resp.headers["X-Width"] = std::to_string(screen ? screen->width() : 0);
		resp.headers["X-Height"] = std::to_string(screen ? screen->height() : 0);
		return resp;
	}

	/// @brief GET /health
	[[nodiscard]] HttpResponse handleHealth()
	{
		const std::lock_guard<std::mutex> lock(m_engineMutex);
		const auto* clk = m_engine.clock();
		std::string json = R"json({"status":"ok","frameNumber":)json"
			+ std::to_string(m_engine.frameNumber())
			+ R"json(,"elapsed":)json"
			+ std::to_string(clk ? clk->elapsed() : 0.0f) + "}";
		return jsonResp(200, std::move(json));
	}

	/// @brief GET /inspect
	[[nodiscard]] HttpResponse handleInspect(const HttpRequest& request)
	{
		const auto params = parseQuery(request.path);
		if (const auto entity = getParam(params, "entity"))
		{
			const auto result = m_inspector.queryState(*entity);
			if (result.has_value())
			{
				return jsonResp(200, R"json({"entity":")json" + jsonEscape(*entity)
					+ R"json(","data":")json" + jsonEscape(*result) + "\"}");
			}
			return jsonResp(404, R"json({"error":"entity not found"})json");
		}
		if (const auto tag = getParam(params, "tag"))
		{
			return jsonResp(200, m_inspector.queryByPrefix(*tag));
		}
		return jsonResp(200, m_inspector.queryAll());
	}

	/// @brief POST /input - 入力コマンドを注入する
	[[nodiscard]] HttpResponse handleInput(const HttpRequest& request)
	{
		if (request.body.empty())
		{
			return jsonResp(400, R"json({"error":"empty body"})json");
		}
		InputCommand cmd;
		if (!parseInputCommand(request.body, cmd))
		{
			return jsonResp(400, R"json({"error":"invalid input command"})json");
		}
		m_inputInjector.inject(cmd);
		return jsonResp(200, R"json({"status":"injected"})json");
	}

	/// @brief POST /command - エンジンコマンドを実行する
	[[nodiscard]] HttpResponse handleCommand(const HttpRequest& request)
	{
		if (request.body.empty())
		{
			return jsonResp(400, R"json({"error":"empty body"})json");
		}
		if (request.body.find("\"stop\"") != std::string::npos)
		{
			m_engine.requestStop();
			return jsonResp(200, R"json({"status":"stop requested"})json");
		}
		return jsonResp(400, R"json({"error":"unknown command"})json");
	}

	/// @brief GET /schema
	[[nodiscard]] HttpResponse handleSchema()
	{
		return jsonResp(200, SnapshotSchema::schemaJson());
	}

	/// @brief POST /step - 指定フレーム数を進める
	[[nodiscard]] HttpResponse handleStep(const HttpRequest& request)
	{
		const auto params = parseQuery(request.path);
		int frameCount = 1;
		if (const auto fs = getParam(params, "frames"))
		{
			try { frameCount = std::stoi(*fs); } catch (...) { frameCount = 1; }
		}
		if (frameCount < 1 || frameCount > 10000)
		{
			return jsonResp(400, R"json({"error":"frames must be 1-10000"})json");
		}
		return jsonResp(200, R"json({"status":"step requested","frames":)json"
			+ std::to_string(frameCount) + "}");
	}

	/// @brief GET /diff - StructuredDiffスタブ
	[[nodiscard]] HttpResponse handleDiff()
	{
		return jsonResp(200, R"json({"status":"no_diff_tracker_configured"})json");
	}

	/// @brief GET /causal - CausalChainスタブ
	[[nodiscard]] HttpResponse handleCausal()
	{
		return jsonResp(200, R"json({"status":"no_causal_chain_configured"})json");
	}

	/// @brief GET /ui - UISnapshotスタブ
	[[nodiscard]] HttpResponse handleUI()
	{
		return jsonResp(200, R"json({"status":"no_ui_snapshot_configured"})json");
	}

	/// @brief GET /inspect/at?back=<N> - 過去スナップショットをJSON返却する
	/// @details back パラメータが不正（負数・非数）なら 400 を返す。
	///          範囲外（N >= depth）なら "{}" を 200 で返す（queryAllAt の仕様通り）。
	[[nodiscard]] HttpResponse handleInspectAt(const HttpRequest& request)
	{
		const auto params = parseQuery(request.path);
		const auto backParam = getParam(params, "back");
		std::size_t back = 0;

		if (backParam.has_value())
		{
			long long signed_back = 0;
			try { signed_back = std::stoll(*backParam); }
			catch (...) { return jsonResp(400, R"json({"error":"invalid 'back' parameter"})json"); }
			if (signed_back < 0) { return jsonResp(400, R"json({"error":"invalid 'back' parameter"})json"); }
			back = static_cast<std::size_t>(signed_back);
		}

		return jsonResp(200, m_inspector.queryAllAt(back));
	}

	/// @brief GET /inspect/depth - ヒストリー深さと容量をJSON返却する
	[[nodiscard]] HttpResponse handleInspectDepth()
	{
		const std::string json = "{\"depth\":"
			+ std::to_string(m_inspector.historyDepth())
			+ ",\"capacity\":"
			+ std::to_string(m_inspector.historyCapacity())
			+ "}";
		return jsonResp(200, json);
	}

	/// @brief JSONからInputCommandを簡易パースする
	[[nodiscard]] static bool parseInputCommand(const std::string& json, InputCommand& cmd)
	{
		if (json.find("\"type\"") == std::string::npos) { return false; }

		/// type値を判定する
		if      (json.find("\"key_down\"") != std::string::npos)   { cmd.type = InputCommandType::KeyDown; }
		else if (json.find("\"key_up\"") != std::string::npos)     { cmd.type = InputCommandType::KeyUp; }
		else if (json.find("\"mouse_move\"") != std::string::npos) { cmd.type = InputCommandType::MouseMove; }
		else if (json.find("\"mouse_down\"") != std::string::npos) { cmd.type = InputCommandType::MouseDown; }
		else if (json.find("\"mouse_up\"") != std::string::npos)   { cmd.type = InputCommandType::MouseUp; }
		else { return false; }

		extractIntField(json, "keyCode", cmd.keyCode);
		extractIntField(json, "mouseButton", cmd.mouseButton);
		extractFloatField(json, "mouseX", cmd.mouseX);
		extractFloatField(json, "mouseY", cmd.mouseY);

		/// 入力値の範囲検証
		if (cmd.keyCode < 0 || cmd.keyCode > 255) { return false; }
		if (cmd.mouseButton < 0 || cmd.mouseButton > 2) { return false; }
		return true;
	}

	/// @brief JSONから数値フィールドの開始位置を見つける
	[[nodiscard]] static std::size_t findFieldValue(
		const std::string& json, const char* fieldName)
	{
		const std::string key = std::string("\"") + fieldName + "\"";
		const auto pos = json.find(key);
		if (pos == std::string::npos) { return std::string::npos; }
		const auto col = json.find(':', pos + key.size());
		if (col == std::string::npos) { return std::string::npos; }
		auto start = col + 1;
		while (start < json.size() && json[start] == ' ') { ++start; }
		return start;
	}

	/// @brief JSONから整数フィールドを抽出する
	static void extractIntField(const std::string& json, const char* name, int& value)
	{
		const auto start = findFieldValue(json, name);
		if (start == std::string::npos) { return; }
		try { value = std::stoi(json.substr(start)); } catch (...) {}
	}

	/// @brief JSONから浮動小数点フィールドを抽出する
	static void extractFloatField(const std::string& json, const char* name, float& value)
	{
		const auto start = findFieldValue(json, name);
		if (start == std::string::npos) { return; }
		try { value = std::stof(json.substr(start)); } catch (...) {}
	}

	Engine& m_engine;                  ///< エンジンへの参照
	Inspector& m_inspector;            ///< インスペクターへの参照
	InputInjector& m_inputInjector;    ///< 入力インジェクターへの参照
	std::mutex m_engineMutex;          ///< エンジンアクセスのミューテックス
};

} // namespace mitiru::observe
