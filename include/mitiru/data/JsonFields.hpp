#pragma once

/// @file JsonFields.hpp
/// @brief nlohmann::json でデータファイルを読み書きするときの小物

#include <charconv>
#include <string>
#include <system_error>
#include <type_traits>

namespace mitiru::data
{

/// @brief float を json に入れるときの値
/// @details そのまま入れると double へ広げた値で書き出され、0.8f が 0.800000011920929 になる。
///          float として最短の 10 進表記を経由して double にすると "0.8" のまま書ける。読み戻した
///          double を float にすれば元の値に戻る。
[[nodiscard]] inline double jsonFloat(float v) noexcept
{
	char buf[32];
	const auto written = std::to_chars(buf, buf + sizeof(buf), v);
	double out = static_cast<double>(v);
	if (written.ec == std::errc{}) { std::from_chars(buf, written.ptr, out); }
	return out;
}

/// @brief オブジェクトのキーを型を確かめて読む。無い・型が違うときは fallback。
/// @details nlohmann の value() は型違いで例外を投げるので、手で書かれたデータファイルには使わない。
template <typename T, typename Json>
[[nodiscard]] T fieldOr(const Json& obj, const char* key, T fallback)
{
	if (!obj.is_object()) { return fallback; }
	const auto it = obj.find(key);
	if (it == obj.end()) { return fallback; }
	if constexpr (std::is_same_v<T, bool>)
	{
		return it->is_boolean() ? it->template get<bool>() : fallback;
	}
	else if constexpr (std::is_arithmetic_v<T>)
	{
		return it->is_number() ? it->template get<T>() : fallback;
	}
	else
	{
		return it->is_string() ? it->template get<T>() : fallback;
	}
}

}  // namespace mitiru::data
