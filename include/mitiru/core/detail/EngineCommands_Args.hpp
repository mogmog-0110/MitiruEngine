#pragma once

/// @file EngineCommands_Args.hpp
/// @brief エンジンコマンド用 CommandArg 取得ヘルパー (EngineCommands.hpp から分割)

#include <string>
#include <vector>

#include <mitiru/core/CommandSystem.hpp>

namespace mitiru
{

namespace detail
{

/// @brief CommandArgからstringを取得する（デフォルト値付き）
[[nodiscard]] inline std::string argString(
	const std::vector<CommandArg>& args, std::size_t idx,
	const std::string& defaultVal = {})
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		return *s;
	}
	return defaultVal;
}

/// @brief CommandArgからintを取得する（デフォルト値付き）
[[nodiscard]] inline int argInt(
	const std::vector<CommandArg>& args, std::size_t idx,
	int defaultVal = 0)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return *v;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		try { return std::stoi(*s); }
		catch (...) { return defaultVal; }
	}
	return defaultVal;
}

/// @brief CommandArgからfloatを取得する（デフォルト値付き）
[[nodiscard]] inline float argFloat(
	const std::vector<CommandArg>& args, std::size_t idx,
	float defaultVal = 0.0f)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<float>(&args[idx]))
	{
		return *v;
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return static_cast<float>(*v);
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		try { return std::stof(*s); }
		catch (...) { return defaultVal; }
	}
	return defaultVal;
}

/// @brief CommandArgからboolを取得する（デフォルト値付き）
[[nodiscard]] inline bool argBool(
	const std::vector<CommandArg>& args, std::size_t idx,
	bool defaultVal = false)
{
	if (idx >= args.size())
	{
		return defaultVal;
	}
	if (auto* v = std::get_if<bool>(&args[idx]))
	{
		return *v;
	}
	if (auto* s = std::get_if<std::string>(&args[idx]))
	{
		return (*s == "true" || *s == "1" || *s == "on" || *s == "yes");
	}
	if (auto* v = std::get_if<int>(&args[idx]))
	{
		return *v != 0;
	}
	return defaultVal;
}

} // namespace detail

} // namespace mitiru
