#pragma once

/// @file Dx12GpuProbe.hpp
/// @brief D3D12 アダプタの列挙、機能の問い合わせ、使用する 1 台の選択、診断ファイル出力の宣言。
/// @details 選択規則と書式は、D3D12 を呼ばない GpuCompat.hpp に置く。このファイルは実際の問い合わせだけを扱う。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/gfx/GpuCompat.hpp>
#include <mitiru/gfx/GpuNotice.hpp>
#include <mitiru/platform/ExitNotice.hpp>
#include <mitiru/platform/Utf8Args.hpp>

#ifndef MITIRU_ENGINE_VERSION
#define MITIRU_ENGINE_VERSION "unknown"
#endif

namespace mitiru::gfx::compat
{

/// @brief 診断ファイルの書き先。ゲームを遊ぶ人の実行時だけ host が設定する。環境変数 MITIRU_GPU_REPORT を優先する
[[nodiscard]] inline std::filesystem::path& reportPathSlot()
{
	static std::filesystem::path p;
	return p;
}

inline void setReportPath(const std::filesystem::path& path) { reportPathSlot() = path; }

[[nodiscard]] inline std::filesystem::path effectiveReportPath()
{
	const std::wstring fromEnv = env::wideValue(L"MITIRU_GPU_REPORT");
	if (!fromEnv.empty()) { return std::filesystem::path(fromEnv); }
	return reportPathSlot();
}

/// @brief 直近の診断の本文。ファイルを書かない実行でも、テストと詳細表示から読める
[[nodiscard]] inline std::string& lastReportText()
{
	static std::string t;
	return t;
}

inline bool writeReportFile(const std::string& text)
{
	lastReportText() = text;
	const std::filesystem::path path = effectiveReportPath();
	if (path.empty()) { return false; }
	std::error_code ec;
	if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path(), ec); }
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) { return false; }
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(out);
}

namespace detail
{

[[nodiscard]] inline std::string utf8(const wchar_t* w) { return mitiru::platform::wideToUtf8(std::wstring(w)); }

[[nodiscard]] inline std::string osVersionText()
{
	using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
	OSVERSIONINFOW v{};
	v.dwOSVersionInfoSize = sizeof(v);
	if (HMODULE nt = ::GetModuleHandleW(L"ntdll.dll"))
	{
		if (auto fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(nt, "RtlGetVersion")); fn != nullptr && fn(&v) == 0)
		{
			char b[64];
			std::snprintf(b, sizeof(b), "Windows %lu.%lu (build %lu)", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
			return b;
		}
	}
	return "Windows (版を読めない)";
}

/// @brief CPU がこの実行で持つ拡張命令。AVX2 を持たない CPU で落ちたときの切り分けに使う
[[nodiscard]] inline std::string cpuFeatureText()
{
	std::string s;
	auto add = [&s](DWORD feature, const char* name) {
		if (::IsProcessorFeaturePresent(feature)) { s += (s.empty() ? "" : " "); s += name; }
	};
	add(PF_SSE3_INSTRUCTIONS_AVAILABLE, "SSE3");
	add(PF_SSSE3_INSTRUCTIONS_AVAILABLE, "SSSE3");
	add(PF_SSE4_1_INSTRUCTIONS_AVAILABLE, "SSE4.1");
	add(PF_SSE4_2_INSTRUCTIONS_AVAILABLE, "SSE4.2");
	add(PF_AVX_INSTRUCTIONS_AVAILABLE, "AVX");
	add(PF_AVX2_INSTRUCTIONS_AVAILABLE, "AVX2");
	add(PF_AVX512F_INSTRUCTIONS_AVAILABLE, "AVX512F");
	return s.empty() ? std::string("SSE2 のみ") : s;
}

[[nodiscard]] inline std::string driverVersionText(IDXGIAdapter* adapter)
{
	LARGE_INTEGER v{};
	if (adapter == nullptr || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) { return {}; }
	char b[64];
	std::snprintf(b, sizeof(b), "%u.%u.%u.%u", static_cast<unsigned>(HIWORD(v.HighPart)), static_cast<unsigned>(LOWORD(v.HighPart)),
	              static_cast<unsigned>(HIWORD(v.LowPart)), static_cast<unsigned>(LOWORD(v.LowPart)));
	return b;
}

[[nodiscard]] inline AdapterInfo infoOf(IDXGIAdapter1* adapter, int index)
{
	DXGI_ADAPTER_DESC1 d{};
	adapter->GetDesc1(&d);
	AdapterInfo a;
	a.index = index;
	a.name = utf8(d.Description);
	a.vendorId = d.VendorId;
	a.deviceId = d.DeviceId;
	a.revision = d.Revision;
	a.dedicatedVramBytes = d.DedicatedVideoMemory;
	a.sharedMemoryBytes = d.SharedSystemMemory;
	a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
	a.driverVersion = driverVersionText(adapter);
	return a;
}

[[nodiscard]] inline bool msaaOk(ID3D12Device* dev, DXGI_FORMAT fmt, UINT samples)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
	q.Format = fmt;
	q.SampleCount = samples;
	return SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) && q.NumQualityLevels > 0;
}

