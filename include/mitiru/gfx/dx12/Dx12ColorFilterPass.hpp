#pragma once
/// @file Dx12ColorFilterPass.hpp
/// @brief 仕上がったフレーム (UI を重ねた後のバックバッファ) 全体に、色の 3x3 行列を掛ける (色覚のフィルタ)。
/// @details バックバッファを写しへ複製し、写しを読んで行列を掛けた色をバックバッファへ描き戻す。
///          行列は線形 RGB に掛けるので、UNORM の描画先では sRGB を一度ほどいてから掛け、掛けた後で戻す。
///          有効な時だけ Engine が呼ぶ。失敗したら何もしない (フレームはフィルタ無しのまま出る)。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12FenceWait.hpp>
#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/gfx/dx12/Dx12ShaderCompiler.hpp>
#include <mitiru/render/ColorVision.hpp>

namespace mitiru::gfx
{

inline constexpr std::string_view kColorFilterHlsl = R"(
cbuffer Params : register(b0) { float4 row0; float4 row1; float4 row2; float4 flags; };
Texture2D<float4> src : register(t0);

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 toLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 toSrgb(float3 c) { c = saturate(c); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }

float4 PSMain(float4 pos : SV_Position) : SV_Target
{
	float4 c = src.Load(int3(pos.xy, 0));
	float3 l = flags.x > 0.5 ? toLinear(c.rgb) : c.rgb;
	float3 o = float3(dot(row0.xyz, l), dot(row1.xyz, l), dot(row2.xyz, l));
	o = flags.x > 0.5 ? toSrgb(o) : saturate(o);
	return float4(o, c.a);
}
)";

class Dx12ColorFilterPass
{
public:
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	Dx12ColorFilterPass() = default;
	Dx12ColorFilterPass(const Dx12ColorFilterPass&) = delete;
	Dx12ColorFilterPass& operator=(const Dx12ColorFilterPass&) = delete;
	~Dx12ColorFilterPass()
	{
		waitGpu();
		if (m_fenceEvent != nullptr) { ::CloseHandle(m_fenceEvent); }
	}

	/// @brief target (RENDER_TARGET 状態のバックバッファ) に行列を掛ける。終わった時も RENDER_TARGET のまま。
	bool apply(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* target, const render::ColorMatrix3& m)
	{
		if (device == nullptr || queue == nullptr || target == nullptr) { return false; }
		const D3D12_RESOURCE_DESC desc = target->GetDesc();
		if (desc.SampleDesc.Count != 1 || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) { return false; }
		m_device = device;
		m_queue = queue;
		if (!ensureShared() || !ensurePipeline(desc.Format) || !ensureCopy(desc)) { return false; }
		// 定数バッファは 1 つを使い回すので、前のフレームの描画が読み終えてから書く
		waitGpu();
		writeParams(m, desc.Format);
		if (FAILED(m_alloc->Reset()) || FAILED(m_list->Reset(m_alloc.Get(), m_pso.Get()))) { return false; }
		recordCopy(target);
		recordDraw(target, desc);
		m_list->Close();
		ID3D12CommandList* lists[] = { m_list.Get() };
		m_queue->ExecuteCommandLists(1, lists);
		m_queue->Signal(m_fence.Get(), ++m_fenceValue);
		return true;
	}

private:
	[[nodiscard]] static bool isSrgb(DXGI_FORMAT f) noexcept
	{
		return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	}

