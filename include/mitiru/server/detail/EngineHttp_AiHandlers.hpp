#pragma once
// EngineHttpServer の AI Lens (ADR 0018) / Inspector 観測 (ADR 0019) / AI フレーム・音観測系ハンドラ実装。
// server/EngineHttpServer.hpp から末尾 include される (単体 include も親経由で自己完結)。

#include <mitiru/server/EngineHttpServer.hpp>

#ifndef __EMSCRIPTEN__

#include <string>
#include <utility>
#include <vector>

#include <mitiru/core/Clock.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/observe/QueryParser.hpp>
#include <mitiru/observe/SnapshotSchema.hpp>
#include <mitiru/server/JsonHelper.hpp>
#include <mitiru/server/PngEncoder.hpp>
#include <mitiru/util/Base64.hpp>

// ── AI Lens (ADR 0018) ────────────────────────────────────────
// reflected GameMemory を構造的に read / diff / what-if する AI 向け面。
// callback が未配線 (game に MITIRU_REFLECT が無い等) なら 503。

inline void mitiru::server::EngineHttpServer::handleAiState(const HttpRequest& req, HttpResponse& resp)
{
	const auto frame = observe::getParam(req.params, "frame");
	if (frame.has_value())  // 過去フレーム (ring offset)
	{
		if (!m_callbacks.aiStateAt)
		{ resp.status = 503; resp.setBody(R"({"error":"reflection not wired"})"); return; }
		int off = 0;
		try { off = std::stoi(*frame); }
		catch (...) { resp.status = 400; resp.setBody(R"({"error":"frame must be an integer"})"); return; }
		resp.status = 200; resp.setBody(m_callbacks.aiStateAt(off));
		return;
	}
	if (!m_callbacks.aiState)
	{ resp.status = 503; resp.setBody(R"({"error":"reflection not wired - game に MITIRU_REFLECT がありますか?"})"); return; }
	resp.status = 200; resp.setBody(m_callbacks.aiState());
}

inline void mitiru::server::EngineHttpServer::handleAiDiff(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.aiStateDiff)
	{ resp.status = 503; resp.setBody(R"({"error":"reflection not wired"})"); return; }
	int from = 1, to = 0;  // 既定: 1 フレーム前 → 現在
	try {
		if (auto f = observe::getParam(req.params, "from"); f.has_value()) { from = std::stoi(*f); }
		if (auto t = observe::getParam(req.params, "to");   t.has_value()) { to   = std::stoi(*t); }
	} catch (...) { resp.status = 400; resp.setBody(R"({"error":"from/to must be integers"})"); return; }
	resp.status = 200; resp.setBody(m_callbacks.aiStateDiff(from, to));
}

inline void mitiru::server::EngineHttpServer::handleAiRingSize(const HttpRequest&, HttpResponse& resp)
{
	const int n = m_callbacks.aiRingSize ? m_callbacks.aiRingSize() : 0;
	resp.status = 200; resp.setBody("{\"ringSize\":" + std::to_string(n) + "}");
}

inline void mitiru::server::EngineHttpServer::handleAiBranch(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.aiBranch)
	{ resp.status = 503; resp.setBody(R"({"error":"branch not wired - game に MITIRU_REFLECT がありますか?"})"); return; }
	const auto keys = detail::extractJsonString(req.body, "keys", "");
	int frames = 30;
	try { frames = std::stoi(detail::extractJsonString(req.body, "frames", "30")); }
	catch (...) { frames = 30; }
	if (frames < 1)   { frames = 1; }
	if (frames > 600) { frames = 600; }  // 上限 10 秒 @60fps
	resp.status = 200; resp.setBody(m_callbacks.aiBranch(keys, frames));
}

// ── Inspector / 観測エンドポイント (ADR 0019) ──────────────────

/// @brief GET /api/health — frame + elapsed を返す簡易ヘルスチェック
inline void mitiru::server::EngineHttpServer::handleHealth(const HttpRequest&, HttpResponse& resp)
{
	std::string json = R"({"status":"ok")";
	if (m_callbacks.getFrameNumber)
	{
		json += ",\"frameNumber\":" + std::to_string(m_callbacks.getFrameNumber());
		const auto* clk = m_callbacks.getClock();
		if (clk) { json += ",\"elapsed\":" + std::to_string(clk->elapsed()); }
	}
	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

/// @brief GET /api/observe/schema — SnapshotSchema の JSON Schema を返す
inline void mitiru::server::EngineHttpServer::handleObserveSchema(const HttpRequest&, HttpResponse& resp)
{
	resp.status = 200;
	resp.setBody(observe::SnapshotSchema::schemaJson());
}

/// @brief GET /api/observe/inspect[?prefix=<p>] — Inspector key-value クエリ
/// @details prefix パラメータがあればプレフィックスフィルタ、なければ全件返す。
inline void mitiru::server::EngineHttpServer::handleObserveInspect(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.inspectorQuery)
	{
		resp.status = 503;
		resp.setBody(R"({"error":"inspector not wired"})");
		return;
	}
	const auto prefix = observe::getParam(req.params, "prefix").value_or("");
	resp.status = 200;
	resp.setBody(m_callbacks.inspectorQuery(prefix));
}

