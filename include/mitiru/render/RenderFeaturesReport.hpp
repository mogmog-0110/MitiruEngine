#pragma once

/// @file RenderFeaturesReport.hpp
/// @brief 機能の表を、`mitiru_host --render-features` の表と JSON、gpu_report.txt の節へ書き出す (ADR 0090)。
/// @details 表の文字列 (note ほか) をそのまま読む。言い回しを持つのは RenderFeatures.hpp の表だけにする。docs/3D_RENDERING.md の表は tools/gen_render_features_doc.py が同じ表から作る。

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <mitiru/render/RenderFeatures.hpp>

namespace mitiru::render::features
{

namespace report_detail
{

inline void appendJsonString(std::string& out, std::string_view text)
{
	out += '"';
	for (const char c : text)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) { out += ' '; } else { out += c; }
		}
	}
	out += '"';
}

inline void appendJsonList(std::string& out, std::string_view csv)
{
	out += '[';
	bool first = true;
	forEachWord(csv, [&](std::string_view w) {
		if (!first) { out += ','; }
		first = false;
		appendJsonString(out, w);
	});
	out += ']';
}

inline void appendCapKeys(std::string& out, std::uint32_t caps)
{
	out += '[';
	bool first = true;
	for (const gfx::compat::CapInfo& c : gfx::compat::kCapTable)
	{
		if ((caps & capBit(c.id)) == 0) { continue; }
		if (!first) { out += ','; }
		first = false;
		appendJsonString(out, c.key);
	}
	out += ']';
}

[[nodiscard]] inline std::string capKeysText(std::uint32_t caps)
{
	std::string out;
	for (const gfx::compat::CapInfo& c : gfx::compat::kCapTable)
	{
		if ((caps & capBit(c.id)) == 0) { continue; }
		if (!out.empty()) { out += ','; }
		out += c.key;
	}
	return out;
}

[[nodiscard]] inline const char* buildName(BuildFlag b) noexcept
{
	switch (b)
	{
	case BuildFlag::Effekseer: return "effekseer";
	case BuildFlag::Onnx: return "onnx";
	case BuildFlag::DirectMl: return "directml";
	case BuildFlag::Fsr3: return "fsr3";
	default: return "none";
	}
}

/// 宣言が作る時期を決めている段。決めていない宣言の行 (今は宣言しても作る時期が変わらない) は "-"、基本の行は空
[[nodiscard]] inline const char* creationGatedText(Feature f) noexcept
{
	const bool dx12 = creationGated(f, Tier::Dx12);
	const bool dawn = creationGated(f, Tier::DawnCore);
	if (dx12 && dawn) { return "DX12,Dawn"; }
	if (dx12) { return "DX12"; }
	if (dawn) { return "Dawn"; }
	return row(f).region == Region::Declared ? "-" : "";
}

[[nodiscard]] inline const char* regionName(Region r) noexcept { return r == Region::Declared ? "declared" : "core"; }

[[nodiscard]] inline const char* undeclaredName(Undeclared u) noexcept
{
	switch (u)
	{
	case Undeclared::Fallback: return "fallback";
	case Undeclared::Skip: return "skip";
	default: return "none";
	}
}

/// 端末での幅。ASCII 以外は 2 桁と数える
[[nodiscard]] inline std::size_t displayWidth(std::string_view text) noexcept
{
	std::size_t w = 0;
	for (const char c : text)
	{
		const auto u = static_cast<unsigned char>(c);
		if (u < 0x80) { ++w; }
		else if ((u & 0xC0) == 0xC0) { w += 2; }
	}
	return w;
}

inline void appendPadded(std::string& out, std::string_view text, std::size_t width)
{
	out += text;
	const std::size_t w = displayWidth(text);
	if (w < width) { out.append(width - w, ' '); }
}

}  // namespace report_detail

