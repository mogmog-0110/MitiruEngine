// mitiru_launcher — Game-as-DLL として dogfood する GUI プロジェクトマネージャ / launcher
//
// ADR 0005: この DLL はエンジンの C++ API を一切呼ばない。CEF への state push は
// 全て `FrameIntents::statePushes[]` 経由。ユーザー操作は全て
// `InputSnapshot::actionEvents[]` で届く (エンジンが CEF dispatch を
// `StateStore::onActionFallback` 経由でこのキューへ流す)。
//
// この DLL の役割:
//   - %APPDATA%/MitiruEngine/projects.json の読み書き (永続化)
//   - Run / Watch 用に子 mitiru_host.exe を起動 (CreateProcessW)
//   - 既存 DLL 追加用ファイルピッカー (GetOpenFileNameW)
//   - [+ New project] scaffold: examples/<name>/ 作成 + CMakeLists パッチ
//   - [Build] ボタン: vcvars64.bat 下で cmake を起動、stdout → ログファイル
//   - explorer / ログファイルを開く (ShellExecuteW)
//
// Win32 API は ADR 0005 上許容 — エンジン API ではないため。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru_launcher
{

constexpr int kMaxRecents = 16;

// ── データ構造 ────────────────────────────────────────────────────

struct Project
{
	std::string name;                           ///< 表示用ラベル
	std::string dllPath;                        ///< ゲーム DLL の絶対パス
	std::string sourceDir;                      ///< 任意: .cpp / CMakeLists の場所
	std::string buildDir;                       ///< 任意: cmake build dir (CMakeCache.txt のある所)
	std::string buildTarget;                    ///< 任意: cmake target 名 (既定は stem(dllPath))
};

struct RecentRun
{
	std::string projectName;
	std::time_t startedAt {0};
	std::time_t endedAt   {0};
	int         exitCode  {-1};
	bool        watching  {false};
};

struct LiveChild
{
	std::string projectName;
	std::time_t startedAt {0};
	bool        watching  {false};
	HANDLE      hProcess  {nullptr};
	HANDLE      hThread   {nullptr};
	DWORD       pid       {0};
};

struct BuildJob
{
	std::string                projectName;
	std::time_t                startedAt {0};
	std::chrono::steady_clock::time_point startedAtSteady;
	HANDLE                     hProcess {nullptr};
	HANDLE                     hThread  {nullptr};
	std::filesystem::path      logPath;
};

struct BuildResult
{
	bool                       attempted     {false};
	bool                       ok            {false};
	int                        exitCode      {-1};
	long long                  durationMs    {0};
	std::time_t                completedAt   {0};
	std::filesystem::path      logPath;
};

/// [+ New project] が起動する実行中の cmake reconfigure。非同期 —
/// 5-10 秒かかる configure 中も UI を固めないよう、エンジンループが毎フレーム poll する。
struct ScaffoldJob
{
	std::string                projectName;
	std::string                expectedDllPath;
	std::string                sourceDir;
	std::string                buildDir;
	HANDLE                     hProcess {nullptr};
	HANDLE                     hThread  {nullptr};
	std::filesystem::path      logPath;
	std::chrono::steady_clock::time_point startedAtSteady;
};

struct LauncherMemory
{
	std::vector<Project>      projects;
	std::deque<RecentRun>     recents;
	std::vector<LiveChild>    children;
	std::vector<BuildJob>     activeBuilds;
	std::unordered_map<std::string, BuildResult> lastBuilds; // key = project 名
	std::vector<ScaffoldJob>  activeScaffolds; // 非同期 cmake reconfigure
	// [▶ Open] がビルド完了待ちのプロジェクト名。
	// reapFinishedBuilds() がこれを消費し、ビルド成功時に game+companion を
	// 起動する。DLL 未ビルドでも (典型的には [+ New project] 直後)
	// [Open] が「ただ動く」感覚になる。
	std::vector<std::string>  pendingOpenAfterBuild;

	std::filesystem::path     appDataDir;
	std::filesystem::path     projectsFile;
	std::filesystem::path     hostExePath;     // mitiru_host.exe (このプロセス)
	std::filesystem::path     hostExeDir;      // hostExePath の親
	std::optional<std::filesystem::path> engineRoot;   // launcher が dev tree 内なら検出
	std::optional<std::filesystem::path> vcvarsBat;    // 初めて必要になった時に検出

	int  pushTick {0};
	bool firstPush {true};

	std::string scratchJson;
};

// ── 文字列 / パス ヘルパ ─────────────────────────────────────────────

std::string utf16ToUtf8(const std::wstring& w)
{
	if (w.empty()) { return {}; }
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
		static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0) { return {}; }
	std::string out(static_cast<std::size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
		out.data(), n, nullptr, nullptr);
	return out;
}

std::wstring utf8ToUtf16(const std::string& s)
{
	if (s.empty()) { return {}; }
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
		static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) { return {}; }
	std::wstring out(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
		out.data(), n);
	return out;
}

std::filesystem::path resolveAppDataDir()
{
	PWSTR appData = nullptr;
	std::filesystem::path base;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
	{
		base = appData;
		CoTaskMemFree(appData);
	}
	else { base = std::filesystem::current_path(); }
	const auto dir = base / "MitiruEngine";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir;
}

/// この DLL の host exe パスを GetModuleFileNameW(nullptr) で取得。
std::filesystem::path resolveHostExePath()
{
	wchar_t buf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) { return {}; }
	return std::filesystem::path{std::wstring{buf, n}};
}

/// host がエンジンの build tree から動いているなら engine source root を返す。
/// ヒューリスティック: host exe dir から上へ辿り `CMakeLists.txt` + `include/mitiru/` を探す。
std::optional<std::filesystem::path> detectEngineRoot(const std::filesystem::path& hostExeDir)
{
	std::filesystem::path p = hostExeDir;
	for (int i = 0; i < 8; ++i)
	{
		std::error_code ec;
		if (std::filesystem::exists(p / "CMakeLists.txt", ec) &&
		    std::filesystem::exists(p / "include" / "mitiru", ec))
		{
			return p;
		}
		if (p.parent_path() == p) { break; }
		p = p.parent_path();
	}
	return std::nullopt;
}

