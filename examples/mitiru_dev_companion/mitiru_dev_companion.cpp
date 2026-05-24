// mitiru_dev_companion — compact "dev console" bar spawned alongside a
// game in watch-mode. Hosts the [Build] / [Inspector] / [Stop] actions
// that used to live in the launcher's project rows.
//
// Atomic-tools rationale: launcher = picker, companion = active-session
// controls. Mixing them produced "what does Run vs Watch vs Build do?"
// confusion (user feedback 2026-05-21).
//
// Session handshake (launcher → companion):
//   When the launcher's [Open] spawns a game + companion, it first writes
//   %TEMP%/mitiru_dev_session_<pid>.json containing:
//     { gamePid, projectName, projectDll, buildDir, buildTarget }
//   The launcher passes the file path to the companion as the THIRD CLI
//   arg of mitiru_host (after the DLL path and any --flags). The companion
//   reads it from MITIRU_COMPANION_SESSION env var (set by the launcher).
//
// Lifecycle:
//   - on_init: read session file, resolve game-process HANDLE, set the
//     companion window to topmost
//   - on_update: poll the game HANDLE; if game exits, intents->requestStop
//     to close ourselves
//   - actions: companion.build (async cmake), companion.stop (TerminateProcess
//     on game), companion.inspector (spawn mitiru_inspector.exe)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/input/Keys.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru_dev_companion
{

constexpr const char* kSessionEnvVar = "MITIRU_COMPANION_SESSION";

struct Session
{
	std::string projectName;
	std::string projectDll;
	std::string buildDir;
	std::string buildTarget;
	DWORD       gamePid {0};
};

struct BuildJob
{
	HANDLE      hProcess {nullptr};
	HANDLE      hThread  {nullptr};
	std::filesystem::path logPath;
	std::chrono::steady_clock::time_point startedAtSteady;
};

struct BuildResult
{
	bool        attempted   {false};
	bool        ok          {false};
	int         exitCode    {-1};
	long long   durationMs  {0};
	std::filesystem::path logPath;
};

struct CompanionMemory
{
	Session                 session;
	HANDLE                  hGame {nullptr};         // OpenProcess result on game pid
	bool                    gameStillAlive {true};   // updated each frame
	bool                    topmostApplied {false};  // EnumWindows once on first frame

	// Cached HWNDs for docking — refreshed each frame because either window
	// could be re-created (companion's CEF restart, game's hot-reload, ...).
	HWND                    selfHwnd {nullptr};
	HWND                    gameHwnd {nullptr};
	int                     dockTick {0};            // throttle SetWindowPos calls
	bool                    inspectorPositioned {false}; // first-position inspector once, then leave the user in control

	std::optional<std::filesystem::path> vcvarsBat;

	std::vector<BuildJob>   activeBuilds;            // single-slot in practice
	BuildResult             lastBuild;

	// Inspector child process — kept around as a toggle (one Inspector at a
	// time; clicking [Inspector] again closes it rather than spawning another).
	HANDLE                  inspectorProc {nullptr};
	HANDLE                  inspectorThread {nullptr};
	DWORD                   inspectorPid {0};

	// Push throttling (no C++-side dedup — JS handles it. Earlier we deduped
	// here too, which silently dropped state when the first push arrived
	// before scene.html had registered onStateChange handlers, leaving the
	// UI stuck on initial defaults forever.)
	int    pushTick      {0};
	bool   firstPush     {true};

	std::string scratchJson;
};

// ── Helpers ──────────────────────────────────────────────────────────

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

std::optional<std::filesystem::path> findVcvarsBat()
{
	const std::vector<std::wstring> candidates = {
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Preview\\VC\\Auxiliary\\Build\\vcvars64.bat",
	};
	for (const auto& w : candidates)
	{
		std::filesystem::path p{w};
		std::error_code ec;
		if (std::filesystem::exists(p, ec)) { return p; }
	}
	return std::nullopt;
}

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

// ── Session load ─────────────────────────────────────────────────────

void loadSession(CompanionMemory& mem)
{
	const char* envPath = std::getenv(kSessionEnvVar);
	if (envPath == nullptr || envPath[0] == '\0') { return; }
	std::filesystem::path p{envPath};
	std::error_code ec;
	if (!std::filesystem::exists(p, ec) || ec) { return; }

	std::ifstream ifs(p);
	if (!ifs) { return; }

	try
	{
		nlohmann::json j;
		ifs >> j;
		mem.session.projectName  = j.value("projectName", "");
		mem.session.projectDll   = j.value("projectDll", "");
		mem.session.buildDir     = j.value("buildDir", "");
		mem.session.buildTarget  = j.value("buildTarget", "");
		mem.session.gamePid      = static_cast<DWORD>(j.value("gamePid", 0));
	}
	catch (...) {}

	if (mem.session.gamePid > 0)
	{
		mem.hGame = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION,
		                        FALSE, mem.session.gamePid);
	}
}