/// @brief GET /api/observe/inspect/at[?back=<N>] — 過去スナップショットを返す
/// @details back=0 が最新コミット、back=1 がその前。負数・非数は 400。
inline void mitiru::server::EngineHttpServer::handleObserveInspectAt(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.inspectorAt)
	{
		resp.status = 503;
		resp.setBody(R"({"error":"inspector not wired"})");
		return;
	}
	const auto backParam = observe::getParam(req.params, "back");
	std::size_t back = 0;
	if (backParam.has_value())
	{
		long long signed_back = 0;
		try { signed_back = std::stoll(*backParam); }
		catch (...) { resp.status = 400; resp.setBody(R"({"error":"invalid 'back' parameter"})"); return; }
		if (signed_back < 0) { resp.status = 400; resp.setBody(R"({"error":"invalid 'back' parameter"})"); return; }
		back = static_cast<std::size_t>(signed_back);
	}
	resp.status = 200;
	resp.setBody(m_callbacks.inspectorAt(back));
}

/// @brief GET /api/observe/inspect/depth — Inspector 履歴の depth と capacity を返す
inline void mitiru::server::EngineHttpServer::handleObserveInspectDepth(const HttpRequest&, HttpResponse& resp)
{
	const std::size_t depth = m_callbacks.inspectorDepth ? m_callbacks.inspectorDepth() : 0;
	const std::size_t cap   = m_callbacks.inspectorCapacity ? m_callbacks.inspectorCapacity() : 0;
	resp.status = 200;
	resp.setBody("{\"depth\":" + std::to_string(depth) + ",\"capacity\":" + std::to_string(cap) + "}");
}

// ── AI フレーム観測 (/api/ai/frame) ────────────────────────────

/// @brief capture() が返す pixel buffer の実寸。callback 未配線時は論理 Screen にフォールバック。
inline std::pair<int, int> mitiru::server::EngineHttpServer::captureSourceDims() const
{
	if (m_callbacks.captureDims) { return m_callbacks.captureDims(); }
	const auto* screen = m_callbacks.getScreen();
	return {screen ? screen->width() : 0, screen ? screen->height() : 0};
}

/// @brief screenshot JSON 断片を組み立てる。失敗時は空文字。
/// @details reqW/reqH の片方指定はアスペクト維持で補完する。
inline std::string mitiru::server::EngineHttpServer::buildScreenshotJson(int srcW, int srcH, int reqW, int reqH)
{
	const auto pixels = m_callbacks.capture();
	if (pixels.empty() || srcW <= 0 || srcH <= 0) { return {}; }
	if (reqW > 0 && reqH <= 0) { reqH = (reqW * srcH) / srcW; }
	if (reqH > 0 && reqW <= 0) { reqW = (reqH * srcW) / srcH; }
	int outW = srcW, outH = srcH;
	std::vector<std::uint8_t> src = pixels;
	if (reqW > 0 && reqH > 0 && (reqW != srcW || reqH != srcH))
	{
		src = detail::resizePixels(pixels, srcW, srcH, reqW, reqH);
		outW = reqW; outH = reqH;
	}
	const auto png = detail::encodePng(src.data(), outW, outH);
	if (png.empty()) { return {}; }
	return "\"screenshot\":{\"width\":" + std::to_string(outW) +
	       ",\"height\":" + std::to_string(outH) +
	       ",\"pngBase64\":\"" + util::Base64::encode(png) + "\"}";
}

/// @brief GET /api/ai/frame — draw list + 縮小 screenshot を 1 レスポンスで返す
/// @details 初回呼び出しで draw log 記録を有効化する (エントリは次フレームから)。
///          ?screenshot=0 で PNG 省略、width/height で縮小指定 (既定 width=640)。
inline void mitiru::server::EngineHttpServer::handleAiFrame(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.drawLogEnable || !m_callbacks.drawLogJson)
	{
		resp.status = 503;
		resp.setBody(R"({"error":"frame observe not wired"})");
		return;
	}

	std::string json = "{";
	if (m_callbacks.getFrameNumber)
	{ json += "\"frameNumber\":" + std::to_string(m_callbacks.getFrameNumber()) + ","; }

	const auto* screen = m_callbacks.getScreen();
	const int w = screen ? screen->width() : 0;
	const int h = screen ? screen->height() : 0;
	json += "\"screen\":{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "},";

	if (!m_drawLogActive)
	{
		m_callbacks.drawLogEnable(true);
		m_drawLogActive = true;
		json += "\"note\":\"draw log enabled; entries appear from the next frame\",";
	}
	json += "\"drawCalls\":" + m_callbacks.drawLogJson();

	const bool wantShot = observe::getParam(req.params, "screenshot").value_or("1") != "0";
	if (wantShot && m_callbacks.capture)
	{
		int reqW = 640, reqH = 0;
		if (const auto p = observe::getParam(req.params, "width"))  { try { reqW = std::stoi(*p); } catch (...) {} }
		if (const auto p = observe::getParam(req.params, "height")) { try { reqH = std::stoi(*p); } catch (...) {} }
		// PNG は capture() の実寸で組む ("screen" フィールドの論理寸法とは別物)。
		const auto [cw, ch] = captureSourceDims();
		const auto shot = buildScreenshotJson(cw, ch, reqW, reqH);
		if (!shot.empty()) { json += "," + shot; }
	}

	json += "}";
	resp.status = 200;
	resp.setBody(json);
}

/// @brief GET /api/ai/audio[?max=N] — 最近の音イベント (SoundIntent 適用記録) を返す
inline void mitiru::server::EngineHttpServer::handleAiAudio(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.audioLogJson)
	{
		resp.status = 503;
		resp.setBody(R"({"error":"audio log not wired"})");
		return;
	}
	int max = 64;
	if (const auto p = observe::getParam(req.params, "max"))
	{ try { max = std::stoi(*p); } catch (...) {} }
	resp.status = 200;
	resp.setBody(m_callbacks.audioLogJson(max));
}

#endif // !__EMSCRIPTEN__