[[nodiscard]] inline std::string featureLevelText(D3D_FEATURE_LEVEL fl)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%d_%d", (static_cast<int>(fl) >> 12) & 0xF, (static_cast<int>(fl) >> 8) & 0xF);
	return b;
}

[[nodiscard]] inline std::string shaderModelText(int sm)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%d_%d", (sm >> 4) & 0xF, sm & 0xF);
	return b;
}

/// @brief 作成できたデバイスの機能を調べ、FAKE_MISSING で指定した機能を欠けたものとして扱う
inline void queryCaps(ID3D12Device* dev, bool tearingSupported, const FakeMissing& fake, AdapterProbe& out)
{
	CapSet c;
	auto set = [&c](GpuCap cap, bool ok) { c.set(static_cast<std::size_t>(cap), ok); };
	set(GpuCap::FeatureLevel11, true);
	set(GpuCap::Msaa4Rgba16f, msaaOk(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, 4));
	set(GpuCap::Msaa4Rgba8, msaaOk(dev, DXGI_FORMAT_R8G8B8A8_UNORM, 4));
	set(GpuCap::Msaa4Depth32, msaaOk(dev, DXGI_FORMAT_D32_FLOAT, 4));
	set(GpuCap::Msaa8Rgba8, msaaOk(dev, DXGI_FORMAT_R8G8B8A8_UNORM, 8));

	D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
	if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))))
	{
		set(GpuCap::TypedUavLoad, o.TypedUAVLoadAdditionalFormats != FALSE);
		set(GpuCap::BindingTier3, o.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3);
		out.resourceHeapTier = static_cast<int>(o.ResourceHeapTier);
		out.resourceBindingTier = static_cast<int>(o.ResourceBindingTier);
	}
	D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R16G16B16A16_FLOAT};
	set(GpuCap::TypedUavStore, SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs)))
	                               && (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0);
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
	set(GpuCap::Int64Ops, SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1))) && o1.Int64ShaderOps);
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
	set(GpuCap::Dxr11, SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5)))
	                       && o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
	set(GpuCap::MeshShader, SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7)))
	                            && o7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
	set(GpuCap::Tearing, tearingSupported);

	// 古い SDK には新しい SM の名前が無いので数値で試し、通った最大を取る
	int maxSm = 0;
	for (int sm = 0x69; sm >= 0x60; --sm)
	{
		D3D12_FEATURE_DATA_SHADER_MODEL s{static_cast<D3D_SHADER_MODEL>(sm)};
		if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &s, sizeof(s)))) { maxSm = static_cast<int>(s.HighestShaderModel); break; }
	}
	if (maxSm == 0) { maxSm = 0x51; }  // FL 11_0 のハードウェアは少なくとも SM 5.1
	out.maxShaderModel = shaderModelText(maxSm);
	set(GpuCap::ShaderModel66, maxSm >= 0x66);

	static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
	                                                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
	fl.NumFeatureLevels = static_cast<UINT>(std::size(kLevels));
	fl.pFeatureLevelsRequested = kLevels;
	out.maxFeatureLevel = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))
	                          ? featureLevelText(fl.MaxSupportedFeatureLevel) : std::string("11_0");

	// 欠けたものとして指定した機能は、実際にあっても外す。FeatureLevel11 はデバイス作成時に適用済みで、WARP には適用しない
	CapSet masked = fake.missing;
	masked.reset(static_cast<std::size_t>(GpuCap::FeatureLevel11));
	c &= ~masked;
	out.caps = c;
}

[[nodiscard]] inline bool tearingSupportedBy(IDXGIFactory* factory)
{
	Microsoft::WRL::ComPtr<IDXGIFactory5> f5;
	BOOL allow = FALSE;
	return factory != nullptr && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(f5.GetAddressOf())))
	       && SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))) && allow != FALSE;
}

}  // namespace detail

struct DeviceChoice
{
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	Selection selection;
	AdapterProbe probe;
	bool warp = false;
	std::string reason;
	std::string reportText;
};

[[nodiscard]] inline ReportContext baseContext(const FakeMissing& fake)
{
	ReportContext ctx;
	ctx.engineVersion = MITIRU_ENGINE_VERSION;
	ctx.osVersion = detail::osVersionText();
	ctx.cpuFeatures = detail::cpuFeatureText();
	if (fake.any())
	{
		const std::string raw = env::value("MITIRU_D3D12_FAKE_MISSING");
		ctx.fakeMissingText = raw;
	}
	return ctx;
}

