#pragma once

/// @file PrefabSystem.hpp
/// @brief プレハブ（テンプレートエンティティ）システム
///
/// JSONベースのプレハブ定義からエンティティを生成する。
/// AI生成コンテンツとの親和性を重視したデータ駆動設計。
///
/// @code
/// mitiru::data::PrefabLibrary library;
/// library.registerPrefab({"Player", R"({"mesh":"player","hp":100})", {}});
/// auto id = library.instantiate("Player", world);
/// @endcode

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mitiru/data/JsonBuilder.hpp"
#include "mitiru/scene/GameWorld.hpp"

namespace mitiru::data
{

/// @brief プレハブ定義
struct Prefab
{
	std::string name;                          ///< プレハブ名
	std::string componentsJson;                ///< コンポーネントデータ（JSON文字列）
	std::vector<std::string> childPrefabs;     ///< 子プレハブ名リスト
};

/// @brief プレハブライブラリ
///
/// プレハブの登録・検索・インスタンス化を管理する。
/// JSONによるバッチ登録・エクスポートに対応。
class PrefabLibrary
{
public:
	/// @brief プレハブを登録する
	/// @param prefab プレハブ定義
	void registerPrefab(const Prefab& prefab)
	{
		m_prefabs[prefab.name] = prefab;
	}

	/// @brief プレハブからエンティティを生成する
	/// @param prefabName プレハブ名
	/// @param world ゲームワールド
	/// @return 生成されたエンティティID（失敗時INVALID_ENTITY）
	[[nodiscard]] scene::EntityId instantiate(
		const std::string& prefabName,
		scene::GameWorld& world) const
	{
		auto it = m_prefabs.find(prefabName);
		if (it == m_prefabs.end()) return scene::INVALID_ENTITY;

		const auto& prefab = it->second;
		auto entityId = world.createEntity(prefab.name);

		/// コンポーネントJSONをパースしてコンポーネントを追加
		applyComponents(entityId, prefab.componentsJson, world);

		/// 子プレハブを再帰的に生成
		for (const auto& childName : prefab.childPrefabs)
		{
			(void)instantiate(childName, world);
		}

		return entityId;
	}

	/// @brief プレハブからオーバーライド付きでエンティティを生成する
	/// @param prefabName プレハブ名
	/// @param overridesJson オーバーライド用JSON文字列
	/// @param world ゲームワールド
	/// @return 生成されたエンティティID（失敗時INVALID_ENTITY）
	[[nodiscard]] scene::EntityId instantiateWithOverrides(
		const std::string& prefabName,
		const std::string& overridesJson,
		scene::GameWorld& world) const
	{
		auto it = m_prefabs.find(prefabName);
		if (it == m_prefabs.end()) return scene::INVALID_ENTITY;

		const auto& prefab = it->second;
		auto entityId = world.createEntity(prefab.name);

		/// ベースコンポーネントを適用
		applyComponents(entityId, prefab.componentsJson, world);

		/// オーバーライドを適用
		applyComponents(entityId, overridesJson, world);

		return entityId;
	}

	/// @brief プレハブを取得する
	/// @param name プレハブ名
	/// @return プレハブ定義（存在しない場合nullopt）
	[[nodiscard]] std::optional<Prefab> getPrefab(const std::string& name) const
	{
		auto it = m_prefabs.find(name);
		if (it == m_prefabs.end()) return std::nullopt;
		return it->second;
	}

	/// @brief 全プレハブ名を取得する
	/// @return プレハブ名のベクタ
	[[nodiscard]] std::vector<std::string> allPrefabNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_prefabs.size());
		for (const auto& [name, prefab] : m_prefabs)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief JSONから複数のプレハブを一括読み込みする
	/// @param json JSON文字列（配列形式）
	/// @return 成功時true
	bool loadFromJson(const std::string& json)
	{
		/// 配列形式のJSONをパースしてプレハブを登録する
		/// 形式: {"prefabs":[{"name":"...","components":"...","children":["..."]},...]}
		JsonReader reader;
		if (!reader.parse(json)) return false;

		auto prefabsArr = reader.getArray("prefabs");
		if (!prefabsArr.has_value()) return false;

		for (const auto& rawPrefab : *prefabsArr)
		{
			JsonReader prefabReader;
			if (!prefabReader.parse(rawPrefab)) continue;

			Prefab prefab;
			auto name = prefabReader.getString("name");
			if (!name.has_value()) continue;
			prefab.name = *name;

			auto comp = prefabReader.getString("components");
			if (comp.has_value())
			{
				prefab.componentsJson = *comp;
			}

			auto children = prefabReader.getArray("children");
			if (children.has_value())
			{
				prefab.childPrefabs = *children;
			}

			m_prefabs[prefab.name] = prefab;
		}

		return true;
	}

	/// @brief 全プレハブをJSON文字列にエクスポートする
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		JsonBuilder builder;
		builder.beginObject();
		builder.key("prefabs");
		builder.beginArray();

		for (const auto& [name, prefab] : m_prefabs)
		{
			builder.beginObject();
			builder.key("name").value(prefab.name);
			builder.key("components").value(prefab.componentsJson);
			builder.key("children");
			builder.beginArray();
			for (const auto& child : prefab.childPrefabs)
			{
				builder.value(child);
			}
			builder.endArray();
			builder.endObject();
		}

		builder.endArray();
		builder.endObject();
		return builder.build();
	}

private:
	std::unordered_map<std::string, Prefab> m_prefabs;

	/// @brief コンポーネントJSONからコンポーネントを適用する
	/// @param entityId エンティティID
	/// @param componentsJson コンポーネントJSON文字列
	/// @param world ゲームワールド
	void applyComponents(
		scene::EntityId entityId,
		const std::string& componentsJson,
		scene::GameWorld& world) const
	{
		if (componentsJson.empty()) return;

		JsonReader reader;
		if (!reader.parse(componentsJson)) return;

		/// meshフィールドがあればMeshComponentを追加
		auto mesh = reader.getString("mesh");
		if (mesh.has_value())
		{
			world.addComponent(entityId, scene::MeshComponent{*mesh});
		}

		/// positionフィールドがあればTransformComponentを追加/更新
		auto posObj = reader.getObject("position");
		if (posObj.has_value())
		{
			scene::TransformComponent tc;
			auto x = posObj->getFloat("x");
			auto y = posObj->getFloat("y");
			auto z = posObj->getFloat("z");
			if (x) tc.position.x = *x;
			if (y) tc.position.y = *y;
			if (z) tc.position.z = *z;
			world.addComponent(entityId, tc);
		}
	}
};

} // namespace mitiru::data
