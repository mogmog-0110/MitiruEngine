#pragma once

/// @file EntityPersistence.hpp
/// @brief エンティティ・コンポーネントの永続化（保存/読み込み）
/// @details MitiruWorldのエンティティとコンポーネントをJSON形式で
///          シリアライズ/デシリアライズする。
///          コンポーネント型ごとにシリアライザ/デシリアライザを登録し、
///          型安全な永続化を実現する。

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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

	/// @brief シリアライズ関数: World + Entity → JSON文字列
	/// @details 指定エンティティが該当コンポーネントを持つ場合にJSON文字列を返す。
	///          持たない場合は空文字列を返す。
	std::function<std::string(const sgc::ecs::World&, sgc::ecs::Entity)> serialize;

	/// @brief デシリアライズ関数: World + Entity + JSON文字列 → コンポーネント復元
	/// @details JSON文字列からコンポーネントを復元し、エンティティに追加する。
	std::function<void(sgc::ecs::World&, sgc::ecs::Entity, const std::string&)> deserialize;

	/// @brief エンティティが該当コンポーネントを持つか確認する関数
	std::function<bool(const sgc::ecs::World&, sgc::ecs::Entity)> hasComponent;
};

/// @brief エンティティ永続化システム
/// @details コンポーネント型ごとにシリアライザ/デシリアライザを登録し、
///          MitiruWorldの全エンティティをJSON形式で保存/読み込みする。
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
///         // JSONパース処理...
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
	/// @param typeName コンポーネント型名（JSONキーとして使用）
	/// @param serialize シリアライズ関数: T → JSON文字列
	/// @param deserialize デシリアライズ関数: JSON文字列 → T
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

		/// hasComponentラッパー
		entry.hasComponent = [](const sgc::ecs::World& world, sgc::ecs::Entity entity) -> bool
		{
			return world.hasComponent<T>(entity);
		};

		m_entries[typeId] = std::move(entry);
	}

	/// @brief 指定コンポーネント型が登録済みか確認する
	/// @tparam T コンポーネント型
	/// @return 登録済みならtrue
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

	/// @brief MitiruWorldの全エンティティをJSON文字列に保存する
	/// @param world 保存対象のワールド
	/// @return JSON形式の文字列
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
		std::string json;
		json += "{\"entities\":[";

		/// 全エンティティを走査するため、内部Worldからエンティティを列挙する
		/// MitiruWorldはforEachEntityを提供しないため、保存対象リストを使う
		bool firstEntity = true;

		for (const auto& entity : m_trackedEntities)
		{
			if (!world.world().isAlive(entity)) continue;

			if (!firstEntity) json += ",";
			json += serializeEntity(world, entity);
			firstEntity = false;
		}

		json += "]}";
		return json;
	}

	/// @brief JSON文字列からMitiruWorldにエンティティを読み込む
	/// @param world 読み込み先のワールド
	/// @param json JSON形式の文字列
	///
	/// @details 簡易JSONパーサーでエンティティ配列を走査し、
	///          各エンティティのコンポーネントを復元する。
	///          タグとラベルも復元する。
	void loadWorld(ecs::MitiruWorld& world, const std::string& json) const
	{
		/// エンティティ配列の開始位置を検索する
		const auto entitiesPos = json.find("\"entities\":[");
		if (entitiesPos == std::string::npos) return;

		auto pos = json.find('[', entitiesPos);
		if (pos == std::string::npos) return;
		++pos;

		/// 各エンティティオブジェクトを処理する
		while (pos < json.size())
		{
			/// 次の '{' を検索する
			const auto objStart = json.find('{', pos);
			if (objStart == std::string::npos) break;

			/// 対応する '}' を検索する（ネストを考慮）
			const auto objEnd = findMatchingBrace(json, objStart);
			if (objEnd == std::string::npos) break;

			const std::string entityJson = json.substr(objStart, objEnd - objStart + 1);
			deserializeEntity(world, entityJson);

			pos = objEnd + 1;

			/// 次のエンティティまたは配列終了を判定する
			const auto next = json.find_first_of(",]", pos);
			if (next == std::string::npos || json[next] == ']') break;
			pos = next + 1;
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
	/// @brief 単一エンティティをJSON文字列にシリアライズする
	/// @param world ワールド
	/// @param entity 対象エンティティ
	/// @return JSON文字列
	[[nodiscard]] std::string serializeEntity(
		const ecs::MitiruWorld& world, sgc::ecs::Entity entity) const
	{
		std::string json;
		json += "{";
		json += "\"id\":" + std::to_string(entity.id) + ",";
		json += "\"generation\":" + std::to_string(entity.generation) + ",";

		/// タグ
		const auto tag = world.getTag(entity);
		json += "\"tag\":\"" + tag + "\",";

		/// コンポーネント
		json += "\"components\":{";
		bool firstComp = true;
		for (const auto& [typeId, entry] : m_entries)
		{
			if (!entry.hasComponent(world.world(), entity)) continue;

			const auto compJson = entry.serialize(world.world(), entity);
			if (compJson.empty()) continue;

			if (!firstComp) json += ",";
			json += "\"" + entry.typeName + "\":" + escapeJsonString(compJson);
			firstComp = false;
		}
		json += "}";

		json += "}";
		return json;
	}

	/// @brief JSON文字列から単一エンティティをデシリアライズする
	/// @param world 読み込み先ワールド
	/// @param entityJson エンティティのJSON文字列
	void deserializeEntity(ecs::MitiruWorld& world, const std::string& entityJson) const
	{
		/// 新しいエンティティを作成する
		auto entity = world.world().createEntity();

		/// タグを復元する
		const auto tagValue = extractJsonStringValue(entityJson, "tag");
		if (!tagValue.empty())
		{
			world.setTag(entity, tagValue);
		}

		/// 追跡リストに追加する（非constのためmutable参照が必要だが、
		/// loadWorldはconst関数なのでここでは追跡しない）

		/// コンポーネントを復元する
		const auto componentsPos = entityJson.find("\"components\":{");
		if (componentsPos == std::string::npos) return;

		for (const auto& [typeId, entry] : m_entries)
		{
			const auto compJsonStr = extractJsonStringValue(entityJson, entry.typeName);
			if (!compJsonStr.empty())
			{
				entry.deserialize(world.world(), entity, compJsonStr);
			}
		}
	}

	/// @brief JSON文字列から指定キーの文字列値を抽出する
	/// @param json JSON文字列
	/// @param key キー名
	/// @return 値の文字列（見つからない場合は空文字列）
	[[nodiscard]] static std::string extractJsonStringValue(
		const std::string& json, const std::string& key)
	{
		const std::string searchKey = "\"" + key + "\":\"";
		const auto pos = json.find(searchKey);
		if (pos == std::string::npos) return {};

		const auto valueStart = pos + searchKey.size();
		const auto valueEnd = json.find('"', valueStart);
		if (valueEnd == std::string::npos) return {};

		return json.substr(valueStart, valueEnd - valueStart);
	}

	/// @brief JSON文字列をエスケープしてクォートで囲む
	/// @param str 入力文字列
	/// @return エスケープ済みクォート付き文字列
	[[nodiscard]] static std::string escapeJsonString(const std::string& str)
	{
		std::string result;
		result += "\"";
		for (const char c : str)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:   result += c; break;
			}
		}
		result += "\"";
		return result;
	}

	/// @brief 対応する閉じ括弧の位置を検索する
	/// @param json JSON文字列
	/// @param openPos 開き括弧 '{' の位置
	/// @return 閉じ括弧 '}' の位置（見つからない場合はnpos）
	[[nodiscard]] static std::size_t findMatchingBrace(
		const std::string& json, std::size_t openPos)
	{
		int depth = 0;
		bool inString = false;

		for (std::size_t i = openPos; i < json.size(); ++i)
		{
			const char c = json[i];

			if (c == '"' && (i == 0 || json[i - 1] != '\\'))
			{
				inString = !inString;
				continue;
			}

			if (inString) continue;

			if (c == '{') ++depth;
			else if (c == '}')
			{
				--depth;
				if (depth == 0) return i;
			}
		}

		return std::string::npos;
	}

	/// @brief 型ID → シリアライザ/デシリアライザエントリ
	std::unordered_map<sgc::TypeIdValue, ComponentPersistenceEntry> m_entries;

	/// @brief 保存対象のエンティティ追跡リスト
	std::vector<sgc::ecs::Entity> m_trackedEntities;
};

} // namespace mitiru::scene