/// @brief 診断を書いて、ファイルがあればその場所を返す。無ければ空
inline std::string emitReport(const std::string& text)
{
	const bool wrote = writeReportFile(text);
	return wrote ? mitiru::platform::wideToUtf8(effectiveReportPath().wstring()) : std::string();
}

/// @brief WARP で動くことを知らせ、窓で終了が選ばれたらプロセスを終了する
/// @details 終了コードは 0。利用者が選んだ終了なので、stub は失敗の窓を重ねない
inline void announceWarp()
{
	const std::string path = mitiru::platform::wideToUtf8(effectiveReportPath().wstring());
	const WarpNoticeInput in{true, env::flag("MITIRU_D3D12_WARP"), playerRunSlot(),
	                         mitiru::platform::exitnotice::dialogSuppressed()};
	switch (decideWarpNotice(in))
	{
	case WarpNotice::None:
		return;
	case WarpNotice::Console:
		console::notice(warpConsoleNotice());
		console::verbose(warpNoticeText(path));
		return;
	case WarpNotice::Dialog:
		console::verbose(warpNoticeText(path));
		if (mitiru::platform::exitnotice::askContinue(L"MitiruEngine",
				mitiru::platform::utf8ToWide(warpNoticeText(path) + "\n\n" + warpChoiceLine())))
		{
			return;
		}
		std::fflush(stdout);
		std::fflush(stderr);
		std::_Exit(0);
	}
}

/// @brief デバイスを選んだ後に起きたことを、診断ファイルの末尾へ 1 節足す
inline void appendReport(const std::string& text)
{
	lastReportText() += text;
	const std::filesystem::path path = effectiveReportPath();
	if (path.empty()) { return; }
	std::ofstream out(path, std::ios::binary | std::ios::app);
	out << text;
}

/// @brief デバイスを選んだ後の準備に失敗したことを診断へ足し、利用者向けのメッセージで例外を投げる
[[noreturn]] inline void failSetup(const char* what, long hr)
{
	char detail[160];
	std::snprintf(detail, sizeof(detail), "%s 0x%08lX", what, hr);
	std::string msg = failureMessage(GpuFailure::DeviceSetup, {}, detail);
	const std::filesystem::path path = effectiveReportPath();
	if (!path.empty())
	{
		std::ofstream out(path, std::ios::binary | std::ios::app);
		out << "\n結果の訂正: デバイスを選んだ後の準備で失敗した\n" << msg << "\n";
		msg += "\n診断ファイル: " + mitiru::platform::wideToUtf8(path.wstring());
	}
	throw GpuCompatError(GpuFailure::DeviceSetup, std::move(msg));
}

[[noreturn]] inline void failWith(GpuFailure kind, const Selection& sel, const std::optional<AdapterProbe>& chosen,
                                  ReportContext ctx, std::string_view detail)
{
	std::string msg = failureMessage(kind, sel, detail);
	ctx.failure = msg;
	const std::string path = emitReport(formatReport(sel, chosen, ctx));
	if (!path.empty()) { msg += "\n診断ファイル: " + path; }
	throw GpuCompatError(kind, std::move(msg));
}

