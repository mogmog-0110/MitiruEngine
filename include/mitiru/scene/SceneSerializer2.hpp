#pragma once

/// @file SceneSerializer2.hpp
/// @brief 拡張シーンシリアライゼーション
///
/// GameWorldとSceneGraphのフルJSON直列化/逆直列化を提供する。
/// ComponentSerializer登録により任意のコンポーネント型を拡張可能。
///
/// @code
/// mitiru::scene::SceneSerializer2 serializer;
/// serializer.registerBuiltins();
/// auto json = serializer.serializeWorld(world);
/// mitiru::scene::GameWorld restored;
/// serializer.deserializeWorld(json, restored);
/// @endcode

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mitiru/data/JsonBuilder.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SceneGraph.hpp"

namespace mitiru::scene
{

/// @brief コンポーネントシリアライズ関数型
/// @details GameWorldからエンティティのコンポーネントを取得しJSON断片を返す
using SerializeFn = std::function<std::optional<std::string>(const GameWorld&, EntityId)>;

/// @brief コンポーネントデシリアライズ関数型
/// @details JSON断片からコンポーネントを復元しGameWorldに登録する
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
/// @details GameWorldとSceneGraphの完全なJSON直列化/逆直列化を提供。
/// 内蔵コンポーネント（Transform, Mesh, Light, Camera, AudioSource）および
/// カスタムコンポーネントの登録をサポートする。
class SceneSerializer2
{
public:
	/// @brief コンポーネントシリアライザを登録する
	/// @param name コンポーネント名（JSONキー）
	/// @param serializeFn シリアライズ関数
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

	/// @brief GameWorld全体をJSON文字列にシリアライズする
	/// @param world 対象のGameWorld
	/// @return JSON文字列
	[[nodiscard]] std::string serializeWorld(const GameWorld& world) const
	{
		data::JsonBuilder builder;
		builder.beginObject();
		builder.key("entities");
		builder.beginArray();

		/// エンティティIDを収集してソート（決定論的な出力のため）
		auto ids = collectEntityIds(world);

		for (const auto id : ids)
		{
			builder.rawValue(serializeEntityInternal(world, id));
		}

		builder.endArray();
		builder.endObject();
		return builder.build();
	}

	/// @brief JSON文字列からGameWorldを復元する
	/// @param json JSON文字列
	/// @param world 復元先のGameWorld
	/// @return 成功時true
	bool deserializeWorld(const std::string& json, GameWorld& world) const
	{
		/// entities配列はオブジェクトの配列なので、常に生JSONパースを使用する
		return deserializeEntitiesFromRaw(json, world);
	}

	/// @brief 単一エンティティをJSON文字列にシリアライズする
	/// @param world 対象のGameWorld
	/// @param entityId エンティティID
	/// @return JSON文字列
	[[nodiscard]] std::string serializeEntity(const GameWorld& world, EntityId entityId) const
	{
		return serializeEntityInternal(world, entityId);
	}

	/// @brief SceneGraphをJSON文字列にシリアライズする
	/// @param graph 対象のSceneGraph
	/// @return JSON文字列
	[[nodiscard]] std::string serializeSceneGraph(const SceneGraph& graph) const
	{
		data::JsonBuilder builder;
		builder.beginObject();
		builder.key("nodes");
		builder.beginArray();

		/// 全ノードを走査してシリアライズ
		auto nodes = collectNodes(graph);
		for (const auto& [nodeId, node] : nodes)
		{
			data::JsonBuilder nb;
			nb.beginObject();
			nb.key("id").value(static_cast<int>(nodeId.value));
			nb.key("name").value(node->name);

			/// ローカル変換行列（16要素）
			nb.key("localTransform");
			nb.beginArray();
			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					nb.value(node->localTransform.m[r][c]);
				}
			}
			nb.endArray();

			/// 親ノードID
			nb.key("parent").value(static_cast<int>(node->parent.value));

			/// 子ノードID配列
			nb.key("children");
			nb.beginArray();
			for (const auto& childId : node->children)
			{
				nb.value(static_cast<int>(childId.value));
			}
			nb.endArray();

			nb.endObject();
			builder.rawValue(nb.build());
		}

