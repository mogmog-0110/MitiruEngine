// mitiru_dev_companion。watch-mode の game と並んで起動する小型の「dev console」バー。
// 以前 launcher の project 行にあった [Build] / [Inspector] / [Stop] の役割を担う。
//
// アトミックツールとしての根拠。launcher = picker、companion = アクティブセッション操作。
// 混在させると「Run と Watch と Build の違いは?」という混乱を招いた (user feedback 2026-05-21)。
//
// セッションの受け渡し (launcher → companion)。
//   launcher の [Open] が game + companion を起動する際、まず以下を含む
//   %TEMP%/mitiru_dev_session_<pid>.json を書く。
//     { gamePid, projectName, projectDll, buildDir, buildTarget }
//   launcher はこのファイルパスを mitiru_host の第 3 CLI 引数として渡す
//   (DLL パスと --flags の後)。companion は MITIRU_COMPANION_SESSION env var
//   から読む (launcher が設定)。
//
// ライフサイクル。
//   - on_init: session ファイルの読み込み、game プロセスの HANDLE の取得、companion 窓を topmost に設定
//   - on_update: game HANDLE を poll。game 終了時は intents->requestStop で自分も閉じる
//   - actions: companion.build (async cmake)、companion.stop (game に TerminateProcess)、
//     companion.inspector (mitiru_inspector.exe を起動)

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
	HANDLE                  hGame {nullptr};         // game pid に対する OpenProcess 結果
	bool                    gameStillAlive {true};   // 毎フレーム更新
	bool                    topmostApplied {false};  // 初回フレームで EnumWindows 1 回

	// docking 用の HWND キャッシュ。どちらの窓も再生成され得る (companion の再起動、
	// game の hot-reload など) ため、毎フレーム更新する。
	HWND                    selfHwnd {nullptr};
	HWND                    gameHwnd {nullptr};
	int                     dockTick {0};            // SetWindowPos 呼び出しを throttle
	bool                    inspectorPositioned {false}; // inspector を初回だけ配置、以後はユーザー任せ

	std::optional<std::filesystem::path> vcvarsBat;

	std::vector<BuildJob>   activeBuilds;            // single-slot in practice
	BuildResult             lastBuild;

	// Inspector 子プロセス。toggle 用に保持する (同時に 1 つだけ。再度 [Inspector] を
	// 押すと新規起動せず閉じる)。
	HANDLE                  inspectorProc {nullptr};
	HANDLE                  inspectorThread {nullptr};
	DWORD                   inspectorPid {0};

	// Push throttle (C++ 側では dedup しない。同じ値の push は UI の data model が無視する)。
	int    pushTick      {0};
	bool   firstPush     {true};

	std::string scratchJson;
};

// ── ヘルパー ──────────────────────────────────────────────────────────

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

// ── Session 読込 ─────────────────────────────────────────────────────

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

// ── 自分を topmost に ─────────────────────────────────────────────────
//
// game 窓に隠れないよう、companion は always-on-top にしたい。適切な
// engine API が (まだ) ないので Win32 を直接使う。このプロセスが所有する
// top-level 窓を列挙し、それぞれに SetWindowPos を実行する。

/// `pid` が所有する最初の可視 top-level 窓を探す。無ければ nullptr。
/// 対象プロセスがまだ窓を作っていない可能性があるので、caller は次のフレームで再試行する。
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
			return FALSE;  // 最初の 1 つで打ち切り
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

/// companion を game 窓の上 (空きが無ければ下) に配置する。
/// 幅は固定する (game に合わせない)。合わせると標準 game ではバーが 1280×80 = 16:1 になる。
/// ~6Hz に throttle し、更新の合間にユーザーが手動で動かせる余地を残す。
void dockToGameWindow(CompanionMemory& mem)
{
	if (++mem.dockTick < 10) { return; }  // 60fps で ~6Hz
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

	// バーは game 窓のすぐ上への配置を優先する。空きが足りない場合 (game が画面上端や
	// マルチモニタ端に接している) は game 下端のすぐ下に配置する。
	int newTop = gameRect.top - myHeight;
	if (newTop < 0) { newTop = gameRect.bottom; }

	SetWindowPos(mem.selfHwnd, HWND_TOPMOST,
		gameRect.left, newTop,
		myWidth, myHeight,
		SWP_NOACTIVATE);
}