// ── Topmost self ─────────────────────────────────────────────────────
//
// Companion wants always-on-top so the game window doesn't bury it. We
// don't have a clean engine API for that (yet), so do the Win32 dance
// directly: enumerate top-level windows owned by THIS process and call
// SetWindowPos on each one.

/// Find the first visible top-level window owned by `pid`. Returns nullptr
/// if none — caller should retry on subsequent frames (the target process
/// may not have created its window yet).
HWND findFirstVisibleWindow(DWORD pid)
{
	struct Ctx { DWORD pid; HWND result; };
	Ctx ctx{pid, nullptr};
	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
		auto* c = reinterpret_cast<Ctx*>(lParam);
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (windowPid == c->pid && IsWindowVisible(hwnd))
		{
			c->result = hwnd;
			return FALSE;  // stop — take the first one
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&ctx));
	return ctx.result;
}

void applyTopmostOnce(CompanionMemory& mem)
{
	if (mem.topmostApplied) { return; }
	if (mem.selfHwnd == nullptr)
	{
		mem.selfHwnd = findFirstVisibleWindow(GetCurrentProcessId());
	}
	if (mem.selfHwnd != nullptr)
	{
		SetWindowPos(mem.selfHwnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		mem.topmostApplied = true;
	}
}

/// Move companion so it sits above (or below if no room) the game window.
/// Width is FIXED (not matching game) — earlier "match game width" made
/// the bar 1280×80 on standard games = 16:1 visual aspect, ugly.
/// Throttled to ~6Hz so the user can still nudge it manually between updates.
void dockToGameWindow(CompanionMemory& mem)
{
	if (++mem.dockTick < 10) { return; }  // ~6Hz at 60fps
	mem.dockTick = 0;

	if (mem.session.gamePid == 0) { return; }

	if (mem.selfHwnd == nullptr)
	{
		mem.selfHwnd = findFirstVisibleWindow(GetCurrentProcessId());
	}
	mem.gameHwnd = findFirstVisibleWindow(mem.session.gamePid);
	if (mem.selfHwnd == nullptr || mem.gameHwnd == nullptr) { return; }

	RECT gameRect{}, selfRect{};
	if (!GetWindowRect(mem.gameHwnd, &gameRect)) { return; }
	if (!GetWindowRect(mem.selfHwnd, &selfRect)) { return; }

	const int myHeight  = selfRect.bottom - selfRect.top;
	const int gameWidth = gameRect.right - gameRect.left;
	constexpr int kCompanionFixedWidth = 700;
	const int myWidth = (gameWidth < kCompanionFixedWidth) ? gameWidth : kCompanionFixedWidth;

	// Prefer placing the bar immediately above the game window. If there's
	// not enough room (game pinned to top of screen / multi-monitor edge),
	// fall back to placing it just below the game's bottom edge.
	int newTop = gameRect.top - myHeight;
	if (newTop < 0) { newTop = gameRect.bottom; }

	SetWindowPos(mem.selfHwnd, HWND_TOPMOST,
		gameRect.left, newTop,
		myWidth, myHeight,
		SWP_NOACTIVATE);
}

/// First-time positioning for the inspector window — drop it just to the
/// right of the game window so the user can see it without hunting. Only
/// fires once per Inspector lifetime; subsequent user drags are respected.
void positionInspectorIfNew(CompanionMemory& mem)
{
	if (mem.inspectorPid == 0 || mem.inspectorPositioned) { return; }
	HWND inspectorHwnd = findFirstVisibleWindow(mem.inspectorPid);
	if (inspectorHwnd == nullptr) { return; }  // not visible yet, retry next frame

	if (mem.gameHwnd == nullptr)
	{
		mem.gameHwnd = findFirstVisibleWindow(mem.session.gamePid);
	}
	int targetLeft = 100, targetTop = 100, targetW = 500, targetH = 540;
	RECT gameRect{};
	if (mem.gameHwnd != nullptr && GetWindowRect(mem.gameHwnd, &gameRect))
	{
		targetLeft = gameRect.right + 8;
		targetTop  = gameRect.top;
		targetH    = (gameRect.bottom - gameRect.top);
		if (targetH < 400) { targetH = 540; }
	}
	SetWindowPos(inspectorHwnd, nullptr, targetLeft, targetTop, targetW, targetH,
		SWP_NOZORDER | SWP_NOACTIVATE);
	mem.inspectorPositioned = true;
}

// ── Game-process polling ─────────────────────────────────────────────

void pollGameAlive(CompanionMemory& mem, mitiru::module::FrameIntents* intents)
{
	if (mem.hGame == nullptr)
	{
		// No session = nothing to chaperone. Stay alive but inert.
		mem.gameStillAlive = false;
		return;
	}
	const DWORD wait = WaitForSingleObject(mem.hGame, 0);
	if (wait == WAIT_OBJECT_0)
	{
		// Game exited — close ourselves so we don't linger as orphans.
		mem.gameStillAlive = false;
		intents->requestStop = 1;
	}
}

// ── Build job ────────────────────────────────────────────────────────

bool spawnBuild(CompanionMemory& mem)
{
	if (mem.session.buildDir.empty() || mem.session.buildTarget.empty())
	{
		return false;
	}
	if (!mem.vcvarsBat.has_value()) { mem.vcvarsBat = findVcvarsBat(); }
	if (!mem.vcvarsBat.has_value()) { return false; }

	std::filesystem::path logPath =
		std::filesystem::temp_directory_path() /
		("mitiru_build_" + mem.session.projectName + ".log");

	std::wstring vcvarsW    = mem.vcvarsBat->wstring();
	std::wstring buildDirW  = utf8ToUtf16(mem.session.buildDir);
	std::wstring targetW    = utf8ToUtf16(mem.session.buildTarget);
	std::wstring logPathW   = logPath.wstring();
	std::wstring inner =
		L"call \"" + vcvarsW + L"\" >nul && "
		L"cmake --build \"" + buildDirW + L"\" --config Debug --target " + targetW +
		L" > \"" + logPathW + L"\" 2>&1";
	std::wstring cmdLine = L"cmd.exe /c \"" + inner + L"\"";

	STARTUPINFOW si{};  si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;  si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi{};
	std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
	buf.push_back(L'\0');
	if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
	                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		return false;
	}
	BuildJob job;
	job.hProcess        = pi.hProcess;
	job.hThread         = pi.hThread;
	job.logPath         = logPath;
	job.startedAtSteady = std::chrono::steady_clock::now();
	mem.activeBuilds.push_back(std::move(job));
	return true;
}

