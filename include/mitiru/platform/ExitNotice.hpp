/// @file ExitNotice.hpp
/// @brief 配布物の host が失敗して終了するときに表示する文面の記録と、その読み書きの宣言。
/// @details host と host を起動する stub の mitiru_start は、last_run.log と同じ場所の last_error.txt を使う。1 行目が `shown` なら host が窓を表示済み、`pending` なら表示できなかったため stub が代わりに表示する。クラッシュ後でも書けるよう、書き込みには動的な確保を使わず固定領域だけを使う。環境変数 MITIRU_NO_DIALOG があると窓を表示せず、テストはこのファイルから文面を読む。

#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/platform/Utf8Args.hpp>

namespace mitiru::platform::exitnotice
{

inline constexpr const wchar_t* kFileName = L"last_error.txt";

/// @brief %LOCALAPPDATA%。getenv は ANSI に直すので使わない。無ければ空
[[nodiscard]] inline std::wstring localAppDataW()
{
	std::wstring local;
	const DWORD need = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	if (need > 0)
	{
		local.resize(need);
		const DWORD got = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), need);
		local.resize(got < need ? got : 0);
	}
	return local;
}

/// @brief ゲームごとの記録を置くフォルダ。host の last_run.log と同じ場所
[[nodiscard]] inline std::filesystem::path dirFor(std::string_view gameName)
{
	const std::wstring local = localAppDataW();
	std::error_code ec;
	const std::filesystem::path root = !local.empty() ? std::filesystem::path(local) : std::filesystem::temp_directory_path(ec);
	return root / L"MitiruGames" / utf8ToWide(gameName);
}

/// host が記録を置くフォルダの名前。`--game-name` があればその値、なければ DLL の名前を使う
[[nodiscard]] inline std::vector<std::string> splitArgs(const std::string& args)
{
	std::vector<std::string> out;
	std::string cur;
	bool quoted = false;
	bool any = false;
	for (const char c : args)
	{
		if (c == '"') { quoted = !quoted; any = true; }
		else if ((c == ' ' || c == '\t') && !quoted)
		{
			if (any) { out.push_back(cur); }
			cur.clear();
			any = false;
		}
		else { cur.push_back(c); any = true; }
	}
	if (any) { out.push_back(cur); }
	return out;
}

[[nodiscard]] inline std::string gameNameFromArgs(const std::string& args)
{
	const std::vector<std::string> tokens = splitArgs(args);
	for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
	{
		if (tokens[i] == "--game-name" && !tokens[i + 1].empty()) { return tokens[i + 1]; }
	}
	for (const std::string& t : tokens)
	{
		if (t.size() > 4 && t.compare(t.size() - 4, 4, ".dll") == 0)
		{
			const std::size_t slash = t.find_last_of("/\\");
			const std::size_t from = slash == std::string::npos ? 0 : slash + 1;
			return t.substr(from, t.size() - 4 - from);
		}
	}
	return "mitiru_host";
}

/// @brief host が窓を出さない実行 (--headless、--headless-3d) の指定が、引数の中に 1 語として含まれるか
/// @details stub は host の引数の解釈を持たないので、語の境界までを見る。タイトルの文字列に含まれるだけでは真にしない
[[nodiscard]] inline bool mentionsHeadless(const std::string& args)
{
	for (const std::string& t : splitArgs(args))
	{
		if (t == "--headless" || t == "--headless-3d") { return true; }
	}
	return false;
}

[[nodiscard]] inline bool dialogSuppressed()
{
	wchar_t buf[8] = {};
	const DWORD n = GetEnvironmentVariableW(L"MITIRU_NO_DIALOG", buf, 8);
	return n > 0 && buf[0] != L'0';
}

/// @brief host が last_run.log を置いたフォルダ。engine の終了直前に文面を残す場所として使い、空なら残さない
[[nodiscard]] inline std::filesystem::path& currentDirSlot()
{
	static std::filesystem::path dir;
	return dir;
}

inline void setCurrentDir(const std::filesystem::path& dir) { currentDirSlot() = dir; }

inline void clear(const std::filesystem::path& dir)
{
	std::error_code ec;
	std::filesystem::remove(dir / kFileName, ec);
}

/// @brief 固定の書式で書く。createNewOnly が true なら、クラッシュ前に書いた文面を上書きしない
inline bool writeRaw(const wchar_t* path, const char* utf8, std::size_t size, bool createNewOnly) noexcept
{
	const HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
	                             createNewOnly ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) { return false; }
	DWORD written = 0;
	const BOOL ok = WriteFile(h, utf8, static_cast<DWORD>(size), &written, nullptr);
	CloseHandle(h);
	return ok != FALSE && written == size;
}

inline bool write(const std::filesystem::path& dir, bool shown, const std::string& message)
{
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	const std::string body = std::string(shown ? "shown\n" : "pending\n") + message;
	return writeRaw((dir / kFileName).c_str(), body.data(), body.size(), false);
}

/// @brief host が窓を表示できずに終了するとき、stub が代わりに表示する文面を残す。すでにあれば書かない
inline void leavePending(const std::string& message) noexcept
{
	try
	{
		const std::filesystem::path& dir = currentDirSlot();
		if (dir.empty()) { return; }
		const std::string body = "pending\n" + message;
		writeRaw((dir / kFileName).c_str(), body.data(), body.size(), true);
	}
	catch (...) {}
}

