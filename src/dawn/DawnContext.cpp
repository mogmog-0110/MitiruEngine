#include "DawnContext.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <thread>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mitiru::gfx
{

namespace
{

constexpr std::uint32_t kVendorNvidia = 0x10DE;
constexpr std::uint32_t kVendorAmd = 0x1002;
constexpr std::uint32_t kVendorIntel = 0x8086;

wgpu::BackendType backendTypeOf(DawnApi api) noexcept
{
	switch (api)
	{
	case DawnApi::D3D11:  return wgpu::BackendType::D3D11;
	case DawnApi::D3D12:  return wgpu::BackendType::D3D12;
	case DawnApi::Vulkan: return wgpu::BackendType::Vulkan;
	}
	return wgpu::BackendType::D3D11;
}

std::string utf8Path(const std::filesystem::path& p)
{
	const auto u8 = p.u8string();
	return std::string(u8.begin(), u8.end());
}

// Dawn は DLL を LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR で開くため、検索先には絶対パスが必要。相対パスでは ERROR_INVALID_PARAMETER になる
std::vector<std::string> runtimeSearchPaths()
{
	std::vector<std::string> out;
	wchar_t exe[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0)
	{
		out.push_back(utf8Path(std::filesystem::path(exe).parent_path()) + "\\");
	}
	wchar_t sys[MAX_PATH] = {};
	if (GetSystemDirectoryW(sys, MAX_PATH) > 0) { out.push_back(utf8Path(std::filesystem::path(sys)) + "\\"); }
	return out;
}

bool dxcAvailableNextToExe()
{
	wchar_t exe[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) { return false; }
	const auto dir = std::filesystem::path(exe).parent_path();
	std::error_code ec;
	return std::filesystem::exists(dir / L"dxcompiler.dll", ec) && std::filesystem::exists(dir / L"dxil.dll", ec);
}

// 選択順位。小さいほど先。CPU（ソフトウェア）は環境変数で許可したときだけ候補に残す
int adapterRank(const wgpu::AdapterInfo& info)
{
	switch (info.adapterType)
	{
	case wgpu::AdapterType::DiscreteGPU:   return 0;
	case wgpu::AdapterType::IntegratedGPU: return 1;
	case wgpu::AdapterType::Unknown:       return 2;
	case wgpu::AdapterType::CPU:           return 3;
	}
	return 2;
}

bool vendorMatches(const std::string& want, std::uint32_t id)
{
	if (want.empty() || want == "any") { return true; }
	if (want == "nvidia") { return id == kVendorNvidia; }
	if (want == "amd") { return id == kVendorAmd; }
	if (want == "intel") { return id == kVendorIntel; }
	return false;
}

struct Picked
{
	wgpu::Adapter adapter;
	std::string description;
	std::string deviceName;
	std::uint32_t vendorId = 0;
	wgpu::AdapterType adapterType = wgpu::AdapterType::Unknown;
	bool compat = false;
};

bool pickAdapter(DawnContext& ctx, DawnApi api, bool compat, Picked& out, std::string& error)
{
	wgpu::RequestAdapterOptions opts{};
	opts.backendType = backendTypeOf(api);
	opts.powerPreference = wgpu::PowerPreference::HighPerformance;
	if (compat) { opts.featureLevel = wgpu::FeatureLevel::Compatibility; }

	const std::string want = env::value("MITIRU_DAWN_VENDOR");
	const bool allowSoftware = env::flag("MITIRU_DAWN_ALLOW_SOFTWARE");
	int bestRank = 1000;
	for (const auto& native : ctx.native->EnumerateAdapters(&opts))
	{
		wgpu::Adapter adapter(native.Get());
		wgpu::AdapterInfo info{};
		adapter.GetInfo(&info);
		if (!vendorMatches(want, info.vendorID)) { continue; }
		if (info.adapterType == wgpu::AdapterType::CPU && !allowSoftware) { continue; }
		const int rank = adapterRank(info);
		if (rank < bestRank)
		{
			bestRank = rank;
			out.adapter = adapter;
			out.description = dawnStr(info.description);
			out.deviceName = dawnStr(info.device);
			out.vendorId = info.vendorID;
			out.adapterType = info.adapterType;
			out.compat = compat;
		}
	}
	if (!out.adapter)
	{
		error = std::string("Dawn の ") + std::string(dawnApiName(api)) + " で使える GPU が見つからない"
		      + (want.empty() ? "" : " (MITIRU_DAWN_VENDOR=" + want + " に合う物が無い)");
		return false;
	}
	return true;
}

// ゲームの DLL は host の static を見られないので、最初の画質を GPU で決められるよう、DX12 の経路 (Dx12Device) と同じ
// 環境変数で区分を渡す。MITIRU_GPU_CLASS_FORCE があればそれを優先する
void announceGpuClass(wgpu::AdapterType type)
{
	const std::string forced = env::value("MITIRU_GPU_CLASS_FORCE");
	const char* name = nullptr;
	switch (type)
	{
	case wgpu::AdapterType::CPU:           name = "software"; break;
	case wgpu::AdapterType::IntegratedGPU: name = "integrated"; break;
	case wgpu::AdapterType::DiscreteGPU:   name = "discrete"; break;
	default: break;
	}
	if (!forced.empty()) { _putenv_s("MITIRU_GPU_CLASS", forced.c_str()); }
	else if (name != nullptr) { _putenv_s("MITIRU_GPU_CLASS", name); }
}

} // namespace

DawnContext::~DawnContext()
{
	// device を native（instance）より先に解放し、native を最後に解放する
	queue = nullptr;
	device = nullptr;
	adapter = nullptr;
	instance = nullptr;
	native.reset();
}

std::shared_ptr<DawnContext> DawnContext::create(DawnApi api, std::string& error)
{
	auto ctx = std::make_shared<DawnContext>();
	ctx->api = api;

	const std::vector<std::string> paths = runtimeSearchPaths();
	std::vector<std::string_view> views(paths.begin(), paths.end());
	dawn::native::DawnInstanceDescriptor dawnInstance{};
	dawnInstance.additionalRuntimeSearchPaths = std::span<std::string_view>(views);
	wgpu::InstanceDescriptor idesc{};
	idesc.nextInChain = &dawnInstance;
	ctx->native = std::make_unique<dawn::native::Instance>(&idesc);
	ctx->instance = wgpu::Instance(ctx->native->Get());

	// 通常レベル、互換レベルの順に探す。D3D11 の FL 11_0 の GPU は互換レベルで見つかる
	Picked picked;
	const bool forceCompat = env::flag("MITIRU_DAWN_COMPAT");
	std::string why;
	if (forceCompat || !pickAdapter(*ctx, api, false, picked, why))
	{
		picked = Picked{};
		if (!pickAdapter(*ctx, api, true, picked, why))
		{
			error = why;
			return nullptr;
		}
	}
	ctx->adapter = picked.adapter;
	ctx->compat = picked.compat;
	ctx->description = picked.description;
	ctx->deviceName = picked.deviceName;
	ctx->vendorId = picked.vendorId;
	announceGpuClass(picked.adapterType);

	// ビルド済みの Dawn は DXC を同梱しない。exe の隣に dxcompiler.dll と dxil.dll が無ければ FXC（d3dcompiler_47）に切り替える
	static const char* kUseDxc[] = {"use_dxc"};
	wgpu::DawnTogglesDescriptor toggles{};
	if ((api == DawnApi::D3D12 || api == DawnApi::D3D11) && !dxcAvailableNextToExe())
	{
		toggles.disabledToggles = kUseDxc;
		toggles.disabledToggleCount = 1;
	}
	// 検証を省くと、互換レベルの描画ごとの確保と数 ms が消える。ただし MSAA の形式や粒のパイプラインを「作れるか」の探りは検証の誤りで判定しているので、
	// 省くと使えない物を使えると見誤る。計測のための opt-in に留める
	static const char* kSkipValidation[] = {"skip_validation"};
	const bool skipValidation = env::flag("MITIRU_DAWN_SKIP_VALIDATION");
	if (skipValidation)
	{
		toggles.enabledToggles = kSkipValidation;
		toggles.enabledToggleCount = 1;
	}

	// 互換レベルの既定上限は小さいため、アダプタが持つ上限をそのまま求める。compute は 1 グループ 128 スレッドなどに制限される
	wgpu::Limits limits{};
	ctx->adapter.GetLimits(&limits);

	wgpu::DeviceDescriptor ddesc{};
	ddesc.nextInChain = &toggles;
	ddesc.requiredLimits = &limits;
	DawnContext* raw = ctx.get();
	ddesc.SetUncapturedErrorCallback(
		[](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, DawnContext* owner) {
			owner->uncapturedErrors.fetch_add(1);
			debug::warnOnce("dawn.error." + dawnStr(message).substr(0, 80),
			                "Dawn の誤り (" + std::to_string(static_cast<unsigned>(type)) + "): " + dawnStr(message));
		},
		raw);
	ddesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
		[raw](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
			if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::FailedCreation) { return; }
			raw->lost = true;
			console::noticef("Dawn のデバイスが失われました (%s)", dawnStr(message).c_str());
		});

	bool done = false;
	std::string deviceError;
	ctx->adapter.RequestDevice(&ddesc, wgpu::CallbackMode::AllowProcessEvents,
		[&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
			if (status == wgpu::RequestDeviceStatus::Success) { ctx->device = std::move(device); }
			else { deviceError = dawnStr(message); }
			done = true;
		});
	if (!ctx->pump(done, 20000) || !ctx->device)
	{
		error = "Dawn のデバイスを作れない (" + deviceError + ")";
		return nullptr;
	}
	ctx->queue = ctx->device.GetQueue();
	return ctx;
}