void reapBuilds(CompanionMemory& mem)
{
	for (auto it = mem.activeBuilds.begin(); it != mem.activeBuilds.end(); )
	{
		const DWORD wait = WaitForSingleObject(it->hProcess, 0);
		if (wait != WAIT_OBJECT_0) { ++it; continue; }

		DWORD exitCode = 0;
		GetExitCodeProcess(it->hProcess, &exitCode);
		mem.lastBuild.attempted  = true;
		mem.lastBuild.ok         = (exitCode == 0);
		mem.lastBuild.exitCode   = static_cast<int>(exitCode);
		mem.lastBuild.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - it->startedAtSteady).count();
		mem.lastBuild.logPath    = it->logPath;

		CloseHandle(it->hProcess);
		CloseHandle(it->hThread);
		it = mem.activeBuilds.erase(it);
	}
}

bool isBuildActive(const CompanionMemory& mem)
{
	return !mem.activeBuilds.empty();
}

// ── Inspector / Stop actions ────────────────────────────────────────

// Forward-declared so spawnInspector / stopGame can flash status messages.
void pushFlash(CompanionMemory& mem,
               mitiru::module::FrameIntents* intents,
               const std::string& kind, const std::string& message);

std::optional<std::filesystem::path> findInspectorExe(const std::filesystem::path& hostDir)
{
	// mitiru_inspector.exe can live in two places depending on layout:
	//   1. Next to mitiru_host.exe (release zip / clean install).
	//   2. Sibling examples/mitiru_inspector/ subdir (dev build tree, where
	//      each target has its own RUNTIME_OUTPUT_DIRECTORY).
	const std::vector<std::filesystem::path> candidates = {
		hostDir / "mitiru_inspector.exe",
		hostDir.parent_path() / "mitiru_inspector" / "mitiru_inspector.exe",
	};
	for (const auto& c : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(c, ec)) { return c; }
	}
	return std::nullopt;
}

