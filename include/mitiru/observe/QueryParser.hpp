#pragma once

/// @file QueryParser.hpp
/// @brief URLクエリ文字列パーサー
/// @details HTTP URLのクエリパラメータを解析するユーティリティ。
///          シンプルな分割処理で '?' → '&' → '=' の順に解析する。

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mitiru::observe
{

/// @brief URLクエリ文字列のパース結果型
using QueryParams = std::map<std::string, std::string>;

/// @brief URLからパス部分を抽出する
/// @param url リクエストURL（例: "/inspect?entity=42"）
/// @return パス部分（例: "/inspect"）
[[nodiscard]] inline std::string extractPath(std::string_view url)
{
	const auto pos = url.find('?');
	if (pos == std::string_view::npos)
	{
		return std::string(url);
	}
	return std::string(url.substr(0, pos));
}

/// @brief URLクエリ文字列を解析する
/// @param url リクエストURL全体（例: "/inspect?entity=42&tag=enemy"）
/// @return パラメータのキーバリューマップ
[[nodiscard]] inline QueryParams parseQuery(std::string_view url)
{
	QueryParams params;

	/// '?' 以降を取得する
	const auto qpos = url.find('?');
	if (qpos == std::string_view::npos)
	{
		return params;
	}

	auto query = url.substr(qpos + 1);

	/// '&' で分割してキーバリューペアを抽出する
	while (!query.empty())
	{
		std::string_view pair;
		const auto ampPos = query.find('&');
		if (ampPos == std::string_view::npos)
		{
			pair = query;
			query = {};
		}
		else
		{
			pair = query.substr(0, ampPos);
			query = query.substr(ampPos + 1);
		}

		if (pair.empty())
		{
			continue;
		}

		/// '=' で分割する
		const auto eqPos = pair.find('=');
		if (eqPos == std::string_view::npos)
		{
			/// 値なしのパラメータ
			params[std::string(pair)] = "";
		}
		else
		{
			auto key = pair.substr(0, eqPos);
			auto value = pair.substr(eqPos + 1);
			params[std::string(key)] = std::string(value);
		}
	}

	return params;
}

/// @brief パラメータマップから指定キーの値を取得する
/// @param params パラメータマップ
/// @param key 検索キー
/// @return 値が存在すればその文字列、なければ nullopt
[[nodiscard]] inline std::optional<std::string> getParam(
	const QueryParams& params, const std::string& key)
{
	const auto it = params.find(key);
	if (it != params.end())
	{
		return it->second;
	}
	return std::nullopt;
}

} // namespace mitiru::observe