/// @brief ハードウェアを優先順に調べ、動く 1 台でデバイスを作る。無ければ (許されれば) WARP
/// @param allowWarp false なら WARP へ落ちず失敗にする
inline DeviceChoice createBestDevice(IDXGIFactory4* factory, bool allowWarp)
{
	using Microsoft::WRL::ComPtr;
	FakeMissing fake = parseFakeMissing(env::value("MITIRU_D3D12_FAKE_MISSING"));
	fakeMissingSlot() = fake;
	for (const std::string& u : fake.unknown)
	{
		console::noticef("MITIRU_D3D12_FAKE_MISSING の %s を読めません", u.c_str());
	}
	const bool tearing = detail::tearingSupportedBy(factory);
	ReportContext ctx = baseContext(fake);

	std::vector<ComPtr<IDXGIAdapter1>> adapters;
	std::vector<AdapterInfo> infos;
	ComPtr<IDXGIFactory6> f6;
	const bool forceWarp = env::flag("MITIRU_D3D12_WARP");
	if (!forceWarp)
	{
		const bool pref = SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(f6.GetAddressOf())));
		for (UINT i = 0;; ++i)
		{
			ComPtr<IDXGIAdapter1> a;
			const HRESULT hr = pref ? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(a.GetAddressOf()))
			                        : factory->EnumAdapters1(i, a.GetAddressOf());
			if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr)) { break; }
			AdapterInfo info = detail::infoOf(a.Get(), static_cast<int>(infos.size()));
			if (info.software) { continue; }
			infos.push_back(std::move(info));
			adapters.push_back(std::move(a));
		}
	}

	DeviceChoice out;
	std::map<int, ComPtr<ID3D12Device>> devices;
	std::map<int, AdapterProbe> probes;
	const auto probeOne = [&](const AdapterInfo& info) {
		AdapterProbe p;
		p.info = info;
		HRESULT hr = fake.has(GpuCap::FeatureLevel11) ? E_FAIL : S_OK;
		ComPtr<ID3D12Device> dev;
		if (SUCCEEDED(hr)) { hr = D3D12CreateDevice(adapters[static_cast<std::size_t>(info.index)].Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(dev.GetAddressOf())); }
		p.hresult = static_cast<std::int32_t>(hr);
		p.deviceCreated = SUCCEEDED(hr);
		if (p.deviceCreated)
		{
			D3D12_FEATURE_DATA_ARCHITECTURE arch{};
			if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch)))) { p.info.integrated = arch.UMA != FALSE; }
			detail::queryCaps(dev.Get(), tearing, fake, p);
			devices[info.index] = dev;
		}
		probes[info.index] = p;
		return p;
	};

	const std::string overrideSpec = env::value("MITIRU_D3D12_ADAPTER");
	out.selection = selectAdapter(infos, probeOne, overrideSpec);
	if (out.selection.chosen >= 0)
	{
		const int idx = infos[static_cast<std::size_t>(out.selection.chosen)].index;
		out.device = devices[idx];
		out.probe = probes[idx];
		out.reason = out.selection.reason;
		ctx.chosenWhy = out.reason;
		out.reportText = formatReport(out.selection, out.probe, ctx);
		return out;
	}

	bool anySkippedForCaps = false;
	for (const AdapterLine& l : out.selection.lines)
	{
		if (l.verdict == AdapterVerdict::Skipped && l.why.rfind("必須の機能が無い", 0) == 0) { anySkippedForCaps = true; }
	}
	if (out.selection.overrideMissed) { failWith(GpuFailure::OverrideMissed, out.selection, std::nullopt, ctx, {}); }
	if (!allowWarp && !forceWarp)
	{
		failWith(anySkippedForCaps ? GpuFailure::MissingRequired : GpuFailure::NoAdapter, out.selection, std::nullopt, ctx, {});
	}

	// ハードウェアで動くものが無い。WARP (ソフトウェア) は動くが、とても遅い
	ComPtr<IDXGIAdapter> warp;
	HRESULT hr = fake.warpFails ? E_FAIL : factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()));
	if (SUCCEEDED(hr)) { hr = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(out.device.GetAddressOf())); }
	if (FAILED(hr))
	{
		char b[48];
		std::snprintf(b, sizeof(b), "WARP 0x%08X", static_cast<unsigned>(hr));
		failWith(anySkippedForCaps ? GpuFailure::MissingRequired : GpuFailure::NoAdapter, out.selection, std::nullopt, ctx, b);
	}
	out.warp = true;
	ComPtr<IDXGIAdapter1> warp1;
	AdapterProbe wp;
	if (SUCCEEDED(warp.As(&warp1))) { wp.info = detail::infoOf(warp1.Get(), -1); }
	wp.info.software = true;
	wp.deviceCreated = true;
	detail::queryCaps(out.device.Get(), tearing, fake, wp);
	out.probe = wp;
	wp.info.index = static_cast<int>(infos.size());
	AdapterLine warpLine{wp.info, AdapterVerdict::Chosen, {}};
	if (const std::vector<GpuCap> lacking = missingRequired(wp.caps); !lacking.empty())
	{
		warpLine.verdict = AdapterVerdict::Skipped;
		warpLine.why = "必須の機能が無い:";
		for (const GpuCap c : lacking)
		{
			warpLine.why += std::string(" ") + capInfo(c).key;
			warpLine.lacking.set(static_cast<std::size_t>(c));
		}
		out.selection.lines.push_back(warpLine);
		failWith(GpuFailure::MissingRequired, out.selection, std::nullopt, ctx, {});
	}
	out.selection.lines.push_back(warpLine);
	ctx.usingWarp = true;
	ctx.chosenWhy = forceWarp ? "MITIRU_D3D12_WARP が指定された"
	                          : (infos.empty() ? "ハードウェアのアダプタが無い" : "ハードウェアのアダプタはどれも D3D12 の必須の機能を満たさない");
	out.reason = ctx.chosenWhy;
	out.reportText = formatReport(out.selection, wp, ctx);
	return out;
}

}  // namespace mitiru::gfx::compat

#endif  // _WIN32
