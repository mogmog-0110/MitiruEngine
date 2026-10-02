#pragma once

/// @file SceneSerializer2.hpp
/// @brief 拡張シーンシリアライゼーション
///
/// GameWorld と SceneGraph のフル JSON 直列化/逆直列化を提供する。
/// ComponentSerializer 登録により任意のコンポーネント型を拡張可能。

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SceneGraph.hpp"

namespace mitiru::scene
{

/// @brief コンポーネントシリアライズ関数型
/// @details GameWorld からエンティティのコンポーネントを取得し JSON 断片を返す
using SerializeFn = std::function<std::optional<std::string>(const GameWorld&, EntityId)>;

/// @brief コンポーネントデシリアライズ関数型
/// @details JSON 断片からコンポーネントを復元し GameWorld に登録する
using DeserializeFn = std::function<bool(const std::string&, GameWorld&, EntityId)>;

/// @brief コンポーネントシリアライザ登録情報
struct ComponentSerializerEntry
{
	std::string name;           ///< コンポーネント名
	SerializeFn serialize;      ///< シリアライズ関数
	DeserializeFn deserialize;  ///< デシリアライズ関数
};

/// @brief 拡張シーンシリアライザ
///
/// @details GameWorld と SceneGraph の完全な JSON 直列化/逆直列化を提供。
/// 内蔵コンポーネント（Transform, Mesh, Light, Camera, AudioSource）および
/// カスタムコンポーネントの登録をサポートする。
class SceneSerializer2
{
public:
	using Json = nlohmann::ordered_json;

	/// @brief コンポーネントシリアライザを登録する
	/// @param name コンポーネント名（JSON キー）
	/// @param serializeFn シリアライズ関数。JSON として読めない断片を返したコンポーネントは書き出さない
	/// @param deserializeFn デシリアライズ関数
	void registerComponentSerializer(
		const std::string& name,
		SerializeFn serializeFn,
		DeserializeFn deserializeFn)
	{
		m_serializers.push_back({name, std::move(serializeFn), std::move(deserializeFn)});
	}

	/// @brief 内蔵コンポーネントシリアライザを一括登録する
	void registerBuiltins()
	{
		registerTransform();
		registerMesh();
		registerLight();
		registerCamera();
		registerAudioSource();
	}

	/// @brief GameWorld 全体を JSON 文字列にシリアライズする (ID 順)
	[[nodiscard]] std::string serializeWorld(const GameWorld& world) const
	{
		Json entities = Json::array();
		for (const auto id : collectEntityIds(world))
		{
			entities.push_back(entityToJson(world, id));
		}
		return Json{{"entities", std::move(entities)}}.dump();
	}

	/// @brief JSON 文字列から GameWorld を復元する
	/// @return "entities" 配列が無い・JSON として読めないときは false
	bool deserializeWorld(const std::string& json, GameWorld& world) const
	{
		const auto doc = Json::parse(json, nullptr, false);
		const auto entities = doc.is_object() ? doc.find("entities") : doc.end();
		if (!doc.is_object() || entities == doc.end() || !entities->is_array()) return false;
		for (const auto& entity : *entities)
		{
			if (entity.is_object()) { entityFromJson(entity, world); }
		}
		return true;
	}

	/// @brief 単一エンティティを JSON 文字列にシリアライズする
	[[nodiscard]] std::string serializeEntity(const GameWorld& world, EntityId entityId) const
	{
		return entityToJson(world, entityId).dump();
	}