		builder.endArray();
		builder.endObject();
		return builder.build();
	}

	/// @brief JSON文字列からSceneGraphを復元する
	/// @param json JSON文字列
	/// @param graph 復元先のSceneGraph
	/// @return 成功時true
	bool deserializeSceneGraph(const std::string& json, SceneGraph& graph) const
	{
		/// "nodes"配列の位置を探す
		const auto nodesKey = json.find("\"nodes\":");
		if (nodesKey == std::string::npos)
		{
			return false;
		}

		/// ノード配列を手動パース
		auto nodesStart = json.find('[', nodesKey);
		if (nodesStart == std::string::npos)
		{
			return false;
		}

		/// 各ノードオブジェクトを抽出してパース
		struct NodeInfo
		{
			std::string name;
			uint32_t parentValue{0xFFFFFFFF};
			float transform[16]{};
			bool hasTransform{false};
		};

		std::vector<NodeInfo> nodeInfos;
		std::vector<NodeId> createdIds;

		size_t pos = nodesStart + 1;
		while (pos < json.size())
		{
			pos = skipWs(json, pos);
			if (pos >= json.size() || json[pos] == ']') break;

			if (json[pos] == '{')
			{
				auto objEnd = findMatchingBrace(json, pos);
				std::string objStr = json.substr(pos, objEnd - pos + 1);

				data::JsonReader nodeReader;
				if (nodeReader.parse(objStr))
				{
					NodeInfo info;
					if (auto n = nodeReader.getString("name")) info.name = *n;
					if (auto p = nodeReader.getInt("parent")) info.parentValue = static_cast<uint32_t>(*p);

					/// ローカル変換行列をパース
					if (auto arr = nodeReader.getArray("localTransform"))
					{
						if (arr->size() == 16)
						{
							info.hasTransform = true;
							for (size_t i = 0; i < 16; ++i)
							{
								info.transform[i] = std::stof((*arr)[i]);
							}
						}
					}

					nodeInfos.push_back(info);

					/// ノードを作成
					auto nid = graph.createNode(info.name);
					createdIds.push_back(nid);

					/// 変換行列を設定
					if (info.hasTransform)
					{
						auto* node = graph.getNode(nid);
						if (node)
						{
							const auto& t = info.transform;
							node->localTransform = sgc::Mat4f(
								t[0], t[1], t[2], t[3],
								t[4], t[5], t[6], t[7],
								t[8], t[9], t[10], t[11],
								t[12], t[13], t[14], t[15]);
						}
					}
				}

				pos = objEnd + 1;
			}
			else
			{
				++pos;
			}

			pos = skipWs(json, pos);
			if (pos < json.size() && json[pos] == ',') ++pos;
		}

		/// 親子関係を復元（インデックスベースの対応付け）
		for (size_t i = 0; i < nodeInfos.size(); ++i)
		{
			const auto& info = nodeInfos[i];
			if (info.parentValue == 0xFFFFFFFF) continue;

			/// 対応する親ノードをcreatedIdsから探す
			for (size_t j = 0; j < createdIds.size(); ++j)
			{
				if (createdIds[j].value == info.parentValue)
				{
					/// SceneGraph::reparentは循環チェックつき
					graph.reparent(createdIds[i], createdIds[j]);
					break;
				}
			}
		}

		return true;
	}