/// DLL パスから上へ辿り CMakeCache.txt を探す — それが build dir。
std::optional<std::filesystem::path> findCmakeBuildDir(const std::filesystem::path& start)
{
	std::filesystem::path p = start;
	for (int i = 0; i < 12; ++i)
	{
		std::error_code ec;
		if (std::filesystem::exists(p / "CMakeCache.txt", ec)) { return p; }
		if (p.parent_path() == p) { break; }
		p = p.parent_path();
	}
	return std::nullopt;
}

/// vcvars64.bat の best-effort 探索。一般的な VS 2022 install パスを試す。
std::optional<std::filesystem::path> findVcvarsBat()
{
	const std::vector<std::wstring> candidates = {
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Preview\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
	};
	for (const auto& w : candidates)
	{
		std::filesystem::path p{w};
		std::error_code ec;
		if (std::filesystem::exists(p, ec)) { return p; }
	}
	return std::nullopt;
}

// ── Intent push ヘルパ (DLL → host) ─────────────────────────────────

void pushStateString(mitiru::module::FrameIntents* intents,
                     const char* key, const std::string& value)
{
	const int cap = static_cast<int>(sizeof(intents->statePushes) /
	                                 sizeof(intents->statePushes[0]));
	if (intents->statePushCount >= cap) { return; }
	auto& slot = intents->statePushes[intents->statePushCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.key, key, sizeof(slot.key) - 1);
	slot.kind = 4;
	std::strncpy(slot.strVal, value.c_str(), sizeof(slot.strVal) - 1);
}

// ── 永続化 ──────────────────────────────────────────────────────

void loadProjects(LauncherMemory& mem)
{
	std::error_code ec;
	if (!std::filesystem::exists(mem.projectsFile, ec) || ec) { return; }
	std::ifstream ifs(mem.projectsFile);
	if (!ifs) { return; }

	try
	{
		nlohmann::json j;
		ifs >> j;
		if (!j.is_array()) { return; }
		for (const auto& item : j)
		{
			Project p;
			p.name        = item.value("name", "");
			p.dllPath     = item.value("dllPath", "");
			p.sourceDir   = item.value("sourceDir", "");
			p.buildDir    = item.value("buildDir", "");
			p.buildTarget = item.value("buildTarget", "");
			if (!p.dllPath.empty()) { mem.projects.push_back(std::move(p)); }
		}
	}
	catch (...) {}
}

void saveProjects(const LauncherMemory& mem)
{
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& p : mem.projects)
	{
		nlohmann::json item = {
			{"name", p.name},
			{"dllPath", p.dllPath},
		};
		if (!p.sourceDir.empty())   { item["sourceDir"]   = p.sourceDir; }
		if (!p.buildDir.empty())    { item["buildDir"]    = p.buildDir; }
		if (!p.buildTarget.empty()) { item["buildTarget"] = p.buildTarget; }
		arr.push_back(item);
	}
	std::ofstream ofs(mem.projectsFile);
	if (ofs) { ofs << arr.dump(2); }
}

/// プロジェクトの欠けた build / source 情報を DLL パスから推測して補完。
/// 冪等 — 空フィールドのみ書き込む。
void inferProjectContext(Project& p)
{
	if (p.dllPath.empty()) { return; }
	const std::filesystem::path dll{p.dllPath};
	if (p.buildTarget.empty()) { p.buildTarget = dll.stem().string(); }
	if (p.sourceDir.empty())   { p.sourceDir   = dll.parent_path().string(); }
	if (p.buildDir.empty())
	{
		if (auto bd = findCmakeBuildDir(dll.parent_path()); bd.has_value())
		{
			p.buildDir = bd->string();
		}
	}
}

// ── 子 mitiru_host 起動 ───────────────────────────────────────

/// ゲーム DLL 用に子 mitiru_host.exe を --watch 付きで起動。
/// dev-companion の [Build] が hot-swap できるようにする。PID または 0 を返す。
DWORD spawnChildGame(LauncherMemory& mem, const Project& proj)
{
	const std::wstring hostExe = mem.hostExePath.wstring();
	const std::wstring dllPathW = utf8ToUtf16(proj.dllPath);
	std::wstring cmdLine = L"\"" + hostExe + L"\" \"" + dllPathW + L"\" --watch";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	const BOOL ok = CreateProcessW(
		hostExe.c_str(), cmdBuf.data(),
		nullptr, nullptr, FALSE,
		CREATE_NEW_PROCESS_GROUP, nullptr,
		mem.hostExeDir.wstring().c_str(),
		&si, &pi);
	if (!ok) { return 0; }

	LiveChild lc;
	lc.projectName = proj.name;
	lc.startedAt   = std::time(nullptr);
	lc.watching    = true;
	lc.hProcess    = pi.hProcess;
	lc.hThread     = pi.hThread;
	lc.pid         = pi.dwProcessId;
	mem.children.push_back(std::move(lc));
	return pi.dwProcessId;
}

/// companion が起動時に読む session ファイルを %TEMP% に書き出す。
std::filesystem::path writeSessionFile(const Project& proj, DWORD gamePid)
{
	const std::string target = proj.buildTarget.empty()
		? std::filesystem::path{proj.dllPath}.stem().string()
		: proj.buildTarget;
	nlohmann::json j = {
		{"projectName", proj.name},
		{"projectDll",  proj.dllPath},
		{"buildDir",    proj.buildDir},
		{"buildTarget", target},
		{"gamePid",     static_cast<long long>(gamePid)},
	};
	std::filesystem::path tempDir = std::filesystem::temp_directory_path();
	std::filesystem::path sessionFile = tempDir /
		("mitiru_dev_session_" + std::to_string(gamePid) + ".json");
	std::ofstream ofs(sessionFile);
	if (!ofs) { return {}; }
	ofs << j.dump(2);
	return sessionFile;
}

