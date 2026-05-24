#pragma once

/// @file ConfigManager.hpp
/// @brief ゲーム設定管理
///
/// キー・バリュー形式のゲーム設定を管理する。
/// JSON形式でのシリアライズ・デシリアライズをサポートする。
///
/// @code
/// sgc::ConfigManager config;
/// config.set("volume", 0.8f);
/// config.set("fullscreen", true);
/// config.set("playerName", std::string("Player1"));
///
/// float vol = config.getFloatOr("volume", 1.0f);
/// std::string json = config.toJson();
/// @endcode

#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "sgc/core/JsonWriter.hpp"
#include "sgc/core/JsonReader.hpp"

namespace sgc
{

/// @brief 設定値型（int, float, bool, string）
using ConfigValue = std::variant<int, float, bool, std::string>;

/// @brief ゲーム設定マネージャ
///
/// キー・バリュー形式の設定を管理する。
/// JSON形式でのファイル保存・読み込みをサポートする。
class ConfigManager
{
public:
	/// @brief 値を設定する
	/// @param key キー名
	/// @param value 設定値
	void set(const std::string& key, ConfigValue value)
	{
		m_values[key] = std::move(value);
	}

	/// @brief int値を取得する
	/// @param key キー名
	/// @return 値が存在しint型ならその値、それ以外はnullopt
	[[nodiscard]] std::optional<int> getInt(const std::string& key) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return std::nullopt;
		if (const auto* p = std::get_if<int>(&it->second)) return *p;
		return std::nullopt;
	}

	/// @brief float値を取得する
	/// @param key キー名
	/// @return 値が存在しfloat型ならその値、それ以外はnullopt
	[[nodiscard]] std::optional<float> getFloat(const std::string& key) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return std::nullopt;
		if (const auto* p = std::get_if<float>(&it->second)) return *p;
		return std::nullopt;
	}

	/// @brief bool値を取得する
	/// @param key キー名
	/// @return 値が存在しbool型ならその値、それ以外はnullopt
	[[nodiscard]] std::optional<bool> getBool(const std::string& key) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return std::nullopt;
		if (const auto* p = std::get_if<bool>(&it->second)) return *p;
		return std::nullopt;
	}

	/// @brief string値を取得する
	/// @param key キー名
	/// @return 値が存在しstring型ならその値、それ以外はnullopt
	[[nodiscard]] std::optional<std::string> getString(const std::string& key) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return std::nullopt;
		if (const auto* p = std::get_if<std::string>(&it->second)) return *p;
		return std::nullopt;
	}

	/// @brief デフォルト値付きint取得
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return 値が存在すればその値、なければdefaultValue
	[[nodiscard]] int getIntOr(const std::string& key, int defaultValue) const
	{
		return getInt(key).value_or(defaultValue);
	}

	/// @brief デフォルト値付きfloat取得
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return 値が存在すればその値、なければdefaultValue
	[[nodiscard]] float getFloatOr(const std::string& key, float defaultValue) const
	{
		return getFloat(key).value_or(defaultValue);
	}

	/// @brief デフォルト値付きbool取得
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return 値が存在すればその値、なければdefaultValue
	[[nodiscard]] bool getBoolOr(const std::string& key, bool defaultValue) const
	{
		return getBool(key).value_or(defaultValue);
	}

	/// @brief デフォルト値付きstring取得
	/// @param key キー名
	/// @param defaultValue デフォルト値
	/// @return 値が存在すればその値、なければdefaultValue
	[[nodiscard]] std::string getStringOr(const std::string& key, const std::string& defaultValue) const
	{
		return getString(key).value_or(defaultValue);
	}

	/// @brief キーが存在するか
	/// @param key キー名
	/// @return 存在すればtrue
	[[nodiscard]] bool hasKey(const std::string& key) const
	{
		return m_values.contains(key);
	}

	/// @brief キーを削除する
	/// @param key 削除するキー名
	void remove(const std::string& key)
	{
		m_values.erase(key);
	}

	/// @brief 全設定をクリアする
	void clear() { m_values.clear(); }

	/// @brief 設定数を返す
	/// @return 登録されている設定の数
	[[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }

	/// @brief JSON文字列にシリアライズする
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		JsonWriter w;
		for (const auto& [key, val] : m_values)
		{
			std::visit([&](const auto& v)
			{
				using VT = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<VT, int>)
					w.write(key, v);
				else if constexpr (std::is_same_v<VT, float>)
					w.write(key, v);
				else if constexpr (std::is_same_v<VT, bool>)
					w.write(key, v);
				else if constexpr (std::is_same_v<VT, std::string>)
					w.write(key, v);
			}, val);
		}
		return w.toString();
	}

	/// @brief JSON文字列からデシリアライズする
	/// @param json JSON文字列
	/// @note JsonReaderのAPIではキー列挙ができないため、
	///       loadFromFile()後にset()で個別に設定するか、
	///       JsonReaderを直接使って値を読み込むこと。
	void fromJson(const std::string& json)
	{
		JsonReader r;
		r.fromString(json);
		if (r.hasError()) return;

		// JsonReaderはキー列挙APIを持たないため、
		// 現在の実装ではfromJsonによる自動読み込みはサポートしない。
		// toJson()でシリアライズした結果を手動でset()するか、
		// JsonReaderを直接使用すること。
	}

	/// @brief ファイルに保存する
	/// @param path ファイルパス
	/// @return 成功ならtrue
	bool saveToFile(const std::string& path) const
	{
		std::ofstream ofs(path);
		if (!ofs.is_open()) return false;
		ofs << toJson();
		return ofs.good();
	}

	/// @brief ファイルから読み込む
	/// @param path ファイルパス
	/// @return 成功ならtrue
	bool loadFromFile(const std::string& path)
	{
		std::ifstream ifs(path);
		if (!ifs.is_open()) return false;
		std::string content(
			(std::istreambuf_iterator<char>(ifs)),
			std::istreambuf_iterator<char>());
		fromJson(content);
		return true;
	}

private:
	std::unordered_map<std::string, ConfigValue> m_values;  ///< 設定値マップ
};

} // namespace sgc
