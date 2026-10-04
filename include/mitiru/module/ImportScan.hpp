#pragma once

/// @file ImportScan.hpp
/// @brief 読み込んだ game の DLL が、Input を通さずに OS から入力を読む関数を import しているか調べる。
/// @details OS から直接読んだキーやパッドの値は InputSnapshot に入らないため、録画、リプレイ、巻き戻し、ロールバックで再現しない。headless では常に「押していない」になるため、1 フレームのやり直し (determinismSentinelEveryFrames)でも見つからない。時計、乱数、スレッドの関数は CRT の起動処理と Jolt も import するため、ここでは調べない。値の食い違いはやり直しの検査で見つかる。

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mitiru::module
{

inline constexpr const char* kOsInputFunctions[] = {
	"GetAsyncKeyState", "GetKeyState", "GetKeyboardState", "GetCursorPos",
	"XInputGetState", "XInputGetStateEx", "XInputGetKeystroke",
};

[[nodiscard]] inline bool isOsInputFunction(const char* name) noexcept
{
	for (const char* f : kOsInputFunctions)
	{
		if (std::strcmp(f, name) == 0) { return true; }
	}
	return false;
}

/// @brief 読み込み済みの module (LoadLibrary の戻り値)が名前で import する OS の入力関数を返す。無ければ空。
[[nodiscard]] inline std::vector<std::string> osInputImports(const void* moduleBase)
{
	std::vector<std::string> found;
#if defined(_WIN32)
	if (moduleBase == nullptr) { return found; }
	const auto* base = static_cast<const std::uint8_t*>(moduleBase);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) { return found; }
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) { return found; }
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (dir.VirtualAddress == 0) { return found; }
	for (const auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name != 0; ++d)
	{
		const DWORD thunkRva = d->OriginalFirstThunk != 0 ? d->OriginalFirstThunk : d->FirstThunk;
		for (const auto* t = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + thunkRva); t->u1.AddressOfData != 0; ++t)
		{
			if (IMAGE_SNAP_BY_ORDINAL(t->u1.Ordinal)) { continue; }
			const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + t->u1.AddressOfData);
			if (isOsInputFunction(byName->Name)) { found.emplace_back(byName->Name); }
		}
	}
#else
	(void)moduleBase;
#endif
	return found;
}

}  // namespace mitiru::module
