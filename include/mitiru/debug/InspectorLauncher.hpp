#pragma once

/// @file InspectorLauncher.hpp
/// @brief 走ってる game から inspector sub-window を spawn する helper
/// @details
/// axis 5 (modular sub-window architecture) の UX 最適化。
/// 「`mitiru inspect` を別ターミナルで叩く」段差を消すため、game コードが
/// 任意のキー (典型的には F12) で `openInspector()` を呼べばその場で
/// inspector を子プロセス起動できる。
///
/// 探索順 (見つかった最初を使う):
///   1. 環境変数 MITIRU_INSPECTOR_EXE (絶対パス)
///   2. game exe の同階層の mitiru_inspector.exe
///   3. game exe の親階層の examples/mitiru_inspector/mitiru_inspector.exe
///      (engine build tree 内で動かしてる開発時用 fallback)
///
/// 起動した inspector は detach されるので、起動元の game は普通に終了して
/// 構わない。inspector 側は SharedSnapshot::Reader で producer の生存を見て
/// stale (>10s) になったら自分で waiting 状態に戻る。

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/ToolRegistry.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#endif

namespace mitiru::debug
{

/// @brief spawn する exe の名前 (mitiru_<名前>.exe の <名前>)。
/// @details game DLL は kToolTable を自分の中に焼き込むので、ツール窓のホストが mitiru_tool_cef だった頃の
///          名前 ("tool_cef") で頼んでくる DLL がある。ツール窓は mitiru_tool が開くので、その名前も向け直す。
inline std::string resolveToolExeName(const std::string& toolName)
{
	return toolName == "tool_cef" ? std::string("tool") : toolName;
}

/// @brief 起動したツール窓の process。窓が閉じられたかを、あとから確かめるために handle を持つ (ToolSpawnLedger)
class ToolProcess
{
public:
	ToolProcess() = default;
	explicit ToolProcess(void* handle) noexcept : m_handle(handle) {}
	ToolProcess(const ToolProcess&) = delete;
	ToolProcess& operator=(const ToolProcess&) = delete;
	ToolProcess(ToolProcess&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}
	ToolProcess& operator=(ToolProcess&& other) noexcept
	{
		if (this != &other) { close(); m_handle = std::exchange(other.m_handle, nullptr); }
		return *this;
	}
	~ToolProcess() { close(); }

