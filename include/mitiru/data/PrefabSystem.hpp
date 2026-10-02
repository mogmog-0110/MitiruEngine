#pragma once

/// @file PrefabSystem.hpp
/// @brief プレハブ（テンプレートエンティティ）システム
///
/// JSON ベースのプレハブ定義からエンティティを生成する。
/// AI 生成コンテンツとの親和性を重視したデータ駆動設計。
///
/// @code
/// mitiru::data::PrefabLibrary library;
/// library.registerPrefab({"Player", R"({"mesh":"player","hp":100})", {}});
/// auto id = library.instantiate("Player", world);
/// @endcode

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"
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
/// JSON によるバッチ登録・エクスポートに対応。
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
	/// @return 生成されたエンティティ ID（失敗時 INVALID_ENTITY）
	[[nodiscard]] scene::EntityId instantiate(
		const std::string& prefabName,
		scene::GameWorld& world) const
	{
		auto it = m_prefabs.find(prefabName);
		if (it == m_prefabs.end()) return scene::INVALID_ENTITY;

		const auto& prefab = it->second;
		auto entityId = world.createEntity(prefab.name);

		/// コンポーネント JSON をパースしてコンポーネントを追加
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
	/// @param overridesJson オーバーライド用 JSON 文字列
	/// @param world ゲームワールド
	/// @return 生成されたエンティティ ID（失敗時 INVALID_ENTITY）
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
	/// @return プレハブ定義（存在しない場合は nullopt）
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

	/// @brief JSON から複数のプレハブを一括読み込みする
	/// @param json {"prefabs":[{"name":"...","components":"...","children":["..."]},...]}。
	///        components は JSON を文字列にしたものでも、オブジェクトそのままでもよい。
	/// @return 成功時 true
	bool loadFromJson(const std::string& json)
	{
		const auto doc = nlohmann::json::parse(json, nullptr, false);
		if (!doc.is_object()) return false;
		const auto prefabs = doc.find("prefabs");
		if (prefabs == doc.end() || !prefabs->is_array()) return false;

		for (const auto& entry : *prefabs)
		{
			if (auto prefab = prefabFromJson(entry)) { m_prefabs[prefab->name] = std::move(*prefab); }
		}
		return true;
	}

	/// @brief 全プレハブを JSON 文字列にエクスポートする
	/// @return JSON 文字列
	[[nodiscard]] std::string toJson() const
	{
		nlohmann::json prefabs = nlohmann::json::array();
		for (const auto& [name, prefab] : m_prefabs)
		{
			prefabs.push_back({
				{"name", prefab.name},
				{"components", prefab.componentsJson},
				{"children", prefab.childPrefabs},
			});
		}
		return nlohmann::json{{"prefabs", std::move(prefabs)}}.dump();
	}

private:
	std::unordered_map<std::string, Prefab> m_prefabs;

	[[nodiscard]] static std::optional<Prefab> prefabFromJson(const nlohmann::json& entry)
	{
		if (!entry.is_object()) return std::nullopt;
		const auto name = entry.find("name");
		if (name == entry.end() || !name->is_string()) return std::nullopt;

		Prefab prefab;
		prefab.name = name->get<std::string>();
		if (const auto comp = entry.find("components"); comp != entry.end())
		{
			prefab.componentsJson = comp->is_string() ? comp->get<std::string>() : comp->dump();
		}
		if (const auto children = entry.find("children"); children != entry.end() && children->is_array())
		{
			for (const auto& child : *children)
			{
				if (child.is_string()) { prefab.childPrefabs.push_back(child.get<std::string>()); }
			}
		}
		return prefab;
	}

	/// @brief コンポーネント JSON からコンポーネントを適用する
	/// @param entityId エンティティ ID
	/// @param componentsJson コンポーネント JSON 文字列
	/// @param world ゲームワールド
	void applyComponents(
		scene::EntityId entityId,
		const std::string& componentsJson,
		scene::GameWorld& world) const
	{
		if (componentsJson.empty()) return;
		const auto comps = nlohmann::json::parse(componentsJson, nullptr, false);
		if (!comps.is_object()) return;

		if (const auto mesh = comps.find("mesh"); mesh != comps.end() && mesh->is_string())
		{
			world.addComponent(entityId, scene::MeshComponent{mesh->get<std::string>()});
		}

		if (const auto pos = comps.find("position"); pos != comps.end() && pos->is_object())
		{
			scene::TransformComponent tc;
			tc.position.x = fieldOr(*pos, "x", tc.position.x);
			tc.position.y = fieldOr(*pos, "y", tc.position.y);
			tc.position.z = fieldOr(*pos, "z", tc.position.z);
			world.addComponent(entityId, tc);
		}
	}
};

} // namespace mitiru::data
