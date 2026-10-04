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

#include <mitiru/platform/LaunchChild.hpp>
#include <mitiru/platform/Utf8Args.hpp>
#include <windows.h>

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
		            L"data\\mitiru_host.exe が見つかりません。配布物のフォルダを展開し直してください。",
		            L"MitiruEngine", MB_ICONERROR | MB_OK);
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
	if (const std::wstring extra = mitiru::platform::commandLineTail(); !extra.empty())
	{
		cmd += L" ";
		cmd += extra;
	}

	// cwd を data\ に固定して起動する (host も自分で基準位置を固定するが、念のため合わせる)。
	DWORD code = 0;
	if (!mitiru::platform::runAndWait(host.wstring(), cmd, dataDir.wstring(), code))
	{
		MessageBoxW(nullptr, L"mitiru_host.exe の起動に失敗しました。", L"MitiruEngine",
		            MB_ICONERROR | MB_OK);
		return 1;
	}
	if (const wchar_t* why = mitiru::platform::loaderFailureText(code))
	{
		const std::wstring text = std::wstring(why) + L"\n配布物のフォルダを展開し直してください (data\\ の中身が欠けています)。";
		MessageBoxW(nullptr, text.c_str(), L"MitiruEngine", MB_ICONERROR | MB_OK);
	}
	return static_cast<int>(code);
}