/// @brief `--render-features --json` の本文。CLI が needs の名前を調べるのにも使う
[[nodiscard]] inline std::string renderFeaturesJson()
{
	using namespace report_detail;
	std::string out = "{\"version\":1,\"tiers\":[";
	for (std::size_t t = 0; t < kTierCount; ++t)
	{
		if (t > 0) { out += ','; }
		appendJsonString(out, tierJsonName(static_cast<Tier>(t)));
	}
	out += "],\"features\":[\n";
	bool firstRow = true;
	for (const FeatureRow& r : kFeatureTable)
	{
		if (!firstRow) { out += ",\n"; }
		firstRow = false;
		out += "{\"key\":";
		appendJsonString(out, r.key);
		out += ",\"label\":";
		appendJsonString(out, r.label);
		out += ",\"region\":";
		appendJsonString(out, regionName(r.region));
		out += ",\"when_undeclared\":";
		appendJsonString(out, undeclaredName(r.whenUndeclared));
		out += ",\"declaration_creates_on\":[";
		bool firstTier = true;
		for (std::size_t t = 0; t < kTierCount; ++t)
		{
			if (!creationGated(r.id, static_cast<Tier>(t))) { continue; }
			if (!firstTier) { out += ','; }
			firstTier = false;
			appendJsonString(out, tierJsonName(static_cast<Tier>(t)));
		}
		out += ']';
		out += ",\"support\":{";
		for (std::size_t t = 0; t < kTierCount; ++t)
		{
			if (t > 0) { out += ','; }
			appendJsonString(out, tierJsonName(static_cast<Tier>(t)));
			out += ':';
			appendJsonString(out, supportJsonName(r.support[t]));
		}
		out += "},\"note\":";
		appendJsonString(out, r.note);
		out += ",\"gpu_caps\":";
		appendCapKeys(out, r.gpuCaps);
		out += ",\"build\":";
		appendJsonString(out, buildName(r.build));
		out += ",\"build_present\":";
		out += buildHas(r.build) ? "true" : "false";
		out += ",\"implies\":";
		appendJsonList(out, r.implies);
		out += ",\"entries\":";
		appendJsonList(out, r.entries);
		out += ",\"screen_entries\":";
		appendJsonList(out, r.screenEntries);
		out += '}';
	}
	out += "\n]}\n";
	return out;
}

[[nodiscard]] inline const char* stateText(FeatureState s) noexcept
{
	switch (s)
	{
	case FeatureState::Active: return "作る";
	case FeatureState::Undeclared: return "未宣言";
	case FeatureState::NoBuild: return "ビルドに無い";
	case FeatureState::NoGpu: return "GPU に無い";
	case FeatureState::Unsupported: return "非対応";
	case FeatureState::QualityCapped: return "画質の上限";
	}
	return "?";
}

/// @brief `--render-features` の表。tier の欄には、その段での状態を付ける (GPU の機能は問い合わせないので NoGpu は出ない)
[[nodiscard]] inline std::string renderFeaturesText(Tier tier)
{
	using namespace report_detail;
	std::string out = "3D 描画の機能 (";
	out += std::to_string(kFeatureCount);
	out += " 行)。状態の欄は ";
	out += tierName(tier);
	out += " の場合。GPU の機能は問い合わせていない\n";
	appendPadded(out, "key", 20);
	appendPadded(out, "区分", 6);
	appendPadded(out, "宣言で作る", 12);
	appendPadded(out, "DX12", 8);
	appendPadded(out, "Dawn", 8);
	appendPadded(out, "互換", 8);
	appendPadded(out, "状態", 14);
	out += "備考\n";
	StateContext ctx;
	ctx.tier = tier;
	for (const FeatureRow& r : kFeatureTable)
	{
		appendPadded(out, r.key, 20);
		appendPadded(out, r.region == Region::Declared ? "宣言" : "基本", 6);
		appendPadded(out, creationGatedText(r.id), 12);
		for (std::size_t t = 0; t < kTierCount; ++t) { appendPadded(out, supportLabel(r.support[t]), 8); }
		appendPadded(out, stateText(resolveState(r.id, ctx)), 14);
		out += r.note;
		out += '\n';
	}
	return out;
}

/// @brief gpu_report.txt の節。段が描けない行を全部書き、ゲームが頼んだ行には印を付ける。送ってもらう 1 ファイルで欠けが分かるようにする
[[nodiscard]] inline std::string gpuReportSection(Tier tier, const std::array<bool, kFeatureCount>& requested,
												const RenderNeeds& needs = {})
{
	std::string out = "\n3D 描画の機能 (";
	out += tierName(tier);
	out += ")\n";
	std::size_t gaps = 0;
	for (const FeatureRow& r : kFeatureTable)
	{
		const Support s = r.support[static_cast<std::size_t>(tier)];
		const bool asked = requested[static_cast<std::size_t>(r.id)];
		const bool undeclared = needs.declaredMode && creationGated(r.id, tier) && !needs.declared.test(static_cast<std::size_t>(r.id));
		if (s == Support::Full && !asked) { continue; }
		++gaps;
		out += "  ";
		out += r.key;
		out += ": ";
		out += undeclared ? "未宣言" : supportLabel(s);
		if (asked) { out += " (ゲームが頼んだ)"; }
		if (undeclared)
		{
			// 表の備考は段が描けない理由なので、未宣言の行には直し方を書く
			out += " - [render] needs に足すと作る";
		}
		else if (!std::string_view{r.note}.empty())
		{
			out += " - ";
			out += r.note;
		}
		out += '\n';
	}
	if (gaps == 0) { out += "  全部の機能に対応している\n"; }
	return out;
}

}  // namespace mitiru::render::features
