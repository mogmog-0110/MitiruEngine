#pragma once
/// @file Dx12LoFiTarget.hpp
/// @brief ローファイ・ポストFX 用の低解像オフスクリーン RT + 量子化/ディザ・フルスクリーンパス。
/// @details ゲームを低い内部解像度のオフスクリーン RT に描画させ（swapchain の backBuffer を
///          一時的に override）、提示時に point サンプル + パレット量子化 + 4×4 Bayer ディザの
///          フルスクリーンパスで実バックバッファへニアレスト拡大する。DX12 のみ。既定 OFF。
///          失敗モード回避: 既定無効・例外時は黙って従来描画にフォールバック（override は外す）。

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

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/render/lofi/LoFiShader.hpp>

namespace mitiru::gfx
{

/// @brief DX12 ローファイ・ポストパス（低解像 RT + 量子化/ディザ拡大）
class Dx12LoFiTarget
{
public:
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 内部リソースを（必要なら再）生成する。失敗時 false。
	[[nodiscard]] bool ensure(ID3D12Device* device, ID3D12CommandQueue* queue, int w, int h)
	{
		if (!device || !queue || w <= 0 || h <= 0) return false;
		m_device = device; m_queue = queue;
		if (!m_pso && !buildPipeline()) return false;
		if (w != m_width || h != m_height) { if (!buildTarget(w, h)) return false; }
		return m_pso && m_tex;
	}

