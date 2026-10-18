// mitiru_start。配布用の極小 GUI ランチャ stub。
//
// 目的: 配布フォルダのトップに <game>.exe として置き、ダブルクリックされたら
//   コンソール窓を一切出さずに data\mitiru_host.exe を起動する。CEF にも engine
//   本体にも link しない (Win32 のみ) ので、これ自体は libcef.dll を必要とせず
//   トップ階層に単独で置ける。実ランタイム一式 (host + DLL + CEF) は data\ に隔離。
//
// レイアウト:
//   <bundle>\<game>.exe          ← この stub
//   <bundle>\data\mitiru_host.exe ← 実ホスト (GUI subsystem)
//   <bundle>\data\launch.mtargs   ← host へ渡す argv (空白区切り、cwd=data 相対)
//   <bundle>\data\<game>\<game>.dll, assets, CEF runtime ...
//
// GUI subsystem (WinMain) なので親に console が無い → cmd 窓が一瞬も出ない。

#include <mitiru/platform/ExitNotice.hpp>
#include <mitiru/platform/GpuPreferenceExports.hpp>
#include <mitiru/platform/LaunchChild.hpp>
#include <mitiru/platform/Utf8Args.hpp>
#include <windows.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

/// data\launch.mtargs を 1 行読み込む (空白区切りの host argv)。なければ空。
/// "..." で囲まれたトークン (--title "My Game" 等) は行全体をコマンドラインに渡し、
/// host 側の CRT による argv 分割で引用符を解釈する。ここで分割する必要はない。
std::string readLaunchArgs(const std::filesystem::path& dataDir)
{
	std::ifstream f(dataDir / "launch.mtargs");
	if (!f) { return {}; }
	std::string line;
	std::getline(f, line);
	// 末尾の CR/空白を取り除く。
	while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
	{
		line.pop_back();
	}
	return line;
}

/// launch.mtargs (UTF-8) を CreateProcessW に渡せる UTF-16 に変換する。
///
/// バイト単位で変換してはいけない。--title に日本語を渡す配布物があり、1 バイトを
/// 1 文字として変換すると窓の表題がそのまま文字化けする (実際に文字化けした)。
std::wstring widen(const std::string& s)
{
	return mitiru::platform::utf8ToWide(s);
}

void showStubFailure(const std::wstring& text)
{
	mitiru::platform::exitnotice::showBox(L"MitiruEngine", text);
}

/// stub 自身が落ちたときも、何も出さずに消えない
LONG WINAPI stubCrashFilter(EXCEPTION_POINTERS* ep)
{
	const DWORD code = ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionCode : 0;
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s (コード 0x%08lX)", mitiru::platform::exitnotice::crashHint(code), static_cast<unsigned long>(code));
	showStubFailure(mitiru::platform::utf8ToWide(buf));
	return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void stubTerminate()
{
	showStubFailure(L"起動の準備で止まりました。配布フォルダを展開し直してください。");
	std::_Exit(3);
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	SetUnhandledExceptionFilter(&stubCrashFilter);
	std::set_terminate(&stubTerminate);

	const std::filesystem::path exe(mitiru::platform::thisExePath());
	const std::filesystem::path dataDir = exe.parent_path() / L"data";
	const std::filesystem::path host = dataDir / L"mitiru_host.exe";

	std::error_code ec;
	if (!std::filesystem::exists(host, ec))
	{
		showStubFailure(L"data\\mitiru_host.exe が見つかりません。配布物のフォルダを展開し直してください。\n"
		                L"ウイルス対策ソフトがファイルを隔離した可能性もあります。");
		return 2;
	}

	// host のコマンドラインを組み立てる。argv[0] は host 自身で、launch.mtargs の中身、
	// このランチャに付いた引数の順に続ける。
	std::wstring cmd = L"\"" + host.wstring() + L"\"";
	const std::string args = readLaunchArgs(dataDir);
	if (!args.empty())
	{
		cmd += L" ";
		cmd += widen(args);
	}
	const std::wstring extra = mitiru::platform::commandLineTail();
	if (!extra.empty())
	{
		cmd += L" ";
		cmd += extra;
	}

	// 前回の失敗の文を、今回のものと取り違えない
	const std::filesystem::path logDir = mitiru::platform::exitnotice::dirFor(mitiru::platform::exitnotice::gameNameFromArgs(args));
	mitiru::platform::exitnotice::clear(logDir);

	// cwd を data\ に固定して起動する (host も自分で基準位置を固定するが、念のため合わせる)。
	DWORD code = 0;
	if (!mitiru::platform::runAndWait(host.wstring(), cmd, dataDir.wstring(), code))
	{
		char detail[96];
		std::snprintf(detail, sizeof(detail), "mitiru_host.exe を起動できませんでした (エラー %lu)。\n配布物のフォルダを展開し直してください。",
		              static_cast<unsigned long>(GetLastError()));
		showStubFailure(mitiru::platform::utf8ToWide(detail));
		return 1;
	}
	// 窓を出さずに終わった host (読み込み前の失敗、落ちた、内部の検査) を、ここで利用者に知らせる
	const bool quiet = mitiru::platform::exitnotice::mentionsHeadless(args + " " + mitiru::platform::wideToUtf8(extra));
	mitiru::platform::exitnotice::reportChildExit(code, logDir, L"MitiruEngine", quiet);
	return static_cast<int>(code);
}
