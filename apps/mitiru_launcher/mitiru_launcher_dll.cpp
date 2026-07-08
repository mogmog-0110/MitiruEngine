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

constexpr const char* kDllTemplate = R"CPP(// {NAME} — mitiru_launcher の [+ New project] が生成した最初のゲーム。
//
// あそびかた: 矢印キー / WASD / パッド左スティックで四角を動かし、
//             光る的に触れるとスコア +1。R でやり直し、ESC で終了。
// 実行:       mitiru_host.exe {NAME}/{NAME}.dll
//
// このエンジンの大事な約束は 3 つだけ:
//   1. ゲームの状態は 1 つの struct (ここでは MyGame) に全部入れる。
//      ポインタ・std::vector・std::string は入れない (= flat POD)。
//      host が MyGame をバイト列としてまるごと記録するので、これだけで
//      巻き戻し (time-travel)・リプレイ・セーブが動く。
//      伸びる配列は mitiru::FixedVec、文字列は mitiru::FixedString を使う。
//   2. 状態を書き換えるのは update() だけ。draw() は「見て描くだけ」(const)。
//   3. 乱数は seed 固定の mitiru::Random を状態の中に持つ。
//      同じ入力をもう一度流せば、必ず同じゲームが再現される (決定論)。
//
// 決定論の確かめかた (録画したものが寸分違わず再生されるかの検証):
//   mitiru_host.exe {NAME}/{NAME}.dll --record run.mtrr
//   mitiru_host.exe {NAME}/{NAME}.dll --replay-test run.mtrr

#include <algorithm>   // std::clamp
#include <cmath>       // std::cos / std::sin
#include <cstdint>

#include <mitiru.hpp>                // エンジンの入口 (Input / Hud / Screen / Random / MITIRU_GAME)
#include <mitiru/core/FixedVec.hpp>  // FixedVec / FixedString (vector / string の flat POD 版)

using namespace mitiru;

// ── ここの数値を書き換えて保存すると、ホットリロードで即反映される ────────────
constexpr float kScreenW    = 1280.0f;  // 論理解像度 (host 既定のウィンドウと同じ)
constexpr float kScreenH    = 720.0f;
constexpr float kSpeed      = 420.0f;   // プレイヤーの移動速度 (px/秒)
constexpr float kPlayerSize = 36.0f;    // プレイヤーの一辺 (px)
constexpr float kTargetR    = 22.0f;    // 的の半径 (px)
constexpr int   kMaxSparks  = 64;       // 火花の同時上限 (上限は型に焼き込む)

// 的を取った時に散る火花 1 粒 (ただのデータ)。
struct Spark { float x, y, vx, vy, life; };

// ── ゲームの状態。全部この struct に置く (まるごとコピーできる flat POD) ──────
struct MyGame
{
	float         playerX    = kScreenW * 0.5f;   // プレイヤーの中心座標
	float         playerY    = kScreenH * 0.5f;
	float         targetX    = kScreenW * 0.25f;  // 的の中心座標
	float         targetY    = kScreenH * 0.35f;
	int           score      = 0;
	float         flashTimer = 0.0f;              // 取った直後の演出の残り秒
	std::uint32_t frame      = 0;                 // 経過フレーム (巻き戻しグラフの横軸)
	FixedVec<Spark, kMaxSparks> sparks;           // std::vector<Spark> の代わり
	FixedString<48>             message;          // std::string の代わり (固定長・null 終端)
	Random        rng{1234};                      // seed 固定 → リプレイでも同じ乱数列