	/// @brief フレーム描画前: オフスクリーン RT をクリアして backBuffer override に差す。
	void beginFrame(Dx12SwapChain* swap, const float clear[4])
	{
		if (!ready() || !swap) return;
		waitGpu();
		m_alloc->Reset();
		m_list->Reset(m_alloc.Get(), nullptr);
		transition(m_texState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_texState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		m_list->ClearRenderTargetView(m_rtv.rtvHandle(), clear, 0, nullptr);
		execute();
		swap->setBackBufferOverride(&m_rtv); // 以後の 2D 描画は低解像 RT へ
	}

	/// @brief 描画後: override を外し、量子化/ディザしながら実バックバッファへ拡大する。
	void resolve(Dx12SwapChain* swap, int fullW, int fullH, const render::lofi::LoFiParamsCB& params)
	{
		if (!ready() || !swap) return;
		swap->clearBackBufferOverride();
		auto* backRt = dynamic_cast<Dx12RenderTarget*>(swap->backBuffer());
		if (!backRt) return;

		std::memcpy(m_cbMapped, &params, sizeof(params));

		waitGpu();
		m_alloc->Reset();
		m_list->Reset(m_alloc.Get(), m_pso.Get());
		transition(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_texState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		m_list->SetGraphicsRootSignature(m_rootSig.Get());
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
		m_list->SetDescriptorHeaps(1, heaps);
		m_list->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
		m_list->SetGraphicsRootConstantBufferView(1, m_cb->GetGPUVirtualAddress());

		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = backRt->rtvHandle();
		m_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(fullW), static_cast<float>(fullH), 0.0f, 1.0f };
		m_list->RSSetViewports(1, &vp);
		D3D12_RECT sc = { 0, 0, static_cast<LONG>(fullW), static_cast<LONG>(fullH) };
		m_list->RSSetScissorRects(1, &sc);
		m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_list->DrawInstanced(3, 1, 0, 0); // フルスクリーン三角形（頂点バッファ不要）
		execute();
	}

	[[nodiscard]] bool ready() const noexcept { return m_pso && m_tex && m_alloc && m_list; }

private:
	bool buildPipeline()
	{
		// ── root signature: SRV table(t0) + CBV(b0) + static POINT sampler(s0) ──
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1; range.BaseShaderRegister = 0;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[0].DescriptorTable.NumDescriptorRanges = 1;
		params[0].DescriptorTable.pDescriptorRanges = &range;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC samp = {};
		samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // ニアレスト
		samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samp.ShaderRegister = 0; samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samp.MaxLOD = D3D12_FLOAT32_MAX;

		D3D12_ROOT_SIGNATURE_DESC rs = {};
		rs.NumParameters = 2; rs.pParameters = params;
		rs.NumStaticSamplers = 1; rs.pStaticSamplers = &samp;
		rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> sig, err;
		if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) return false;
		if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSig)))) return false;

		// ── shaders ──
		ComPtr<ID3DBlob> vs = compile(render::lofi::LOFI_VS, "VSMain", "vs_5_0");
		ComPtr<ID3DBlob> ps = compile(render::lofi::LOFI_PS, "PSMain", "ps_5_0");
		if (!vs || !ps) return false;

		// ── PSO（無ブレンド・無深度・入力レイアウト無し）──
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_rootSig.Get();
		pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = UINT_MAX;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.DepthStencilState.DepthEnable = FALSE;
		pd.DepthStencilState.StencilEnable = FALSE;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pd.SampleDesc.Count = 1;
		if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_pso)))) return false;

		// ── 専用 command allocator / list / fence ──
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&m_alloc)))) return false;
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_list)))) return false;
		m_list->Close();
		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		// ── descriptor heaps（RTV 1 / SRV 1 shader-visible）+ params CB ──
		D3D12_DESCRIPTOR_HEAP_DESC rh = {}; rh.NumDescriptors = 1; rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (FAILED(m_device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&m_rtvHeap)))) return false;
		D3D12_DESCRIPTOR_HEAP_DESC sh = {}; sh.NumDescriptors = 1; sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)))) return false;

		D3D12_HEAP_PROPERTIES up = {}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC cbd = {};
		cbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; cbd.Width = 256; cbd.Height = 1;
		cbd.DepthOrArraySize = 1; cbd.MipLevels = 1; cbd.SampleDesc.Count = 1;
		cbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(m_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &cbd,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cb)))) return false;
		D3D12_RANGE rr = { 0, 0 };
		m_cb->Map(0, &rr, reinterpret_cast<void**>(&m_cbMapped));
		return true;
	}

	bool buildTarget(int w, int h)
	{
		waitGpu();
		m_tex.Reset();
		D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC td = {};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = static_cast<UINT64>(w); td.Height = static_cast<UINT>(h);
		td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv = {}; cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, &cv, IID_PPV_ARGS(&m_tex)))) return false;
		m_texState = D3D12_RESOURCE_STATE_COMMON;

		// RTV
		m_device->CreateRenderTargetView(m_tex.Get(), nullptr,
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		m_rtv = Dx12RenderTarget::createFromBackBuffer(m_device, m_tex.Get(),
			m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), w, h);
		// SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = 1;
		m_device->CreateShaderResourceView(m_tex.Get(), &sv,
			m_srvHeap->GetCPUDescriptorHandleForHeapStart());

		m_width = w; m_height = h;
		return true;
	}

	ComPtr<ID3DBlob> compile(std::string_view src, const char* entry, const char* target)
	{
		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
			entry, target, 0, 0, &blob, &err))) return nullptr;
		return blob;
	}

	void transition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		if (before == after) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = m_tex.Get();
		b.Transition.StateBefore = before; b.Transition.StateAfter = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_list->ResourceBarrier(1, &b);
	}

	void execute()
	{
		m_list->Close();
		ID3D12CommandList* lists[] = { m_list.Get() };
		m_queue->ExecuteCommandLists(1, lists);
		m_queue->Signal(m_fence.Get(), ++m_fenceVal);
	}

	void waitGpu()
	{
		if (!m_fence) return;
		if (m_fence->GetCompletedValue() < m_fenceVal)
		{
			m_fence->SetEventOnCompletion(m_fenceVal, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	ID3D12Device* m_device = nullptr;
	ID3D12CommandQueue* m_queue = nullptr;
	ComPtr<ID3D12RootSignature> m_rootSig;
	ComPtr<ID3D12PipelineState> m_pso;
	ComPtr<ID3D12Resource> m_tex;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_srvHeap;
	ComPtr<ID3D12Resource> m_cb;
	std::uint8_t* m_cbMapped = nullptr;
	Dx12RenderTarget m_rtv;
	ComPtr<ID3D12CommandAllocator> m_alloc;
	ComPtr<ID3D12GraphicsCommandList> m_list;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent = nullptr;
	UINT64 m_fenceVal = 0;
	D3D12_RESOURCE_STATES m_texState = D3D12_RESOURCE_STATE_COMMON;
	int m_width = 0, m_height = 0;
};

} // namespace mitiru::gfx

#endif // _WIN32
