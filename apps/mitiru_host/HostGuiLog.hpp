#pragma once

/// @file HostGuiLog.hpp
/// @brief 配布物の host (GUI サブシステム) で、出力先がないときのログ保存と起動失敗の通知。
/// @details ダブルクリックで起動した GUI の exe には標準出力も標準エラーも無く、DLL やアセットが読めないという
///          知らせが消えて、遊ぶ人には何も出ないまま終わる。出力先がないときだけ
///          %LOCALAPPDATA%/MitiruGames/<ゲーム>/last_run.log へ切り替え、0 以外で終了したらログの末尾を
///          メッセージボックスに表示する。コンソール、ランチャーから引き継いだパイプ、CI が出力先にあるときは何もしない。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <mitiru/platform/Utf8Args.hpp>
#include <windows.h>
#endif

namespace mitiru::host
{

class HostGuiLog
{
public:
	/// @brief 出力先がないときだけログファイルへ切り替える。gameName はフォルダ名に使う
	void attachIfOrphaned(const std::string& gameName)
	{
#ifdef _WIN32
		if (hasOutput(STD_ERROR_HANDLE) || hasOutput(STD_OUTPUT_HANDLE)) { return; }
		const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
		std::error_code ec;
		const std::filesystem::path root = (base != nullptr) ? std::filesystem::path(base)
		                                                     : std::filesystem::temp_directory_path(ec);
		const std::filesystem::path dir = root / L"MitiruGames" / mitiru::platform::utf8ToWide(gameName);
		std::filesystem::create_directories(dir, ec);
		const std::filesystem::path path = dir / L"last_run.log";
		std::ofstream(path, std::ios::trunc).flush();  // 前回の分を消す。以後は両方とも追記で開く
		if (_wfreopen(path.c_str(), L"a", stderr) == nullptr) { return; }
		if (_wfreopen(path.c_str(), L"a", stdout) == nullptr) { return; }
		std::setvbuf(stderr, nullptr, _IONBF, 0);
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		m_path = path;
#else
		(void)gameName;
#endif
	}

	/// @brief ログファイルへ切り替えた実行が 0 以外で終了したときだけ、ログの末尾を表示する。headless では表示しない
	void reportExit(int code, bool headless) const
	{
#ifdef _WIN32
		if (m_path.empty() || code == 0 || headless) { return; }
		std::fflush(stderr);
		std::fflush(stdout);
		std::string text = "ゲームを続けられませんでした (終了コード " + std::to_string(code) + ")。\n\n";
		text += tail(12);
		text += "\nログの全体は " + mitiru::platform::wideToUtf8(m_path.wstring()) + " にあります。";
		MessageBoxW(nullptr, mitiru::platform::utf8ToWide(text).c_str(), L"MitiruEngine", MB_ICONERROR | MB_OK);
#else
		(void)code;
		(void)headless;
#endif
	}

	/// @brief 切り替えたログファイル。出力先があって切り替えていなければ空
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
	std::filesystem::path m_path;

#ifdef _WIN32
	[[nodiscard]] static bool hasOutput(DWORD which)
	{
		const HANDLE h = GetStdHandle(which);
		return h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
	}
#endif

	[[nodiscard]] std::string tail(std::size_t maxLines) const
	{
		std::ifstream in(m_path);
		std::vector<std::string> lines;
		for (std::string line; std::getline(in, line);)
		{
			if (!line.empty()) { lines.push_back(line); }
		}
		const std::size_t first = lines.size() > maxLines ? lines.size() - maxLines : 0;
		std::string out;
		for (std::size_t i = first; i < lines.size(); ++i) { out += lines[i] + "\n"; }
		return out;
	}
};

}  // namespace mitiru::host
