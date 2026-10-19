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
#include <mitiru/platform/BootReport.hpp>
#include <mitiru/platform/ExitNotice.hpp>
#include <mitiru/platform/SystemInfoText.hpp>
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
		m_orphaned = true;
		m_game = gameName;
		const std::filesystem::path dir = mitiru::platform::exitnotice::dirFor(gameName);
		const std::filesystem::path path = dir / L"last_run.log";
		std::ofstream(path, std::ios::trunc).flush();  // 前回の分を消す。以後は両方とも追記で開く
		mitiru::platform::exitnotice::clear(dir);      // 前回の失敗の文を、今回のものと取り違えさせない
		// freopen は開けなくても元の出力を閉じる。閉じたままだと、後の書き込みが CRT の検査で abort するので、捨て先へつなぐ
		if (_wfreopen(path.c_str(), L"a", stderr) == nullptr || _wfreopen(path.c_str(), L"a", stdout) == nullptr)
		{
			discardOutput();
			return;
		}
		std::setvbuf(stderr, nullptr, _IONBF, 0);
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		m_path = path;
#else
		(void)gameName;
#endif
	}

	/// @brief ログファイルへ切り替えた実行が 0 以外で終了したときだけ、遊ぶ人へ知らせる。headless では窓を出さない
	/// @param summary 失敗の文 (利用者向けの日本語)。空ならログの末尾を見せる
	/// @details 見せた文は last_error.txt に `shown` で残す。host を起動した stub は、これを見て二重に窓を出さない。
	void reportExit(int code, bool headless, const std::string& summary = {}) const
	{
#ifdef _WIN32
		if ((m_path.empty() && !m_orphaned) || code == 0) { return; }
		std::fflush(stderr);
		std::fflush(stdout);
		std::string text;
		if (!summary.empty()) { text = summary + "\n\n"; }
		else { text = "ゲームを続けられませんでした (終了コード " + std::to_string(code) + ")。\n\n" + tail(12); }
		text += bootReportFooter(code, text);
		if (!m_path.empty()) { mitiru::platform::exitnotice::write(m_path.parent_path(), true, text); }
		if (headless) { return; }
		mitiru::platform::exitnotice::showBox(L"MitiruEngine", mitiru::platform::utf8ToWide(text));
#else
		(void)code;
		(void)headless;
		(void)summary;
#endif
	}

	/// @brief 切り替えたログファイル。出力先があって切り替えていなければ空
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
#ifdef _WIN32
	static void discardOutput()
	{
		(void)_wfreopen(L"NUL", L"w", stderr);
		(void)_wfreopen(L"NUL", L"w", stdout);
	}
#endif

	std::filesystem::path m_path;
	std::string m_game;
	bool m_orphaned = false;  ///< 出力先が無かった。ログを作れなくても、失敗の窓は出す

	/// 配布物では boot_report.txt を整えて書き、送ってもらう場所を示す。GPU の失敗は診断の場所を文の中に持つので、二度示さない
	[[nodiscard]] std::string bootReportFooter(int code, const std::string& shownText) const
	{
#ifdef _WIN32
		namespace br = mitiru::platform::bootreport;
		const std::wstring dirText = br::dirFromEnv();
		if (dirText.empty())
		{
			return m_path.empty() ? std::string() : "\nログの全体は " + mitiru::platform::wideToUtf8(m_path.wstring()) + " にあります。";
		}
		const std::filesystem::path dir(dirText);
		const std::string reportPath = mitiru::platform::pathToUtf8(dir / br::kFileName);
		if (shownText.find(reportPath) != std::string::npos) { return {}; }
		const std::string existing = br::readText(dir / br::kFileName, 64 * 1024);
		br::Input in;
		in.game = m_game;
		in.failed = true;
		in.hasExitCode = true;
		in.exitCode = static_cast<unsigned long>(code);
		in.reason = shownText;
		in.os = mitiru::platform::sysinfo::osVersionText();
		in.cpu = mitiru::platform::sysinfo::cpuFeatureText();
		in.timeText = br::nowText();
		in.gpuReport = br::isComposed(existing) ? std::string() : existing;
		in.logTail = br::tailLines(m_path, 40, 8 * 1024);
		if (!br::write(dir, br::compose(in))) { return "\n診断のファイルを作れませんでした。"; }
		return "\n動かないときは、次のファイルを送ってください。\n" + reportPath;
#else
		(void)code;
		(void)shownText;
		return {};
#endif
	}

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