bool DawnContext::pump(const bool& done, int timeoutMs) const
{
	const auto start = std::chrono::steady_clock::now();
	while (!done)
	{
		instance.ProcessEvents();
		if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) { return false; }
		if (!done) { std::this_thread::sleep_for(std::chrono::microseconds(200)); }
	}
	return true;
}

bool DawnContext::waitIdle(int timeoutMs) const
{
	bool done = false;
	queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
		[&done](wgpu::QueueWorkDoneStatus, wgpu::StringView) { done = true; });
	return pump(done, timeoutMs);
}

std::vector<std::uint8_t> DawnContext::readTexture(const wgpu::Texture& texture, std::uint32_t width, std::uint32_t height) const
{
	const std::uint32_t rowBytes = width * 4;
	const std::uint32_t paddedRow = (rowBytes + 255u) & ~255u;
	const std::uint64_t total = static_cast<std::uint64_t>(paddedRow) * height;

	wgpu::BufferDescriptor bd{};
	bd.size = total;
	bd.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
	wgpu::Buffer readback = device.CreateBuffer(&bd);

	wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
	wgpu::TexelCopyTextureInfo src{};
	src.texture = texture;
	wgpu::TexelCopyBufferInfo dst{};
	dst.buffer = readback;
	dst.layout.bytesPerRow = paddedRow;
	dst.layout.rowsPerImage = height;
	const wgpu::Extent3D extent{width, height, 1};
	encoder.CopyTextureToBuffer(&src, &dst, &extent);
	wgpu::CommandBuffer commands = encoder.Finish();
	queue.Submit(1, &commands);

	bool done = false;
	bool mapped = false;
	readback.MapAsync(wgpu::MapMode::Read, 0, total, wgpu::CallbackMode::AllowProcessEvents,
		[&](wgpu::MapAsyncStatus status, wgpu::StringView) {
			mapped = status == wgpu::MapAsyncStatus::Success;
			done = true;
		});
	if (!pump(done, 20000) || !mapped) { return {}; }

	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rowBytes) * height);
	const auto* mappedPtr = static_cast<const std::uint8_t*>(readback.GetConstMappedRange(0, total));
	if (mappedPtr != nullptr)
	{
		for (std::uint32_t y = 0; y < height; ++y)
		{
			std::memcpy(pixels.data() + static_cast<std::size_t>(y) * rowBytes, mappedPtr + static_cast<std::size_t>(y) * paddedRow, rowBytes);
		}
	}
	readback.Unmap();
	return pixels;
}

wgpu::ShaderModule DawnContext::makeShader(const std::string& wgsl, const char* label) const
{
	wgpu::ShaderSourceWGSL source{};
	source.code = wgpu::StringView(wgsl.data(), wgsl.size());
	wgpu::ShaderModuleDescriptor desc{};
	desc.nextInChain = &source;
	desc.label = label;
	return device.CreateShaderModule(&desc);
}

} // namespace mitiru::gfx