/// mitiru_dev_companion を 760x80 のコンパクトバーで起動。session は
/// (ここで設定する) MITIRU_COMPANION_SESSION 環境変数から読む。
bool spawnCompanion(LauncherMemory& mem,
                    const Project& /*proj*/,
                    const std::filesystem::path& sessionFile)
{
	const std::wstring hostExe = mem.hostExePath.wstring();
	std::filesystem::path companionDll =
		mem.hostExeDir / "mitiru_dev_companion" / "mitiru_dev_companion.dll";
	std::error_code ec;
	if (!std::filesystem::exists(companionDll, ec)) { return false; }

	std::wstring cmdLine = L"\"" + hostExe + L"\" \"" + companionDll.wstring() +
		L"\" --size 760x80";

	// MITIRU_COMPANION_SESSION を注入した env block を構築。
	std::wstring envBlock;
	LPWCH parentEnv = GetEnvironmentStringsW();
	if (parentEnv != nullptr)
	{
		LPWCH p = parentEnv;
		while (*p != L'\0')
		{
			const std::wstring entry{p};
			if (entry.find(L"MITIRU_COMPANION_SESSION=") != 0)
			{
				envBlock.append(entry);
				envBlock.push_back(L'\0');
			}
			p += entry.size() + 1;
		}
		FreeEnvironmentStringsW(parentEnv);
	}
	std::wstring sessionEntry = L"MITIRU_COMPANION_SESSION=" + sessionFile.wstring();
	envBlock.append(sessionEntry);
	envBlock.push_back(L'\0');
	envBlock.push_back(L'\0');

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	const BOOL ok = CreateProcessW(
		hostExe.c_str(), cmdBuf.data(),
		nullptr, nullptr, FALSE,
		CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP,
		envBlock.data(),
		mem.hostExeDir.wstring().c_str(),
		&si, &pi);
	if (!ok) { return false; }
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}

void reapExitedChildren(LauncherMemory& mem)
{
	for (auto it = mem.children.begin(); it != mem.children.end(); )
	{
		const DWORD wait = WaitForSingleObject(it->hProcess, 0);
		if (wait == WAIT_OBJECT_0)
		{
			DWORD exitCode = 0;
			GetExitCodeProcess(it->hProcess, &exitCode);

			RecentRun rr;
			rr.projectName = it->projectName;
			rr.startedAt   = it->startedAt;
			rr.endedAt     = std::time(nullptr);
			rr.exitCode    = static_cast<int>(exitCode);
			rr.watching    = it->watching;
			mem.recents.push_front(std::move(rr));
			while (mem.recents.size() > kMaxRecents) { mem.recents.pop_back(); }

			CloseHandle(it->hProcess);
			CloseHandle(it->hThread);
			it = mem.children.erase(it);
		}
		else { ++it; }
	}
}

// ── Build サブプロセス ─────────────────────────────────────────────────

bool spawnBuild(LauncherMemory& mem, const Project& proj)
{
	if (proj.buildDir.empty() || proj.buildTarget.empty()) { return false; }
	if (!mem.vcvarsBat.has_value()) { mem.vcvarsBat = findVcvarsBat(); }
	if (!mem.vcvarsBat.has_value()) { return false; }

	std::filesystem::path tempDir = std::filesystem::temp_directory_path();
	std::filesystem::path logPath = tempDir / ("mitiru_build_" + proj.name + ".log");

	// コマンド:
	//   cmd.exe /c "call "<vcvars64.bat>" >nul && cmake --build "<buildDir>" --config Debug --target <target> > "<log>" 2>&1"
	std::wstring vcvarsW = mem.vcvarsBat->wstring();
	std::wstring buildDirW = utf8ToUtf16(proj.buildDir);
	std::wstring targetW   = utf8ToUtf16(proj.buildTarget);
	std::wstring logPathW  = logPath.wstring();

	std::wstring inner =
		L"call \"" + vcvarsW + L"\" >nul && "
		L"cmake --build \"" + buildDirW + L"\" --config Debug --target " + targetW +
		L" > \"" + logPathW + L"\" 2>&1";

	// cmd.exe /c "<inner>" — 外側の quoting が必要
	std::wstring cmdLine = L"cmd.exe /c \"" + inner + L"\"";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	const BOOL ok = CreateProcessW(
		nullptr, cmdBuf.data(),
		nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr,
		mem.hostExeDir.wstring().c_str(),
		&si, &pi);
	if (!ok) { return false; }

	BuildJob job;
	job.projectName     = proj.name;
	job.startedAt       = std::time(nullptr);
	job.startedAtSteady = std::chrono::steady_clock::now();
	job.hProcess        = pi.hProcess;
	job.hThread         = pi.hThread;
	job.logPath         = logPath;
	mem.activeBuilds.push_back(std::move(job));
	return true;
}

// reapFinishedBuilds() が保留中の [Open] を完了できるよう前方宣言。
DWORD spawnChildGame(LauncherMemory& mem, const Project& proj);
std::filesystem::path writeSessionFile(const Project& proj, DWORD gamePid);
bool spawnCompanion(LauncherMemory& mem, const Project& proj,
                    const std::filesystem::path& sessionFile);
void pushFlash(LauncherMemory& mem,
               mitiru::module::FrameIntents* intents,
               const std::string& kind, const std::string& message);