private:
	std::vector<ComponentSerializerEntry> m_serializers;

	// ── 内蔵コンポーネントシリアライザ登録 ──────────────────

	/// @brief TransformComponentシリアライザを登録する
	void registerTransform()
	{
		registerComponentSerializer("Transform",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<TransformComponent>(id);
				if (!c) return std::nullopt;
				data::JsonBuilder b;
				b.beginObject();
				b.array("position", {c->position.x, c->position.y, c->position.z});
				b.array("rotation", {c->rotation.x, c->rotation.y, c->rotation.z});
				b.array("scale", {c->scale.x, c->scale.y, c->scale.z});
				b.endObject();
				return b.build();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				TransformComponent tc;
				if (!parseVec3FromJson(json, "position", tc.position)) return false;
				if (!parseVec3FromJson(json, "rotation", tc.rotation)) return false;
				if (!parseVec3FromJson(json, "scale", tc.scale)) return false;
				w.addComponent(id, tc);
				return true;
			});
	}

	/// @brief MeshComponentシリアライザを登録する
	void registerMesh()
	{
		registerComponentSerializer("Mesh",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<MeshComponent>(id);
				if (!c) return std::nullopt;
				data::JsonBuilder b;
				b.beginObject();
				b.key("meshId").value(c->meshId);
				b.endObject();
				return b.build();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				data::JsonReader r;
				if (!r.parse(json)) return false;
				MeshComponent mc;
				if (auto v = r.getString("meshId")) mc.meshId = *v;
				w.addComponent(id, mc);
				return true;
			});
	}

	/// @brief LightComponentシリアライザを登録する
	void registerLight()
	{
		registerComponentSerializer("Light",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<LightComponent>(id);
				if (!c) return std::nullopt;
				data::JsonBuilder b;
				b.beginObject();
				b.key("type").value(lightTypeToString(c->type));
				b.array("color", {c->color.x, c->color.y, c->color.z});
				b.key("intensity").value(c->intensity);
				b.key("range").value(c->range);
				b.endObject();
				return b.build();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				data::JsonReader r;
				if (!r.parse(json)) return false;
				LightComponent lc;
				if (auto v = r.getString("type")) lc.type = stringToLightType(*v);
				if (!parseVec3FromJson(json, "color", lc.color)) return false;
				if (auto v = r.getFloat("intensity")) lc.intensity = *v;
				if (auto v = r.getFloat("range")) lc.range = *v;
				w.addComponent(id, lc);
				return true;
			});
	}

	/// @brief CameraComponentシリアライザを登録する
	void registerCamera()
	{
		registerComponentSerializer("Camera",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<CameraComponent>(id);
				if (!c) return std::nullopt;
				data::JsonBuilder b;
				b.beginObject();
				b.key("fov").value(c->fov);
				b.key("nearClip").value(c->nearClip);
				b.key("farClip").value(c->farClip);
				b.key("active").value(c->active);
				b.endObject();
				return b.build();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				data::JsonReader r;
				if (!r.parse(json)) return false;
				CameraComponent cc;
				if (auto v = r.getFloat("fov")) cc.fov = *v;
				if (auto v = r.getFloat("nearClip")) cc.nearClip = *v;
				if (auto v = r.getFloat("farClip")) cc.farClip = *v;
				if (auto v = r.getBool("active")) cc.active = *v;
				w.addComponent(id, cc);
				return true;
			});
	}

	/// @brief AudioSourceComponentシリアライザを登録する
	void registerAudioSource()
	{
		registerComponentSerializer("AudioSource",
			[](const GameWorld& w, EntityId id) -> std::optional<std::string>
			{
				const auto* c = w.getComponent<AudioSourceComponent>(id);
				if (!c) return std::nullopt;
				data::JsonBuilder b;
				b.beginObject();
				b.key("soundId").value(c->soundId);
				b.key("volume").value(c->volume);
				b.key("loop").value(c->loop);
				b.endObject();
				return b.build();
			},
			[](const std::string& json, GameWorld& w, EntityId id) -> bool
			{
				data::JsonReader r;
				if (!r.parse(json)) return false;
				AudioSourceComponent ac;
				if (auto v = r.getString("soundId")) ac.soundId = *v;
				if (auto v = r.getFloat("volume")) ac.volume = *v;
				if (auto v = r.getBool("loop")) ac.loop = *v;
				w.addComponent(id, ac);
				return true;
			});
	}

	// ── 内部ヘルパー ──────────────────────────────────────────

	/// @brief 単一エンティティのJSON文字列を生成する
	[[nodiscard]] std::string serializeEntityInternal(
		const GameWorld& world, EntityId entityId) const
	{
		data::JsonBuilder builder;
		builder.beginObject();
		builder.key("id").value(static_cast<int>(entityId));
		builder.key("name").value(world.entityName(entityId));

		builder.key("components");
		builder.beginObject();
		for (const auto& entry : m_serializers)
		{
			auto result = entry.serialize(world, entityId);
			if (result)
			{
				builder.key(entry.name).rawValue(*result);
			}
		}
		builder.endObject();

		builder.endObject();
		return builder.build();
	}

	/// @brief エンティティIDを収集する
	/// @note GameWorldにはイテレータが無いため、forEach<TransformComponent>等を活用し、
	///       IDの範囲を推定する。最大4096エンティティまで走査。
	[[nodiscard]] std::vector<EntityId> collectEntityIds(const GameWorld& world) const
	{
		std::vector<EntityId> ids;
		/// GameWorldのm_nextIdは非公開なので、合理的な範囲でブルートフォース
		for (EntityId id = 1; id <= 4096; ++id)
		{
			if (world.isValid(id))
			{
				ids.push_back(id);
			}
		}
		return ids;
	}

	/// @brief SceneGraphの全有効ノードを収集する
	[[nodiscard]] std::vector<std::pair<NodeId, const SceneNode*>> collectNodes(
		const SceneGraph& graph) const
	{
		std::vector<std::pair<NodeId, const SceneNode*>> result;
		/// NodeIdはインデックス+世代のパック値。合理的な範囲でスキャン
		for (uint16_t idx = 0; idx < 4096; ++idx)
		{
			for (uint16_t gen = 1; gen <= 256; ++gen)
			{
				auto nid = NodeId::make(idx, gen);
				const auto* node = graph.getNode(nid);
				if (node)
				{
					result.emplace_back(nid, node);
					break; /// 同一インデックスで複数世代が生存することはない
				}
			}
		}
		return result;
	}

	/// @brief JSON内のVec3配列をパースする
	static bool parseVec3FromJson(const std::string& json, const std::string& key, sgc::Vec3f& out)
	{
		data::JsonReader r;
		if (!r.parse(json)) return false;
		auto arr = r.getArray(key);
		if (!arr || arr->size() != 3) return false;
		try
		{
			out.x = std::stof((*arr)[0]);
			out.y = std::stof((*arr)[1]);
			out.z = std::stof((*arr)[2]);
		}
		catch (...)
		{
			return false;
		}
		return true;
	}

	/// @brief LightTypeを文字列に変換する
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

	/// @brief 文字列をLightTypeに変換する
	[[nodiscard]] static LightType stringToLightType(const std::string& s)
	{
		if (s == "Directional") return LightType::Directional;
		if (s == "Spot") return LightType::Spot;
		return LightType::Point;
	}

	/// @brief entitiesをローJSON文字列から手動パースして復元する
	bool deserializeEntitiesFromRaw(const std::string& json, GameWorld& world) const
	{
		/// "entities"キーの位置を探す
		const auto entKey = json.find("\"entities\":");
		if (entKey == std::string::npos) return false;

		auto arrStart = json.find('[', entKey);
		if (arrStart == std::string::npos) return false;

		/// 各エンティティオブジェクトを抽出
		size_t pos = arrStart + 1;
		while (pos < json.size())
		{
			pos = skipWs(json, pos);
			if (pos >= json.size() || json[pos] == ']') break;

			if (json[pos] == '{')
			{
				auto objEnd = findMatchingBrace(json, pos);
				std::string objStr = json.substr(pos, objEnd - pos + 1);

				deserializeSingleEntity(objStr, world);

				pos = objEnd + 1;
			}
			else
			{
				++pos;
			}

			pos = skipWs(json, pos);
			if (pos < json.size() && json[pos] == ',') ++pos;
		}
		return true;
	}

	/// @brief 単一エンティティJSON文字列からGameWorldに復元する
	bool deserializeSingleEntity(const std::string& entityJson, GameWorld& world) const
	{
		data::JsonReader reader;
		if (!reader.parse(entityJson)) return false;

		auto name = reader.getString("name");
		auto id = world.createEntity(name.value_or(""));

		/// componentsオブジェクトを取得して各コンポーネントを復元
		auto compsReader = reader.getObject("components");
		if (!compsReader) return true; /// コンポーネントなしのエンティティ

		const auto& compsJson = compsReader->source();

		for (const auto& entry : m_serializers)
		{
			/// コンポーネント名のキーを探す
			auto keyStr = "\"" + entry.name + "\":";
			auto keyPos = compsJson.find(keyStr);
			if (keyPos == std::string::npos) continue;

			auto valStart = keyPos + keyStr.size();
			valStart = skipWs(compsJson, valStart);
			if (valStart >= compsJson.size()) continue;

			/// 値（オブジェクト）を抽出
			if (compsJson[valStart] == '{')
			{
				auto valEnd = findMatchingBrace(compsJson, valStart);
				std::string compJson = compsJson.substr(valStart, valEnd - valStart + 1);
				entry.deserialize(compJson, world, id);
			}
		}

		return true;
	}

	/// @brief 空白をスキップする
	[[nodiscard]] static size_t skipWs(const std::string& s, size_t pos)
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
			s[pos] == '\n' || s[pos] == '\r'))
		{
			++pos;
		}
		return pos;
	}

	/// @brief 対応する閉じブレースの位置を返す
	[[nodiscard]] static size_t findMatchingBrace(const std::string& s, size_t openPos)
	{
		const char open = s[openPos];
		const char close = (open == '{') ? '}' : ']';
		int depth = 0;
		bool inStr = false;
		for (size_t i = openPos; i < s.size(); ++i)
		{
			if (s[i] == '\\' && inStr)
			{
				++i;
				continue;
			}
			if (s[i] == '"') inStr = !inStr;
			if (!inStr)
			{
				if (s[i] == open) ++depth;
				if (s[i] == close)
				{
					--depth;
					if (depth == 0) return i;
				}
			}
		}
		return s.size() - 1;
	}
};

} // namespace mitiru::scene
