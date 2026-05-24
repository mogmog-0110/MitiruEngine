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

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#endif

namespace mitiru::debug
{

/// @brief inspector exe を探して spawn する (内部用 — args を任意で渡せる)
/// @return 起動成功で true
inline bool spawnInspector(int producerPid, const std::string& extraArgs)
{
#ifdef _WIN32
	if (producerPid == 0)
	{
		producerPid = _getpid();
	}

	// 1. env var override
	std::string exePath;
	if (const char* env = std::getenv("MITIRU_INSPECTOR_EXE"); env && *env)
	{
		exePath = env;
	}

	// 2. same dir as the running game exe
	if (exePath.empty())
	{
		wchar_t buf[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0)
		{
			std::filesystem::path self{buf};
			auto candidate = self.parent_path() / "mitiru_inspector.exe";
			if (std::filesystem::exists(candidate))
			{
				exePath = candidate.string();
			}
			// 3. dev fallback: engine build tree
			if (exePath.empty())
			{
				auto devCandidate = self.parent_path().parent_path()
					/ "mitiru_inspector" / "mitiru_inspector.exe";
				if (std::filesystem::exists(devCandidate))
				{
					exePath = devCandidate.string();
				}
			}
		}
	}

	if (exePath.empty())
	{
		return false;
	}

	// Build command line: `"<exePath>" <pid> <extraArgs>`
	std::wstring wexe;
	wexe.assign(exePath.begin(), exePath.end());
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
	if (!ok) { return false; }
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
#else
	(void)producerPid;
	(void)extraArgs;
	return false;
#endif
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
	// Escape backslashes / quotes for the command line — names are user input
	// from JS dispatch payload.
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

}  // namespace mitiru::debug
