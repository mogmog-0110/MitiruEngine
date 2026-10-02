#pragma once

/// @file Prefab.hpp
/// @brief エンティティテンプレート（プレハブ）
/// @details JSON データからエンティティを生成するためのテンプレートシステム。
///          コンポーネントのデータを JSON 文字列として保持する。

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::ecs
{

/// @brief プレハブコンポーネント定義
/// @details コンポーネント名と、その JSON 形式のデータを保持する。
struct PrefabComponent
{
	std::string name;       ///< コンポーネント名（例: "Position", "Velocity"）
	std::string jsonData;   ///< コンポーネントデータのJSON文字列
};

/// @brief プレハブ（エンティティテンプレート）
/// @details エンティティの構成をコンポーネントの集合として定義する。
struct Prefab
{
	std::string name;                          ///< プレハブ名
	std::vector<PrefabComponent> components;   ///< コンポーネント定義群
};

/// @brief プレハブライブラリ
/// @details 名前付きプレハブを登録・検索・シリアライズする。
///
/// @code
/// mitiru::ecs::PrefabLibrary lib;
/// lib.add(Prefab{"player", {{"Position", "{\"x\":0,\"y\":0}"}}});
/// auto prefab = lib.get("player");
/// @endcode
class PrefabLibrary
{
public:
	/// @brief プレハブを追加する
	/// @param prefab 追加するプレハブ
	void add(Prefab prefab)
	{
		const auto name = prefab.name;
		m_prefabs[name] = std::move(prefab);
	}

	/// @brief 名前でプレハブを取得する
	/// @param name プレハブ名
	/// @return プレハブ（存在しない場合は nullopt）
	[[nodiscard]] std::optional<Prefab> get(const std::string& name) const
	{
		const auto it = m_prefabs.find(name);
		if (it == m_prefabs.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief プレハブが存在するか判定する
	/// @param name プレハブ名
	/// @return 存在すれば true
	[[nodiscard]] bool has(const std::string& name) const
	{
		return m_prefabs.find(name) != m_prefabs.end();
	}

	/// @brief プレハブを削除する
	/// @param name 削除するプレハブ名
	void remove(const std::string& name)
	{
		m_prefabs.erase(name);
	}

	/// @brief 登録されている全プレハブ名を取得する
	/// @return プレハブ名のベクタ
	[[nodiscard]] std::vector<std::string> listNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_prefabs.size());
		for (const auto& [name, prefab] : m_prefabs)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief プレハブを JSON 文字列に変換する
	/// @details jsonData は JSON として埋め込む。JSON として読めないデータは null になる。
	[[nodiscard]] static std::string toJson(const Prefab& prefab)
	{
		nlohmann::ordered_json components = nlohmann::ordered_json::array();
		for (const auto& comp : prefab.components)
		{
			auto data = nlohmann::ordered_json::parse(comp.jsonData, nullptr, false);
			if (data.is_discarded()) { data = nullptr; }
			components.push_back({{"name", comp.name}, {"data", std::move(data)}});
		}
		return nlohmann::ordered_json{{"name", prefab.name}, {"components", std::move(components)}}.dump();
	}

	/// @brief JSON 文字列からプレハブを構築する。読めない JSON からは空のプレハブを返す。
	[[nodiscard]] static Prefab fromJson(const std::string& jsonStr)
	{
		Prefab prefab;
		const auto doc = nlohmann::ordered_json::parse(jsonStr, nullptr, false);
		if (!doc.is_object()) return prefab;
		if (const auto name = doc.find("name"); name != doc.end() && name->is_string())
		{
			prefab.name = name->get<std::string>();
		}
		const auto components = doc.find("components");
		if (components == doc.end() || !components->is_array()) return prefab;
		for (const auto& comp : *components)
		{
			const auto name = comp.is_object() ? comp.find("name") : comp.end();
			if (!comp.is_object() || name == comp.end() || !name->is_string()) continue;
			const auto data = comp.find("data");
			prefab.components.push_back({name->get<std::string>(), data == comp.end() ? "null" : data->dump()});
		}
		return prefab;
	}

	/// @brief 登録数を取得する
	/// @return プレハブ数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_prefabs.size();
	}

private:
	/// @brief 名前 → プレハブ
	std::unordered_map<std::string, Prefab> m_prefabs;
};

} // namespace mitiru::ecs
