#pragma once

/// @file CrashReport.hpp
/// @brief クラッシュ報告の宣言。
/// @details ModuleFault、game DLL、フレーム番号、リプレイ、入力台本の位置を 1 ファイルに書く。通常はローカルだけに残し、MITIRU_WITH_CRASH_REPORT で DSN を指定したときだけ送信する。置き場所は docs/CRASH_REPORTS.md を参照。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/ModuleFaultGuard.hpp>

#ifndef MITIRU_ENGINE_VERSION
#define MITIRU_ENGINE_VERSION "unknown"
#endif

namespace mitiru::debug
{

/// @brief game の異常終了を示す host の終了コード。4 と 5 は GPU 喪失用として Engine_Run.hpp で使う。
inline constexpr int kExitGameFaulted = 6;

/// @brief 例外処理中にも読めるよう、固定長の値と atomic で落ちたときの状況を持つ。
struct CrashContext
{
	char gameDll[1024]         = {};
	char replayPath[1024]      = {};
	char inputScriptPath[1024] = {};
	char gameName[256]         = {};  ///< 出荷するゲームの名前 (ship.json か --game-name)。空なら開発中
	std::atomic<std::uint64_t> frame{0};
	std::atomic<std::int64_t>  replayRecord{-1};  ///< リプレイで次に読む記録番号 (-1=リプレイ無し)
};

/// @brief プロセスごとに 1 つだけ持つ。engine がフレーム番号と DLL、host がリプレイと入力台本を書く。
[[nodiscard]] inline CrashContext& crashContext() noexcept
{
	static CrashContext ctx;
	return ctx;
}

/// @brief Windows の ANSI 変換を避け、UTF-8 文字列から日本語を保った path を作る。
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view s)
{
	return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p)
{
	const std::u8string u = p.u8string();
	return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

template <std::size_t N>
inline void setCrashContextText(char (&dst)[N], std::string_view text) noexcept
{
	const std::size_t n = text.size() < N - 1 ? text.size() : N - 1;
	for (std::size_t i = 0; i < n; ++i) { dst[i] = text[i]; }
	dst[n] = '\0';
}

/// @brief 保存先は MITIRU_CRASH_DIR、%LOCALAPPDATA% の下、一時フォルダの順で選ぶ。
/// @details %LOCALAPPDATA% の下は、ゲーム名があれば MitiruGames/<ゲーム>/crashes (last_run.log の隣)、
///          無ければ MitiruEngine/crashes。書き込めない場合や配布フォルダを汚す場合があるため、exe の隣には保存しない。
[[nodiscard]] inline std::filesystem::path crashDirectory()
{
	if (const char* env = std::getenv("MITIRU_CRASH_DIR"); env != nullptr && env[0] != '\0')
	{
		return pathFromUtf8(env);
	}
	const char* game = crashContext().gameName;
	const auto under = [game](const std::filesystem::path& root) {
		return game[0] != '\0' ? root / "MitiruGames" / pathFromUtf8(game) / "crashes"
		                        : root / "MitiruEngine" / "crashes";
	};
#ifdef _WIN32
	if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local != nullptr && local[0] != L'\0')
	{
		return under(std::filesystem::path(local));
	}
#endif
	std::error_code ec;
	const auto tmp = std::filesystem::temp_directory_path(ec);
	return under(ec ? std::filesystem::path(".") : tmp);
}

/// @brief "ACCESS_VIOLATION (0xC0000005) in game.dll+0x1a2b (on_update)" 形式の 1 行にまとめる。
[[nodiscard]] inline std::string summarizeFault(const module::ModuleFault& f)
{
	char buf[512];
	std::snprintf(buf, sizeof(buf), "%s (0x%08X) in %s+0x%llx (%s)",
	              module::describeFaultCode(f.code), static_cast<unsigned>(f.code),
	              f.moduleName[0] != '\0' ? f.moduleName : "?",
	              static_cast<unsigned long long>(f.moduleOffset),
	              (f.callback != nullptr && f.callback[0] != '\0') ? f.callback : "host");
	return buf;
}

namespace detail
{

inline std::string accessLine(const module::ModuleFault& f)
{
	if (f.accessKind < 0) { return {}; }
	const char* kind = f.accessKind == 1 ? "write" : (f.accessKind == 8 ? "execute" : "read");
	char buf[96];
	std::snprintf(buf, sizeof(buf), "access: %s at 0x%llx\n", kind,
	              static_cast<unsigned long long>(f.accessAddress));
	return buf;
}

inline std::string positionLine(const char* label, const char* path, long long pos, const char* unit)
{
	if (path == nullptr || path[0] == '\0') { return std::string(label) + ": -\n"; }
	return std::string(label) + ": " + path + " @ " + unit + " " + std::to_string(pos) + "\n";
}

inline std::string localTimeText()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	return buf;
}

}  // namespace detail

/// @brief grep しやすいよう、1 行 1 項目の "key: value" 形式で出力する。
[[nodiscard]] inline std::string formatCrashReport(const module::ModuleFault& f, const CrashContext& ctx,
                                                   std::string_view action = {})
{
	const auto frame = static_cast<long long>(ctx.frame.load(std::memory_order_relaxed));
	const bool inGame = f.callback != nullptr && f.callback[0] != '\0';
	char abi[64];
	std::snprintf(abi, sizeof(abi), "v%u (wire 0x%08X)",
	              static_cast<unsigned>(module::kCurrentApiVersion),
	              static_cast<unsigned>(module::kWireApiVersion));

	std::string s = "MitiruEngine crash report\n";
	s += "time: " + detail::localTimeText() + "\n";
	s += std::string("kind: ") + (inGame ? "game callback fault (host kept running)" : "host crash") + "\n";
	if (!action.empty()) { s += "action: " + std::string(action) + "\n"; }
	s += "fault: " + summarizeFault(f) + "\n";
	s += detail::accessLine(f);
	s += std::string("game dll: ") + (ctx.gameDll[0] != '\0' ? ctx.gameDll : "-") + "\n";
	s += std::string("engine: ") + MITIRU_ENGINE_VERSION + "\n";
	s += std::string("abi: ") + abi + "\n";
	s += "frame: " + std::to_string(frame) + "\n";
	s += detail::positionLine("replay", ctx.replayPath, ctx.replayRecord.load(std::memory_order_relaxed), "record");
	s += detail::positionLine("input script", ctx.inputScriptPath, frame, "frame");
	s += std::string("minidump: ") + (f.dumpPath[0] != '\0' ? f.dumpPath : "-") + "\n";
	return s;
}

/// @brief minidump があれば、その隣に同じ名前の .txt として報告を書く。
/// @return 書いたファイルの path。書けなければ空の path を返し、理由を stderr に出す。
inline std::filesystem::path writeCrashReport(const module::ModuleFault& f, const CrashContext& ctx,
                                              std::string_view action = {})
{
	std::filesystem::path path;
	if (f.dumpPath[0] != '\0')
	{
		path = pathFromUtf8(f.dumpPath).replace_extension(".txt");
	}
	else
	{
		static std::atomic<int> sequence{0};
		const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		path = crashDirectory() / ("crash-" + std::to_string(stamp) + "-" + std::to_string(++sequence) + ".txt");
	}
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary);
	out << formatCrashReport(f, ctx, action);
	if (!out)
	{
		console::noticef("クラッシュの報告を %s に書くのに失敗しました。MITIRU_CRASH_DIR で書き込める置き場を指定してください。",
			pathToUtf8(path).c_str());
		return {};
	}
	return path;
}

}  // namespace mitiru::debug