	[[nodiscard]] bool running() const noexcept
	{
#ifdef _WIN32
		return m_handle != nullptr && WaitForSingleObject(m_handle, 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}

private:
	void close() noexcept
	{
#ifdef _WIN32
		if (m_handle != nullptr) { CloseHandle(m_handle); }
#endif
		m_handle = nullptr;
	}

	void* m_handle = nullptr;
};

/// @brief 環境変数の名前 (MITIRU_<TOOLNAME>_EXE)
inline std::string toolExeEnvName(const std::string& toolName)
{
	std::string envName = "MITIRU_";
	for (char c : toolName) { envName += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
	return envName + "_EXE";
}

/// @brief mitiru_<toolName>.exe の場所。見つからなければ空
/// @details 探索順: 環境変数 MITIRU_<TOOLNAME>_EXE / game exe 同階層 / 開発 build tree。
///          開く前に「そもそも開けるか」を知りたい呼び元 (HTTP を窓のために開く前) も使う。
inline std::string findToolExe(const std::string& requestedTool)
{
#ifdef _WIN32
	const std::string toolName = resolveToolExeName(requestedTool);
	const std::string exeLeaf = "mitiru_" + toolName + ".exe";

	// 1. 環境変数による override
	std::string exePath = env::value(toolExeEnvName(toolName).c_str());
	if (!exePath.empty()) { return exePath; }

	// 2. 走っている game exe と同階層 / 3. 開発 build tree (examples/mitiru_<tool>/)
	wchar_t buf[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) { return {}; }
	const std::filesystem::path selfDir = std::filesystem::path{buf}.parent_path();
	const std::filesystem::path sameDirCandidate = selfDir / exeLeaf;
	if (std::filesystem::exists(sameDirCandidate)) { return sameDirCandidate.string(); }
	const std::filesystem::path devCandidate = selfDir.parent_path() / ("mitiru_" + toolName) / exeLeaf;
	if (std::filesystem::exists(devCandidate)) { return devCandidate.string(); }
#else
	(void)requestedTool;
#endif
	return {};
}

/// @brief mitiru_<toolName>.exe を探して別窓 spawn する (内部用)
/// @details 探し方は findToolExe。
///          ゲームの DLL は host へ intent を出すだけで、host がこれを呼んでツール窓を開く。
/// @return 起動した process (exe が見つからない、起動に失敗したなら nullopt で無害)
inline std::optional<ToolProcess> spawnToolProcess(const std::string& requestedTool, int producerPid, const std::string& extraArgs)
{
#ifdef _WIN32
	const std::string toolName = resolveToolExeName(requestedTool);
	if (producerPid == 0)
	{
		producerPid = _getpid();
	}
	const std::string exeLeaf = "mitiru_" + toolName + ".exe";
	const std::string exePath = findToolExe(requestedTool);
	const std::string envName = toolExeEnvName(toolName);

	if (exePath.empty())
	{
		// 9-4: 「窓が出ない」を目視で切り分けられるよう、探した場所を全部 stderr に出す。
		const auto u8 = [](const std::filesystem::path& p) {
			const auto s = p.u8string();
			return std::string(s.begin(), s.end());
		};
		console::noticef("ツールの %s が見つからないので、環境変数 %s に場所を指定してください", exeLeaf.c_str(), envName.c_str());
		return std::nullopt;
	}

	// コマンドライン構築: `"<exePath>" <pid> <extraArgs>`
	std::wstring wexe(exePath.begin(), exePath.end());
	std::wstring cmd = L"\"" + wexe + L"\" " + std::to_wstring(producerPid);
	if (!extraArgs.empty())
	{
		std::wstring wargs(extraArgs.begin(), extraArgs.end());
		cmd += L" " + wargs;
	}

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessW(
		nullptr,
		cmd.data(),
		nullptr, nullptr,
		FALSE,
		DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
		nullptr, nullptr,
		&si, &pi);
	if (!ok)
	{
		console::noticef("ツールの %s の起動に失敗しました (GetLastError=%lu)", exePath.c_str(), GetLastError());
		return std::nullopt;
	}
	CloseHandle(pi.hThread);
	return ToolProcess(pi.hProcess);
#else
	(void)requestedTool;
	(void)producerPid;
	(void)extraArgs;
	return std::nullopt;
#endif
}

/// @brief spawnToolProcess の handle を持たない版 (起動できたかだけ要る呼び元用)
/// @return 起動成功で true (exe が見つからなければ false で無害)
inline bool spawnTool(const std::string& requestedTool, int producerPid, const std::string& extraArgs)
{
	return spawnToolProcess(requestedTool, producerPid, extraArgs).has_value();
}

/// @brief inspector exe を探して spawn する (spawnTool("inspector") の薄い別名)
/// @return 起動成功で true
inline bool spawnInspector(int producerPid, const std::string& extraArgs)
{
	return spawnTool("inspector", producerPid, extraArgs);
}

/// @brief inspector を default panel (state) で開く
/// @param producerPid 監視対象 (0 = 自プロセス)
inline bool openInspector(int producerPid = 0)
{
	return spawnInspector(producerPid, {});
}

/// @brief 名前付き Inspectable に絞った inspector window を 1 つ開く
/// @param name registerInspectable / LocalInspectable で付けた id
/// @details command palette がユーザの選択を受けてこれを呼ぶ。直接 game コード
///          から `mitiru::debug::openInspectable("player1")` と呼ぶこともできる
///          (palette を経由しない hardcoded shortcut)。
inline bool openInspectable(const std::string& name, int producerPid = 0)
{
	if (name.empty()) { return false; }
	// コマンドライン用に backslash / quote を escape。name は JS dispatch
	// payload 由来の user input。
	std::string safe;
	safe.reserve(name.size());
	for (char c : name)
	{
		if (c == '\\' || c == '"') { continue; }
		if (c == ' ') { return false; }
		safe.push_back(c);
	}
	return spawnInspector(producerPid, "--inspectable " + safe);
}

/// @brief host 側コードからツール独立ウィンドウを 1 つ開く (これが正面の入口)
/// @details main.cpp など host を書く人が「このデバッグ窓を使う」と決めた時に呼ぶ。
///          共有 registry (ToolRegistry.hpp の kToolTable) を引いて該当 exe を spawn する。
///          ゲームのキー入力とは無関係。欲しい窓の行を書いた host だけがその窓を開く。
/// @param t        開く窓 (Tool::Inspector / InputMonitor / TimeTravel / 今後追加分)
/// @param producerPid 監視対象プロセス (0 = 自プロセス = game DLL を載せた host)
/// @return 起動成功で true (exe が見つからなければ false で無害)
inline bool openTool(Tool t, int producerPid = 0)
{
	for (const auto& spec : mitiru::detail::kToolTable)
	{
		if (spec.tool != t) { continue; }
		return spawnTool(spec.exe, producerPid, spec.args);
	}
	console::verbose("kToolTable にない Tool の値で openTool が呼ばれました。ToolRegistry.hpp の kToolTable に行を足してください。");
	return false;
}

/// @brief openTool に追加引数を付けて開く (例: replay scrubber に .mtrr path を渡す)。
/// @details registry の既定 args の後ろに extraArgs を足して spawn する。Tool 窓が
///          producer 監視でなく特定ファイルを開く場合 (replay 等) に使う。
inline bool openTool(Tool t, const std::string& extraArgs, int producerPid = 0)
{
	for (const auto& spec : mitiru::detail::kToolTable)
	{
		if (spec.tool != t) { continue; }
		std::string args = spec.args;
		if (!extraArgs.empty())
		{
			if (!args.empty()) { args += ' '; }
			args += extraArgs;
		}
		return spawnTool(spec.exe, producerPid, args);
	}
	console::verbose("kToolTable にない Tool の値で openTool が呼ばれました。ToolRegistry.hpp の kToolTable に行を足してください。");
	return false;
}

}  // namespace mitiru::debug
