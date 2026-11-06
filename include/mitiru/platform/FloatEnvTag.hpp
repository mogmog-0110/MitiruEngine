/// @file FloatEnvTag.hpp
/// @brief 数学関数の実行環境を `.mtrr` の envTag に入れる文字列と、録画と今の差の 1 行。
///
/// OS の UCRT は CPU の命令セットで sin / cos / exp などの実装を実行時に選ぶので、別の PC や
/// Windows のバージョンで結果が変わりうる。環境が違う録画を再生してずれたとき、原因の候補を
/// 録画の側から読めるように、使った経路を envTag の末尾へ残す。
#pragma once

#include <string>
#include <string_view>

namespace mitiru::platform
{

struct FloatEnv
{
	bool        cpuFma3 = false;  ///< CPU が FMA3 を持つ
	bool        fma3InUse = false;  ///< UCRT が FMA3 の経路を使っている
	std::string ucrt;             ///< ucrtbase.dll の製品バージョン。調べられなければ空
};

/// @brief envTag に足す部分。例 "fma3=cpu1,use0|ucrt=10.0.26100.4768"
[[nodiscard]] inline std::string floatEnvTagPart(const FloatEnv& e)
{
	std::string s = "fma3=cpu";
	s += e.cpuFma3 ? '1' : '0';
	s += ",use";
	s += e.fma3InUse ? '1' : '0';
	if (!e.ucrt.empty()) { s += "|ucrt=" + e.ucrt; }
	return s;
}

namespace detail
{
/// "|key=" の次から次の '|' までを返す。無ければ空
[[nodiscard]] inline std::string tagField(std::string_view tag, std::string_view key)
{
	const std::string needle = "|" + std::string(key) + "=";
	const std::size_t at = tag.find(needle);
	if (at == std::string_view::npos) { return {}; }
	const std::size_t from = at + needle.size();
	const std::size_t to = tag.find('|', from);
	return std::string(tag.substr(from, to == std::string_view::npos ? tag.size() - from : to - from));
}
}  // namespace detail

/// @brief 録画の envTag と今の環境の違いを 1 行で返す。録画に数学関数の記録が無い (古い録画) か、
///        違いが無ければ空。ずれるかどうかは言わず、事実だけを書く
[[nodiscard]] inline std::string floatEnvDifference(std::string_view recordedTag, const FloatEnv& now)
{
	const std::string recFma = detail::tagField(recordedTag, "fma3");
	if (recFma.empty()) { return {}; }
	const std::string recUcrt = detail::tagField(recordedTag, "ucrt");
	const std::string nowTag = "|" + floatEnvTagPart(now);
	const std::string nowFma = detail::tagField(nowTag, "fma3");
	const std::string nowUcrt = detail::tagField(nowTag, "ucrt");

	std::string out;
	if (recFma != nowFma) { out += "FMA3 が録画は " + recFma + "、今は " + nowFma; }
	if (recUcrt != nowUcrt)
	{
		if (!out.empty()) { out += "、"; }
		out += "UCRT が録画は " + (recUcrt.empty() ? std::string("不明") : recUcrt)
		     + "、今は " + (nowUcrt.empty() ? std::string("不明") : nowUcrt);
	}
	if (out.empty()) { return {}; }
	return "録画した環境と数学関数の実行環境が違います (" + out + ")";
}

}  // namespace mitiru::platform
