#pragma once

/// @file BootReport.hpp
/// @brief stub と host が共有する、起動診断の読み書きと整形。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/platform/ExitNotice.hpp>
#include <mitiru/platform/SystemInfoText.hpp>
#include <mitiru/platform/Utf8Args.hpp>

namespace mitiru::platform::bootreport
{

inline constexpr const wchar_t* kFileName = L"boot_report.txt";
inline constexpr const wchar_t* kDirEnv = L"MITIRU_BOOT_REPORT_DIR";

[[nodiscard]] inline bool canWriteIn(const std::filesystem::path& dir) noexcept { return exitnotice::canWriteIn(dir); }

[[nodiscard]] inline std::filesystem::path pickWritableDir(
	const std::vector<std::filesystem::path>& candidates) noexcept
{
	for (const std::filesystem::path& dir : candidates)
	{
		if (canWriteIn(dir)) { return dir; }
	}
	return {};
}

[[nodiscard]] inline std::vector<std::filesystem::path> candidateDirs(
	const std::filesystem::path& besideExe, std::string_view gameName)
{
	std::vector<std::filesystem::path> out;
	out.push_back(besideExe);
	out.push_back(exitnotice::dirFor(gameName));
	std::error_code ec;
	const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
	if (!ec) { out.push_back(temp / L"MitiruGames" / utf8ToWide(gameName)); }
	return out;
}

[[nodiscard]] inline std::wstring dirFromEnv()
{
	const DWORD need = GetEnvironmentVariableW(kDirEnv, nullptr, 0);
	if (need == 0) { return {}; }
	std::wstring value(need, L'\0');
	const DWORD got = GetEnvironmentVariableW(kDirEnv, value.data(), need);
	if (got == 0 || got >= need) { return {}; }
	value.resize(got);
	return value;
}

[[nodiscard]] inline std::string readText(
	const std::filesystem::path& file, std::size_t maxBytes) noexcept
{
	try
	{
		if (maxBytes == 0) { return {}; }
		const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ,
		                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) { return {}; }
		const DWORD amount = static_cast<DWORD>(
			std::min<std::size_t>(maxBytes, std::numeric_limits<DWORD>::max()));
		std::string text(amount, '\0');
		DWORD got = 0;
		const BOOL ok = ReadFile(h, text.data(), amount, &got, nullptr);
		CloseHandle(h);
		if (!ok) { return {}; }
		text.resize(got);
		return text;
	}
	catch (...) {}
	return {};
}

[[nodiscard]] inline std::string tailLines(
	const std::filesystem::path& file, std::size_t maxLines, std::size_t maxBytes) noexcept
{
	try
	{
		if (maxLines == 0 || maxBytes == 0) { return {}; }
		const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ,
		                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) { return {}; }
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(h, &size) || size.QuadPart < 0)
		{
			CloseHandle(h);
			return {};
		}
		const std::size_t amount = static_cast<std::size_t>(
			std::min<unsigned long long>(static_cast<unsigned long long>(size.QuadPart),
			                             std::min<std::size_t>(maxBytes, std::numeric_limits<DWORD>::max())));
		const LONGLONG offset = size.QuadPart - static_cast<LONGLONG>(amount);
		LARGE_INTEGER position{};
		position.QuadPart = offset;
		if (!SetFilePointerEx(h, position, nullptr, FILE_BEGIN))
		{
			CloseHandle(h);
			return {};
		}
		std::string text(amount, '\0');
		DWORD got = 0;
		const BOOL ok = ReadFile(h, text.data(), static_cast<DWORD>(amount), &got, nullptr);
		CloseHandle(h);
		if (!ok) { return {}; }
		text.resize(got);
		if (offset > 0)
		{
			const std::size_t firstEnd = text.find('\n');
			text.erase(0, firstEnd == std::string::npos ? text.size() : firstEnd + 1);
		}
		std::vector<std::string> lines;
		std::size_t from = 0;
		while (from <= text.size())
		{
			const std::size_t end = text.find('\n', from);
			std::string line = text.substr(from, end == std::string::npos ? std::string::npos : end - from);
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			if (!line.empty()) { lines.push_back(std::move(line)); }
			if (end == std::string::npos) { break; }
			from = end + 1;
		}
		const std::size_t begin = lines.size() > maxLines ? lines.size() - maxLines : 0;
		std::string out;
		for (std::size_t i = begin; i < lines.size(); ++i)
		{
			if (!out.empty()) { out += '\n'; }
			out += lines[i];
		}
		return out;
	}
	catch (...) {}
	return {};
}

struct Input
{
	std::string game;
	bool failed = false;
	bool hasExitCode = false;
	unsigned long exitCode = 0;
	std::string reason;
	std::string os;
	std::string cpu;
	std::string timeText;
	std::string gpuReport;
	std::string logTail;
};

[[nodiscard]] inline std::string compose(const Input& in)
{
	char exitCode[32] = {};
	if (in.hasExitCode) { std::snprintf(exitCode, sizeof(exitCode), "終了コード: 0x%08lX\n", in.exitCode); }
	return "MitiruEngine 起動の診断\n"
	       "このファイルをそのまま送ってください。\n\n"
	       "ゲーム: " + in.game + "\n"
	       "日時: " + in.timeText + "\n"
	       "結果: " + (in.failed ? "起動できなかった、または途中で終わった" : "起動できた (注意あり)") + "\n"
	       + exitCode +
	       "OS: " + in.os + "\n"
	       "CPU の命令: " + in.cpu + "\n\n"
	       "理由:\n" + in.reason + "\n\n"
	       "--- GPU の診断 ---\n"
	       + (in.gpuReport.empty() ? "(GPU の診断は残っていない。起動の前に止まった可能性がある)" : in.gpuReport) + "\n\n"
	       "--- ログの末尾 ---\n"
	       + (in.logTail.empty() ? "(ログは無い)" : in.logTail) + "\n";
}

[[nodiscard]] inline std::string nowText()
{
	SYSTEMTIME t{};
	GetLocalTime(&t);
	char text[32];
	std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u",
	              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return text;
}

inline bool write(const std::filesystem::path& dir, const std::string& text) noexcept
{
	try
	{
		if (dir.empty()) { return false; }
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (ec) { return false; }
		return exitnotice::writeRaw((dir / kFileName).c_str(), text.data(), text.size(), false);
	}
	catch (...) {}
	return false;
}

inline void clear(const std::filesystem::path& dir) noexcept
{
	try
	{
		if (dir.empty()) { return; }
		std::error_code ec;
		std::filesystem::remove(dir / kFileName, ec);
	}
	catch (...) {}
}

/// @brief host が先に整形を済ませたファイルか。stub が二重に包まないために見分ける
[[nodiscard]] inline bool isComposed(const std::string& text)
{
	return text.rfind("MitiruEngine 起動の診断", 0) == 0;
}

[[nodiscard]] inline bool needsAttention(const std::string& text)
{
	return text.find("WARP") != std::string::npos
	       || text.find("結果の訂正") != std::string::npos
	       || text.find("検査で落ちた") != std::string::npos
	       || text.find("検査の子プロセスが落ちた") != std::string::npos
	       || text.find("検査が時間内に終わらなかった") != std::string::npos;
}

}  // namespace mitiru::platform::bootreport

#endif  // _WIN32
