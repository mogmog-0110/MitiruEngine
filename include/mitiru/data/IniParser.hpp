#pragma once

/// @file IniParser.hpp
/// @brief INIファイルパーサー（Siv3D INI風）
/// @details [セクション]とキー=値の形式を解析する。
///          コメント行（; または #）もサポートする。
///
/// @code
/// mitiru::data::IniParser ini;
/// ini.parse("[window]\nwidth=1280\nheight=720\n[audio]\nvolume=0.8");
/// auto w = ini.getInt("window", "width");    // 1280
/// auto v = ini.getFloat("audio", "volume");  // 0.8f
/// @endcode

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::data
{

/// @brief INIファイルパーサー
/// @details セクション付きKey=Value形式のテキストを解析する。
///          セクションなしのキーは空文字列セクションに格納される。
class IniParser
{
public:
	/// @brief INI文字列を解析する
	/// @param text INI形式のテキスト
	void parse(const std::string& text)
	{
		m_data.clear();
		std::string currentSection;
		std::istringstream stream(text);
		std::string line;

		while (std::getline(stream, line))
		{
			// 先頭・末尾の空白を除去する
			line = trim(line);

			// 空行・コメント行をスキップする
			if (line.empty() || line[0] == ';' || line[0] == '#')
			{
				continue;
			}

			// セクションヘッダ
			if (line.front() == '[' && line.back() == ']')
			{
				currentSection = line.substr(1, line.size() - 2);
				continue;
			}

			// キー=値
			const auto eq = line.find('=');
			if (eq != std::string::npos)
			{
				const auto key = trim(line.substr(0, eq));
				const auto value = trim(line.substr(eq + 1));
				m_data[currentSection][key] = value;
			}
		}
	}

	/// @brief 文字列値を取得する
	/// @param section セクション名
	/// @param key キー名
	/// @param defaultVal デフォルト値
	/// @return 値の文字列
	[[nodiscard]] std::string get(const std::string& section,
		const std::string& key, const std::string& defaultVal = {}) const
	{
		auto sit = m_data.find(section);
		if (sit == m_data.end()) return defaultVal;
		auto kit = sit->second.find(key);
		if (kit == sit->second.end()) return defaultVal;
		return kit->second;
	}

	/// @brief 整数値を取得する
	/// @param section セクション名
	/// @param key キー名
	/// @param defaultVal デフォルト値
	[[nodiscard]] int getInt(const std::string& section,
		const std::string& key, int defaultVal = 0) const
	{
		const auto s = get(section, key);
		if (s.empty()) return defaultVal;
		try { return std::stoi(s); }
		catch (...) { return defaultVal; }
	}

	/// @brief 浮動小数点値を取得する
	/// @param section セクション名
	/// @param key キー名
	/// @param defaultVal デフォルト値
	[[nodiscard]] float getFloat(const std::string& section,
		const std::string& key, float defaultVal = 0) const
	{
		const auto s = get(section, key);
		if (s.empty()) return defaultVal;
		try { return std::stof(s); }
		catch (...) { return defaultVal; }
	}

	/// @brief ブール値を取得する
	/// @param section セクション名
	/// @param key キー名
	/// @param defaultVal デフォルト値
	[[nodiscard]] bool getBool(const std::string& section,
		const std::string& key, bool defaultVal = false) const
	{
		const auto s = get(section, key);
		if (s.empty()) return defaultVal;
		return (s == "true" || s == "1" || s == "yes" || s == "on");
	}

	/// @brief セクションが存在するか
	/// @param section セクション名
	[[nodiscard]] bool hasSection(const std::string& section) const
	{
		return m_data.count(section) > 0;
	}

	/// @brief キーが存在するか
	/// @param section セクション名
	/// @param key キー名
	[[nodiscard]] bool hasKey(const std::string& section,
		const std::string& key) const
	{
		auto sit = m_data.find(section);
		if (sit == m_data.end()) return false;
		return sit->second.count(key) > 0;
	}

	/// @brief セクション一覧を取得する
	[[nodiscard]] std::vector<std::string> sections() const
	{
		std::vector<std::string> result;
		for (const auto& [section, kv] : m_data)
		{
			result.push_back(section);
		}
		return result;
	}

	/// @brief 全データをクリアする
	void clear() { m_data.clear(); }

private:
	/// @brief 文字列の前後の空白を除去する
	static std::string trim(const std::string& s)
	{
		const auto start = s.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) return {};
		const auto end = s.find_last_not_of(" \t\r\n");
		return s.substr(start, end - start + 1);
	}

	/// @brief セクション→(キー→値)の2重マップ
	std::unordered_map<std::string,
		std::unordered_map<std::string, std::string>> m_data;
};

} // namespace mitiru::data