	// 毎フレーム呼ばれる。in = 入力、hud = HTML UI / 音、dt = 前フレームからの経過秒。
	void update(Input in, Hud hud, float dt)
	{
		++frame;
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::R)) { *this = MyGame{}; return; }  // 状態を丸ごと作りなおす = リスタート

		// ① 移動 (矢印 + WASD + パッドを in.move() が 1 本に合成してくれる)
		playerX += in.move().x * kSpeed * dt;
		playerY += in.move().y * kSpeed * dt;
		playerX = std::clamp(playerX, kPlayerSize * 0.5f, kScreenW - kPlayerSize * 0.5f);
		playerY = std::clamp(playerY, kPlayerSize * 0.5f, kScreenH - kPlayerSize * 0.5f);

		// ② 的に触れたらスコア +1。的は乱数で引っ越し、火花を散らす
		const float dx = targetX - playerX, dy = targetY - playerY;
		const float reach = kTargetR + kPlayerSize * 0.5f;
		if (dx * dx + dy * dy < reach * reach)
		{
			score += 1;
			flashTimer = 0.3f;
			spawnSparks();
			targetX = rng.nextFloat(80.0f, kScreenW - 80.0f);
			targetY = rng.nextFloat(80.0f, kScreenH - 80.0f);
		}

		// ③ 火花を進め、寿命が尽きた粒から消す
		if (flashTimer > 0.0f) { flashTimer -= dt; }
		for (int i = 0; i < static_cast<int>(sparks.size()); )
		{
			Spark& sp = sparks[i];
			sp.x += sp.vx * dt;
			sp.y += sp.vy * dt;
			sp.life -= dt;
			if (sp.life <= 0.0f) { sparks.removeAt(i); continue; }  // swap-remove (O(1))
			++i;
		}

		// ④ HTML の HUD へ値を送る (assets/scene.html の data-m-text が受け取る)
		hud.set("view.hud.score", score);
		message.set(score >= 10 ? "nice!  R = restart" : "arrows / WASD = move");
	}

	// 的を取った瞬間、火花を 12 粒まく。向きも速さも seed 済み乱数から引くので再現できる。
	void spawnSparks()
	{
		for (int i = 0; i < 12; ++i)
		{
			const float ang = rng.nextFloat(0.0f, 6.2831853f);
			const float spd = rng.nextFloat(120.0f, 320.0f);
			Spark sp{};
			sp.x    = targetX;
			sp.y    = targetY;
			sp.vx   = std::cos(ang) * spd;
			sp.vy   = std::sin(ang) * spd;
			sp.life = 0.5f;
			if (!sparks.push_back(sp)) { break; }  // 満杯なら追加しない (途中でヒープ確保もしない)
		}
	}

	// 毎フレーム呼ばれる。末尾の const が「描くだけで状態は書かない」という約束 —
	// draw() で状態を書くとリプレイ・巻き戻しの再現が崩れるので、コンパイラに守らせる。
	void draw(Screen& s) const
	{
		s.fillScreen(flashTimer > 0.0f ? hex(0x1C2A3E) : hex(0x141826));

		// 的 (外輪 + 芯)
		s.fillCircle(targetX, targetY, kTargetR, color::Yellow);
		s.fillCircle(targetX, targetY, kTargetR * 0.45f, color::White);

		// 火花 (寿命が残っているほど大きく描く)
		for (const Spark& sp : sparks)
		{
			s.fillCircle(sp.x, sp.y, 2.0f + sp.life * 6.0f, color::Orange);
		}

		// プレイヤー
		s.drawRect(playerX - kPlayerSize * 0.5f, playerY - kPlayerSize * 0.5f,
		           kPlayerSize, kPlayerSize, color::Cyan);

		// 画面下の操作ヒント。drawTextInRect は枠に収めて描く (はみ出さない) テキスト API。
		s.drawTextInRect(Rect{0.0f, kScreenH - 48.0f, kScreenW, 32.0f},
		                 message.c_str(), color::Gray, 18.0f,
		                 Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
	}
};

// ── 巻き戻し / inspector への申告 ────────────────────────────────────────────
// 状態からスカラを 1 個引く関数 = inspector の巻き戻しグラフの 1 本になる。
double scoreProbe(const void* m) { return static_cast<const MyGame*>(m)->score; }

// 状態の主要フィールドを host に申告する。inspector / AI が窓を開かずに全状態を
// 構造的に読めるようになる。要素 struct (Spark) を先に宣言する。
MITIRU_REFLECT_STRUCT(Spark, x, y, vx, vy, life);
MITIRU_REFLECT(MyGame, playerX, playerY, targetX, targetY, score, flashTimer,
               frame, sparks, message);

// これ 1 つで DLL の入口 (load / unload)・状態バイト数の申告・巻き戻し観測が
// そろう。MyGame が flat POD でない場合はここが compile error になって教えてくれる。
MITIRU_GAME_SERIES(MyGame,
	{ "score", "Score", &scoreProbe, 0.0, 0 });
)CPP";