	/// @brief SceneGraph を JSON 文字列にシリアライズする
	[[nodiscard]] std::string serializeSceneGraph(const SceneGraph& graph) const
	{
		Json nodes = Json::array();
		for (const auto& [nodeId, node] : collectNodes(graph))
		{
			Json transform = Json::array();
			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c) { transform.push_back(data::jsonFloat(node->localTransform.m[r][c])); }
			}
			Json children = Json::array();
			for (const auto& childId : node->children) { children.push_back(static_cast<int>(childId.value)); }
			nodes.push_back({
				{"id", static_cast<int>(nodeId.value)},
				{"name", node->name},
				{"localTransform", std::move(transform)},
				{"parent", static_cast<int>(node->parent.value)},
				{"children", std::move(children)},
			});
		}
		return Json{{"nodes", std::move(nodes)}}.dump();
	}

	/// @brief JSON 文字列から SceneGraph を復元する
	/// @details 親子は保存時のノード ID で結ぶ。
	/// @return "nodes" 配列が無い・JSON として読めないときは false
	bool deserializeSceneGraph(const std::string& json, SceneGraph& graph) const
	{
		const auto doc = Json::parse(json, nullptr, false);
		const auto nodes = doc.is_object() ? doc.find("nodes") : doc.end();
		if (!doc.is_object() || nodes == doc.end() || !nodes->is_array()) return false;

		std::vector<std::uint32_t> parents;
		std::vector<NodeId> created;
		for (const auto& node : *nodes)
		{
			if (!node.is_object()) continue;
			const auto nid = graph.createNode(data::fieldOr(node, "name", std::string{}));
			applyTransform(node, graph, nid);
			// 親の無いノードは NodeId::invalid (0xFFFFFFFF) を int で書いた -1 になっている
			parents.push_back(static_cast<std::uint32_t>(data::fieldOr(node, "parent", -1)));
			created.push_back(nid);
		}
		for (std::size_t i = 0; i < created.size(); ++i)
		{
			for (std::size_t j = 0; j < created.size(); ++j)
			{
				if (created[j].value == parents[i]) { graph.reparent(created[i], created[j]); break; }
			}
		}
		return true;
	}