/// Post WM_CLOSE to every top-level window owned by the given PID.
/// Used to gracefully tear down the inspector child process when the user
/// toggles it off (sending WM_CLOSE lets engine cleanup run, instead of
/// the violent TerminateProcess).
void closeProcessWindows(DWORD pid)
{
	struct Ctx { DWORD pid; };
	Ctx ctx{pid};
	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
		auto* c = reinterpret_cast<Ctx*>(lParam);
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (windowPid == c->pid && IsWindowVisible(hwnd))
		{
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&ctx));
}

bool isInspectorAlive(const CompanionMemory& mem)
{
	if (mem.inspectorProc == nullptr) { return false; }
	const DWORD wait = WaitForSingleObject(mem.inspectorProc, 0);
	return wait != WAIT_OBJECT_0;
}

void reapInspector(CompanionMemory& mem)
{
	// Detect user-driven close (they hit X on the inspector window). We need
	// to drop our handles so the next [Inspector] click respawns instead of
	// trying to talk to a dead process.
	if (mem.inspectorProc != nullptr && !isInspectorAlive(mem))
	{
		CloseHandle(mem.inspectorProc);
		if (mem.inspectorThread) { CloseHandle(mem.inspectorThread); }
		mem.inspectorProc        = nullptr;
		mem.inspectorThread      = nullptr;
		mem.inspectorPid         = 0;
		mem.inspectorPositioned  = false;  // reset so next spawn re-positions
	}
}

void toggleInspector(CompanionMemory& mem,
                     mitiru::module::FrameIntents* intents)
{
	// Toggle behaviour: if Inspector is currently up, close it. Otherwise
	// spawn a new one. Keeps the desktop from accumulating clutter when the
	// user button-mashes [Inspector].
	if (isInspectorAlive(mem))
	{
		closeProcessWindows(mem.inspectorPid);
		// Don't block — the next reapInspector() call (next frame) will
		// notice the process exited and clean handles.
		// No flash: the [Inspector]/[Hide Inspector] button label already
		// reflects the state change — a toast saying "closed" is redundant.
		return;
	}

	if (mem.session.gamePid == 0)
	{
		pushFlash(mem, intents, "error", "No game PID — Inspector needs an active session.");
		return;
	}
	wchar_t hostExeBuf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, hostExeBuf, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) { return; }
	std::filesystem::path hostDir = std::filesystem::path{std::wstring{hostExeBuf, n}}.parent_path();

	auto inspectorExe = findInspectorExe(hostDir);
	if (!inspectorExe.has_value())
	{
		pushFlash(mem, intents, "error",
			"mitiru_inspector.exe not found — build the mitiru_inspector target.");
		return;
	}

	std::wstring cmdLine = L"\"" + inspectorExe->wstring() + L"\" " +
		std::to_wstring(mem.session.gamePid);
	std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
	buf.push_back(L'\0');

	STARTUPINFOW si{};  si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessW(inspectorExe->wstring().c_str(), buf.data(),
	                               nullptr, nullptr, FALSE,
	                               CREATE_NEW_PROCESS_GROUP, nullptr,
	                               inspectorExe->parent_path().wstring().c_str(),
	                               &si, &pi);
	if (!ok)
	{
		pushFlash(mem, intents, "error",
			"CreateProcess failed for mitiru_inspector.exe.");
		return;
	}
	mem.inspectorProc   = pi.hProcess;
	mem.inspectorThread = pi.hThread;
	mem.inspectorPid    = pi.dwProcessId;
	// No flash on success — the button label flips to "Hide Inspector" and the
	// inspector window itself appears, so a "opened" toast adds nothing.
	// Error paths above still flash (failures aren't visible from the button).
}

