#pragma once

/// @file ConfigManager.hpp
/// @brief ランタイム設定管理
///
/// キー・バリュー形式の設定管理を提供する。
/// JSONによる読み書き、変更通知コールバックに対応。
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
#include <variant>
#include <vector>

#include "mitiru/data/JsonBuilder.hpp"

namespace mitiru::data
{

/// @brief 設定値の型
using ConfigValue = std::variant<std::string, int, float, bool>;

/// @brief 変更通知コールバック型
using ConfigChangeCallback = std::function<void(const ConfigValue&)>;

/// @brief ランタイム設定マネージャー
///
/// キー・バリュー形式で設定を管理し、
/// JSON読み書きや変更通知コールバックを提供する。
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
	/// @return 値（型不一致またはキー不在時nullopt）
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
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<std::string> getString(const std::string& key) const
	{
		return get<std::string>(key);
	}

	/// @brief 整数値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<int> getInt(const std::string& key) const
	{
		return get<int>(key);
	}

	/// @brief 浮動小数点値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<float> getFloat(const std::string& key) const
	{
		return get<float>(key);
	}

	/// @brief 真偽値を取得する
	/// @param key キー名
	/// @return 値（存在しない場合nullopt）
	[[nodiscard]] std::optional<bool> getBool(const std::string& key) const
	{
		return get<bool>(key);
	}

	/// @brief JSONから設定を読み込む
	/// @param json JSON文字列
	/// @return 成功時true
	bool loadFromJson(const std::string& json)
	{
		JsonReader reader;
		if (!reader.parse(json)) return false;

		/// 既知のキーパターンに基づいて値を読み取る
		/// JsonReaderのparse結果をフラットに走査する
		/// ここでは簡易的にconfigオブジェクト内のフィールドを読む
		auto configObj = reader.getObject("config");
		if (!configObj.has_value())
		{
			/// トップレベルを直接読む
			return loadFromReader(reader);
		}
		return loadFromReader(*configObj);
	}

	/// @brief 設定をJSON文字列にエクスポートする
	/// @return JSON文字列
	[[nodiscard]] std::string saveToJson() const
	{
		JsonBuilder builder;
		builder.beginObject();

		for (const auto& [key, value] : m_values)
		{
			builder.key(key);
			std::visit([&builder](const auto& v)
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::string>)
				{
					builder.value(v);
				}
				else if constexpr (std::is_same_v<T, int>)
				{
					builder.value(v);
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					builder.value(v);
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					builder.value(v);
				}
			}, value);
		}

		builder.endObject();
		return builder.build();
	}

	/// @brief キーが存在するか確認する
	/// @param key キー名
	/// @return 存在する場合true
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

	/// @brief JsonReaderから設定を読み込む
	/// @param reader JsonReader
	/// @return 成功時true
	bool loadFromReader(const JsonReader& reader)
	{
		/// JsonReaderのsource()からキーを再解析する簡易実装
		/// トップレベルのフラットな値を読み取る
		const auto& src = reader.source();
		if (src.empty()) return false;

		/// 再度パースしてキー・値ペアを取得
		JsonReader flatReader;
		if (!flatReader.parse(src)) return false;

		/// よく使われるキーパターンを試行
		/// フラットなキー・値ペアを走査する独自ロジック
		parseAndSetValues(src);
		return true;
	}

	/// @brief JSON文字列からキー・値ペアを解析して設定する
	/// @param json JSON文字列
	void parseAndSetValues(const std::string& json)
	{
		size_t pos = 0;

		/// '{' をスキップ
		pos = skipWs(json, pos);
		if (pos >= json.size() || json[pos] != '{') return;
		++pos;

		while (pos < json.size())
		{
			pos = skipWs(json, pos);
			if (pos >= json.size() || json[pos] == '}') break;

			/// キーを読む
			if (json[pos] != '"') break;
			++pos;
			size_t keyEnd = json.find('"', pos);
			if (keyEnd == std::string::npos) break;
			std::string key = json.substr(pos, keyEnd - pos);
			pos = keyEnd + 1;

			pos = skipWs(json, pos);
			if (pos >= json.size() || json[pos] != ':') break;
			++pos;
			pos = skipWs(json, pos);

			/// 値を読む
			if (pos >= json.size()) break;

			if (json[pos] == '"')
			{
				/// 文字列値
				++pos;
				size_t valEnd = json.find('"', pos);
				if (valEnd == std::string::npos) break;
				m_values[key] = json.substr(pos, valEnd - pos);
				pos = valEnd + 1;
			}
			else if (json[pos] == 't' || json[pos] == 'f')
			{
				/// 真偽値
				if (json.substr(pos, 4) == "true")
				{
					m_values[key] = true;
					pos += 4;
				}
				else if (json.substr(pos, 5) == "false")
				{
					m_values[key] = false;
					pos += 5;
				}
			}
			else if (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))
			{
				/// 数値
				size_t start = pos;
				bool isFloat = false;
				while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
					json[pos] != ' ' && json[pos] != '\n' && json[pos] != '\r' && json[pos] != '\t')
				{
					if (json[pos] == '.') isFloat = true;
					++pos;
				}
				std::string numStr = json.substr(start, pos - start);
				if (isFloat)
				{
					try { m_values[key] = std::stof(numStr); } catch (...) {}
				}
				else
				{
					try { m_values[key] = std::stoi(numStr); } catch (...) {}
				}
			}
			else
			{
				/// 不明な値はスキップ
				while (pos < json.size() && json[pos] != ',' && json[pos] != '}')
				{
					++pos;
				}
			}

			pos = skipWs(json, pos);
			if (pos < json.size() && json[pos] == ',') ++pos;
		}
	}

	/// @brief 空白をスキップする
	[[nodiscard]] static size_t skipWs(const std::string& s, size_t pos)
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
		{
			++pos;
		}
		return pos;
	}
};

} // namespace mitiru::data
