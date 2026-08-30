// mitiru_selfrun: 自己展開ランチャー。
//
// この exe の末尾に AssetPack (配布フォルダ一式 + mitiru_boot.txt) を連結して配る。
// 初回起動でローカルへ展開し、以後は展開済みをそのまま起動する。CEF のランタイムは
// exe に埋め込めない (実行中の DLL 群をメモリからは動かせない) ので、「1 ファイルで
// 配って、初回に自分でフォルダを作る」のがこの器の仕事になる。
//
// mitiru_boot.txt (パック内、行指向):
//   name=oscar_rythm            展開先フォルダの名前
//   exe=data/mitiru_host.exe    起動する exe (パック内の相対パス)
//   args=oscar_rythm/oscar_rythm.dll   渡す引数 (省略可)
//   cwd=data                    作業ディレクトリ (省略時は exe の場所)
//
// engine の C++ API には依存しない (AssetPack は純データのヘッダ)。GUI サブシステムで
// ビルドし、コンソール窓を出さない。

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/platform/Utf8Args.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{

namespace fs = std::filesystem;

struct Boot
{
	std::string name = "mitiru_game";
	std::string exe;
	std::string args;
	std::string cwd;
};

Boot parseBoot(const std::vector<uint8_t>& bytes)
{
	Boot b;
	std::string text(bytes.begin(), bytes.end());
	std::size_t pos = 0;
	while (pos <= text.size())
	{
		const std::size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) { line.pop_back(); }
		const std::size_t eq = line.find('=');
		if (eq == std::string::npos) { continue; }
		const std::string key = line.substr(0, eq);
		const std::string val = line.substr(eq + 1);
		if (key == "name")      { b.name = val; }
		else if (key == "exe")  { b.exe = val; }
		else if (key == "args") { b.args = val; }
		else if (key == "cwd")  { b.cwd = val; }
	}
	return b;
}

/// パックの目次から安定な指紋を作る。ビルドし直した exe は別のフォルダへ展開され、
/// 古い展開物を上書きして壊すことがない。
std::uint64_t fingerprint(const mitiru::vfs::AssetPack& pack)
{
	std::uint64_t h = 1469598103934665603ULL;
	auto mix = [&h](const void* p, std::size_t n) {
		const auto* b = static_cast<const unsigned char*>(p);
		for (std::size_t i = 0; i < n; ++i) { h = (h ^ b[i]) * 1099511628211ULL; }
	};
	for (const auto& path : pack.list())
	{
		mix(path.data(), path.size());
		const auto size = pack.sizeOf(path);
		mix(&size, sizeof(size));
	}
	return h;
}

fs::path installRootFor(const Boot& boot, std::uint64_t fp)
{
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fp));
#ifdef _WIN32
	const char* base = std::getenv("LOCALAPPDATA");
	fs::path root = (base != nullptr) ? fs::path(base) : fs::temp_directory_path();
#else
	const char* home = std::getenv("HOME");
	fs::path root = (home != nullptr) ? fs::path(home) / ".local" / "share"
	                                  : fs::temp_directory_path();
#endif
	return root / "MitiruGames" / boot.name / hex;
}

void fail(const char* msg)
{
#ifdef _WIN32
	MessageBoxA(nullptr, msg, "mitiru_selfrun", MB_ICONERROR | MB_OK);
#else
	std::fprintf(stderr, "mitiru_selfrun: %s\n", msg);
#endif
}

fs::path selfPath()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH * 2] = {};
	GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
	return fs::path(buf);
#else
	return fs::read_symlink("/proc/self/exe");
#endif
}

int runLauncher()
{
	auto pack = mitiru::vfs::AssetPack::open(selfPath());
	if (!pack)
	{
		fail("この exe には配布パックが連結されていません。\n"
		     "mitiru_selfpack で作った exe を実行してください。");
		return 1;
	}

	const auto bootBytes = pack->read("mitiru_boot.txt");
	if (!bootBytes)
	{
		fail("パックに mitiru_boot.txt がありません。");
		return 1;
	}
	const Boot boot = parseBoot(*bootBytes);
	if (boot.exe.empty())
	{
		fail("mitiru_boot.txt に exe= がありません。");
		return 1;
	}

	const fs::path root   = installRootFor(boot, fingerprint(*pack));
	const fs::path marker = root / ".mitiru_complete";

	std::error_code ec;
	if (!fs::exists(marker, ec))
	{
		// 途中で落ちた展開が残っていても、marker が無い限り全部書き直すので壊れない。
		for (const auto& path : pack->list())
		{
			const auto data = pack->read(path);
			if (!data) { fail("パックの読み出しに失敗しました。"); return 1; }
			const fs::path dst = root / fs::path(path);
			fs::create_directories(dst.parent_path(), ec);
			std::ofstream f(dst, std::ios::binary | std::ios::trunc);
			if (!f) { fail("展開先に書き込めません。"); return 1; }
			f.write(reinterpret_cast<const char*>(data->data()),
			        static_cast<std::streamsize>(data->size()));
			if (!f) { fail("展開の書き込みに失敗しました。"); return 1; }
		}
		std::ofstream(marker).put('1');
	}

	const fs::path exe = root / fs::path(boot.exe);
	const fs::path cwd = boot.cwd.empty() ? exe.parent_path() : root / fs::path(boot.cwd);

#ifdef _WIN32
	std::wstring cmd = L"\"" + exe.wstring() + L"\"";
	if (!boot.args.empty())
	{
		const std::string& a = boot.args;
		// バイト単位で広げてはいけない。args は mitiru_boot.txt の UTF-8 で、
		// 日本語 (--title "オスカーのガーデニング") が 1 バイト 1 文字に化ける。
		cmd += L" " + mitiru::platform::utf8ToWide(a);
	}
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE,
	                    0, nullptr, cwd.wstring().c_str(), &si, &pi))
	{
		fail("ゲームの起動に失敗しました。");
		return 1;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
#else
	const std::string sh = "cd \"" + cwd.string() + "\" && \"" + exe.string() + "\" " + boot.args + " &";
	return std::system(sh.c_str());
#endif
}

}  // namespace

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return runLauncher(); }
#else
int main() { return runLauncher(); }
#endif