struct Notice
{
	bool shown = false;
	std::string message;
};

[[nodiscard]] inline std::optional<Notice> read(const std::filesystem::path& dir)
{
	const HANDLE h = CreateFileW((dir / kFileName).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) { return std::nullopt; }
	std::string text(16384, '\0');
	DWORD got = 0;
	const BOOL ok = ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr);
	CloseHandle(h);
	if (!ok) { return std::nullopt; }
	text.resize(got);
	Notice n;
	const std::size_t nl = text.find('\n');
	const std::string head = text.substr(0, nl);
	n.shown = head == "shown";
	n.message = nl == std::string::npos ? std::string() : text.substr(nl + 1);
	return n;
}

/// @brief NTSTATUS で終了したプロセスをクラッシュとみなすか
[[nodiscard]] inline bool isCrashCode(DWORD code) noexcept
{
	return (code & 0xF0000000u) == 0xC0000000u || code == 0xE06D7363u || code == 0x40000015u || code == 0x80000003u;
}

/// @brief クラッシュの理由と、遊ぶ人が次にすることを 1 文で示す UTF-8 の固定文字列を返す
[[nodiscard]] inline const char* crashHint(DWORD code) noexcept
{
	switch (code)
	{
	case 0xC000001Du:
		return "このパソコンの CPU が、ゲームの使う命令に対応していない可能性があります。";
	case 0xC0000135u:
		return "ゲームに必要な DLL が見つかりません。配布フォルダを展開し直してください。Windows 10 以降が必要です。";
	case 0xC0000139u:
		return "ゲームの DLL の版が合いません。配布フォルダを展開し直してください。";
	case 0xC000007Bu:
		return "ゲームの DLL が壊れているか、64 bit 版ではありません。配布フォルダを展開し直してください。";
	case 0xC06D007Eu:
	case 0xC06D007Fu:
		return "ゲームに必要な DLL が見つからないか、版が合いません。配布フォルダを展開し直してください。";
	case 0xC0000142u:
		return "ゲームの DLL を初期化できませんでした。配布フォルダを展開し直してください。";
	case 0xC00000FDu:
		return "ゲームが内部で止まりました。グラフィックスドライバーを最新にして、もう一度起動してください。";
	case 0xC0000005u:
		return "ゲームが突然止まりました。グラフィックスドライバーを最新にして、もう一度起動してください。";
	case 0xC0000374u:
	case 0xC0000409u:
		return "ゲームが内部の検査で止まりました。もう一度起動してください。直らなければ、診断のファイルを送ってください。";
	default:
		return "ゲームが途中で止まりました。もう一度起動してください。直らなければ、診断のファイルを送ってください。";
	}
}

/// @brief 窓を表示する。MITIRU_NO_DIALOG があると表示しない
inline void showBox(const std::wstring& title, const std::wstring& text)
{
	if (dialogSuppressed()) { return; }
	MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

/// @brief 続けるか終わるかを選ぶ窓。OK なら true。環境変数 MITIRU_NO_DIALOG があると、窓を表示せず続ける扱いにする
[[nodiscard]] inline bool askContinue(const std::wstring& title, const std::wstring& text)
{
	if (dialogSuppressed()) { return true; }
	return MessageBoxW(nullptr, text.c_str(), title.c_str(),
	                   MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1 | MB_SETFOREGROUND | MB_TOPMOST) != IDCANCEL;
}

/// @brief host が 0 以外で終了したとき、host が窓を表示していなければ代わりに表示する
/// @param dir ゲームの記録を置くフォルダ。last_error.txt が `shown` なら何もしない
/// @param quiet true なら窓を表示しない。headless や台本の実行を利用者の画面に表示しないために使う
/// @details 表示する文面は dir/stub_message.txt にも残す。窓を表示しない環境のテストは、このファイルを読んで確かめる。
inline void reportChildExit(DWORD code, const std::filesystem::path& dir, const std::wstring& title, bool quiet)
{
	if (code == 0) { return; }
	const std::optional<Notice> notice = read(dir);
	if (notice && notice->shown) { return; }
	char tail[64];
	std::snprintf(tail, sizeof(tail), " (終了コード 0x%08lX)", static_cast<unsigned long>(code));
	std::string msg;
	if (notice && !notice->message.empty()) { msg = notice->message; }
	else if (isCrashCode(code)) { msg = std::string(crashHint(code)) + tail; }
	else { msg = std::string("ゲームが途中で終わりました") + tail + "。"; }
	std::error_code ec;
	if (std::filesystem::exists(dir, ec))
	{
		msg += "\n\n診断のファイルを次のフォルダに残しています。\n" + wideToUtf8(dir.wstring());
	}
	// host が読み込みに失敗して一度も動かなかった場合はフォルダがないため、表示した文面を残す場所を作る
	std::filesystem::create_directories(dir, ec);
	writeRaw((dir / L"stub_message.txt").c_str(), msg.data(), msg.size(), false);
	if (!quiet) { showBox(title, utf8ToWide(msg)); }
}

}  // namespace mitiru::platform::exitnotice

#endif  // _WIN32