void reapFinishedBuilds(LauncherMemory& mem,
                        mitiru::module::FrameIntents* intents)
{
	for (auto it = mem.activeBuilds.begin(); it != mem.activeBuilds.end(); )
	{
		const DWORD wait = WaitForSingleObject(it->hProcess, 0);
		if (wait != WAIT_OBJECT_0) { ++it; continue; }

		DWORD exitCode = 0;
		GetExitCodeProcess(it->hProcess, &exitCode);

		BuildResult res;
		res.attempted   = true;
		res.ok          = (exitCode == 0);
		res.exitCode    = static_cast<int>(exitCode);
		res.completedAt = std::time(nullptr);
		res.durationMs  = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - it->startedAtSteady).count();
		res.logPath     = it->logPath;
		const std::string finishedName = it->projectName;
		const bool finishedOk = res.ok;
		mem.lastBuilds[finishedName] = std::move(res);

		CloseHandle(it->hProcess);
		CloseHandle(it->hThread);
		it = mem.activeBuilds.erase(it);

		// Auto-Build 後の Auto-Open (未ビルド DLL の launcher.open)。
		auto pIt = std::find(mem.pendingOpenAfterBuild.begin(),
		                     mem.pendingOpenAfterBuild.end(),
		                     finishedName);
		if (pIt == mem.pendingOpenAfterBuild.end()) { continue; }
		mem.pendingOpenAfterBuild.erase(pIt);

		if (!finishedOk)
		{
			if (intents)
			{
				pushFlash(mem, intents, "error",
					"Build FAILED for '" + finishedName + "' — open can't proceed.");
			}
			continue;
		}

		// プロジェクトエントリを探して open 処理を完了する。
		const Project* proj = nullptr;
		for (const auto& p : mem.projects)
		{
			if (p.name == finishedName) { proj = &p; break; }
		}
		if (proj == nullptr) { continue; }

		const DWORD pid = spawnChildGame(mem, *proj);
		if (pid == 0)
		{
			if (intents) {
				pushFlash(mem, intents, "error",
					"Build ok but CreateProcess failed for '" + finishedName + "'.");
			}
			continue;
		}
		std::filesystem::path sf = writeSessionFile(*proj, pid);
		if (!sf.empty())
		{
			spawnCompanion(mem, *proj, sf);
		}
		if (intents)
		{
			pushFlash(mem, intents, "ok",
				"Built '" + finishedName + "' and opened.");
			// launcher を自動で閉じる (上の同期 launcher.open と
			// 同じ atomic-tools の理由)。
			intents->requestStop = 1;
		}
	}
}

bool isBuildActive(const LauncherMemory& mem, const std::string& name)
{
	for (const auto& b : mem.activeBuilds)
	{
		if (b.projectName == name) { return true; }
	}
	return false;
}

// ── ファイルピッカー (Add Project) ────────────────────────────────────────

std::optional<std::string> pickDllFile()
{
	wchar_t buffer[MAX_PATH] = {};
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner   = nullptr;
	ofn.lpstrFilter = L"Game DLL (*.dll)\0*.dll\0All Files\0*.*\0";
	ofn.lpstrFile   = buffer;
	ofn.nMaxFile    = MAX_PATH;
	ofn.lpstrTitle  = L"Select game DLL";
	ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn)) { return std::nullopt; }
	return utf16ToUtf8(buffer);
}

void openInExplorer(const std::filesystem::path& path)
{
	if (path.empty()) { return; }
	ShellExecuteW(nullptr, L"open", L"explorer.exe", path.wstring().c_str(),
	              nullptr, SW_SHOWNORMAL);
}

void openLogInNotepad(const std::filesystem::path& logPath)
{
	if (logPath.empty()) { return; }
	std::error_code ec;
	if (!std::filesystem::exists(logPath, ec)) { return; }
	ShellExecuteW(nullptr, L"open", L"notepad.exe", logPath.wstring().c_str(),
	              nullptr, SW_SHOWNORMAL);
}

// ── Scaffold (+ New project) ─────────────────────────────────────────
//
// 方針: launcher が dev tree から動いている時、
//   <engine_root>/examples/<name>/ にテンプレ cpp + CMakeLists + assets を生成し、
//   examples/CMakeLists.txt に `add_subdirectory(<name>)` を追記して
//   cmake reconfigure を起こす。reconfigure 後に [Build] が機能する。
//
// release zip から動いている (engine_root 無し) 場合、scaffold dispatch は
// エラーを報告する。

constexpr const char* kDllTemplate = R"CPP(// {NAME} — game DLL scaffolded by mitiru_launcher.
//
// Per ADR 0005, never call into engine C++ — read InputSnapshot, write
// to FrameIntents, that's the entire host boundary.

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/input/Keys.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace {NAME} {

namespace keys = mitiru::keys;  // brief alias instead of `using namespace`
                                // (avoids wingdi.h `Escape` collision when
                                // engine headers indirectly pull windows.h)

struct GameMemory
{
	float playerX  = -1.0f;     // -1 = "not yet initialized" → centered on first draw
	float playerY  = -1.0f;
	float screenW  = 1280.0f;   // updated each frame from Screen::width/height
	float screenH  =  720.0f;
	int   pushTick = 0;
	std::string scratchJson;    // reused JSON buffer for inspectable export
};

constexpr float kSpeed       = 320.0f;
constexpr float kPlayerSize  = 32.0f;

void on_init(void* /*memory*/) {}

// Export one "player" inspectable so the dev-companion [Inspector] button
// has something to show. The companion spawns mitiru_inspector.exe which
// reads the engine's SharedSnapshot file and renders whatever names this
// callback registers. Add more entries (enemies, score, etc.) as the game
// grows.
void exportPlayerInspectable(GameMemory& mem,
                             mitiru::module::FrameIntents* intents)
{
	if (intents->exportedInspectableCount >= 8) { return; }
	auto& slot = intents->exportedInspectables[intents->exportedInspectableCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.name,  "player",        sizeof(slot.name)  - 1);
	std::strncpy(slot.title, "Player state",  sizeof(slot.title) - 1);

	nlohmann::json j = {
		{"x",       mem.playerX},
		{"y",       mem.playerY},
		{"screenW", mem.screenW},
		{"screenH", mem.screenH},
	};
	mem.scratchJson = j.dump();
	const auto cap = sizeof(slot.json) - 1;
	const auto n = mem.scratchJson.size() < cap ? mem.scratchJson.size() : cap;
	if (n > 0) { std::memcpy(slot.json, mem.scratchJson.data(), n); }
	slot.json[n] = '\0';
	slot.jsonLen = static_cast<std::int32_t>(n);
}

void on_update(void* memory, float dt,
               const mitiru::module::InputSnapshot* input,
               mitiru::module::FrameIntents* intents)
{
	auto& mem = *static_cast<GameMemory*>(memory);

	if (input->keysJustPressed[keys::Escape]) { intents->requestStop = 1; }

	float dx = 0.0f, dy = 0.0f;
	if (input->keysDown[keys::Left])  { dx -= 1.0f; }
	if (input->keysDown[keys::Right]) { dx += 1.0f; }
	if (input->keysDown[keys::Up])    { dy -= 1.0f; }
	if (input->keysDown[keys::Down])  { dy += 1.0f; }
	mem.playerX += dx * kSpeed * dt;
	mem.playerY += dy * kSpeed * dt;

	// Clamp to the current window size — if the user resizes the window
	// smaller than where we drew last frame, snap back into view.
	const float half = kPlayerSize * 0.5f;
	if (mem.playerX < half)               { mem.playerX = half; }
	if (mem.playerX > mem.screenW - half) { mem.playerX = mem.screenW - half; }
	if (mem.playerY < half)               { mem.playerY = half; }
	if (mem.playerY > mem.screenH - half) { mem.playerY = mem.screenH - half; }

	// Push the player inspectable ~10 Hz (every 6 frames at 60 fps).
	if (++mem.pushTick >= 6)
	{
		mem.pushTick = 0;
		exportPlayerInspectable(mem, intents);
	}
}

void on_draw(void* memory, mitiru::Screen* screen)
{
	auto& mem = *static_cast<GameMemory*>(memory);
	mem.screenW = static_cast<float>(screen->width());
	mem.screenH = static_cast<float>(screen->height());
	// First-frame center.
	if (mem.playerX < 0.0f) { mem.playerX = mem.screenW * 0.5f; }
	if (mem.playerY < 0.0f) { mem.playerY = mem.screenH * 0.5f; }

	screen->clear(sgc::Colorf{0.07f, 0.09f, 0.14f, 1.0f});
	screen->drawRect(
		sgc::Rectf{mem.playerX - kPlayerSize * 0.5f,
		           mem.playerY - kPlayerSize * 0.5f,
		           kPlayerSize, kPlayerSize},
		sgc::Colorf{0.40f, 0.85f, 0.95f, 1.0f});
}

void on_shutdown(void* /*memory*/) {}

}  // namespace {NAME}

