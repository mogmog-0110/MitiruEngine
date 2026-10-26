#pragma once

/// @file KeyLayoutLabel.hpp
/// @brief 位置で割り当てたキーの現在の刻印を OS から取得し、画面に表示する。
/// @details 文字キーは位置で扱うため、割り当てが "W" でも AZERTY では Z と刻印されたキーを指す。名前を取得せず "W" と表示すると、キーの刻印と食い違う。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>

#include <mitiru/input/KeyPosition.hpp>

namespace mitiru::platform
{

/// @brief positionVk の位置にあるキーの刻印を UTF-8 で返す。位置で指定するキーではない場合や、OS が名前を持たない場合は空文字列を返す。
/// @details 呼び出したスレッドのキーボード配列を使う。ウィンドウを作成したスレッドから呼ぶ。
[[nodiscard]] inline std::string layoutKeyLabel(int positionVk)
{
	const unsigned scan = input::scancodeFromPositionVk(positionVk);
	if (scan == 0) { return {}; }
	wchar_t wide[64] = {};
	const int n = GetKeyNameTextW(static_cast<LONG>(scan << 16), wide, 64);
	if (n <= 0) { return {}; }
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, n, nullptr, 0, nullptr, nullptr);
	if (bytes <= 0) { return {}; }
	std::string out(static_cast<std::size_t>(bytes), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, n, out.data(), bytes, nullptr, nullptr);
	return out;
}

/// @brief 呼び出したスレッドの現在のキーボード配列の識別子 (HKL)。配列が切り替わったかを調べるために使う。
[[nodiscard]] inline std::uintptr_t currentKeyboardLayoutId() noexcept
{
	return reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(0));
}

}  // namespace mitiru::platform
