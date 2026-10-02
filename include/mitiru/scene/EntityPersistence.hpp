#pragma once

/// @file EntityPersistence.hpp
/// @brief エンティティ・コンポーネントの永続化（保存/読み込み）
/// @details MitiruWorld のエンティティとコンポーネントを JSON 形式で
///          シリアライズ/デシリアライズする。
///          コンポーネント型ごとにシリアライザ/デシリアライザを登録し、
///          型安全な永続化を実現する。

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <sgc/core/TypeId.hpp>
#include <sgc/ecs/Entity.hpp>
#include <sgc/ecs/World.hpp>

#include <mitiru/ecs/MitiruWorld.hpp>

namespace mitiru::scene
{

/// @brief コンポーネントシリアライザ/デシリアライザ登録エントリ
/// @details 型消去されたシリアライズ/デシリアライズ関数と型名を保持する。
struct ComponentPersistenceEntry
{
	std::string typeName;  ///< コンポーネント型名（JSONキーとして使用）

	/// @brief シリアライズ関数: World + Entity → JSON 文字列
	/// @details 指定エンティティが該当コンポーネントを持つ場合に JSON 文字列を返す。
	///          持たない場合は空文字列を返す。
	std::function<std::string(const sgc::ecs::World&, sgc::ecs::Entity)> serialize;

	/// @brief デシリアライズ関数: World + Entity + JSON 文字列 → コンポーネント復元
	/// @details JSON 文字列からコンポーネントを復元し、エンティティに追加する。
	std::function<void(sgc::ecs::World&, sgc::ecs::Entity, const std::string&)> deserialize;

	/// @brief エンティティが該当コンポーネントを持つか確認する関数
	std::function<bool(const sgc::ecs::World&, sgc::ecs::Entity)> hasComponent;
};

/// @brief エンティティ永続化システム
/// @details コンポーネント型ごとにシリアライザ/デシリアライザを登録し、
///          MitiruWorld の全エンティティを JSON 形式で保存/読み込みする。
///
/// @code
/// mitiru::scene::EntityPersistence persistence;
///
/// // Position コンポーネントのシリアライザ登録
/// persistence.registerComponent<Position>("Position",
///     [](const Position& pos) {
///         return "{\"x\":" + std::to_string(pos.x) +
///                ",\"y\":" + std::to_string(pos.y) + "}";
///     },
///     [](const std::string& json) {
///         Position pos;
///         // JSON パース処理...
///         return pos;
///     });
///
/// // 保存
/// auto json = persistence.saveWorld(world);
///
/// // 読み込み
/// persistence.loadWorld(world, json);
/// @endcode
class EntityPersistence
{
public:
	/// @brief コンポーネント型のシリアライザ/デシリアライザを登録する
	/// @tparam T コンポーネント型
	/// @param typeName コンポーネント型名（JSON キーとして使用）
	/// @param serialize シリアライズ関数: T → JSON 文字列
	/// @param deserialize デシリアライズ関数: JSON 文字列 → T
	template <typename T>
	void registerComponent(
		const std::string& typeName,
		std::function<std::string(const T&)> serialize,
		std::function<T(const std::string&)> deserialize)
	{
		const auto typeId = sgc::typeId<T>();

		ComponentPersistenceEntry entry;
		entry.typeName = typeName;

		/// 型安全なシリアライザをラップする
		entry.serialize = [serialize](const sgc::ecs::World& world, sgc::ecs::Entity entity) -> std::string
		{
			const auto* comp = world.getComponent<T>(entity);
			if (!comp) return {};
			return serialize(*comp);
		};

		/// 型安全なデシリアライザをラップする
		entry.deserialize = [deserialize](sgc::ecs::World& world, sgc::ecs::Entity entity, const std::string& json)
		{
			T component = deserialize(json);
			world.addComponent(entity, std::move(component));
		};

		/// hasComponent ラッパー
		entry.hasComponent = [](const sgc::ecs::World& world, sgc::ecs::Entity entity) -> bool
		{
			return world.hasComponent<T>(entity);
		};

		m_entries[typeId] = std::move(entry);
	}

	/// @brief 指定コンポーネント型が登録済みか確認する
	/// @tparam T コンポーネント型
	/// @return 登録済みなら true
	template <typename T>
	[[nodiscard]] bool isRegistered() const
	{
		return m_entries.find(sgc::typeId<T>()) != m_entries.end();
	}

