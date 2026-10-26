#pragma once

/// @file Dx12HdrDisplay.hpp
/// @brief ウィンドウがあるモニターが HDR (Advanced Color) か、最大の明るさと OS の SDR の基準の白は何 nits かを DXGI と Windows に問い合わせる
/// @details 出力の選び方は Microsoft の資料 (ms-learn-hdr-dx) のとおり、ウィンドウが置かれたモニターに合う DXGI の出力の
///          GetDesc1 の色空間が G2084/P2020 かどうか。SDR の基準の白は DisplayConfig の SDR_WHITE_LEVEL (取れなければ 203 nits)。
///          SDR のモニターでも Advanced Color の SDR は G22/P709 と報告されるので、HDR と区別できる。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include <mitiru/render/HdrMath.hpp>

namespace mitiru::gfx
{

struct HdrDisplayInfo
{
	bool hdrActive = false;   ///< モニターが今 HDR で表示している (色空間が G2084/P2020)
	float peakNits = render::kFallbackPeakNits;
	float minNits = 0.0f;
	float paperWhiteNits = render::kFallbackPaperWhiteNits;

	[[nodiscard]] bool operator==(const HdrDisplayInfo&) const noexcept = default;
};

[[nodiscard]] inline bool isHdrColorSpace(DXGI_COLOR_SPACE_TYPE cs) noexcept
{
	return cs == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

namespace detail
{

/// モニターの GDI 名 (\\.\DISPLAY1) に対応する SDR の基準の白の生の値。取れなければ 0
[[nodiscard]] inline ULONG sdrWhiteLevelRaw(const wchar_t* gdiName)
{
	UINT32 pathCount = 0;
	UINT32 modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) { return 0; }
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) { return 0; }
	for (UINT32 i = 0; i < pathCount; ++i)
	{
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
		source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		source.header.size = sizeof(source);
		source.header.adapterId = paths[i].sourceInfo.adapterId;
		source.header.id = paths[i].sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) { continue; }
		if (std::wstring(source.viewGdiDeviceName) != gdiName) { continue; }
		DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
		white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
		white.header.size = sizeof(white);
		white.header.adapterId = paths[i].targetInfo.adapterId;
		white.header.id = paths[i].targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) { return white.SDRWhiteLevel; }
	}
	return 0;
}

}  // namespace detail

/// @brief hwnd があるモニターの HDR の状態。DXGI の出力は新しい factory から数え直す (IsCurrent が FALSE の factory は古い一覧を返すため)。
///        見つからなければ hdrActive = false
[[nodiscard]] inline HdrDisplayInfo queryHdrDisplay(HWND hwnd)
{
	HdrDisplayInfo info;
	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (monitor == nullptr) { return info; }
	Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) { return info; }
	Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
	for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++a)
	{
		Microsoft::WRL::ComPtr<IDXGIOutput> output;
		for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o)
		{
			Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
			DXGI_OUTPUT_DESC1 desc1 = {};
			if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&desc1)) || desc1.Monitor != monitor) { continue; }
			info.hdrActive = isHdrColorSpace(desc1.ColorSpace);
			info.peakNits = desc1.MaxLuminance > 0.0f ? desc1.MaxLuminance : info.peakNits;
			info.minNits = desc1.MinLuminance;
			MONITORINFOEXW mi = {};
			mi.cbSize = sizeof(mi);
			if (GetMonitorInfoW(monitor, &mi))
			{
				info.paperWhiteNits = render::sdrWhiteLevelToNits(detail::sdrWhiteLevelRaw(mi.szDevice));
			}
			return info;
		}
	}
	return info;
}

}  // namespace mitiru::gfx

#endif  // _WIN32
