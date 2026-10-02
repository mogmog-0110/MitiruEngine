#pragma once

/// @file ConfigManager.hpp
/// @brief ランタイム設定管理
///
/// キー・バリュー形式の設定管理を提供する。
/// JSON による読み書き、変更通知コールバックに対応。
///
/// @code
/// mitiru::data::ConfigManager config;
/// config.set("volume", 0.8f);
/// config.set("fullscreen", true);
/// auto vol = config.getFloat("volume"); // 0.8f
///
/// config.onChange("volume", [](const auto& val) {
///     // 音量変更時に呼ばれる
/// });
/// @endcode

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "mitiru/data/JsonFields.hpp"

namespace mitiru::data
{

/// @brief 設定値の型
using ConfigValue = std::variant<std::string, int, float, bool>;

/// @brief 変更通知コールバック型
using ConfigChangeCallback = std::function<void(const ConfigValue&)>;

/// @brief ランタイム設定マネージャー
///
/// キー・バリュー形式で設定を管理し、
/// JSON 読み書きや変更通知コールバックを提供する。
class ConfigManager
{
public:
	/// @brief 設定値を設定する
	/// @param key キー名
	/// @param value 設定値
	void set(const std::string& key, const ConfigValue& value)
	{
		m_values[key] = value;
		notifyChange(key, value);
	}

	/// @brief 設定値を型指定で取得する
	/// @tparam T 取得する型
	/// @param key キー名
	/// @return 値（型不一致またはキー不在時 nullopt）
	template <typename T>
	[[nodiscard]] std::optional<T> get(const std::string& key) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return std::nullopt;
		if (auto* val = std::get_if<T>(&it->second))
		{
			return *val;
		}
		return std::nullopt;
	}

	/// @brief 文字列値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合 nullopt）
	[[nodiscard]] std::optional<std::string> getString(const std::string& key) const
	{
		return get<std::string>(key);
	}

	/// @brief 整数値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合 nullopt）
	[[nodiscard]] std::optional<int> getInt(const std::string& key) const
	{
		return get<int>(key);
	}

	/// @brief 浮動小数点値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合 nullopt）
	[[nodiscard]] std::optional<float> getFloat(const std::string& key) const
	{
		return get<float>(key);
	}

	/// @brief 真偽値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合 nullopt）
	[[nodiscard]] std::optional<bool> getBool(const std::string& key) const
	{
		return get<bool>(key);
	}

	/// @brief JSON から設定を読み込む
	/// @param json 最上位のオブジェクト、または {"config":{...}} の中身を読む。配列や入れ子の値は読まない。
	/// @return 成功時 true
	bool loadFromJson(const std::string& json)
	{
		const auto doc = nlohmann::json::parse(json, nullptr, false);
		if (!doc.is_object()) return false;

		const auto config = doc.find("config");
		const auto& source = (config != doc.end() && config->is_object()) ? *config : doc;
		for (const auto& [key, v] : source.items())
		{
			if (v.is_string())              m_values[key] = v.get<std::string>();
			else if (v.is_boolean())        m_values[key] = v.get<bool>();
			else if (v.is_number_integer()) m_values[key] = v.get<int>();
			else if (v.is_number_float())   m_values[key] = v.get<float>();
		}
		return true;
	}

	/// @brief 設定を JSON 文字列にエクスポートする
	/// @return JSON 文字列
	[[nodiscard]] std::string saveToJson() const
	{
		nlohmann::json doc = nlohmann::json::object();
		for (const auto& [key, value] : m_values)
		{
			std::visit([&doc, &key](const auto& v)
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, float>) { doc[key] = jsonFloat(v); }
				else                                    { doc[key] = v; }
			}, value);
		}
		return doc.dump();
	}

	/// @brief キーが存在するか確認する
	/// @param key キー名
	/// @return 存在する場合 true
	[[nodiscard]] bool hasKey(const std::string& key) const
	{
		return m_values.count(key) > 0;
	}

	/// @brief 全キーのリストを返す
	/// @return キー名のベクタ
	[[nodiscard]] std::vector<std::string> keys() const
	{
		std::vector<std::string> result;
		result.reserve(m_values.size());
		for (const auto& [key, value] : m_values)
		{
			result.push_back(key);
		}
		return result;
	}

	/// @brief デフォルト値を設定する（既存キーは上書きしない）
	/// @param key キー名
	/// @param value デフォルト値
	void setDefault(const std::string& key, const ConfigValue& value)
	{
		if (m_values.count(key) == 0)
		{
			m_values[key] = value;
		}
	}

	/// @brief 変更通知コールバックを登録する
	/// @param key 監視対象のキー名
	/// @param callback コールバック関数
	void onChange(const std::string& key, ConfigChangeCallback callback)
	{
		m_listeners[key].push_back(std::move(callback));
	}

private:
	std::unordered_map<std::string, ConfigValue> m_values;
	std::unordered_map<std::string, std::vector<ConfigChangeCallback>> m_listeners;

	/// @brief 変更通知を発行する
	/// @param key 変更されたキー
	/// @param value 新しい値
	void notifyChange(const std::string& key, const ConfigValue& value)
	{
		auto it = m_listeners.find(key);
		if (it == m_listeners.end()) return;
		for (const auto& callback : it->second)
		{
			callback(value);
		}
	}
};

} // namespace mitiru::data
