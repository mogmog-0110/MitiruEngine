#pragma once

/// @file JsonHelper.hpp
/// @brief HTTP リクエスト本文 (JSON) の最上位フィールドを 1 つ取り出す
/// @details 本文が JSON として読めない・キーが無い・型が違うときは既定値を返す。
///          数値を文字列で送ってきた ("30") 場合も型違いとして既定値になる。

#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace mitiru::server::detail
{

/// @brief 本文を読み、最上位にあるキーの値を返す。無ければ nullptr。
[[nodiscard]] inline const nlohmann::json* findJsonField(
	const nlohmann::json& body, const char* field)
{
	if (!body.is_object()) { return nullptr; }
	const auto it = body.find(field);
	return it == body.end() ? nullptr : &*it;
}

[[nodiscard]] inline std::string extractJsonString(
	const std::string& json, const char* field,
	const std::string& defaultVal = {})
{
	const auto body = nlohmann::json::parse(json, nullptr, false);
	const auto* v = findJsonField(body, field);
	return (v && v->is_string()) ? v->get<std::string>() : defaultVal;
}

[[nodiscard]] inline int extractJsonInt(
	const std::string& json, const char* field, int defaultVal = 0)
{
	const auto body = nlohmann::json::parse(json, nullptr, false);
	const auto* v = findJsonField(body, field);
	if (!v || !v->is_number()) { return defaultVal; }
	// int に収まらない値は既定値。範囲外の浮動小数点→整数変換は未定義動作になる。
	const double d = v->get<double>();
	if (!(d >= static_cast<double>(std::numeric_limits<int>::min())
		&& d <= static_cast<double>(std::numeric_limits<int>::max())))
	{
		return defaultVal;
	}
	return v->is_number_float() ? static_cast<int>(d) : static_cast<int>(v->get<std::int64_t>());
}

[[nodiscard]] inline bool extractJsonBool(
	const std::string& json, const char* field, bool defaultVal = false)
{
	const auto body = nlohmann::json::parse(json, nullptr, false);
	const auto* v = findJsonField(body, field);
	return (v && v->is_boolean()) ? v->get<bool>() : defaultVal;
}

[[nodiscard]] inline float extractJsonFloat(
	const std::string& json, const char* field, float defaultVal = 0.0f)
{
	const auto body = nlohmann::json::parse(json, nullptr, false);
	const auto* v = findJsonField(body, field);
	return (v && v->is_number()) ? v->get<float>() : defaultVal;
}

} // namespace mitiru::server::detail