extern "C" __declspec(dllexport)
void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)
{
	if (!api || !memory) { return; }
	if (*memory == nullptr) {
		*memory = new {NAME}::GameMemory{};
	}
	api->version     = mitiru::module::kCurrentApiVersion;
	api->on_init     = &{NAME}::on_init;
	api->on_update   = &{NAME}::on_update;
	api->on_draw     = &{NAME}::on_draw;
	api->on_shutdown = &{NAME}::on_shutdown;
}

extern "C" __declspec(dllexport)
void mitiru_module_unload(void* /*memory*/) {}
)CPP";

constexpr const char* kCMakeTemplate = R"CMAKE(# {NAME} — scaffolded by mitiru_launcher.
# Built as SHARED so mitiru_host can load it via Engine::loadModule (ADR 0005).

add_library({NAME} SHARED {NAME}.cpp)
target_link_libraries({NAME} PRIVATE mitiru)

if(MSVC)
	target_compile_options({NAME} PRIVATE /bigobj)
endif()

# Drop the DLL + assets next to mitiru_host (matches hello_game layout).
if(TARGET mitiru_host)
	set(_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/{NAME}")
	set_target_properties({NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_runtime_dir}")
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
		add_custom_command(TARGET {NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/assets"
				"${_runtime_dir}/assets"
			COMMENT "{NAME}: copying assets/ next to DLL")
	endif()
	add_dependencies({NAME} mitiru_host)
endif()
)CMAKE";

constexpr const char* kSceneHtmlTemplate = R"HTML(<!doctype html>
<html lang="ja">
<head>
	<meta charset="utf-8">
	<title>{NAME}</title>
	<style>
		html, body { margin: 0; padding: 0; height: 100vh; width: 100vw;
			background: transparent; color: #eef; font-family: sans-serif;
			overflow: hidden; pointer-events: none; }
		.hint { position: absolute; bottom: 18px; left: 22px; font-size: 12px;
			opacity: 0.5; }
	</style>
</head>
<body>
	<div class="hint">{NAME} — arrow keys to move · ESC to quit</div>
</body>
</html>
)HTML";

/// テンプレ文字列内の `{NAME}` プレースホルダを置換する。
std::string applyTemplate(const char* tmpl, const std::string& name)
{
	std::string s{tmpl};
	const std::string placeholder = "{NAME}";
	std::size_t pos = 0;
	while ((pos = s.find(placeholder, pos)) != std::string::npos)
	{
		s.replace(pos, placeholder.size(), name);
		pos += name.size();
	}
	return s;
}

bool isValidProjectName(const std::string& name)
{
	if (name.empty() || name.size() > 48) { return false; }
	if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
	{
		return false;
	}
	for (char c : name)
	{
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { return false; }
	}
	return true;
}

/// scaffolding の同期ファイル生成フェーズの結果。
/// コストの高い cmake reconfigure は別途起動し、~5-10 秒 UI を固めないよう
/// mem.activeScaffolds で追跡する。
struct ScaffoldStartResult
{
	bool        ok {false};
	std::string message;
	std::string dllPath;
	std::string sourceDir;
	std::string buildDir;
	std::string buildTarget;
	bool        cmakeStarted {false}; // reconfigure が activeScaffolds に積まれたら true
};

ScaffoldStartResult scaffoldStart(LauncherMemory& mem, const std::string& name)
{
	ScaffoldStartResult r;

	if (!isValidProjectName(name))
	{
		r.message = "Invalid name. Use letters/digits/underscore; first char letter or _.";
		return r;
	}
	if (!mem.engineRoot.has_value())
	{
		r.message = "Scaffolding requires engine source (clone the MitiruEngineDev repo). "
		            "You're running from a release zip; only adding existing DLLs works here.";
		return r;
	}

	std::filesystem::path dir = *mem.engineRoot / "examples" / name;
	std::error_code ec;
	const bool dirAlreadyExists = std::filesystem::exists(dir, ec);

	if (dirAlreadyExists)
	{
		// 以前 scaffold 済み (launcher リストで ✕ を押した — これはエントリのみ
		// 削除しファイルは残す)。カスタマイズを上書きせず、リストへ再登録して
		// cmake configure を再実行し build tree を同期させる。
		// 既存ソースファイルはそのまま保持。
	}
	else
	{
		if (!std::filesystem::create_directories(dir, ec) || ec)
		{
			r.message = "Failed to create directory: " + ec.message();
			return r;
		}
		std::filesystem::create_directories(dir / "assets", ec);

		// ソースファイル書き込み (高速: 数 KB のテキスト — 同期で問題ない)。
		{
			std::ofstream cpp(dir / (name + ".cpp"));
			cpp << applyTemplate(kDllTemplate, name);
		}
		{
			std::ofstream cm(dir / "CMakeLists.txt");
			cm << applyTemplate(kCMakeTemplate, name);
		}
		{
			std::ofstream html(dir / "assets" / "scene.html");
			html << applyTemplate(kSceneHtmlTemplate, name);
		}
	}

	// examples/CMakeLists.txt に `add_subdirectory(name)` を追記 — 冪等。
	std::filesystem::path examplesCm = *mem.engineRoot / "examples" / "CMakeLists.txt";
	if (std::filesystem::exists(examplesCm, ec))
	{
		std::string contents;
		{
			std::ifstream ifs(examplesCm);
			std::stringstream ss;
			ss << ifs.rdbuf();
			contents = ss.str();
		}
		const std::string needle = "add_subdirectory(" + name + ")";
		if (contents.find(needle) == std::string::npos)
		{
			if (!contents.empty() && contents.back() != '\n') { contents += '\n'; }
			contents += needle + "\n";
			std::ofstream ofs(examplesCm);
			ofs << contents;
		}
	}

	std::filesystem::path engineBuild = *mem.engineRoot / "build";
	std::filesystem::path runtimeDir  =
		engineBuild / "examples" / "mitiru_host" / name;
	r.dllPath     = (runtimeDir / (name + ".dll")).string();
	r.sourceDir   = dir.string();
	r.buildDir    = engineBuild.string();
	r.buildTarget = name;

	// cmake reconfigure を非同期で開始。vcvars64.bat 無しならスキップ —
	// 新 subdir は CMakeLists に追加済みだが、[Build] が機能する前に
	// ユーザー自身で cmake を再実行する必要がある。
	if (!mem.vcvarsBat.has_value()) { mem.vcvarsBat = findVcvarsBat(); }
	if (mem.vcvarsBat.has_value())
	{
		std::filesystem::path reconfigLog =
			std::filesystem::temp_directory_path() /
			("mitiru_reconfigure_" + name + ".log");
		std::wstring vcvarsW = mem.vcvarsBat->wstring();
		std::wstring engineRootW = mem.engineRoot->wstring();
		std::wstring buildDirW = engineBuild.wstring();
		std::wstring inner =
			L"call \"" + vcvarsW + L"\" >nul && "
			L"cmake -S \"" + engineRootW + L"\" -B \"" + buildDirW +
			L"\" > \"" + reconfigLog.wstring() + L"\" 2>&1";
		std::wstring cmdLine = L"cmd.exe /c \"" + inner + L"\"";

		STARTUPINFOW si{};  si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;  si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi{};
		std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
		buf.push_back(L'\0');
		if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
		                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			ScaffoldJob job;
			job.projectName     = name;
			job.expectedDllPath = r.dllPath;
			job.sourceDir       = r.sourceDir;
			job.buildDir        = r.buildDir;
			job.hProcess        = pi.hProcess;
			job.hThread         = pi.hThread;
			job.logPath         = reconfigLog;
			job.startedAtSteady = std::chrono::steady_clock::now();
			mem.activeScaffolds.push_back(std::move(job));
			r.cmakeStarted = true;
		}
	}

	r.ok = true;
	const char* verb = dirAlreadyExists ? "Re-registering" : "Scaffolding";
	if (r.cmakeStarted)
	{
		r.message = std::string{verb} + " '" + name + "' — cmake configure running…";
	}
	else
	{
		r.message = std::string{verb} + " '" + name + "' (no vcvars: cmake configure skipped). "
		            "Run cmake -S . -B build manually to enable [Build].";
	}
	return r;
}

