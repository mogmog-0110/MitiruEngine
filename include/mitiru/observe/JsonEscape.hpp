#pragma once

/// @file JsonEscape.hpp
/// @brief 手で組む JSON 文字列に値を埋め込むためのエスケープ (引用符は付けない)
/// @details エスケープは nlohmann/json に任せる。不正な UTF-8 は U+FFFD に置き換えるので、
///          受け手 (inspector / AI) の JSON パーサがスナップショットごと読めなくなることはない。

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace mitiru::observe
{

[[nodiscard]] inline std::string jsonEscape(std::string_view input)
{
	std::string quoted = nlohmann::json(input).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	quoted.pop_back();
	quoted.erase(0, 1);
	return quoted;
}

/// @brief 引用符付きの JSON 文字列リテラルにする
[[nodiscard]] inline std::string jsonQuoted(std::string_view input)
{
	return nlohmann::json(input).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace mitiru::observe
