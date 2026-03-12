#pragma once

/// @file Prefab.hpp
/// @brief エンティティテンプレート（プレハブ）
/// @details JSONデータからエンティティを生成するためのテンプレートシステム。
///          コンポーネントのデータをJSON文字列として保持する。

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::ecs
{

/// @brief プレハブコンポーネント定義
/// @details コンポーネント名とそのJSON形式のデータを保持する。
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
	/// @return プレハブ（存在しない場合はnullopt）
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

	/// @brief プレハブをJSON文字列に変換する
	/// @param prefab 対象プレハブ
	/// @return JSON形式の文字列
	[[nodiscard]] static std::string toJson(const Prefab& prefab)
	{
		std::string json;
		json += "{";
		json += "\"name\":\"" + prefab.name + "\",";
		json += "\"components\":[";
		for (std::size_t i = 0; i < prefab.components.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			const auto& comp = prefab.components[i];
			json += "{\"name\":\"" + comp.name + "\",\"data\":" + comp.jsonData + "}";
		}
		json += "]";
		json += "}";
		return json;
	}

	/// @brief JSON文字列からプレハブを構築する（簡易パーサー）
	/// @param jsonStr JSON形式の文字列
	/// @return 構築されたプレハブ
	/// @note 簡易実装のため、正規のJSONパーサーではない
	[[nodiscard]] static Prefab fromJson(const std::string& jsonStr)
	{
		Prefab prefab;

		/// "name":"..." の抽出
		const auto nameKey = jsonStr.find("\"name\":\"");
		if (nameKey != std::string::npos)
		{
			const auto nameStart = nameKey + 8;
			const auto nameEnd = jsonStr.find('"', nameStart);
			if (nameEnd != std::string::npos)
			{
				prefab.name = jsonStr.substr(nameStart, nameEnd - nameStart);
			}
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