bool isScaffoldActive(const LauncherMemory& mem)
{
	return !mem.activeScaffolds.empty();
}

/// on_update から毎フレーム呼ばれる。完了した scaffold cmake-configure
/// プロセスを回収し、完了 flash を push し、scaffold したプロジェクトを
/// 完全ビルド可能エントリとして採用する (scaffoldStart では buildDir 空で
/// 登録された。ここで埋める)。
void reapFinishedScaffolds(LauncherMemory& mem,
                           mitiru::module::FrameIntents* intents);  // 前方宣言

// ── CEF への state push ────────────────────────────────────────────────

void pushProjectsState(LauncherMemory& mem,
                      mitiru::module::FrameIntents* intents)
{
	nlohmann::json arr = nlohmann::json::array();
	for (std::size_t i = 0; i < mem.projects.size(); ++i)
	{
		const auto& p = mem.projects[i];
		bool runningRun = false, runningWatch = false;
		for (const auto& c : mem.children)
		{
			if (c.projectName == p.name)
			{
				if (c.watching) { runningWatch = true; } else { runningRun = true; }
			}
		}
		nlohmann::json entry = {
			{"index",        static_cast<int>(i)},
			{"name",         p.name},
			{"dllPath",      p.dllPath},
			{"runningRun",   runningRun},
			{"runningWatch", runningWatch},
			{"buildEnabled", !p.buildDir.empty() && !p.buildTarget.empty()},
			{"buildActive",  isBuildActive(mem, p.name)},
		};
		if (auto it = mem.lastBuilds.find(p.name); it != mem.lastBuilds.end())
		{
			entry["buildLast"] = {
				{"ok",         it->second.ok},
				{"exitCode",   it->second.exitCode},
				{"durationMs", it->second.durationMs},
				{"completedAt", static_cast<long long>(it->second.completedAt)},
			};
		}
		arr.push_back(std::move(entry));
	}
	mem.scratchJson = arr.dump();
	pushStateString(intents, "view.launcher.projects", mem.scratchJson);
}