/// inspector 窓の初回配置。探さなくても見えるよう、game 窓のすぐ右に置く。
/// Inspector の生存期間中に 1 回だけ実行する。以後はユーザーによるドラッグを尊重する。
void positionInspectorIfNew(CompanionMemory& mem)
{
	if (mem.inspectorPid == 0 || mem.inspectorPositioned) { return; }
	HWND inspectorHwnd = findFirstVisibleWindow(mem.inspectorPid);
	if (inspectorHwnd == nullptr) { return; }  // まだ非可視、次フレームで再試行

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

// ── game プロセスの poll ─────────────────────────────────────────────

void pollGameAlive(CompanionMemory& mem, mitiru::module::FrameIntents* intents)
{
	if (mem.hGame == nullptr)
	{
		// session 無し = 付き添う対象が無い。生きたまま何もしない。
		mem.gameStillAlive = false;
		return;
	}
	const DWORD wait = WaitForSingleObject(mem.hGame, 0);
	if (wait == WAIT_OBJECT_0)
	{
		// game 終了。孤立したプロセスとして残らないよう自分も閉じる。
		mem.gameStillAlive = false;
		intents->requestStop = 1;
	}
}

// ── ビルドジョブ ────────────────────────────────────────────────────────

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

// ── Inspector / Stop アクション ────────────────────────────────────────

// spawnInspector / stopGame が status メッセージを flash できるよう前方宣言する。
void pushFlash(CompanionMemory& mem,
               mitiru::module::FrameIntents* intents,
               const std::string& kind, const std::string& message);

std::optional<std::filesystem::path> findInspectorExe(const std::filesystem::path& hostDir)
{
	// mitiru_inspector.exe はレイアウトによって 2 箇所に存在し得る。
	//   1. mitiru_host.exe の隣 (release zip / clean install)。
	//   2. examples/mitiru_inspector/ サブディレクトリ (dev build tree。
	//      各ターゲットが独自の RUNTIME_OUTPUT_DIRECTORY を持つ)。
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

/// 指定 PID が所有するすべての top-level 窓に WM_CLOSE を送る。
/// ユーザーが toggle で off にしたとき、inspector 子プロセスを正常に閉じるために使う
/// (強制的な TerminateProcess ではなく、WM_CLOSE で engine の cleanup を実行する)。
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
	// ユーザー操作による close (inspector 窓の X を押した) を検出する。終了したプロセスに
	// 操作しようとするのを避け、次の [Inspector] クリックで再起動するよう handle を破棄する。
	if (mem.inspectorProc != nullptr && !isInspectorAlive(mem))
	{
		CloseHandle(mem.inspectorProc);
		if (mem.inspectorThread) { CloseHandle(mem.inspectorThread); }
		mem.inspectorProc        = nullptr;
		mem.inspectorThread      = nullptr;
		mem.inspectorPid         = 0;
		mem.inspectorPositioned  = false;  // 次回起動時に再配置するよう reset
	}
}

void toggleInspector(CompanionMemory& mem,
                     mitiru::module::FrameIntents* intents)
{
	// toggle の挙動。Inspector が起動中なら閉じる。そうでなければ新規起動する。
	// [Inspector] を連打しても複数の窓が開かないようにする。
	if (isInspectorAlive(mem))
	{
		closeProcessWindows(mem.inspectorPid);
		// block しない。次のフレームの reapInspector() がプロセスの終了を検出し、handle を解放する。
		// flash 無し。[Inspector]/[Hide Inspector] のボタンラベルがすでに状態変化を表すので、
		// 「closed」の toast は冗長。
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
	// 成功時は flash 無し。ボタンラベルが "Hide Inspector" に変わり、inspector 窓も
	// 表示されるので「opened」toast は不要。
	// 上のエラー経路では flash する (失敗はボタンから分からないため)。
}

void stopGame(CompanionMemory& mem)
{
	if (mem.hGame != nullptr)
	{
		TerminateProcess(mem.hGame, 0);
	}
}

// ── state push ──────────────────────────────────────────────────────

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
	pushStateString(intents, "view.env", mem.scratchJson);
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
	pushStateString(intents, "view.build", mem.scratchJson);
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
	pushStateString(intents, "view.flash", mem.scratchJson);
}

// ── アクション dispatch ─────────────────────────────────────────────────

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

	// companion のアクション後はキーボードフォーカスを game に戻す。companion ボタンを
	// クリックすると矢印キーが効かなくなる事態を防ぐ。Inspector toggle は例外。新規
	// inspector 窓を起動した直後はフォーカスをそこに残す (いずれにしても inspector を確実に
	// ForegroundWindow にはできないが、caller が開いたため OS が前面に出す)。それ以外は
	// フォーカスを game に戻す。
	if (mem.gameHwnd == nullptr && mem.session.gamePid != 0)
	{
		mem.gameHwnd = findFirstVisibleWindow(mem.session.gamePid);
	}
	if (mem.gameHwnd != nullptr)
	{
		// AllowSetForegroundWindow + SetForegroundWindow は sibling プロセスへ
		// フォーカスを移す定石。AllowSetForegroundWindow が無いと、Win32 が通知せずに
		// フォーカスの移動を拒否することがある。
		AllowSetForegroundWindow(mem.session.gamePid);
		SetForegroundWindow(mem.gameHwnd);
	}
}

// ── Module コールバック ────────────────────────────────────────────────

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

	// 注: keys は完全修飾する (`using namespace` を使わない)。<windows.h> が
	// グローバル名前空間を `Escape` (wingdi の関数) で汚染するため。
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
	// 開いている inspector も一緒に閉じる。game session が終わるため。
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
	api->version     = mitiru::module::kWireApiVersion;  // 数値 + build 指紋 (H-1/H-4)
	api->on_init     = &mitiru_dev_companion::on_init;
	api->on_update   = &mitiru_dev_companion::on_update;
	api->on_draw     = &mitiru_dev_companion::on_draw;
	api->on_shutdown = &mitiru_dev_companion::on_shutdown;
}

__declspec(dllexport)
void mitiru_module_unload(void* /*memory*/) {}

}  // extern "C"
