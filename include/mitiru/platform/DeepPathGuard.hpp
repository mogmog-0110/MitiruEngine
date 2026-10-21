/// @file DeepPathGuard.hpp
/// @brief 深すぎるフォルダにある exe を、GPU に触れる前に止める。
///
/// NVIDIA のカーネルドライバー (nvlddmkm 32.0.16.1742) は、D3D12 を使う exe の NT パス
/// (\Device\HarddiskVolumeN\...) を UTF-8 に変換し、約 512 バイトの領域へ長さを確認せずコピーする。
/// この領域を超えるとカーネルヒープが破損し、その exe の起動時か、同じ Windows の起動中に行われる後の D3D12 起動時に Windows が停止する。
/// 日本語は UTF-8 で 1 文字 3 バイトになるため、約 170 文字でこの長さに達し、MAX_PATH の範囲内でも起きる。
#pragma once

#ifdef _WIN32

#include <cstddef>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mitiru/platform/LaunchChild.hpp>

namespace mitiru::platform
{

/// 512 バイトに達する前の 400 バイトで止め、ドライバーがパスの末尾に何か足す分の余裕を取る
inline constexpr std::size_t kDeepPathLimitUtf8Bytes = 400;

inline constexpr const char* kDeepPathNotice =
	"ゲームを置いたフォルダが深すぎて、GPU のドライバーが PC ごと落ちることがあるので起動を止めました。"
	"もっと浅いフォルダ (例: C:\\Games) に置いてください";

[[nodiscard]] inline std::size_t utf8Bytes(std::wstring_view w)
{
	if (w.empty()) { return 0; }
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	return n > 0 ? static_cast<std::size_t>(n) : w.size() * 3;
}

[[nodiscard]] inline std::string deepPathProblem(std::wstring_view ntPath)
{
	return utf8Bytes(ntPath) > kDeepPathLimitUtf8Bytes ? std::string(kDeepPathNotice) : std::string();
}

/// @brief ドライバーが見る \Device\HarddiskVolumeN\... の形を返す。取得できなければ空文字列を返す
[[nodiscard]] inline std::wstring ntPathOf(const std::wstring& path)
{
	const HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE) { return {}; }
	std::wstring buf(1024, L'\0');
	DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), VOLUME_NAME_NT);
	if (n >= buf.size())
	{
		buf.resize(n + 1);
		n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), VOLUME_NAME_NT);
	}
	CloseHandle(h);
	buf.resize(n < buf.size() ? n : 0);
	return buf;
}

/// @brief 現在のプロセスの exe を調べる。NT パスを取得できないときは、ボリューム名の分を加えた DOS パスで測る
[[nodiscard]] inline std::string currentDeepPathProblem()
{
	const std::wstring exe = thisExePath();
	const std::wstring nt = ntPathOf(exe);
	return deepPathProblem(nt.empty() ? std::wstring(L"\\Device\\HarddiskVolume00") + exe : nt);
}

}  // namespace mitiru::platform

#endif  // _WIN32