void stopGame(CompanionMemory& mem)
{
	if (mem.hGame != nullptr)
	{
		TerminateProcess(mem.hGame, 0);
	}
}

// ── State push ──────────────────────────────────────────────────────

void pushEnvState(CompanionMemory& mem,
                  mitiru::module::FrameIntents* intents)
{
	nlohmann::json env = {
		{"projectName",   mem.session.projectName},
		{"projectDll",    mem.session.projectDll},
		{"buildTarget",   mem.session.buildTarget},
		{"buildEnabled",  !mem.session.buildDir.empty() && !mem.session.buildTarget.empty() && mem.vcvarsBat.has_value()},
		{"gamePid",       static_cast<long long>(mem.session.gamePid)},
		{"gameAlive",     mem.gameStillAlive},
		{"inspectorOpen", isInspectorAlive(mem)},
	};
	mem.scratchJson = env.dump();
	pushStateString(intents, "view.companion.env", mem.scratchJson);
}

void pushBuildState(CompanionMemory& mem,
                    mitiru::module::FrameIntents* intents)
{
	nlohmann::json j = {
		{"active",      isBuildActive(mem)},
		{"attempted",   mem.lastBuild.attempted},
	};
	if (mem.lastBuild.attempted)
	{
		j["ok"]         = mem.lastBuild.ok;
		j["exitCode"]   = mem.lastBuild.exitCode;
		j["durationMs"] = mem.lastBuild.durationMs;
	}
	mem.scratchJson = j.dump();
	pushStateString(intents, "view.companion.build", mem.scratchJson);
}

void pushFlash(CompanionMemory& mem,
               mitiru::module::FrameIntents* intents,
               const std::string& kind, const std::string& message)
{
	nlohmann::json j = {
		{"kind", kind},
		{"message", message},
		{"at", static_cast<long long>(std::time(nullptr))},
	};
	mem.scratchJson = j.dump();
	pushStateString(intents, "view.companion.flash", mem.scratchJson);
}

// ── Action dispatch ─────────────────────────────────────────────────

void processActionEvents(CompanionMemory& mem,
                         const mitiru::module::InputSnapshot* input,
                         mitiru::module::FrameIntents* intents)
{
	if (input->actionEventCount == 0) { return; }

	for (std::int32_t i = 0; i < input->actionEventCount; ++i)
	{
		const std::string name{input->actionEvents[i].name};
		if (name == "companion.build")
		{
			if (isBuildActive(mem))
			{
				pushFlash(mem, intents, "info", "Build already in progress.");
			}
			else if (!spawnBuild(mem))
			{
				if (mem.session.buildDir.empty()) {
					pushFlash(mem, intents, "error", "No build dir configured.");
				} else if (!mem.vcvarsBat.has_value()) {
					pushFlash(mem, intents, "error", "vcvars64.bat not found — install VS 2022.");
				} else {
					pushFlash(mem, intents, "error", "Build failed to start.");
				}
			}
		}
		else if (name == "companion.stop")
		{
			stopGame(mem);
		}
		else if (name == "companion.inspector")
		{
			toggleInspector(mem, intents);
		}
	}

	// Return keyboard focus to the game after any companion action — clicking
	// a companion button shouldn't trap the user with their arrow keys going
	// nowhere. Inspector toggle is the exception: when we just spawned a new
	// inspector window, leave focus on it so the user can navigate there
	// (we can't reliably ForegroundWindow the inspector either way — caller
	// chose to open it, OS will surface it). For everything else, snap focus
	// back to the game.
	if (mem.gameHwnd == nullptr && mem.session.gamePid != 0)
	{
		mem.gameHwnd = findFirstVisibleWindow(mem.session.gamePid);
	}
	if (mem.gameHwnd != nullptr)
	{
		// AllowSetForegroundWindow + SetForegroundWindow is the canonical
		// dance for transferring focus to a sibling process. Without
		// AllowSetForegroundWindow Win32 may silently refuse the focus grab.
		AllowSetForegroundWindow(mem.session.gamePid);
		SetForegroundWindow(mem.gameHwnd);
	}
}

