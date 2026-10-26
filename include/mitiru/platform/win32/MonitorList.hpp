#pragma once

/// @file MonitorList.hpp
/// @brief 接続中のモニタ一覧の宣言。名前、位置、リフレッシュレート、HDR 対応を持ち、表示先の選択とウィンドウの移動先の探索に使う。
/// @details モニタ番号は抜き差しで変わるため、保存後も選び直せる EDID 名を使う。
/// 名前がないモニタは "Built-in Display" または "Display <番号>" とする。
/// リフレッシュレートと HDR 対応は QueryDisplayConfig から読む。DXGI の色空間が示すのは現在の HDR 出力であり、HDR 対応ではない。
/// 仮想ディスプレイも実モニタと同じ一覧に含める。

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <mitiru/module/detail/HostEnvironment62.hpp>

namespace mitiru::platform
{

struct MonitorInfo
{
	std::string   name;              ///< ゲームに見せる名前 (UTF-8)。重なりの調整前
	std::wstring  deviceName;        ///< "\\.\DISPLAY1" のような GDI の名前
	HMONITOR      handle = nullptr;
	RECT          rect{};
	RECT          work{};
	bool          primary = false;
	std::uint32_t refreshMilliHz = 0;
	bool          hdrCapable = false;
};

namespace detail
{

[[nodiscard]] inline std::string wideToUtf8(const wchar_t* text)
{
	if (text == nullptr || text[0] == L'\0') { return {}; }
	const int length = static_cast<int>(std::wcslen(text));
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
	if (bytes <= 0) { return {}; }
	std::string out(static_cast<std::size_t>(bytes), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), bytes, nullptr, nullptr);
	return out;
}

/// QueryDisplayConfig が返す 1 本の経路。GDI 名で EnumDisplayMonitors の結果と結び付ける
struct DisplayPath
{
	std::wstring  gdiName;
	std::string   friendlyName;
	bool          internalPanel = false;
	std::uint32_t refreshMilliHz = 0;
	bool          hdrCapable = false;
};

[[nodiscard]] inline std::vector<DisplayPath> queryDisplayPaths()
{
	std::vector<DisplayPath> out;
	UINT32 pathCount = 0;
	UINT32 modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) { return out; }
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
	{
		return out;
	}
	paths.resize(pathCount);
	for (const DISPLAYCONFIG_PATH_INFO& path : paths)
	{
		DisplayPath entry;
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
		source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id };
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) { continue; }
		entry.gdiName = source.viewGdiDeviceName;

		DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
		target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id };
		if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
		{
			entry.friendlyName = wideToUtf8(target.monitorFriendlyDeviceName);
			entry.internalPanel = target.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
			                      target.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
			                      target.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
		}

		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
		color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), path.targetInfo.adapterId, path.targetInfo.id };
		if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) { entry.hdrCapable = color.advancedColorSupported != 0; }

		entry.refreshMilliHz = module::detail::refreshMilliHzFromRational(path.targetInfo.refreshRate.Numerator,
		                                                                 path.targetInfo.refreshRate.Denominator);
		out.push_back(std::move(entry));
	}
	return out;
}

/// "\\.\DISPLAY2" の末尾の数字。取れなければ空
[[nodiscard]] inline std::string trailingDigits(const std::wstring& deviceName)
{
	std::size_t start = deviceName.size();
	while (start > 0 && deviceName[start - 1] >= L'0' && deviceName[start - 1] <= L'9') { --start; }
	std::string out;
	for (std::size_t i = start; i < deviceName.size(); ++i) { out.push_back(static_cast<char>(deviceName[i])); }
	return out;
}

[[nodiscard]] inline BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) { return TRUE; }
	MonitorInfo m;
	m.handle = monitor;
	m.deviceName = info.szDevice;
	m.rect = info.rcMonitor;
	m.work = info.rcWork;
	m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
	reinterpret_cast<std::vector<MonitorInfo>*>(param)->push_back(std::move(m));
	return TRUE;
}

}  // namespace detail

/// 接続中のモニタ一覧。主モニタを先頭にし、残りは左から右、上から下の順に並べる
[[nodiscard]] inline std::vector<MonitorInfo> enumerateMonitors()
{
	std::vector<MonitorInfo> list;
	EnumDisplayMonitors(nullptr, nullptr, &detail::collectMonitor, reinterpret_cast<LPARAM>(&list));
	std::sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
		if (a.primary != b.primary) { return a.primary; }
		if (a.rect.left != b.rect.left) { return a.rect.left < b.rect.left; }
		return a.rect.top < b.rect.top;
	});
	const std::vector<detail::DisplayPath> paths = detail::queryDisplayPaths();
	for (MonitorInfo& m : list)
	{
		const auto path = std::find_if(paths.begin(), paths.end(), [&](const detail::DisplayPath& p) { return p.gdiName == m.deviceName; });
		if (path != paths.end())
		{
			m.name = path->friendlyName;
			m.refreshMilliHz = path->refreshMilliHz;
			m.hdrCapable = path->hdrCapable;
			if (m.name.empty() && path->internalPanel) { m.name = "Built-in Display"; }
		}
		if (m.refreshMilliHz == 0)
		{
			// 経路が見つからない画面では整数の Hz を使う。0 と 1 は機器の既定値を示すため使わない
			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (EnumDisplaySettingsW(m.deviceName.c_str(), ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
			{
				m.refreshMilliHz = mode.dmDisplayFrequency * 1000u;
			}
		}
		if (m.name.empty())
		{
			const std::string digits = detail::trailingDigits(m.deviceName);
			m.name = digits.empty() ? "Display" : "Display " + digits;
		}
	}
	return list;
}

/// hwnd があるモニタの list 内の添字。見つからなければ -1
[[nodiscard]] inline int monitorIndexOfWindow(const std::vector<MonitorInfo>& list, HWND hwnd)
{
	const HMONITOR here = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		if (list[i].handle == here) { return static_cast<int>(i); }
	}
	return -1;
}

}  // namespace mitiru::platform