void pushRecentsState(LauncherMemory& mem,
                     mitiru::module::FrameIntents* intents)
{
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& r : mem.recents)
	{
		arr.push_back({
			{"projectName", r.projectName},
			{"startedAt",   static_cast<long long>(r.startedAt)},
			{"endedAt",     static_cast<long long>(r.endedAt)},
			{"exitCode",    r.exitCode},
			{"watching",    r.watching},
		});
	}
	mem.scratchJson = arr.dump();
	pushStateString(intents, "view.launcher.recents", mem.scratchJson);
}

void pushEnvState(LauncherMemory& mem,
                  mitiru::module::FrameIntents* intents)
{
	nlohmann::json env = {
		{"engineRoot",   mem.engineRoot.has_value() ? mem.engineRoot->string() : ""},
		{"canScaffold",  mem.engineRoot.has_value()},
		{"vcvarsFound",  mem.vcvarsBat.has_value()},
	};
	mem.scratchJson = env.dump();
	pushStateString(intents, "view.launcher.env", mem.scratchJson);
}

void pushFlash(LauncherMemory& mem,
               mitiru::module::FrameIntents* intents,
               const std::string& kind, const std::string& message)
{
	nlohmann::json j = {
		{"kind",    kind},     // "ok" | "error" | "info"  種別
		{"message", message},
		{"at",      static_cast<long long>(std::time(nullptr))},
	};
	mem.scratchJson = j.dump();
	pushStateString(intents, "view.launcher.flash", mem.scratchJson);
}

// ── Action event の dispatch (CEF → DLL) ───────────────────────────────

void processActionEvents(LauncherMemory& mem,
                         const mitiru::module::InputSnapshot* input,
                         mitiru::module::FrameIntents* intents)
{
	for (std::int32_t i = 0; i < input->actionEventCount; ++i)
	{
		const auto& ev = input->actionEvents[i];
		const std::string name{ev.name};

		nlohmann::json payload;
		try { payload = nlohmann::json::parse(std::string{ev.payloadJson}); }
		catch (...) { payload = nlohmann::json::object(); }

		if (name == "launcher.add_project")
		{
			if (auto picked = pickDllFile(); picked.has_value())
			{
				Project p;
				std::filesystem::path path{*picked};
				p.name    = path.stem().string();
				p.dllPath = *picked;
				inferProjectContext(p);
				mem.projects.push_back(std::move(p));
				saveProjects(mem);
			}
		}
		else if (name == "launcher.remove_project")
		{
			const int idx = payload.value("index", -1);
			if (idx >= 0 && idx < static_cast<int>(mem.projects.size()))
			{
				mem.projects.erase(mem.projects.begin() + idx);
				saveProjects(mem);
			}
		}
		else if (name == "launcher.open")
		{
			const int idx = payload.value("index", -1);
			if (idx < 0 || idx >= static_cast<int>(mem.projects.size())) { continue; }
			const auto& proj = mem.projects[idx];

			// DLL が未ビルドなら、先に auto-build してから open を試みる。
			// 「scaffold した、さあ動かそう」という自然な流れに合わせる。
			std::error_code dllEc;
			const bool dllExists =
				!proj.dllPath.empty() &&
				std::filesystem::exists(proj.dllPath, dllEc);

			if (!dllExists)
			{
				if (proj.buildDir.empty() || proj.buildTarget.empty())
				{
					pushFlash(mem, intents, "error",
						"DLL not built and no build dir configured for '" + proj.name + "'. "
						"Build it with cmake first.");
					continue;
				}
				if (isBuildActive(mem, proj.name))
				{
					pushFlash(mem, intents, "info",
						"'" + proj.name + "' is already building — will open when done.");
					mem.pendingOpenAfterBuild.push_back(proj.name);
					continue;
				}
				if (!spawnBuild(mem, proj))
				{
					std::string why = "Auto-build failed to start. ";
					if (!mem.vcvarsBat.has_value()) {
						why += "vcvars64.bat not found — install Visual Studio 2022.";
					}
					pushFlash(mem, intents, "error", why);
					continue;
				}
				mem.pendingOpenAfterBuild.push_back(proj.name);
				pushFlash(mem, intents, "info",
					"Building '" + proj.name + "' — will open when ready.");
				continue;
			}

			// 通常経路: DLL ビルド済み、game + companion を起動。
			const DWORD pid = spawnChildGame(mem, proj);
			if (pid == 0)
			{
				pushFlash(mem, intents, "error",
					"Failed to launch '" + proj.name + "' — CreateProcess error.");
				continue;
			}
			std::filesystem::path sf = writeSessionFile(proj, pid);
			if (!sf.empty()) { spawnCompanion(mem, proj, sf); }

			// launcher は一時的な picker — atomic-tools 哲学では dev session へ
			// 引き渡した後に画面に居座るべきでない。自分を閉じる。別プロジェクトを
			// 選びたい時、ユーザーは .bat (または将来 companion の "Switch project"
			// メニュー) から再起動する。
			intents->requestStop = 1;
		}
		else if (name == "launcher.open_folder")
		{
			const int idx = payload.value("index", -1);
			if (idx >= 0 && idx < static_cast<int>(mem.projects.size()))
			{
				const auto& p = mem.projects[idx];
				std::filesystem::path dir = p.sourceDir.empty()
					? std::filesystem::path{p.dllPath}.parent_path()
					: std::filesystem::path{p.sourceDir};
				openInExplorer(dir);
			}
		}
		else if (name == "launcher.scaffold")
		{
			const std::string newName = payload.value("name", "");
			// 他の scaffold 実行中なら拒否 — 今は single-slot。
			if (isScaffoldActive(mem))
			{
				pushFlash(mem, intents, "info",
					"A scaffold is already running — wait for it to finish.");
				continue;
			}
			auto r = scaffoldStart(mem, newName);
			if (r.ok)
			{
				// プロジェクトを今すぐ登録 (cmake configure 完了前でも) し、
				// リストに即座に現れるようにする。cmake が始まらない時は buildDir を
				// 空のままにし、configure 成功時に reapFinishedScaffolds() が埋める。
				Project p;
				p.name        = newName;
				p.dllPath     = r.dllPath;
				p.sourceDir   = r.sourceDir;
				p.buildTarget = r.buildTarget;
				if (!r.cmakeStarted)
				{
					p.buildDir = r.buildDir; // configure を飛ばす → Build を即有効化 (ユーザーが手動で cmake 再実行)
				}
				mem.projects.push_back(std::move(p));
				saveProjects(mem);
				pushFlash(mem, intents, r.cmakeStarted ? "info" : "ok", r.message);
			}
			else
			{
				pushFlash(mem, intents, "error", r.message);
			}
		}
		else if (name == "launcher.show_build_log")
		{
			const std::string projName = payload.value("name", "");
			if (auto it = mem.lastBuilds.find(projName); it != mem.lastBuilds.end())
			{
				openLogInNotepad(it->second.logPath);
			}
		}
	}
}

