#pragma once

/// @file MitiruWorld.hpp
/// @brief Mitiru 拡張 ECS ワールド
/// @details sgc::ecs::World をラップし、文字列タグ・セマンティックラベル・
///          JSON スナップショット等のメタデータを管理する機能を足す。

#include <bitset>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/ecs/World.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/SemanticLabel.hpp>

namespace mitiru::ecs
{

/// @brief よく使うタグの型付き列挙
/// @details 文字列タグよりも、ビット比較で速く判定できる。
///          新しいタグを追加する場合は Count の前に挿入すること。
enum class CommonTag : std::uint8_t
{
	Player,
	Enemy,
	Platform,
	Collectible,
	UI,
	Camera,
	Trigger,
	Count  ///< 番兵（タグ数を表す）
};

/// @brief CommonTag 用ビットセット型
using CommonTagSet = std::bitset<static_cast<std::size_t>(CommonTag::Count)>;

/// @brief Mitiru拡張ECSワールド
/// @details sgc::ecs::Worldを内部に保持し、文字列タグとラベルによる
///          メタデータ管理機能を付与する。
///
/// @code
/// mitiru::ecs::MitiruWorld world;
/// auto entity = world.world().createEntity();
/// world.setTag(entity, "player");          // legacy (string)
/// world.addCommonTag(entity, CommonTag::Player); // preferred (typed)
/// world.addLabel(entity, "controllable");
/// @endcode
class MitiruWorld
{
public:
	/// @brief 内部の sgc ワールドへの参照を取得する
	/// @return sgc::ecs::World への参照
	[[nodiscard]] sgc::ecs::World& world() noexcept
	{
		return m_world;
	}

	/// @brief 内部の sgc ワールドへの const 参照を取得する
	/// @return sgc::ecs::World への const 参照
	[[nodiscard]] const sgc::ecs::World& world() const noexcept
	{
		return m_world;
	}

	// ── 型付き tag system (推奨) ───────────────────────────

	/// @brief エンティティに型付きタグを追加する
	/// @param entity 対象エンティティ
	/// @param tag 追加する CommonTag
	void addCommonTag(sgc::ecs::Entity entity, CommonTag tag)
	{
		if (!m_world.isAlive(entity))
		{
			return;
		}
		m_commonTags[entity.id].set(static_cast<std::size_t>(tag));
	}

	/// @brief エンティティから型付きタグを削除する
	/// @param entity 対象エンティティ
	/// @param tag 削除する CommonTag
	void removeCommonTag(sgc::ecs::Entity entity, CommonTag tag)
	{
		if (!m_world.isAlive(entity))
		{
			return;
		}
		const auto it = m_commonTags.find(entity.id);
		if (it != m_commonTags.end())
		{
			it->second.reset(static_cast<std::size_t>(tag));
		}
	}

	/// @brief エンティティが型付きタグを持つか判定する
	/// @param entity 対象エンティティ
	/// @param tag 検索する CommonTag
	/// @return 持っていれば true
	[[nodiscard]] bool hasCommonTag(sgc::ecs::Entity entity, CommonTag tag) const
	{
		if (!m_world.isAlive(entity))
		{
			return false;
		}
		const auto it = m_commonTags.find(entity.id);
		if (it == m_commonTags.end())
		{
			return false;
		}
		return it->second.test(static_cast<std::size_t>(tag));
	}

	/// @brief エンティティの型付きタグセット全体を取得する
	/// @param entity 対象エンティティ
	/// @return CommonTagSet（未設定の場合は空のビットセット）
	[[nodiscard]] CommonTagSet getCommonTags(sgc::ecs::Entity entity) const
	{
		if (!m_world.isAlive(entity))
		{
			return {};
		}
		const auto it = m_commonTags.find(entity.id);
		return (it != m_commonTags.end()) ? it->second : CommonTagSet{};
	}

	// ── 文字列 tag system (legacy、可能なら型付き tag を使う) ─

	/// @brief エンティティに文字列タグを設定する
	/// @param entity 対象エンティティ
	/// @param tag タグ文字列
	/// @deprecated 新規コードでは addCommonTag() を推奨。文字列比較はビット比較より遅い。
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
	/// @deprecated 新規コードでは hasCommonTag() を推奨。
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

	/// @brief 全エンティティとメタデータの JSON スナップショットを取得する
	/// @return JSON 形式の文字列
	[[nodiscard]] std::string snapshot() const
	{
		std::string json;
		json += "{";
		json += "\"entityCount\":" + std::to_string(entityCount()) + ",";

		/// タグ一覧（文字列タグ）
		json += "\"tags\":{";
		bool firstTag = true;
		for (const auto& [id, tag] : m_tags)
		{
			if (!firstTag)
			{
				json += ",";
			}
			json += "\"" + std::to_string(id) + "\":\"" + observe::jsonEscape(tag) + "\"";
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
		json += "},";

		/// 型付きタグ一覧
		json += "\"commonTags\":{";
		bool firstCommon = true;
		for (const auto& [id, tagSet] : m_commonTags)
		{
			if (!firstCommon)
			{
				json += ",";
			}
			json += "\"" + std::to_string(id) + "\":" + commonTagSetToJson(tagSet);
			firstCommon = false;
		}
		json += "}";

		json += "}";
		return json;
	}

	/// @brief エンティティを破棄し、そのメタデータも片付ける
	/// @param entity 破棄するエンティティ
	void destroyEntity(sgc::ecs::Entity entity)
	{
		m_tags.erase(entity.id);
		m_labels.erase(entity.id);
		m_commonTags.erase(entity.id);
		m_world.destroyEntity(entity);
	}

private:
	sgc::ecs::World m_world;  ///< 内部のsgcワールド

	/// @brief エンティティ ID → 文字列タグ (legacy)
	std::unordered_map<sgc::ecs::EntityId, std::string> m_tags;

	/// @brief エンティティ ID → セマンティックラベル
	std::unordered_map<sgc::ecs::EntityId, observe::SemanticLabel> m_labels;

	/// @brief エンティティ ID → 型付きタグビットセット
	std::unordered_map<sgc::ecs::EntityId, CommonTagSet> m_commonTags;

	/// @brief CommonTag 列挙値の文字列名を返す
	[[nodiscard]] static const char* commonTagName(CommonTag tag) noexcept
	{
		switch (tag)
		{
		case CommonTag::Player:      return "Player";
		case CommonTag::Enemy:       return "Enemy";
		case CommonTag::Platform:    return "Platform";
		case CommonTag::Collectible: return "Collectible";
		case CommonTag::UI:          return "UI";
		case CommonTag::Camera:      return "Camera";
		case CommonTag::Trigger:     return "Trigger";
		default:                     return "Unknown";
		}
	}

	/// @brief CommonTagSet を JSON 配列文字列に変換する
	[[nodiscard]] static std::string commonTagSetToJson(const CommonTagSet& tagSet)
	{
		std::string json = "[";
		bool first = true;
		for (std::size_t i = 0; i < static_cast<std::size_t>(CommonTag::Count); ++i)
		{
			if (tagSet.test(i))
			{
				if (!first)
				{
					json += ",";
				}
				json += "\"";
				json += commonTagName(static_cast<CommonTag>(i));
				json += "\"";
				first = false;
			}
		}
		json += "]";
		return json;
	}
};

} // namespace mitiru::ecs
