#pragma once

/// @file EngineHttpBridge.hpp
/// @brief Engine と EngineHttpServer 間のコールバック配線
/// @details initHttpServer() のコールバック設定ロジックを Engine.hpp から分離し、
///          Engine クラスの肥大化を防止する。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <mitiru/core/Config.hpp>
#include <mitiru/core/SceneDocument.hpp>
#include <mitiru/server/EngineHttpServer.hpp>

namespace mitiru
{

class Clock;
class Screen;
class Game;

namespace server
{

/// @brief Engine の公開メソッド群へのアクセスを提供する軽量インターフェース
/// @details Engine のメンバーに直接アクセスするのではなく、必要な関数ポインタだけを受け取る。
struct EngineBridgeContext
{
	std::function<std::uint64_t()> getFrameNumber;
	std::function<const Clock*()> getClock;
	std::function<const Screen*()> getScreen;
	std::function<std::vector<std::uint8_t>()> capture;
	std::function<std::string()> getSnapshot;
	std::function<void()> requestStop;
	std::map<std::string, std::string>* gameFlags = nullptr;
	const EngineConfig* config = nullptr;
};

/// @brief HTTP APIサーバーのコールバックを配線する
/// @param cb 配線先のコールバック構造体
/// @param ctx エンジン側の関数群
/// @param game ゲームインスタンス（シーン操作用）
inline void initEngineHttpCallbacks(EngineCallbacks& cb,
                                    const EngineBridgeContext& ctx,
                                    Game& game)
{
	// ── エンジン基本コールバック ──

	cb.getFrameNumber = ctx.getFrameNumber;
	cb.getClock       = ctx.getClock;
	cb.getScreen      = ctx.getScreen;
	cb.capture        = ctx.capture;
	cb.getSnapshot    = ctx.getSnapshot;
	cb.requestStop    = ctx.requestStop;

	// ── シーン操作コールバック ──

	cb.getSceneJson = [&game]() -> std::string {
		return game.scene().toJson();
	};

	cb.createNode = [&game](const std::string& name, const std::string& type,
		int parentId) -> int {
		auto& scene = game.scene();
		if (type == "mesh")   return scene.addMesh(name, "cube", parentId);
		if (type == "light")  return scene.addLight(name, "directional", parentId);
		if (type == "camera") return scene.addCamera(name, parentId);
		return scene.createEmpty(name, parentId);
	};

	cb.deleteNode = [&game](int nodeId) -> bool {
		auto& scene = game.scene();
		if (!scene.getNode(nodeId)) return false;
		scene.removeNode(nodeId);
		return true;
	};

	cb.updateNodeProperty = [&game](int nodeId, const std::string& prop,
		const std::string& value) -> bool {
		auto* node = game.scene().getNode(nodeId);
		if (!node) return false;

		if (prop == "name")
		{
			node->name = value;
			return true;
		}

		// position/rotation/scale: カンマ区切り "x,y,z" を解析する
		auto parseVec3 = [](const std::string& s, float out[3]) -> bool {
			float v[3] = {};
			int idx = 0;
			std::size_t start = 0;
			for (std::size_t i = 0; i <= s.size() && idx < 3; ++i)
			{
				if (i == s.size() || s[i] == ',')
				{
					try { v[idx] = std::stof(s.substr(start, i - start)); }
					catch (...) { return false; }
					++idx;
					start = i + 1;
				}
			}
			if (idx != 3) return false;
			out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
			return true;
		};

		if (prop == "position") return parseVec3(value, node->position);
		if (prop == "rotation") return parseVec3(value, node->rotation);
		if (prop == "scale")    return parseVec3(value, node->scale);

		return false;
	};

	cb.addTrait = [&game](int nodeId, const std::string& traitType,
		const std::string& traitData) -> bool {
		auto* node = game.scene().getNode(nodeId);
		if (!node) return false;

		if (traitType == "mesh")
		{
			auto& t = node->addTrait<MeshTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else if (traitType == "light")
		{
			auto& t = node->addTrait<LightTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else if (traitType == "camera")
		{
			node->addTrait<CameraTrait>();
		}
		else if (traitType == "physics")
		{
			auto& t = node->addTrait<PhysicsTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else if (traitType == "script")
		{
			auto& t = node->addTrait<ScriptTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else if (traitType == "audio")
		{
			auto& t = node->addTrait<AudioTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else if (traitType == "custom")
		{
			auto& t = node->addTrait<CustomTrait>();
			if (!traitData.empty()) t.fromJson(traitData);
		}
		else
		{
			return false;
		}
		return true;
	};

	cb.removeTrait = [&game](int nodeId, const std::string& traitType) -> bool {
		auto* node = game.scene().getNode(nodeId);
		if (!node) return false;
		node->removeTrait(traitType);
		return true;
	};

	cb.saveScene = [&game](const std::string& path) -> bool {
		return game.saveScene(path);
	};

	cb.loadScene = [&game](const std::string& path) -> bool {
		return game.loadScene(path);
	};

	// ── エディタ制御コールバック ──
	// gameFlagsを経由して次フレームでGame側が処理する

	auto* flags = ctx.gameFlags;

	cb.selectNode = [flags](int nodeId) -> bool {
		(*flags)["editor.selectNode"] = std::to_string(nodeId);
		return true;
	};

	cb.focusNode = [flags](int nodeId) -> bool {
		(*flags)["editor.focusNode"] = std::to_string(nodeId);
		return true;
	};

	cb.setEditorCamera = [flags](float yaw, float pitch, float distance,
		float px, float py, float pz) -> bool {
		(*flags)["editor.camera.yaw"] = std::to_string(yaw);
		(*flags)["editor.camera.pitch"] = std::to_string(pitch);
		(*flags)["editor.camera.distance"] = std::to_string(distance);
		(*flags)["editor.camera.pivotX"] = std::to_string(px);
		(*flags)["editor.camera.pivotY"] = std::to_string(py);
		(*flags)["editor.camera.pivotZ"] = std::to_string(pz);
		(*flags)["editor.camera.pending"] = "1";
		return true;
	};

	// ── デバッグコールバック ──

	cb.getErrors = [flags]() -> std::string {
		const auto it = flags->find("engine.errors");
		return (it != flags->end()) ? it->second : "[]";
	};

	cb.getLog = [flags]() -> std::string {
		const auto it = flags->find("engine.log");
		return (it != flags->end()) ? it->second : "[]";
	};

	const auto* cfg = ctx.config;
	cb.getProjectInfo = [cfg]() -> std::string {
		std::string json = "{";
		json += "\"title\":\"" + cfg->title + "\"";
		json += ",\"windowWidth\":" + std::to_string(cfg->windowWidth);
		json += ",\"windowHeight\":" + std::to_string(cfg->windowHeight);
		json += ",\"headless\":" + std::string(cfg->headless ? "true" : "false");
		json += ",\"gfxBackend\":" + std::to_string(static_cast<int>(cfg->gfxBackend));
		json += ",\"targetTps\":" + std::to_string(cfg->targetTps);
		json += "}";
		return json;
	};

	// ── ゲーム制御コールバック ──

	cb.runGame = [flags]() -> bool {
		(*flags)["engine.paused"] = "0";
		return true;
	};

	cb.stopGame = [flags]() -> bool {
		(*flags)["engine.paused"] = "1";
		return true;
	};
}

} // namespace server
} // namespace mitiru