// ── モジュールコールバック ─────────────────────────────────────────────────

void launcher_on_init(void* memory)
{
	if (memory == nullptr) { return; }
	auto& mem = *static_cast<LauncherMemory*>(memory);

	mem.appDataDir   = resolveAppDataDir();
	mem.projectsFile = mem.appDataDir / "projects.json";
	mem.hostExePath  = resolveHostExePath();
	if (!mem.hostExePath.empty()) { mem.hostExeDir = mem.hostExePath.parent_path(); }
	mem.engineRoot   = detectEngineRoot(mem.hostExeDir);

	loadProjects(mem);
	// build 情報を持たない (旧 schema) プロジェクトに補完する。
	for (auto& p : mem.projects) { inferProjectContext(p); }
}

void launcher_on_update(void* memory, float /*dt*/,
                        const mitiru::module::InputSnapshot* input,
                        mitiru::module::FrameIntents* intents)
{
	if (memory == nullptr || input == nullptr || intents == nullptr) { return; }
	auto& mem = *static_cast<LauncherMemory*>(memory);

	reapExitedChildren(mem);
	reapFinishedBuilds(mem, intents);
	reapFinishedScaffolds(mem, intents);
	processActionEvents(mem, input, intents);

	if (mem.firstPush || ++mem.pushTick >= 6)
	{
		mem.pushTick  = 0;
		mem.firstPush = false;
		pushEnvState(mem, intents);
		pushProjectsState(mem, intents);
		pushRecentsState(mem, intents);
	}
}

void launcher_on_draw(void* memory, mitiru::Screen* screen)
{
	if (memory == nullptr || screen == nullptr) { return; }
	screen->clear(sgc::Colorf{0.07f, 0.08f, 0.12f, 1.0f});
}

void launcher_on_shutdown(void* memory)
{
	if (memory == nullptr) { return; }
	auto& mem = *static_cast<LauncherMemory*>(memory);
	if (!mem.projectsFile.empty()) { saveProjects(mem); }
	for (auto& c : mem.children)
	{
		if (c.hProcess) { CloseHandle(c.hProcess); }
		if (c.hThread)  { CloseHandle(c.hThread); }
	}
	mem.children.clear();
	for (auto& b : mem.activeBuilds)
	{
		if (b.hProcess) { CloseHandle(b.hProcess); }
		if (b.hThread)  { CloseHandle(b.hThread); }
	}
	mem.activeBuilds.clear();
	for (auto& s : mem.activeScaffolds)
	{
		if (s.hProcess) { CloseHandle(s.hProcess); }
		if (s.hThread)  { CloseHandle(s.hThread); }
	}
	mem.activeScaffolds.clear();
}

// 行外定義 (上で前方宣言済み。Project + memory アクセスが必要)。
void reapFinishedScaffolds(LauncherMemory& mem,
                           mitiru::module::FrameIntents* intents)
{
	for (auto it = mem.activeScaffolds.begin(); it != mem.activeScaffolds.end(); )
	{
		const DWORD wait = WaitForSingleObject(it->hProcess, 0);
		if (wait != WAIT_OBJECT_0) { ++it; continue; }

		DWORD exitCode = 0;
		GetExitCodeProcess(it->hProcess, &exitCode);
		const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - it->startedAtSteady).count();

		// 該当プロジェクトの buildDir を埋めて [Build] を有効化する。
		for (auto& p : mem.projects)
		{
			if (p.name == it->projectName)
			{
				if (exitCode == 0) { p.buildDir = it->buildDir; }
				break;
			}
		}
		saveProjects(mem);

		if (exitCode == 0)
		{
			pushFlash(mem, intents, "ok",
				"Scaffold ready: '" + it->projectName +
				"' — click [Build] (" + std::to_string(ms / 1000) + "s configure).");
		}
		else
		{
			pushFlash(mem, intents, "error",
				"Scaffold cmake configure FAILED (exit " +
				std::to_string(exitCode) + "). Log: " + it->logPath.string());
		}

		CloseHandle(it->hProcess);
		CloseHandle(it->hThread);
		it = mem.activeScaffolds.erase(it);
	}
}

}  // namespace mitiru_launcher

// ── DLL エクスポート ──────────────────────────────────────────────────────

extern "C"
{

__declspec(dllexport)
void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)
{
	if (api == nullptr || memory == nullptr) { return; }
	if (*memory == nullptr)
	{
		*memory = new mitiru_launcher::LauncherMemory{};
	}
	api->version     = mitiru::module::kCurrentApiVersion;
	api->on_init     = &mitiru_launcher::launcher_on_init;
	api->on_update   = &mitiru_launcher::launcher_on_update;
	api->on_draw     = &mitiru_launcher::launcher_on_draw;
	api->on_shutdown = &mitiru_launcher::launcher_on_shutdown;
}

__declspec(dllexport)
void mitiru_module_unload(void* /*memory*/) {}

}  // extern "C"
