#pragma once

/// @file ContrastReport.hpp
/// @brief コントラストの検査結果を JSON と一覧の文字にする。
/// @details 通った数だけを見せないよう、unknown (下地が透けて測れない) を別に数え、近似であることを必ず添える。

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/ui_rml/ContrastCheck.hpp>

namespace mitiru::ui_rml
{

inline constexpr const char* kContrastCaveat =
	"文字と下地の色から計算した値です。下地がゲームの絵に透ける文字は unknown で、実際の絵の上で見て確かめてください。"
	"色覚の列は Machado (2009) の再現による近似で、見分けられる保証ではありません。";

[[nodiscard]] inline std::string hexOf(const Rgba& c)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", static_cast<int>(c.r * 255.0f + 0.5f),
	              static_cast<int>(c.g * 255.0f + 0.5f), static_cast<int>(c.b * 255.0f + 0.5f),
	              static_cast<int>(c.a * 255.0f + 0.5f));
	return buf;
}

[[nodiscard]] inline std::string contrastReportJson(const std::vector<ContrastFinding>& findings, bool uiActive)
{
	nlohmann::json root;
	root["caveat"] = kContrastCaveat;
	root["ui"] = uiActive;
	root["summary"] = { { "total", findings.size() },
	                    { "pass", countVerdict(findings, ContrastVerdict::Pass) },
	                    { "fail", countVerdict(findings, ContrastVerdict::Fail) },
	                    { "unknown", countVerdict(findings, ContrastVerdict::Unknown) } };
	nlohmann::json list = nlohmann::json::array();
	for (const ContrastFinding& f : findings)
	{
		nlohmann::json j;
		j["where"] = f.where;
		j["text"] = f.text;
		j["verdict"] = contrastVerdictName(f.result.verdict);
		j["ratio"] = std::round(f.result.ratio * 100.0f) / 100.0f;
		j["required"] = f.result.required;
		j["color"] = hexOf(f.color);
		j["background"] = hexOf(f.background);
		j["fontPx"] = f.fontPx;
		j["large"] = f.large;
		if (!f.uncertain.empty()) { j["uncertain"] = f.uncertain; }
		if (f.underColorVision[0] > 0.0f)
		{
			j["underColorVision"] = { { "protan", std::round(f.underColorVision[0] * 100.0f) / 100.0f },
			                          { "deutan", std::round(f.underColorVision[1] * 100.0f) / 100.0f },
			                          { "tritan", std::round(f.underColorVision[2] * 100.0f) / 100.0f } };
		}
		list.push_back(std::move(j));
	}
	root["findings"] = std::move(list);
	return root.dump(2);
}

}  // namespace mitiru::ui_rml