	/// @brief 登録済みコンポーネント型数を取得する
	/// @return 登録数
	[[nodiscard]] std::size_t registeredCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 登録済みコンポーネント型名の一覧を取得する
	/// @return 型名のベクタ
	[[nodiscard]] std::vector<std::string> registeredTypeNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_entries.size());
		for (const auto& [id, entry] : m_entries)
		{
			names.push_back(entry.typeName);
		}
		return names;
	}

	/// @brief MitiruWorld の全エンティティを JSON 文字列に保存する
	/// @param world 保存対象のワールド
	/// @return JSON 形式の文字列
	///
	/// @details 出力形式:
	/// @code
	/// {
	///   "entities": [
	///     {
	///       "id": 0, "generation": 0,
	///       "tag": "player",
	///       "components": {
	///         "Position": "{\"x\":100,\"y\":200}",
	///         "Velocity": "{\"dx\":1,\"dy\":0}"
	///       }
	///     }
	///   ]
	/// }
	/// @endcode
	[[nodiscard]] std::string saveWorld(const ecs::MitiruWorld& world) const
	{
		nlohmann::ordered_json entities = nlohmann::ordered_json::array();
		for (const auto& entity : m_trackedEntities)
		{
			if (world.world().isAlive(entity)) { entities.push_back(serializeEntity(world, entity)); }
		}
		return nlohmann::ordered_json{{"entities", std::move(entities)}}.dump();
	}

	/// @brief JSON 文字列から MitiruWorld にエンティティを読み込む
	/// @details エンティティを新しく作り、タグと登録済みコンポーネントを復元する。
	///          JSON として読めない・"entities" が配列でないときは何もしない。
	void loadWorld(ecs::MitiruWorld& world, const std::string& json) const
	{
		const auto doc = nlohmann::json::parse(json, nullptr, false);
		if (!doc.is_object()) return;
		const auto entities = doc.find("entities");
		if (entities == doc.end() || !entities->is_array()) return;
		for (const auto& entity : *entities)
		{
			if (entity.is_object()) { deserializeEntity(world, entity); }
		}
	}

	// ── エンティティ追跡 ────────────────────────────────────

	/// @brief 保存対象のエンティティを追跡リストに追加する
	/// @param entity 追跡するエンティティ
	void trackEntity(sgc::ecs::Entity entity)
	{
		m_trackedEntities.push_back(entity);
	}

	/// @brief 追跡リストをクリアする
	void clearTrackedEntities() noexcept
	{
		m_trackedEntities.clear();
	}

	/// @brief 追跡中のエンティティ数を取得する
	/// @return エンティティ数
	[[nodiscard]] std::size_t trackedEntityCount() const noexcept
	{
		return m_trackedEntities.size();
	}

private:
	[[nodiscard]] nlohmann::ordered_json serializeEntity(
		const ecs::MitiruWorld& world, sgc::ecs::Entity entity) const
	{
		nlohmann::ordered_json components = nlohmann::ordered_json::object();
		for (const auto& [typeId, entry] : m_entries)
		{
			if (!entry.hasComponent(world.world(), entity)) continue;
			auto compJson = entry.serialize(world.world(), entity);
			if (!compJson.empty()) { components[entry.typeName] = std::move(compJson); }
		}
		return {
			{"id", entity.id},
			{"generation", entity.generation},
			{"tag", world.getTag(entity)},
			{"components", std::move(components)},
		};
	}

	void deserializeEntity(ecs::MitiruWorld& world, const nlohmann::json& obj) const
	{
		auto entity = world.world().createEntity();
		if (const auto tag = obj.find("tag"); tag != obj.end() && tag->is_string() && !tag->get<std::string>().empty())
		{
			world.setTag(entity, tag->get<std::string>());
		}
		const auto components = obj.find("components");
		if (components == obj.end() || !components->is_object()) return;
		for (const auto& [typeId, entry] : m_entries)
		{
			const auto comp = components->find(entry.typeName);
			if (comp != components->end() && comp->is_string())
			{
				entry.deserialize(world.world(), entity, comp->get<std::string>());
			}
		}
	}

	/// @brief 型 ID → シリアライザ/デシリアライザエントリ
	std::unordered_map<sgc::TypeIdValue, ComponentPersistenceEntry> m_entries;

	/// @brief 保存対象のエンティティ追跡リスト
	std::vector<sgc::ecs::Entity> m_trackedEntities;
};

} // namespace mitiru::scene
