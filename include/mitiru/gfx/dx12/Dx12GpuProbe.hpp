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
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/BootTrace.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/gfx/GpuCompat.hpp>
#include <mitiru/gfx/GpuNotice.hpp>
#include <mitiru/platform/BootReport.hpp>
#include <mitiru/platform/ExitNotice.hpp>
#include <mitiru/platform/SystemInfoText.hpp>
#include <mitiru/platform/Utf8Args.hpp>

#ifndef MITIRU_ENGINE_VERSION
#define MITIRU_ENGINE_VERSION "unknown"
#endif

namespace mitiru::gfx::compat
{

inline constexpr DWORD kChildProbeTimeoutMs = 20000;
inline constexpr DWORD kTimedOutExitCode = 0xFFFFFFFFu;
inline constexpr DWORD kChildOk = 0;
inline constexpr DWORD kChildCreateFailed = 1;
inline constexpr DWORD kChildNoFactory = 3;
inline constexpr DWORD kChildNoAdapter = 4;

struct ChildProbeSetting
{
	bool enabled = false;
	std::filesystem::path exe;
	std::filesystem::path skipFile;
};

[[nodiscard]] inline ChildProbeSetting& childProbeSlot()
{
	static ChildProbeSetting slot;
	return slot;
}

[[nodiscard]] inline bool childProbeActive()
{
	const std::string value = env::value("MITIRU_GPU_PROBE_CHILD");
	if (value == "0") { return false; }
	if (value == "1") { return !childProbeSlot().exe.empty(); }
	return childProbeSlot().enabled && !childProbeSlot().exe.empty();
}

[[nodiscard]] inline DWORD childProbeTimeoutMs()
{
	const std::string value = env::value("MITIRU_GPU_PROBE_TIMEOUT_MS");
	if (!value.empty())
	{
		char* end = nullptr;
		const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
		if (end != value.c_str() && *end == '\0' && parsed > 0) { return static_cast<DWORD>(parsed); }
	}
	return kChildProbeTimeoutMs;
}

struct ChildProbeResult
{
	bool ran = false;
	bool crashed = false;
	bool timedOut = false;
	DWORD exitCode = 0;
};

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
	const std::wstring bootDir = mitiru::platform::bootreport::dirFromEnv();
	if (!bootDir.empty())
	{
		return std::filesystem::path(bootDir) / mitiru::platform::bootreport::kFileName;
	}
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

[[nodiscard]] inline std::string osVersionText() { return mitiru::platform::sysinfo::osVersionText(); }

[[nodiscard]] inline std::string cpuFeatureText() { return mitiru::platform::sysinfo::cpuFeatureText(); }

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

inline void enumerateHardware(IDXGIFactory4* factory, std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>>& adapters,
                              std::vector<AdapterInfo>& infos)
{
	Microsoft::WRL::ComPtr<IDXGIFactory6> f6;
	const bool pref = SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(f6.GetAddressOf())));
	for (UINT i = 0;; ++i)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		const HRESULT hr = pref
			? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()))
			: factory->EnumAdapters1(i, adapter.GetAddressOf());
		if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr)) { break; }
		AdapterInfo info = infoOf(adapter.Get(), static_cast<int>(infos.size()));
		if (info.software) { continue; }
		infos.push_back(std::move(info));
		adapters.push_back(std::move(adapter));
	}
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

