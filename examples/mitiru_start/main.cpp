// mitiru_start — 配布用の極小 GUI ランチャ stub。
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

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

/// data\launch.mtargs を 1 行読む (空白区切りの host argv)。無ければ空。
std::string readLaunchArgs(const std::filesystem::path& dataDir)
{
	std::ifstream f(dataDir / "launch.mtargs");
	if (!f) { return {}; }
	std::string line;
	std::getline(f, line);
	// 末尾 CR/空白を落とす。
	while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
	{
		line.pop_back();
	}
	return line;
}

/// ASCII 想定の引数を wide に広げる (相対 DLL パス + フラグのみ。非 ASCII は使わない)。
std::wstring widen(const std::string& s)
{
	return std::wstring(s.begin(), s.end());
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	wchar_t buf[MAX_PATH] = {0};
	GetModuleFileNameW(nullptr, buf, MAX_PATH);

	const std::filesystem::path exe(buf);
	const std::filesystem::path dataDir = exe.parent_path() / L"data";
	const std::filesystem::path host = dataDir / L"mitiru_host.exe";

	std::error_code ec;
	if (!std::filesystem::exists(host, ec))
	{
		MessageBoxW(nullptr,
		            L"data\\mitiru_host.exe が見つかりません。配布フォルダを移動・改変していませんか?",
		            L"MitiruEngine", MB_ICONERROR | MB_OK);
		return 2;
	}

	// host の command line を組む。argv[0] は host 自身、続けて launch.mtargs の中身。
	std::wstring cmd = L"\"" + host.wstring() + L"\"";
	const std::string args = readLaunchArgs(dataDir);
	if (!args.empty())
	{
		cmd += L" ";
		cmd += widen(args);
	}

	// cwd を data\ に固定して起動 (host も自分で anchor するが念のため揃える)。
	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	STARTUPINFOW        si{};
	PROCESS_INFORMATION pi{};
	si.cb = sizeof(si);

	const BOOL ok = CreateProcessW(host.wstring().c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
	                               0, nullptr, dataDir.wstring().c_str(), &si, &pi);
	if (!ok)
	{
		MessageBoxW(nullptr, L"mitiru_host.exe の起動に失敗しました。", L"MitiruEngine",
		            MB_ICONERROR | MB_OK);
		return 1;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
