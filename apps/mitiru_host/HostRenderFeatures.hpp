#pragma once

/// @file HostRenderFeatures.hpp
/// @brief backend が描けなかった 3D の機能を、フレームの頭で集めて端末と gpu_report.txt に出す (ADR 0090)。
/// @details 数えるのは IRenderer3D / ISceneFx の入口 (RenderFeatureNote.hpp)。出すか、どの深さで出すかは decideFeatureNotice が決める。
///          最初のフレームで頼まれた物は 2 回目の呼び出しでまとめ、後から頼まれた物はその都度出す。

#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>

#include <mitiru/core/Env.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/gfx/GpuNotice.hpp>
#ifdef _WIN32
#include <mitiru/gfx/dx12/Dx12GpuProbe.hpp>
#endif
#include <mitiru/render/RenderFeaturesReport.hpp>

namespace mitiru::host
{

class RenderFeatureReporter
{
public:
	/// 端末に常に出す行の上限。残りは件数だけ出す
	static constexpr int kMaxNoticeLines = 5;

	/// MITIRU_FEATURE_CENSUS=<file> のとき、host が終わる時にゲームが頼んだ宣言の行の key を 1 行ずつ追記する。
	/// 場面ごとに host を起動して集める census (ADR 0090 の 8) が、ファイルを足していって集合を作る
	~RenderFeatureReporter()
	{
		const std::string path = env::value("MITIRU_FEATURE_CENSUS");
		if (path.empty()) { return; }
		std::ofstream out(path, std::ios::app);
		const auto used = render::features::usedFeatures();
		for (const render::features::FeatureRow& row : render::features::kFeatureTable)
		{
			if (row.region == render::features::Region::Declared && used[static_cast<std::size_t>(row.id)]) { out << row.key << '\n'; }
		}
	}

	/// @brief 毎フレームの頭に呼ぶ。1 回目は何もせず、最初のフレームの描画が終わった 2 回目から集める
	void onFrameStart()
	{
		if (++m_calls < 2) { return; }
		const std::optional<render::features::Tier> tier = render::features::activeTier();
		if (!tier) { return; }
		const std::array<bool, render::features::kFeatureCount> fresh = render::features::takeUnreportedNotes();
		bool any = false;
		for (const bool b : fresh) { any = any || b; }
		if (!any) { return; }
		for (std::size_t i = 0; i < fresh.size(); ++i) { m_requested[i] = m_requested[i] || fresh[i]; }
		report(*tier, fresh);
	}

private:
	void report(render::features::Tier tier, const std::array<bool, render::features::kFeatureCount>& fresh)
	{
		using namespace render::features;
		const RenderNeeds needs = activeNeeds();
		int hidden = 0;
		std::string lateLines;
		for (std::size_t i = 0; i < kFeatureCount; ++i)
		{
			if (!fresh[i]) { continue; }
			const Feature f = static_cast<Feature>(i);
			const Support support = supportAt(f, tier);
			StateContext ctx;
			ctx.tier = tier;
			ctx.declaredMode = needs.declaredMode;
			ctx.declared = needs.declared;
			const FeatureState state = resolveState(f, ctx);
			const NoticeDecision decision = decideFeatureNotice({state, support, gfx::compat::playerRunSlot()});
			const std::string text = featureNoticeText(f, state, tier);
			if (decision.console == NoticeLevel::Notice)
			{
				if (m_printed < kMaxNoticeLines) { console::notice(text); ++m_printed; }
				else { ++hidden; }
			}
			else if (decision.console == NoticeLevel::Detail)
			{
				console::verbose(text);
			}
			if (decision.report && m_sectionWritten) { lateLines += "  " + text + "\n"; }
		}
		if (hidden > 0) { console::noticef("ほか %d 件は mitiru_host --render-features で一覧を見られます", hidden); }
#ifdef _WIN32
		if (!m_sectionWritten) { gfx::compat::appendReport(gpuReportSection(tier, m_requested, needs)); }
		else if (!lateLines.empty()) { gfx::compat::appendReport(lateLines); }
#endif
		m_sectionWritten = true;
	}

	int m_calls = 0;
	int m_printed = 0;
	bool m_sectionWritten = false;
	std::array<bool, render::features::kFeatureCount> m_requested{};
};

}  // namespace mitiru::host