inline int probeChildMain(int adapterIndex)
{
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

	Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())))) { return static_cast<int>(kChildNoFactory); }

	std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;
	std::vector<AdapterInfo> infos;
	detail::enumerateHardware(factory.Get(), adapters, infos);
	if (adapterIndex < 0 || static_cast<std::size_t>(adapterIndex) >= adapters.size()) { return static_cast<int>(kChildNoAdapter); }

	const std::string fault = env::value("MITIRU_TEST_PROBE_FAULT");
	const std::size_t colon = fault.find(':');
	const std::string kind = fault.substr(0, colon);
	bool matched = !kind.empty() && colon == std::string::npos;
	if (!matched && colon != std::string::npos)
	{
		const std::string target = fault.substr(colon + 1);
		bool digits = !target.empty();
		for (const char c : target)
		{
			if (c < '0' || c > '9') { digits = false; break; }
		}
		if (digits)
		{
			matched = std::atoi(target.c_str()) == adapterIndex;
		}
		else
		{
			auto lower = [](std::string text) {
				for (char& c : text)
				{
					if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
				}
				return text;
			};
			matched = lower(infos[static_cast<std::size_t>(adapterIndex)].name).find(lower(target)) != std::string::npos;
		}
	}
	if (matched)
	{
		if (kind == "access") { *static_cast<volatile int*>(nullptr) = 1; }
		if (kind == "illegal") { __ud2(); }
		if (kind == "hang") { Sleep(INFINITE); }
	}

	Microsoft::WRL::ComPtr<ID3D12Device> device;
	const HRESULT hr = D3D12CreateDevice(adapters[static_cast<std::size_t>(adapterIndex)].Get(), D3D_FEATURE_LEVEL_11_0,
	                                     IID_PPV_ARGS(device.GetAddressOf()));
	if (FAILED(hr)) { return static_cast<int>(kChildCreateFailed); }

	AdapterProbe probe;
	probe.info = infos[static_cast<std::size_t>(adapterIndex)];
	probe.deviceCreated = true;
	probe.hresult = static_cast<std::int32_t>(hr);
	const FakeMissing fake = parseFakeMissing(env::value("MITIRU_D3D12_FAKE_MISSING"));
	detail::queryCaps(device.Get(), detail::tearingSupportedBy(factory.Get()), fake, probe);
	return 0;
}

inline ChildProbeResult runProbeChild(const std::filesystem::path& exe, int adapterIndex, DWORD timeoutMs)
{
	mitiru::debug::boottrace::mark("probe_child_begin");
	struct EndMark { ~EndMark() { mitiru::debug::boottrace::mark("probe_child_end"); } } endMark;
	ChildProbeResult out;
	std::wstring command = L"\"" + exe.wstring() + L"\" --gpu-probe-child " + std::to_wstring(adapterIndex);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
	{
		return out;
	}

	out.ran = true;
	CloseHandle(process.hThread);
	const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
	if (wait == WAIT_TIMEOUT)
	{
		TerminateProcess(process.hProcess, kTimedOutExitCode);
		WaitForSingleObject(process.hProcess, INFINITE);
		out.timedOut = true;
		out.exitCode = kTimedOutExitCode;
	}
	else if (wait == WAIT_OBJECT_0)
	{
		if (!GetExitCodeProcess(process.hProcess, &out.exitCode)) { out.exitCode = kTimedOutExitCode; }
	}
	else
	{
		out.exitCode = kTimedOutExitCode;
	}
	CloseHandle(process.hProcess);
	// 3 と 4 は子の側の準備の失敗 (列挙の食い違いなど)。ドライバーが落ちた証拠ではないので、親が自分で検査する
	out.crashed = !out.timedOut && out.exitCode != kChildOk && out.exitCode != kChildCreateFailed
	              && out.exitCode != kChildNoFactory && out.exitCode != kChildNoAdapter;
	return out;
}

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
		msg += "\n診断ファイル: " + mitiru::platform::wideToUtf8(path.wstring()) + "\n動かないときは、このファイルを送ってください。";
	}
	throw GpuCompatError(GpuFailure::DeviceSetup, std::move(msg));
}