	void writeParams(const render::ColorMatrix3& m, DXGI_FORMAT format) noexcept
	{
		float p[16] = {};
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c) { p[r * 4 + c] = m[static_cast<std::size_t>(r * 3 + c)]; }
		}
		// SRGB の形式は読み書きでハードウェアが変換するので、シェーダーで二重にほどかない
		p[12] = isSrgb(format) ? 0.0f : 1.0f;
		std::memcpy(m_cbMapped, p, sizeof(p));
	}

	bool ensureShared()
	{
		if (m_rootSig) { return true; }
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[0].DescriptorTable.NumDescriptorRanges = 1;
		params[0].DescriptorTable.pDescriptorRanges = &range;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rs = {};
		rs.NumParameters = 2;
		rs.pParameters = params;
		ComPtr<ID3DBlob> sig, err;
		if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) { return false; }
		if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)))) { return false; }
		return createListAndHeaps();
	}

	bool createListAndHeaps()
	{
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc)))) { return false; }
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_list)))) { return false; }
		m_list->Close();
		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) { return false; }
		m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		D3D12_DESCRIPTOR_HEAP_DESC rh = {};
		rh.NumDescriptors = 1;
		rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (FAILED(m_device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&m_rtvHeap)))) { return false; }
		D3D12_DESCRIPTOR_HEAP_DESC sh = {};
		sh.NumDescriptors = 1;
		sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)))) { return false; }
		if (FAILED(createGpuBuffer(m_device, D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ, m_cb))) { return false; }
		D3D12_RANGE none = { 0, 0 };
		return SUCCEEDED(m_cb->Map(0, &none, reinterpret_cast<void**>(&m_cbMapped)));
	}

	bool ensurePipeline(DXGI_FORMAT format)
	{
		if (m_pso && m_psoFormat == format) { return true; }
		ComPtr<ID3DBlob> vs, ps, err;
		if (FAILED(compileDx12Shader(kColorFilterHlsl, "VSMain", "vs_5_0", 0, &vs, &err))) { return false; }
		if (FAILED(compileDx12Shader(kColorFilterHlsl, "PSMain", "ps_5_0", 0, &ps, &err))) { return false; }
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_rootSig.Get();
		pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = UINT_MAX;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = format;
		pd.SampleDesc.Count = 1;
		m_pso.Reset();
		if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { return false; }
		m_psoFormat = format;
		return true;
	}

	bool ensureCopy(const D3D12_RESOURCE_DESC& target)
	{
		if (m_copy && m_copyWidth == target.Width && m_copyHeight == target.Height && m_copyFormat == target.Format) { return true; }
		waitGpu();
		D3D12_RESOURCE_DESC d = {};
		d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		d.Width = target.Width;
		d.Height = target.Height;
		d.DepthOrArraySize = 1;
		d.MipLevels = 1;
		d.Format = target.Format;
		d.SampleDesc.Count = 1;
		if (FAILED(createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, m_copy))) { return false; }
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = target.Format;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = 1;
		m_device->CreateShaderResourceView(m_copy.Get(), &sv, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
		m_copyWidth = target.Width;
		m_copyHeight = target.Height;
		m_copyFormat = target.Format;
		return true;
	}

	void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = r;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_list->ResourceBarrier(1, &b);
	}

	void recordCopy(ID3D12Resource* target)
	{
		barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_list->CopyResource(m_copy.Get(), target);
		barrier(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		barrier(m_copy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	void recordDraw(ID3D12Resource* target, const D3D12_RESOURCE_DESC& desc)
	{
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		m_device->CreateRenderTargetView(target, nullptr, rtv);
		m_list->SetGraphicsRootSignature(m_rootSig.Get());
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
		m_list->SetDescriptorHeaps(1, heaps);
		m_list->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
		m_list->SetGraphicsRootConstantBufferView(1, m_cb->GetGPUVirtualAddress());
		m_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		const D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f };
		const D3D12_RECT sc = { 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
		m_list->RSSetViewports(1, &vp);
		m_list->RSSetScissorRects(1, &sc);
		m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_list->DrawInstanced(3, 1, 0, 0);
		barrier(m_copy.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	}

	void waitGpu()
	{
		if (!m_fence) { return; }
		(void)waitForFenceOrReport(m_fence.Get(), m_fenceValue, m_fenceEvent, "Dx12ColorFilterPass");
	}

	ID3D12Device* m_device = nullptr;
	ID3D12CommandQueue* m_queue = nullptr;
	ComPtr<ID3D12RootSignature> m_rootSig;
	ComPtr<ID3D12PipelineState> m_pso;
	DXGI_FORMAT m_psoFormat = DXGI_FORMAT_UNKNOWN;
	GpuResource m_copy;
	UINT64 m_copyWidth = 0;
	UINT m_copyHeight = 0;
	DXGI_FORMAT m_copyFormat = DXGI_FORMAT_UNKNOWN;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_srvHeap;
	GpuResource m_cb;
	std::uint8_t* m_cbMapped = nullptr;
	ComPtr<ID3D12CommandAllocator> m_alloc;
	ComPtr<ID3D12GraphicsCommandList> m_list;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent = nullptr;
	UINT64 m_fenceValue = 0;
};

} // namespace mitiru::gfx

#endif // _WIN32
