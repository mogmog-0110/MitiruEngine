#pragma once

/// @file MitiruWorld.hpp
/// @brief Mitiru拡張ECSワールド
/// @details sgc::ecs::Worldをラップし、文字列タグ・セマンティックラベル・
///          JSONスナップショット等のメタデータ管理機能を追加する。

#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/ecs/World.hpp>
#include <mitiru/observe/SemanticLabel.hpp>

namespace mitiru::ecs
{

/// @brief Mitiru拡張ECSワールド
/// @details sgc::ecs::Worldを内部に保持し、文字列タグとラベルによる
///          メタデータ管理機能を付与する。
///
/// @code
/// mitiru::ecs::MitiruWorld world;
/// auto entity = world.world().createEntity();
/// world.setTag(entity, "player");
/// world.addLabel(entity, "controllable");
/// @endcode
class MitiruWorld
{
public:
	/// @brief 内部のsgcワールドへの参照を取得する
	/// @return sgc::ecs::World への参照
	[[nodiscard]] sgc::ecs::World& world() noexcept
	{
		return m_world;
	}

	/// @brief 内部のsgcワールドへのconst参照を取得する
	/// @return sgc::ecs::World への const 参照
	[[nodiscard]] const sgc::ecs::World& world() const noexcept
	{
		return m_world;
	}

	/// @brief エンティティに文字列タグを設定する
	/// @param entity 対象エンティティ
	/// @param tag タグ文字列
	void setTag(sgc::ecs::Entity entity, std::string tag)
	{
		if (!m_world.isAlive(entity))
		{
			return;
		}
		m_tags[entity.id] = std::move(tag);
	}

	/// @brief エンティティの文字列タグを取得する
	/// @param entity 対象エンティティ
	/// @return タグ文字列（未設定の場合は空文字列）
	[[nodiscard]] std::string getTag(sgc::ecs::Entity entity) const
	{
		if (!m_world.isAlive(entity))
		{
			return {};
		}
		const auto it = m_tags.find(entity.id);
		return (it != m_tags.end()) ? it->second : std::string{};
	}

	/// @brief エンティティにセマンティックラベルを追加する
	/// @param entity 対象エンティティ
	/// @param label ラベル文字列
	void addLabel(sgc::ecs::Entity entity, const std::string& label)
	{
		if (!m_world.isAlive(entity))
		{
			return;
		}
		m_labels[entity.id].addLabel(label);
	}

	/// @brief エンティティが指定ラベルを持つか判定する
	/// @param entity 対象エンティティ
	/// @param label 検索するラベル
	/// @return 持っていれば true
	[[nodiscard]] bool hasLabel(sgc::ecs::Entity entity, const std::string& label) const
	{
		if (!m_world.isAlive(entity))
		{
			return false;
		}
		const auto it = m_labels.find(entity.id);
		if (it == m_labels.end())
		{
			return false;
		}
		return it->second.hasLabel(label);
	}

	/// @brief エンティティからラベルを削除する
	/// @param entity 対象エンティティ
	/// @param label 削除するラベル
	void removeLabel(sgc::ecs::Entity entity, const std::string& label)
	{
		if (!m_world.isAlive(entity))
		{
			return;
		}
		const auto it = m_labels.find(entity.id);
		if (it != m_labels.end())
		{
			it->second.removeLabel(label);
		}
	}

	/// @brief 生存エンティティ数を取得する
	/// @return エンティティ数
	[[nodiscard]] std::size_t entityCount() const noexcept
	{
		return m_world.entityCount();
	}

	/// @brief 全エンティティ+メタデータのJSONスナップショットを取得する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string snapshot() const
	{
		std::string json;
		json += "{";
		json += "\"entityCount\":" + std::to_string(entityCount()) + ",";

		/// タグ一覧
		json += "\"tags\":{";
		bool firstTag = true;
		for (const auto& [id, tag] : m_tags)
		{
			if (!firstTag)
			{
				json += ",";
			}
			json += "\"" + std::to_string(id) + "\":\"" + tag + "\"";
			firstTag = false;
		}
		json += "},";

		/// ラベル一覧
		json += "\"labels\":{";
		bool firstLabel = true;
		for (const auto& [id, labelSet] : m_labels)
		{
			if (!firstLabel)
			{
				json += ",";
			}
			json += "\"" + std::to_string(id) + "\":" + labelSet.toJson();
			firstLabel = false;
		}
		json += "}";

		json += "}";
		return json;
	}

	/// @brief エンティティ破棄時にメタデータもクリーンアップする
	/// @param entity 破棄するエンティティ
	void destroyEntity(sgc::ecs::Entity entity)
	{
		m_tags.erase(entity.id);
		m_labels.erase(entity.id);
		m_world.destroyEntity(entity);
	}

private:
	sgc::ecs::World m_world;  ///< 内部のsgcワールド

	/// @brief エンティティID → 文字列タグ
	std::unordered_map<sgc::ecs::EntityId, std::string> m_tags;

	/// @brief エンティティID → セマンティックラベル
	std::unordered_map<sgc::ecs::EntityId, observe::SemanticLabel> m_labels;
};

} // namespace mitiru::ecs