private:
	std::vector<ComponentSerializerEntry> m_serializers;

	// ── 内蔵コンポーネントシリアライザ登録 ──────────────────

	[[nodiscard]] static Json vec3(const sgc::Vec3f& v)
	{
		return Json::array({data::jsonFloat(v.x), data::jsonFloat(v.y), data::jsonFloat(v.z)});
	}

	/// @brief キーの値が数値 3 つの配列なら out に入れて true
	static bool readVec3(const Json& obj, const char* key, sgc::Vec3f& out)
	{
		const auto it = obj.find(key);
		if (it == obj.end() || !it->is_array() || it->size() != 3) return false;
		for (const auto& e : *it)
		{
			if (!e.is_number()) return false;
		}
		out = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
		return true;
	}

	/// @brief 断片を JSON オブジェクトとして読む。読めなければ nullopt。
	[[nodiscard]] static std::optional<Json> parseObject(const std::string& fragment)
	{
		auto obj = Json::parse(fragment, nullptr, false);
		if (!obj.is_object()) return std::nullopt;
		return obj;
	}

	void registerTransform()
	{
		registerComponentSerializer("Transform",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<TransformComponent>(id);
				if (!c) return std::nullopt;
				return Json{{"position", vec3(c->position)}, {"rotation", vec3(c->rotation)}, {"scale", vec3(c->scale)}}.dump();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				const auto obj = parseObject(json);
				TransformComponent tc;
				if (!obj || !readVec3(*obj, "position", tc.position) || !readVec3(*obj, "rotation", tc.rotation)
					|| !readVec3(*obj, "scale", tc.scale))
				{
					return false;
				}
				w.addComponent(id, tc);
				return true;
			});
	}

	void registerMesh()
	{
		registerComponentSerializer("Mesh",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<MeshComponent>(id);
				if (!c) return std::nullopt;
				return Json{{"meshId", c->meshId}}.dump();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				const auto obj = parseObject(json);
				if (!obj) return false;
				MeshComponent mc;
				mc.meshId = data::fieldOr(*obj, "meshId", mc.meshId);
				w.addComponent(id, mc);
				return true;
			});
	}

	void registerLight()
	{
		registerComponentSerializer("Light",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<LightComponent>(id);
				if (!c) return std::nullopt;
				return Json{
					{"type", lightTypeToString(c->type)},
					{"color", vec3(c->color)},
					{"intensity", data::jsonFloat(c->intensity)},
					{"range", data::jsonFloat(c->range)},
				}.dump();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				const auto obj = parseObject(json);
				LightComponent lc;
				if (!obj || !readVec3(*obj, "color", lc.color)) return false;
				lc.type = stringToLightType(data::fieldOr(*obj, "type", std::string{}));
				lc.intensity = data::fieldOr(*obj, "intensity", lc.intensity);
				lc.range = data::fieldOr(*obj, "range", lc.range);
				w.addComponent(id, lc);
				return true;
			});
	}

	void registerCamera()
	{
		registerComponentSerializer("Camera",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<CameraComponent>(id);
				if (!c) return std::nullopt;
				return Json{
					{"fov", data::jsonFloat(c->fov)},
					{"nearClip", data::jsonFloat(c->nearClip)},
					{"farClip", data::jsonFloat(c->farClip)},
					{"active", c->active},
				}.dump();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				const auto obj = parseObject(json);
				if (!obj) return false;
				CameraComponent cc;
				cc.fov = data::fieldOr(*obj, "fov", cc.fov);
				cc.nearClip = data::fieldOr(*obj, "nearClip", cc.nearClip);
				cc.farClip = data::fieldOr(*obj, "farClip", cc.farClip);
				cc.active = data::fieldOr(*obj, "active", cc.active);
				w.addComponent(id, cc);
				return true;
			});
	}

	void registerAudioSource()
	{
		registerComponentSerializer("AudioSource",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<AudioSourceComponent>(id);
				if (!c) return std::nullopt;
				return Json{{"soundId", c->soundId}, {"volume", data::jsonFloat(c->volume)}, {"loop", c->loop}}.dump();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				const auto obj = parseObject(json);
				if (!obj) return false;
				AudioSourceComponent ac;
				ac.soundId = data::fieldOr(*obj, "soundId", ac.soundId);
				ac.volume = data::fieldOr(*obj, "volume", ac.volume);
				ac.loop = data::fieldOr(*obj, "loop", ac.loop);
				w.addComponent(id, ac);
				return true;
			});
	}

	// ── 内部ヘルパー ──────────────────────────────────────────

	[[nodiscard]] Json entityToJson(const GameWorld& world, EntityId entityId) const
	{
		Json components = Json::object();
		for (const auto& entry : m_serializers)
		{
			const auto fragment = entry.serialize(world, entityId);
			if (!fragment) continue;
			auto value = Json::parse(*fragment, nullptr, false);
			if (!value.is_discarded()) { components[entry.name] = std::move(value); }
		}
		return Json{
			{"id", static_cast<int>(entityId)},
			{"name", world.entityName(entityId)},
			{"components", std::move(components)},
		};
	}

	void entityFromJson(const Json& entity, GameWorld& world) const
	{
		const auto id = world.createEntity(data::fieldOr(entity, "name", std::string{}));
		const auto components = entity.find("components");
		if (components == entity.end() || !components->is_object()) return;
		for (const auto& entry : m_serializers)
		{
			const auto it = components->find(entry.name);
			if (it != components->end()) { entry.deserialize(it->dump(), world, id); }
		}
	}

	static void applyTransform(const Json& node, SceneGraph& graph, NodeId nid)
	{
		const auto arr = node.find("localTransform");
		if (arr == node.end() || !arr->is_array() || arr->size() != 16) return;
		float t[16]{};
		for (std::size_t i = 0; i < 16; ++i)
		{
			if (!(*arr)[i].is_number()) return;
			t[i] = (*arr)[i].get<float>();
		}
		if (auto* n = graph.getNode(nid))
		{
			n->localTransform = sgc::Mat4f(
				t[0], t[1], t[2], t[3],
				t[4], t[5], t[6], t[7],
				t[8], t[9], t[10], t[11],
				t[12], t[13], t[14], t[15]);
		}
	}

	/// @note GameWorld にはイテレータが無いため、最大 4096 エンティティまで走査する。
	[[nodiscard]] static std::vector<EntityId> collectEntityIds(const GameWorld& world)
	{
		std::vector<EntityId> ids;
		for (EntityId id = 1; id <= 4096; ++id)
		{
			if (world.isValid(id)) { ids.push_back(id); }
		}
		return ids;
	}

	/// @brief SceneGraph の全有効ノードを収集する (NodeId はインデックス+世代のパック値)
	[[nodiscard]] static std::vector<std::pair<NodeId, const SceneNode*>> collectNodes(const SceneGraph& graph)
	{
		std::vector<std::pair<NodeId, const SceneNode*>> result;
		for (uint16_t idx = 0; idx < 4096; ++idx)
		{
			for (uint16_t gen = 1; gen <= 256; ++gen)
			{
				const auto nid = NodeId::make(idx, gen);
				if (const auto* node = graph.getNode(nid))
				{
					result.emplace_back(nid, node);
					break; /// 同一インデックスで複数世代が生存することはない
				}
			}
		}
		return result;
	}

	[[nodiscard]] static std::string lightTypeToString(LightType type)
	{
		switch (type)
		{
		case LightType::Directional: return "Directional";
		case LightType::Point:       return "Point";
		case LightType::Spot:        return "Spot";
		}
		return "Point";
	}

	[[nodiscard]] static LightType stringToLightType(const std::string& s)
	{
		if (s == "Directional") return LightType::Directional;
		if (s == "Spot") return LightType::Spot;
		return LightType::Point;
	}
};

} // namespace mitiru::scene
