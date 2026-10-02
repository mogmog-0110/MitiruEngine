#pragma once

/// @file AiDisplay.hpp
/// @brief 自動検証の窓を置く場所を決める。仮想ディスプレイ (Virtual Display Driver 等) があればそこ、
///        無ければ画面外。実モニタには一画素も重ねない
/// @details 人が使っているモニタに窓を出せば、その間 PC は使えなくなる。仮想ディスプレイは人が見ない
///          画面なので、実際に合成された窓の見た目 (PrintWindow) を撮る置き場に使える。窓が大きくて
///          仮想ディスプレイからはみ出し、実モニタにかかる時は置かずに画面外を選ぶ。
///          選ぶ規則 (chooseAiWindowOrigin) は OS を呼ばない純粋な関数で、列挙だけが Win32 に依る。

#include <algorithm>
#include <optional>
#include <string>
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

struct DisplayArea
{
	int  x = 0, y = 0, w = 0, h = 0;
	bool isVirtual = false;
};

struct WindowOrigin
{
	int x = 0, y = 0;
};

namespace detail
{
	[[nodiscard]] inline bool overlaps(const DisplayArea& d, int x, int y, int w, int h) noexcept
	{
		return x < d.x + d.w && d.x < x + w && y < d.y + d.h && d.y < y + h;
	}
}  // namespace detail

/// @brief 外形 outerW x outerH の窓を、どの実モニタにも重ならない仮想ディスプレイの左上に置く。
///        候補は x の小さい順。置ける所が無ければ nullopt (呼び出し側は画面外へ置く)
[[nodiscard]] inline std::optional<WindowOrigin> chooseAiWindowOrigin(
	std::vector<DisplayArea> displays, int outerW, int outerH)
{
	std::sort(displays.begin(), displays.end(),
	          [](const DisplayArea& a, const DisplayArea& b) { return a.x < b.x; });
	for (const auto& v : displays)
	{
		if (!v.isVirtual) { continue; }
		const bool hitsReal = std::any_of(displays.begin(), displays.end(), [&](const DisplayArea& d) {
			return !d.isVirtual && detail::overlaps(d, v.x, v.y, outerW, outerH);
		});
		if (!hitsReal) { return WindowOrigin{v.x, v.y}; }
	}
	return std::nullopt;
}

#ifdef _WIN32
/// @brief 有効なディスプレイを列挙する。アダプタ名に "Virtual" を含むものを仮想とみなす
///        (tools/vdisplay_set.ps1 と同じ判定)
[[nodiscard]] inline std::vector<DisplayArea> enumerateDisplays()
{
	std::vector<DisplayArea> out;
	for (DWORD i = 0;; ++i)
	{
		DISPLAY_DEVICEW dev{};
		dev.cb = sizeof(dev);
		if (!EnumDisplayDevicesW(nullptr, i, &dev, 0)) { break; }
		if ((dev.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) { continue; }
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		if (!EnumDisplaySettingsW(dev.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) { continue; }
		const std::wstring adapter = dev.DeviceString;
		out.push_back({static_cast<int>(mode.dmPosition.x), static_cast<int>(mode.dmPosition.y),
		               static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight),
		               adapter.find(L"Virtual") != std::wstring::npos});
	}
	return out;
}
#endif

}  // namespace mitiru::platform
