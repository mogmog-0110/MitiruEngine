#pragma once

/// @file JsonHelper.hpp
/// @brief 簡易JSONフィールド抽出ユーティリティ

#include <cstddef>
#include <string>

namespace mitiru::server::detail
{

/// @brief JSON文字列フィールドを抽出する
[[nodiscard]] inline std::string extractJsonString(
	const std::string& json, const char* field,
	const std::string& defaultVal = {})
{
	const std::string key = std::string("\"") + field + "\"";
	const auto pos = json.find(key);
	if (pos == std::string::npos) { return defaultVal; }
	const auto col = json.find(':', pos + key.size());
	if (col == std::string::npos) { return defaultVal; }

	auto start = col + 1;
	while (start < json.size() && (json[start] == ' ' || json[start] == '\t'))
	{
		++start;
	}
	if (start >= json.size() || json[start] != '"') { return defaultVal; }
	++start;
	auto end = json.find('"', start);
	if (end == std::string::npos) { return defaultVal; }
	return json.substr(start, end - start);
}

/// @brief JSONから整数フィールドを抽出する
[[nodiscard]] inline int extractJsonInt(
	const std::string& json, const char* field, int defaultVal = 0)
{
	const std::string key = std::string("\"") + field + "\"";
	const auto pos = json.find(key);
	if (pos == std::string::npos) { return defaultVal; }
	const auto col = json.find(':', pos + key.size());
	if (col == std::string::npos) { return defaultVal; }
	auto start = col + 1;
	while (start < json.size() && json[start] == ' ') { ++start; }
	try { return std::stoi(json.substr(start)); }
	catch (...) { return defaultVal; }
}

/// @brief JSONからブール値フィールドを抽出する
[[nodiscard]] inline bool extractJsonBool(
	const std::string& json, const char* field, bool defaultVal = false)
{
	const std::string key = std::string("\"") + field + "\"";
	const auto pos = json.find(key);
	if (pos == std::string::npos) { return defaultVal; }
	const auto col = json.find(':', pos + key.size());
	if (col == std::string::npos) { return defaultVal; }
	auto start = col + 1;
	while (start < json.size() && json[start] == ' ') { ++start; }
	if (start < json.size() && json[start] == 't') { return true; }
	if (start < json.size() && json[start] == 'f') { return false; }
	return defaultVal;
}

/// @brief JSONから浮動小数点フィールドを抽出する
[[nodiscard]] inline float extractJsonFloat(
	const std::string& json, const char* field, float defaultVal = 0.0f)
{
	const std::string key = std::string("\"") + field + "\"";
	const auto pos = json.find(key);
	if (pos == std::string::npos) { return defaultVal; }
	const auto col = json.find(':', pos + key.size());
	if (col == std::string::npos) { return defaultVal; }
	auto start = col + 1;
	while (start < json.size() && json[start] == ' ') { ++start; }
	try { return std::stof(json.substr(start)); }
	catch (...) { return defaultVal; }
}

} // namespace mitiru::server::detail
