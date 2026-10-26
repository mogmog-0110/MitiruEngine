#pragma once

/// @file Dx12HdrPresentPass.hpp
/// @brief SDR の絵 (RGBA8、sRGB 符号化) と highlight excess (RGBA16F) から、scRGB (FP16、1 = 80 nits) の Present 用の絵を書く全画面パス
/// @details 式は HdrMath.hpp の scRgbOutput。どのレンダーターゲット (スワップチェーンの実バッファ、試験の画面外の FP16) にも描けるよう、
///          リソースとディスクリプタは呼び出し側が渡す。定数とディスクリプタは frame slot ごとに持ち、GPU が前の slot を読み終えてから書き直す呼び出し側の約束に頼る。

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
#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12Shader.hpp>
#include <mitiru/render/HdrMath.hpp>

namespace mitiru::gfx
{

inline constexpr const char* kHdrPresentHlsl = R"hlsl(
Texture2D<float4> g_ldr   : register(t0);
Texture2D<float4> g_extra : register(t1);
SamplerState      s_lin   : register(s0);

cbuffer CbHdrPresent : register(b0)
{
    float Scale;      // 基準の白の nits / 80
    float ExtraOn;    // 1 なら highlight excess を足す
    float2 OutSize;   // 出力の画素数 (excess の uv を作る)
};

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 srgbToLinear(float3 c)
{
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target
{
    float3 lin = srgbToLinear(g_ldr.Load(int3(int2(pos.xy), 0)).rgb);
    if (ExtraOn > 0.5) { lin += g_extra.SampleLevel(s_lin, pos.xy / OutSize, 0).rgb; }
    return float4(lin * Scale, 1.0);
}
)hlsl";

class Dx12HdrPresentPass
{
public:
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	static constexpr UINT kSlots = 3;
	static constexpr UINT kViewsPerSlot = 2;

	/// @brief ルートシグネチャ・PSO・ディスクリプタと定数の置き場を作る。失敗は std::runtime_error
	void init(ID3D12Device* device, DXGI_FORMAT targetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT)
	{
		m_device = device;
		D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kViewsPerSlot, 0, 0, 0};
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[1].DescriptorTable = {1, &range};
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rd = {};
		rd.NumParameters = 2;
		rd.pParameters = params;
		rd.NumStaticSamplers = 1;
		rd.pStaticSamplers = &sampler;
		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
		    FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(m_rootSig.GetAddressOf()))))
		{
			throw std::runtime_error("Dx12HdrPresentPass: root signature");
		}
		const auto vs = Dx12Shader::createVertexShader(kHdrPresentHlsl, "VSMain");
		const auto ps = Dx12Shader::createPixelShader(kHdrPresentHlsl, "PSMain");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_rootSig.Get();
		pd.VS = vs.shaderBytecode();
		pd.PS = ps.shaderBytecode();
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.RasterizerState.DepthClipEnable = FALSE;
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = UINT_MAX;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = targetFormat;
		pd.SampleDesc.Count = 1;
		if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_pso.GetAddressOf()))))
		{
			throw std::runtime_error("Dx12HdrPresentPass: pipeline state");
		}
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		hd.NumDescriptors = kSlots * kViewsPerSlot;
		hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_heap.GetAddressOf()))))
		{
			throw std::runtime_error("Dx12HdrPresentPass: descriptor heap");
		}
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bd = {};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = static_cast<UINT64>(kSlots) * kCbStride;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		const D3D12_RANGE none = {0, 0};
		if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		                                           IID_PPV_ARGS(m_cb.GetAddressOf()))) ||
		    FAILED(m_cb->Map(0, &none, reinterpret_cast<void**>(&m_cbMapped))))
		{
			throw std::runtime_error("Dx12HdrPresentPass: constant buffer");
		}
	}

	[[nodiscard]] bool ready() const noexcept { return m_pso != nullptr; }

	/// @brief ldr (PIXEL_SHADER_RESOURCE の RGBA8) と extra (PIXEL_SHADER_RESOURCE の RGBA16F、null なら足さない) を読み、
	///        dstRtv (RENDER_TARGET の FP16、width x height) へ scRGB を書く。rootSig と PSO とディスクリプタの heap を差し替える
	void record(ID3D12GraphicsCommandList* cmd, UINT slot, ID3D12Resource* ldr, ID3D12Resource* extra,
	            D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, UINT width, UINT height, float paperWhiteNits)
	{
		slot %= kSlots;
		const UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
		cpu.ptr += static_cast<SIZE_T>(slot) * kViewsPerSlot * inc;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		m_device->CreateShaderResourceView(ldr, &srv, cpu);
		cpu.ptr += inc;
		srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		m_device->CreateShaderResourceView(extra, &srv, cpu);   // null なら null ディスクリプタ (読まれない)

		struct Cb { float scale; float extraOn; float outW; float outH; } cb{
			paperWhiteNits / render::kScRgbWhiteNits, extra != nullptr ? 1.0f : 0.0f, static_cast<float>(width), static_cast<float>(height)};
		std::memcpy(m_cbMapped + static_cast<std::size_t>(slot) * kCbStride, &cb, sizeof(cb));

		D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_heap->GetGPUDescriptorHandleForHeapStart();
		gpu.ptr += static_cast<UINT64>(slot) * kViewsPerSlot * inc;
		const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
		const D3D12_RECT sr{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
		cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
		cmd->RSSetViewports(1, &vp);
		cmd->RSSetScissorRects(1, &sr);
		cmd->SetGraphicsRootSignature(m_rootSig.Get());
		cmd->SetPipelineState(m_pso.Get());
		ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
		cmd->SetDescriptorHeaps(1, heaps);
		cmd->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kCbStride);
		cmd->SetGraphicsRootDescriptorTable(1, gpu);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->IASetVertexBuffers(0, 0, nullptr);
		cmd->DrawInstanced(3, 1, 0, 0);
	}

private:
	static constexpr std::size_t kCbStride = 256;

	ID3D12Device* m_device = nullptr;
	ComPtr<ID3D12RootSignature> m_rootSig;
	ComPtr<ID3D12PipelineState> m_pso;
	ComPtr<ID3D12DescriptorHeap> m_heap;
	ComPtr<ID3D12Resource> m_cb;
	std::uint8_t* m_cbMapped = nullptr;
};

}  // namespace mitiru::gfx

#endif  // _WIN32