[[noreturn]] inline void failWith(GpuFailure kind, const Selection& sel, const std::optional<AdapterProbe>& chosen,
                                  ReportContext ctx, std::string_view detail)
{
	std::string msg = failureMessage(kind, sel, detail);
	ctx.failure = msg;
	const std::string path = emitReport(formatReport(sel, chosen, ctx));
	if (!path.empty()) { msg += "\n診断ファイル: " + path + "\n動かないときは、このファイルを送ってください。"; }
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
	const bool forceWarp = env::flag("MITIRU_D3D12_WARP") || retryWithWarpSlot();
	warpUsedSlot() = false;
	if (!forceWarp) { detail::enumerateHardware(factory, adapters, infos); }

	SkipList skipList;
	const std::wstring skipFromEnv = env::wideValue(L"MITIRU_GPU_SKIP_FILE");
	const std::filesystem::path skipFile = !skipFromEnv.empty() ? std::filesystem::path(skipFromEnv) : childProbeSlot().skipFile;
	if (!skipFile.empty())
	{
		std::ifstream in(skipFile, std::ios::binary);
		if (in)
		{
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			skipList = parseSkipList(text);
		}
	}

	// 子プロセスの検査を通ったアダプタの記録 (OkList)。デバイスを作っている最中に落ちた前の起動の印が残っていれば、
	// その鍵を記録から外して、今回は検査し直す
	const std::filesystem::path okFile = skipFile.empty() ? std::filesystem::path{} : skipFile.parent_path() / L"gpu_ok.txt";
	// 印は、起動したプロセスごとに別のファイルにする。同時に起動した別のプロセスが、デバイスを作っている最中の印を消さないため
	const std::wstring pendingPrefix = L"gpu_probe_pending.";
	const std::filesystem::path pendingFile = skipFile.empty()
		? std::filesystem::path{}
		: skipFile.parent_path() / (pendingPrefix + std::to_wstring(static_cast<unsigned long>(GetCurrentProcessId())) + L".txt");
	const auto readText = [](const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};
	const auto writeText = [](const std::filesystem::path& path, const std::string& text, std::ios::openmode mode) {
		std::error_code ec;
		if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path(), ec); }
		std::ofstream out(path, std::ios::binary | mode);
		out << text;
	};
	OkList okList;
	if (!okFile.empty())
	{
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(okFile.parent_path(), ec))
		{
			const std::wstring name = entry.path().filename().wstring();
			if (name.rfind(pendingPrefix, 0) != 0) { continue; }
			// 持ち主のプロセスがまだ生きていれば、デバイスを作っている最中の別の起動。触らない
			const unsigned long owner = wcstoul(name.c_str() + pendingPrefix.size(), nullptr, 10);
			if (owner != 0)
			{
				HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, owner);
				if (h != nullptr)
				{
					const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
					CloseHandle(h);
					if (alive) { continue; }
				}
			}
			const OkList pendingKeys = parseOkList(readText(entry.path()));
			std::filesystem::remove(entry.path(), ec);
			if (!pendingKeys.keys.empty()) { writeText(okFile, withoutOkKey(readText(okFile), pendingKeys.keys.front()), std::ios::trunc); }
		}
		okList = parseOkList(readText(okFile));
	}

	DeviceChoice out;
	std::map<int, ComPtr<ID3D12Device>> devices;
	std::map<int, AdapterProbe> probes;
	// WARP でのやり直しでは、ハードウェアを指す指定は意味がない (合うアダプタが無いと失敗してしまう)
	const std::string overrideSpec = retryWithWarpSlot() ? std::string() : env::value("MITIRU_D3D12_ADAPTER");
	const auto probeOne = [&](const AdapterInfo& info) {
		AdapterProbe p;
		p.info = info;
		const std::string key = skipKey(info);
		if (overrideSpec.empty() && skipList.contains(key))
		{
			p.skippedByHistory = true;
			probes[info.index] = p;
			return p;
		}
		// 同じドライバーと engine の版で、前の起動が最後までデバイスを作れていたなら、子プロセスを省く
		const bool wantChild = childProbeActive() && !fake.has(GpuCap::FeatureLevel11);
		const std::string okKeyText = okKey(info, MITIRU_ENGINE_VERSION);
		const bool trusted = wantChild && overrideSpec.empty() && !okFile.empty() && okList.contains(okKeyText);
		bool childPassed = false;
		if (trusted) { mitiru::debug::boottrace::mark("probe_child_skipped"); }
		if (wantChild && !trusted)
		{
			const ChildProbeResult child = runProbeChild(childProbeSlot().exe, info.index, childProbeTimeoutMs());
			childPassed = child.ran && !child.crashed && !child.timedOut && child.exitCode == kChildOk;
			if (child.crashed || child.timedOut)
			{
				p.probeCrashed = child.crashed;
				p.probeTimedOut = child.timedOut;
				p.hresult = static_cast<std::int32_t>(child.timedOut ? kTimedOutExitCode : child.exitCode);
				p.deviceCreated = false;
				if (overrideSpec.empty() && !skipFile.empty())
				{
					std::error_code ec;
					if (skipFile.has_parent_path()) { std::filesystem::create_directories(skipFile.parent_path(), ec); }
					std::ofstream skipOut(skipFile, std::ios::binary | std::ios::app);
					if (skipOut)
					{
						skipOut << formatSkipLine(key, child.timedOut ? kSkipReasonTimeout : "検査の子プロセスが落ちた");
					}
					if (child.timedOut) { skipList.addTimeout(key); }
					else { skipList.add(key); }
				}
				probes[info.index] = p;
				return p;
			}
		}
		HRESULT hr = fake.has(GpuCap::FeatureLevel11) ? E_FAIL : S_OK;
		ComPtr<ID3D12Device> dev;
		mitiru::debug::boottrace::mark("probe_create_device_begin");
		// 記録を頼りに子プロセスを省いたときは、デバイスを作り終えるまで印を置く。ここで落ちると印が残り、次の起動が検査し直す
		const bool markPending = trusted && !pendingFile.empty();
		if (markPending) { writeText(pendingFile, formatOkLine(okKeyText), std::ios::trunc); }
		if (SUCCEEDED(hr)) { hr = D3D12CreateDevice(adapters[static_cast<std::size_t>(info.index)].Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(dev.GetAddressOf())); }
		mitiru::debug::boottrace::mark("probe_create_device_end");
		p.hresult = static_cast<std::int32_t>(hr);
		p.deviceCreated = SUCCEEDED(hr);
		if (p.deviceCreated)
		{
			D3D12_FEATURE_DATA_ARCHITECTURE arch{};
			if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch)))) { p.info.integrated = arch.UMA != FALSE; }
			detail::queryCaps(dev.Get(), tearing, fake, p);
			devices[info.index] = dev;
		}
		// 機能の問い合わせまで済んだ。印を外し、子プロセスが通したアダプタなら記録に足す
		if (markPending) { std::error_code removeEc; std::filesystem::remove(pendingFile, removeEc); }
		if (childPassed && p.deviceCreated && overrideSpec.empty() && !okFile.empty())
		{
			writeText(okFile, formatOkLine(okKeyText), std::ios::app);
			okList.keys.push_back(okKeyText);
		}
		probes[info.index] = p;
		return p;
	};

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
	warpUsedSlot() = true;
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
	bool anyProbeFault = false;
	for (const auto& [index, pr] : probes) { anyProbeFault = anyProbeFault || pr.probeCrashed || pr.probeTimedOut || pr.skippedByHistory; }
	ctx.chosenWhy = anyProbeFault && !forceWarp ? "ハードウェアのアダプタは検査で落ちたか、前回の検査で落ちたため使えない"
	              : forceWarp ? (retryWithWarpSlot() ? "選んだ GPU の準備に失敗したので、WARP でやり直した" : "MITIRU_D3D12_WARP が指定された")
	                          : (infos.empty() ? "ハードウェアのアダプタが無い" : "ハードウェアのアダプタはどれも D3D12 の必須の機能を満たさない");
	out.reason = ctx.chosenWhy;
	out.reportText = formatReport(out.selection, wp, ctx);
	return out;
}

}  // namespace mitiru::gfx::compat

#endif  // _WIN32
