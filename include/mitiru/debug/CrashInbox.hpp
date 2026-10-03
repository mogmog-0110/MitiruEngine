#pragma once

/// @file CrashInbox.hpp
/// @brief 前の実行が残したクラッシュ報告を次の起動で拾い、利用者へ知らせるか、作者へ送るかを決める。
/// @details 報告 (.txt) の隣に .seen が無いものを「まだ扱っていない」とみなす。送る先 (ship.json の
///          crashReportUrl) が無い時と、利用者が断った時は、置き場を知らせるだけで何も送らない。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <mitiru/settings/UserSettings.hpp>

namespace mitiru::debug
{

struct PendingCrash
{
	std::filesystem::path report;  ///< 報告 (.txt)
	std::filesystem::path dump;    ///< minidump (.dmp)。書けなかった時は空
	std::filesystem::path log;     ///< 落ちた実行の last_run.log の写し (.log)。無ければ空
};

[[nodiscard]] inline std::filesystem::path crashSeenMarker(const PendingCrash& c)
{
	return std::filesystem::path(c.report).replace_extension(".seen");
}

/// @brief dir にある、まだ扱っていない報告を名前の順 (= 落ちた時刻の順) に返す。
[[nodiscard]] inline std::vector<PendingCrash> pendingCrashes(const std::filesystem::path& dir)
{
	std::vector<PendingCrash> out;
	std::error_code ec;
	for (const auto& e : std::filesystem::directory_iterator(dir, ec))
	{
		if (!e.is_regular_file(ec) || e.path().extension() != ".txt") { continue; }
		PendingCrash c{ e.path(), {}, {} };
		if (std::filesystem::exists(crashSeenMarker(c), ec)) { continue; }
		for (const auto& [ext, slot] : { std::pair{ ".dmp", &c.dump }, std::pair{ ".log", &c.log } })
		{
			const auto p = std::filesystem::path(c.report).replace_extension(ext);
			if (std::filesystem::exists(p, ec)) { *slot = p; }
		}
		out.push_back(std::move(c));
	}
	std::sort(out.begin(), out.end(), [](const PendingCrash& a, const PendingCrash& b) { return a.report < b.report; });
	return out;
}

/// @brief 扱い終えた印を付ける。outcome は "sent" か "kept" (.seen の中身。人が読むだけ)。
inline bool markCrashSeen(const PendingCrash& c, std::string_view outcome)
{
	std::ofstream out(crashSeenMarker(c), std::ios::binary | std::ios::trunc);
	out << outcome << "\n";
	return static_cast<bool>(out);
}

enum class CrashInboxStep : std::uint8_t
{
	Nothing,     ///< 扱っていない報告が無い
	AskConsent,  ///< 送ってよいかを利用者に聞く (1 回だけ。答えは settings.json の privacy.crashReports)
	ShowKept,    ///< 送らない。報告を残した場所を知らせる
	Upload,      ///< 利用者が許しているので送る
};

/// @brief 送ってよいと利用者が答えるまで、Upload にはならない。
[[nodiscard]] constexpr CrashInboxStep decideCrashStep(std::size_t pending, bool hasEndpoint,
                                                       settings::CrashReportConsent consent) noexcept
{
	if (pending == 0) { return CrashInboxStep::Nothing; }
	if (!hasEndpoint || consent == settings::CrashReportConsent::Never) { return CrashInboxStep::ShowKept; }
	if (consent == settings::CrashReportConsent::Ask) { return CrashInboxStep::AskConsent; }
	return CrashInboxStep::Upload;
}

}  // namespace mitiru::debug