constexpr const char* kCMakeTemplate = R"CMAKE(# {NAME} — mitiru_launcher の [+ New project] が生成。
# SHARED でビルドし、mitiru_host.exe がロードする game DLL になる。
#
# 実行:  mitiru_host.exe {NAME}/{NAME}.dll

add_library({NAME} SHARED {NAME}.cpp)
target_link_libraries({NAME} PRIVATE mitiru)

if(MSVC)
	target_compile_options({NAME} PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/{NAME}")

	set_target_properties({NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_runtime_dir}")

	# assets/ (scene.html 等) を source-tracked stamp 経由で deploy する。
	# POST_BUILD 直付けは DLL が dirty な時しか走らず、HTML だけ編集した変更を
	# 取りこぼすため stamp 方式にする。
	file(GLOB_RECURSE _assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_assets_stamp "${CMAKE_CURRENT_BINARY_DIR}/{NAME}_assets.stamp")
	add_custom_command(
		OUTPUT  "${_assets_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_assets_stamp}"
		DEPENDS ${_assets}
		VERBATIM
		COMMENT "{NAME}: deploying assets/ (source-tracked)")
	add_custom_target({NAME}_assets ALL DEPENDS "${_assets_stamp}")
	add_dependencies({NAME} {NAME}_assets)

	# data-m-* 属性の値を DOM に反映するエンジン付属 JS。page 側に手書き JS は不要。
	set(_cefjs
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_cef_state.js"
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind.js")
	set(_cefjs_stamp "${CMAKE_CURRENT_BINARY_DIR}/{NAME}_cefjs.stamp")
	add_custom_command(
		OUTPUT  "${_cefjs_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cefjs}
			"${_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_cefjs_stamp}"
		DEPENDS ${_cefjs}
		VERBATIM
		COMMENT "{NAME}: deploying mitiru_runtime JS (source-tracked)")
	add_custom_target({NAME}_cefjs ALL DEPENDS "${_cefjs_stamp}")
	add_dependencies({NAME} {NAME}_cefjs)

	add_dependencies({NAME} mitiru_host)
endif()
)CMAKE";

constexpr const char* kSceneHtmlTemplate = R"HTML(<!doctype html>
<html lang="ja">
<head>
	<meta charset="utf-8">
	<title>{NAME}</title>
	<style>
		/* ゲーム窓に重なる透明 HUD。絵は C++ (draw)、文字 UI はこの HTML が担当 */
		html, body { margin: 0; padding: 0; height: 100vh; width: 100vw;
			background: transparent; color: #eef; font-family: sans-serif;
			overflow: hidden; pointer-events: none; }
		.score { position: absolute; top: 16px; left: 22px;
			font-size: 28px; font-weight: 700; letter-spacing: 0.06em; }
		.score small { font-size: 14px; opacity: 0.55; margin-right: 10px; }
	</style>
</head>
<body>
	<!-- C++ の hud.set("view.hud.score", ...) の値がここに入る (手書き JS 不要) -->
	<div class="score"><small>SCORE</small><span data-m-text="view.hud.score">0</span></div>

	<!-- 値を DOM に反映するエンジン付属の JS (CMakeLists が assets/mitiru_runtime/ へ copy) -->
	<script src="mitiru_runtime/mitiru_cef_state.js"></script>
	<script src="mitiru_runtime/mitiru_bind.js"></script>
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
	api->version     = mitiru::module::kWireApiVersion;  // 数値 + build 指紋 (H-1/H-4)
	api->on_init     = &mitiru_launcher::launcher_on_init;
	api->on_update   = &mitiru_launcher::launcher_on_update;
	api->on_draw     = &mitiru_launcher::launcher_on_draw;
	api->on_shutdown = &mitiru_launcher::launcher_on_shutdown;
}

__declspec(dllexport)
void mitiru_module_unload(void* memory)
{
	if (memory == nullptr) { return; }
	// 残存する子プロセス / build job の HANDLE を閉じてから解放する
	// (shutdown 済みなら各 vector は空で no-op)。
	mitiru_launcher::launcher_on_shutdown(memory);
	delete static_cast<mitiru_launcher::LauncherMemory*>(memory);
}

}  // extern "C"
