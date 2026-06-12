#pragma once
// EngineHttpServer のスクリーンショット / シーン操作 / エディタ系ハンドラ実装。
// server/EngineHttpServer.hpp から末尾 include される (単体 include も親経由で自己完結)。

#include <mitiru/server/EngineHttpServer.hpp>

#ifndef __EMSCRIPTEN__

#include <string>
#include <vector>

#include <mitiru/observe/QueryParser.hpp>
#include <mitiru/server/JsonHelper.hpp>
#include <mitiru/server/PngEncoder.hpp>

// ── スクリーンショットエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleScreenshot(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.getFrameNumber) { resp.status = 500; resp.setBody(R"({"error":"engine not available"})"); return; }

	const auto pixels = m_callbacks.capture();
	if (pixels.empty()) { resp.status = 404; resp.setBody(R"({"error":"no screenshot available"})"); return; }

	// 寸法は capture() の実寸 (window) を使う。論理 Screen サイズで解釈すると
	// 縮小描画ゲーム (論理≠実寸) で stride がズレて画像が崩れる。
	const auto [w, h] = captureSourceDims();
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

inline void mitiru::server::EngineHttpServer::handleScene(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.getFrameNumber) { resp.status = 200; resp.setBody(R"({"nodes":[]})"); return; }
	resp.status = 200;
	resp.setBody(m_callbacks.getSnapshot());
}

inline void mitiru::server::EngineHttpServer::handleSceneTree(const HttpRequest&, HttpResponse& resp)
{
	if (!m_callbacks.getSceneJson) { resp.status = 200; resp.setBody(R"({"nodes":[]})"); return; }
	resp.status = 200;
	resp.setBody(m_callbacks.getSceneJson());
}

inline void mitiru::server::EngineHttpServer::handleCreateNode(const HttpRequest& req, HttpResponse& resp)
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

inline void mitiru::server::EngineHttpServer::handleDeleteNode(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.deleteNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
	const int nodeId = extractNodeIdFromPath(req.path, "/api/scene/node/");
	if (nodeId < 0) { resp.status = 400; resp.setBody(R"({"success":false,"message":"invalid node id"})"); return; }
	const bool ok = m_callbacks.deleteNode(nodeId);
	resp.status = ok ? 200 : 404;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

inline void mitiru::server::EngineHttpServer::handleUpdateNode(const HttpRequest& req, HttpResponse& resp)
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

inline void mitiru::server::EngineHttpServer::handleAddTrait(const HttpRequest& req, HttpResponse& resp)
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

inline void mitiru::server::EngineHttpServer::handleRemoveTrait(const HttpRequest& req, HttpResponse& resp)
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

inline void mitiru::server::EngineHttpServer::handleSaveScene(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.saveScene) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
	const auto filePath = detail::extractJsonString(req.body, "filePath");
	if (filePath.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'filePath'"})"); return; }
	const bool ok = m_callbacks.saveScene(filePath);
	resp.status = ok ? 200 : 500;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

inline void mitiru::server::EngineHttpServer::handleLoadScene(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.loadScene) { resp.status = 500; resp.setBody(R"({"success":false,"message":"scene not available"})"); return; }
	const auto filePath = detail::extractJsonString(req.body, "filePath");
	if (filePath.empty()) { resp.status = 400; resp.setBody(R"({"success":false,"message":"missing 'filePath'"})"); return; }
	const bool ok = m_callbacks.loadScene(filePath);
	resp.status = ok ? 200 : 500;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

// ── エディタエンドポイント ──────────────────────────────

inline void mitiru::server::EngineHttpServer::handleEditorState(const HttpRequest&, HttpResponse& resp)
{
	resp.status = 200;
	resp.setBody("{\"editor\":{}}");
}

inline void mitiru::server::EngineHttpServer::handleSetCamera(const HttpRequest& req, HttpResponse& resp)
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

inline void mitiru::server::EngineHttpServer::handleSelectNode(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.selectNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"selection not available"})"); return; }
	const int nodeId = detail::extractJsonInt(req.body, "nodeId", -1);
	const bool ok = m_callbacks.selectNode(nodeId);
	resp.status = ok ? 200 : 404;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

inline void mitiru::server::EngineHttpServer::handleFocusNode(const HttpRequest& req, HttpResponse& resp)
{
	if (!m_callbacks.focusNode) { resp.status = 500; resp.setBody(R"({"success":false,"message":"focus not available"})"); return; }
	const int nodeId = detail::extractJsonInt(req.body, "nodeId", -1);
	const bool ok = m_callbacks.focusNode(nodeId);
	resp.status = ok ? 200 : 404;
	resp.setBody("{\"success\":" + std::string(ok ? "true" : "false") + "}");
}

#endif // !__EMSCRIPTEN__
