#pragma once

/// @file WindowShot.hpp
/// @brief 窓のクライアント領域を、DWM が合成した絵のまま RGBA で取る (PrintWindow + PW_RENDERFULLCONTENT)
/// @details バックバッファの読み戻し (Engine::capture) は「描いたつもりの絵」で、DXGI の提示・DWM の合成・
///          DPI の拡大を通った後の「人が見る絵」とは別物になりうる。こちらは合成側から取るので、提示の
///          失敗や拡大のにじみまで写る。窓を前に出さずに撮れる (アクティブ化もフォーカス移動もしない)。

#include <cstdint>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace mitiru::platform
{

struct WindowShot
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;  ///< 上の行から、1 画素 4 byte (A は常に 255)

	[[nodiscard]] bool empty() const noexcept { return rgba.empty(); }
};

#ifdef _WIN32

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

/// @brief hwnd のクライアント領域を撮る。失敗すれば空
[[nodiscard]] inline WindowShot captureWindowClient(HWND hwnd)
{
	WindowShot shot;
	RECT rc{};
	if (hwnd == nullptr || !GetClientRect(hwnd, &rc)) { return shot; }
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) { return shot; }

	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;  // 負 = 上の行から
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	void* bits = nullptr;
	HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	const bool ok = dib != nullptr && bits != nullptr && [&] {
		HGDIOBJ old = SelectObject(mem, dib);
		const BOOL printed = PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
		GdiFlush();
		SelectObject(mem, old);
		return printed != FALSE;
	}();
	if (ok)
	{
		const auto* src = static_cast<const std::uint8_t*>(bits);
		shot.width = w;
		shot.height = h;
		shot.rgba.resize(static_cast<std::size_t>(w) * h * 4);
		for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
		{
			shot.rgba[i * 4 + 0] = src[i * 4 + 2];
			shot.rgba[i * 4 + 1] = src[i * 4 + 1];
			shot.rgba[i * 4 + 2] = src[i * 4 + 0];
			shot.rgba[i * 4 + 3] = 255;
		}
	}
	if (dib != nullptr) { DeleteObject(dib); }
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return shot;
}

#endif  // _WIN32

}  // namespace mitiru::platform
