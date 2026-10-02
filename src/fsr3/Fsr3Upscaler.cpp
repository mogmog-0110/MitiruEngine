// render/dx12/Fsr3Upscaler.hpp の実装。FidelityFX SDK v1.1.4 の FSR3 upscaler を DX12 backend で動かす。
#include <mitiru/render/dx12/Fsr3Upscaler.hpp>

#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace mitiru::render::dx12
{

namespace
{

void onFfxMessage(FfxMsgType type, const wchar_t* message)
{
	std::fwprintf(stderr, L"[mitiru][fsr3] %ls: %ls\n", type == FFX_MESSAGE_TYPE_ERROR ? L"error" : L"warning",
	              message != nullptr ? message : L"");
}

[[nodiscard]] D3D12_RESOURCE_STATES toDx12State(FfxResourceStates s) noexcept
{
	switch (s)
	{
	case FFX_RESOURCE_STATE_UNORDERED_ACCESS: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	case FFX_RESOURCE_STATE_RENDER_TARGET: return D3D12_RESOURCE_STATE_RENDER_TARGET;
	case FFX_RESOURCE_STATE_COMPUTE_READ: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	default: return D3D12_RESOURCE_STATE_COMMON;
	}
}

[[nodiscard]] FfxResourceStates toFfxState(D3D12_RESOURCE_STATES s) noexcept
{
	switch (s)
	{
	case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return FFX_RESOURCE_STATE_UNORDERED_ACCESS;
	case D3D12_RESOURCE_STATE_RENDER_TARGET: return FFX_RESOURCE_STATE_RENDER_TARGET;
	case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return FFX_RESOURCE_STATE_COMPUTE_READ;
	case D3D12_RESOURCE_STATE_COPY_SOURCE: return FFX_RESOURCE_STATE_COPY_SRC;
	case D3D12_RESOURCE_STATE_COPY_DEST: return FFX_RESOURCE_STATE_COPY_DEST;
	case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return FFX_RESOURCE_STATE_PIXEL_READ;
	default: return FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
	}
}

[[nodiscard]] FfxResource wrap(const Fsr3Texture& t, const wchar_t* name, FfxResourceUsage usage = FFX_RESOURCE_USAGE_READ_ONLY)
{
	if (t.resource == nullptr) { return ffxGetResourceDX12(nullptr, FfxResourceDescription{}, name); }
	return ffxGetResourceDX12(t.resource, ffxGetResourceDescriptionDX12(t.resource, usage), name, toFfxState(t.state));
}

} // namespace

struct Fsr3Upscaler::Impl
{
	std::vector<unsigned char>                 scratch;
	FfxFsr3UpscalerContext                     context{};
	Microsoft::WRL::ComPtr<ID3D12Resource>     dilatedDepth;
	Microsoft::WRL::ComPtr<ID3D12Resource>     dilatedMotion;
	Microsoft::WRL::ComPtr<ID3D12Resource>     prevNearestDepth;
	FfxResourceStates                          dilatedDepthState = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
	FfxResourceStates                          dilatedMotionState = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
	FfxResourceStates                          prevNearestDepthState = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
	std::uint32_t                              displayWidth = 0;
	std::uint32_t                              displayHeight = 0;
	bool                                       contextAlive = false;

	~Impl()
	{
		if (contextAlive) { ffxFsr3UpscalerContextDestroy(&context); }
	}
};

Fsr3Upscaler::Fsr3Upscaler() = default;
Fsr3Upscaler::~Fsr3Upscaler() = default;

namespace
{

/// @brief FSR が他のエフェクトと分け合う前提の資源 (膨らませた深度・動き、前フレームの最も近い深度) を作る
[[nodiscard]] bool createShared(ID3D12Device* device, const FfxCreateResourceDescription& d,
                                Microsoft::WRL::ComPtr<ID3D12Resource>& out)
{
	D3D12_RESOURCE_DESC rd = {};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rd.Width = d.resourceDescription.width;
	rd.Height = d.resourceDescription.height;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.Format = ffxGetDX12FormatFromSurfaceFormat(d.resourceDescription.format);
	rd.SampleDesc.Count = 1;
	const auto usage = static_cast<std::uint32_t>(d.resourceDescription.usage);
	if ((usage & FFX_RESOURCE_USAGE_UAV) != 0) { rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; }
	if ((usage & FFX_RESOURCE_USAGE_RENDERTARGET) != 0) { rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; }
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, toDx12State(d.initialState), nullptr,
	                                           IID_PPV_ARGS(out.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	out->SetName(d.name);
	return true;
}

} // namespace

bool Fsr3Upscaler::create(ID3D12Device* device, std::uint32_t renderWidth, std::uint32_t renderHeight,
                          std::uint32_t displayWidth, std::uint32_t displayHeight)
{
	destroy();
	auto impl = std::make_unique<Impl>();
	impl->scratch.resize(ffxGetScratchMemorySizeDX12(FFX_FSR3UPSCALER_CONTEXT_COUNT));
	FfxFsr3UpscalerContextDescription desc = {};
	if (ffxGetInterfaceDX12(&desc.backendInterface, ffxGetDeviceDX12(device), impl->scratch.data(), impl->scratch.size(),
	                        FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK)
	{
		m_error = "FSR3 の DX12 backend を作れない";
		return false;
	}
	desc.maxRenderSize = {renderWidth, renderHeight};
	desc.maxUpscaleSize = {displayWidth, displayHeight};
	desc.fpMessage = onFfxMessage;
#ifndef NDEBUG
	desc.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
#endif
	const FfxErrorCode rc = ffxFsr3UpscalerContextCreate(&impl->context, &desc);
	if (rc != FFX_OK)
	{
		m_error = "FSR3 の文脈を作れない (ffxFsr3UpscalerContextCreate = " + std::to_string(rc) +
		          "、SM 6.2 の compute を持たない GPU か、シェーダーの permutation が無い)";
		return false;
	}
	impl->contextAlive = true;
	FfxFsr3UpscalerSharedResourceDescriptions shared = {};
	if (ffxFsr3UpscalerGetSharedResourceDescriptions(&impl->context, &shared) != FFX_OK ||
	    !createShared(device, shared.dilatedDepth, impl->dilatedDepth) ||
	    !createShared(device, shared.dilatedMotionVectors, impl->dilatedMotion) ||
	    !createShared(device, shared.reconstructedPrevNearestDepth, impl->prevNearestDepth))
	{
		m_error = "FSR3 の共有資源を作れない";
		return false;
	}
	impl->dilatedDepthState = shared.dilatedDepth.initialState;
	impl->dilatedMotionState = shared.dilatedMotionVectors.initialState;
	impl->prevNearestDepthState = shared.reconstructedPrevNearestDepth.initialState;
	impl->displayWidth = displayWidth;
	impl->displayHeight = displayHeight;
	m_impl = std::move(impl);
	m_error.clear();
	return true;
}

void Fsr3Upscaler::destroy() noexcept { m_impl.reset(); }

bool Fsr3Upscaler::ready() const noexcept { return m_impl != nullptr; }

bool Fsr3Upscaler::dispatch(const Fsr3Dispatch& d)
{
	if (!m_impl || d.commandList == nullptr) { return false; }
	Impl& s = *m_impl;
	FfxFsr3UpscalerDispatchDescription dd = {};
	dd.commandList = ffxGetCommandListDX12(d.commandList);
	dd.color = wrap(d.color, L"FSR3_InputColor");
	dd.depth = wrap(d.depth, L"FSR3_InputDepth");
	dd.motionVectors = wrap(d.motionVectors, L"FSR3_InputMotionVectors");
	dd.reactive = wrap(d.reactive, L"FSR3_InputReactiveMask");
	dd.output = wrap(d.output, L"FSR3_Output", FFX_RESOURCE_USAGE_UAV);
	dd.dilatedDepth = ffxGetResourceDX12(s.dilatedDepth.Get(), ffxGetResourceDescriptionDX12(s.dilatedDepth.Get(), FFX_RESOURCE_USAGE_UAV),
	                                     L"FSR3_DilatedDepth", s.dilatedDepthState);
	dd.dilatedMotionVectors = ffxGetResourceDX12(s.dilatedMotion.Get(), ffxGetResourceDescriptionDX12(s.dilatedMotion.Get(), FFX_RESOURCE_USAGE_UAV),
	                                             L"FSR3_DilatedMotionVectors", s.dilatedMotionState);
	dd.reconstructedPrevNearestDepth = ffxGetResourceDX12(
		s.prevNearestDepth.Get(), ffxGetResourceDescriptionDX12(s.prevNearestDepth.Get(), FFX_RESOURCE_USAGE_UAV),
		L"FSR3_ReconstructedPrevNearestDepth", s.prevNearestDepthState);
	dd.jitterOffset = {d.jitterX, d.jitterY};
	dd.motionVectorScale = {d.motionScaleX, d.motionScaleY};
	dd.renderSize = {d.renderWidth, d.renderHeight};
	dd.upscaleSize = {s.displayWidth, s.displayHeight};
	dd.enableSharpening = d.sharpness > 0.0f;
	dd.sharpness = d.sharpness;
	dd.frameTimeDelta = d.frameTimeMs;
	dd.preExposure = 1.0f;
	dd.reset = d.reset;
	dd.cameraNear = d.cameraNear;
	dd.cameraFar = d.cameraFar;
	dd.cameraFovAngleVertical = d.fovY;
	dd.viewSpaceToMetersFactor = 1.0f;
	const FfxErrorCode rc = ffxFsr3UpscalerContextDispatch(&s.context, &dd);
	if (rc != FFX_OK)
	{
		m_error = "ffxFsr3UpscalerContextDispatch = " + std::to_string(rc);
		return false;
	}
	return true;
}

} // namespace mitiru::render::dx12