// ── Module callbacks ────────────────────────────────────────────────

void on_init(void* memory)
{
	if (!memory) { return; }
	auto& mem = *static_cast<CompanionMemory*>(memory);
	loadSession(mem);
	mem.vcvarsBat = findVcvarsBat();
}

void on_update(void* memory, float /*dt*/,
               const mitiru::module::InputSnapshot* input,
               mitiru::module::FrameIntents* intents)
{
	if (!memory || !input || !intents) { return; }
	auto& mem = *static_cast<CompanionMemory*>(memory);

	// Note: full-qualify keys (no `using namespace`) because <windows.h>
	// pollutes the global namespace with `Escape` (a wingdi function).
	if (input->keysJustPressed[mitiru::keys::Escape]) { intents->requestStop = 1; }

	applyTopmostOnce(mem);
	dockToGameWindow(mem);
	positionInspectorIfNew(mem);
	reapBuilds(mem);
	reapInspector(mem);
	pollGameAlive(mem, intents);
	processActionEvents(mem, input, intents);

	if (mem.firstPush || ++mem.pushTick >= 6)
	{
		mem.pushTick  = 0;
		mem.firstPush = false;
		pushEnvState(mem, intents);
		pushBuildState(mem, intents);
	}
}

void on_draw(void* memory, mitiru::Screen* screen)
{
	if (!memory || !screen) { return; }
	screen->clear(sgc::Colorf{0.08f, 0.10f, 0.14f, 1.0f});
}

void on_shutdown(void* memory)
{
	if (!memory) { return; }
	auto& mem = *static_cast<CompanionMemory*>(memory);
	if (mem.hGame) { CloseHandle(mem.hGame); mem.hGame = nullptr; }
	for (auto& b : mem.activeBuilds)
	{
		if (b.hProcess) { CloseHandle(b.hProcess); }
		if (b.hThread)  { CloseHandle(b.hThread); }
	}
	mem.activeBuilds.clear();
	// Take any open inspector down with us — game session is ending.
	if (mem.inspectorPid != 0) { closeProcessWindows(mem.inspectorPid); }
	if (mem.inspectorProc)   { CloseHandle(mem.inspectorProc);   mem.inspectorProc   = nullptr; }
	if (mem.inspectorThread) { CloseHandle(mem.inspectorThread); mem.inspectorThread = nullptr; }
}

}  // namespace mitiru_dev_companion

extern "C"
{

__declspec(dllexport)
void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)
{
	if (!api || !memory) { return; }
	if (*memory == nullptr) {
		*memory = new mitiru_dev_companion::CompanionMemory{};
	}
	api->version     = mitiru::module::kCurrentApiVersion;
	api->on_init     = &mitiru_dev_companion::on_init;
	api->on_update   = &mitiru_dev_companion::on_update;
	api->on_draw     = &mitiru_dev_companion::on_draw;
	api->on_shutdown = &mitiru_dev_companion::on_shutdown;
}

__declspec(dllexport)
void mitiru_module_unload(void* /*memory*/) {}

}  // extern "C"
