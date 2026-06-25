#pragma once
// このヘッダは RenderPipeline2D.hpp から include される — 直接 include しないこと。

#ifdef _WIN32

#include <mitiru/debug/Log.hpp>

namespace mitiru::render
{

inline RenderPipeline2D RenderPipeline2D::createFromDx12(
	gfx::Dx12Device* dx12Device,
	float screenWidth,
	float screenHeight)
{
	RenderPipeline2D pipeline;
	pipeline.m_screenWidth = screenWidth;
	pipeline.m_screenHeight = screenHeight;
	pipeline.m_useDx12Path = true;
	pipeline.m_dx12Device = dx12Device;

	auto* device = dx12Device->nativeDevice();
	pipeline.m_dx12NativeDevice = device;
	pipeline.m_dx12Queue = dx12Device->commandQueue();

	/// ── 基本 2D ルートシグネチャ ───────────────────────
	/// 0: VS CBV b0 (projection 4x4)
	/// 1: PS CBV b0 (uUseTexture float4)
	/// 2: PS SRV table t0 (albedo)
	/// s0: static sampler (linear clamp)
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER params[3] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].Descriptor.ShaderRegister = 0;
	params[1].Descriptor.RegisterSpace = 0;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges = &srvRange;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 3;
	rsd.pParameters = params;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers = &sampler;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
	if (FAILED(D3D12SerializeRootSignature(
			&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)))
	{
		throw std::runtime_error(
			"RenderPipeline2D: SerializeRootSignature failed");
	}
	if (FAILED(device->CreateRootSignature(
			0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
			IID_PPV_ARGS(&pipeline.m_dx12RootSig))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateRootSignature failed");
	}

	/// ── シェーダーをコンパイル ───────────────────────
	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> compileErr;
	D3DCompile(
		DEFAULT_VS_2D.data(), DEFAULT_VS_2D.size(),
		nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
		0, 0, &vsBlob, &compileErr);
	D3DCompile(
		DEFAULT_PS_2D.data(), DEFAULT_PS_2D.size(),
		nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
		0, 0, &psBlob, &compileErr);
	if (!vsBlob || !psBlob)
	{
		throw std::runtime_error(
			"RenderPipeline2D: D3DCompile (default 2D) failed");
	}
	pipeline.m_dx12VsBlob = vsBlob;
	pipeline.m_dx12PsBlob = psBlob;

	/// ── PSO (base 2D) ───────────────────────────────
	const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
		  0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
		  0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,
		  0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	pipeline.m_dx12Pipeline = pipeline.buildDx12Pso(
		device, pipeline.m_dx12RootSig.Get(),
		vsBlob.Get(), psBlob.Get(), layout,
		static_cast<UINT>(std::size(layout)));

	/// ── SRV heap (null SRV 1 個: テクスチャ未使用パス) ─
	D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 1;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(
			&dhd, IID_PPV_ARGS(&pipeline.m_dx12SrvHeap))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateDescriptorHeap failed");
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
	nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullSrv.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(
		nullptr, &nullSrv,
		pipeline.m_dx12SrvHeap->GetCPUDescriptorHandleForHeapStart());

	/// ── upload heap バッファ (VB / IB / PS CB) を slot 別に確保 ─────────
	constexpr std::uint32_t INITIAL_VB = 65536;
	constexpr std::uint32_t INITIAL_IB = 32768;
	const float psConst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < kDx12Ring; ++i)
	{
		pipeline.m_dx12VertexBuffer[i] = createUploadBufferDx12(device, INITIAL_VB);
		pipeline.m_dx12IndexBuffer[i]  = createUploadBufferDx12(device, INITIAL_IB);
		pipeline.m_dx12VbCapacity[i]   = INITIAL_VB;
		pipeline.m_dx12IbCapacity[i]   = INITIAL_IB;
		/// PS CB は 256 バイトアライン必須
		pipeline.m_dx12PsCb[i] = createUploadBufferDx12(device, 256);
		pipeline.updateCbDx12(pipeline.m_dx12PsCb[i].Get(), psConst, sizeof(psConst));
	}

	/// projection CB は全 slot 共有 (resize でのみ更新)
	pipeline.m_dx12VsCb = createUploadBufferDx12(device, 256);
	const auto ortho = OrthoMatrix::create(screenWidth, screenHeight);
	pipeline.updateCbDx12(
		pipeline.m_dx12VsCb.Get(), ortho.m, sizeof(ortho.m));

	/// ── ring allocator + 単一 command list + fence ─
	for (int i = 0; i < kDx12Ring; ++i)
	{
		if (FAILED(device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&pipeline.m_dx12Alloc[i]))))
		{
			throw std::runtime_error(
				"RenderPipeline2D: CreateCommandAllocator failed");
		}
	}
	if (FAILED(device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			pipeline.m_dx12Alloc[0].Get(), nullptr,
			IID_PPV_ARGS(&pipeline.m_dx12Cl))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateCommandList failed");
	}
	pipeline.m_dx12Cl->Close();
	if (FAILED(device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(&pipeline.m_dx12Fence))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateFence failed");
	}
	pipeline.m_dx12FenceEvent =
		CreateEventW(nullptr, FALSE, FALSE, nullptr);
	pipeline.m_dx12FenceValue = 0;

	/// ── point-filter root signature + PSO (eager build for pixel-grid) ──
	/// MitiruEngine 規約: draw() 内での遅延初期化は禁止。pixel-grid の
	/// PixelArtFilter::Point パスで使う root sig + PSO もここで eager に構築する。
	/// 構築失敗時は両ポインタを nullptr のままにし、submitPixelGridDx12 は
	/// linear PSO にフォールバックする (point variant は linear と同じ shader /
	/// blend / 入力 layout で root sig の static sampler の Filter だけが
	/// D3D12_FILTER_MIN_MAG_MIP_POINT に差し替わるだけなので、視覚的には
	/// pixel-art が bilinear で滲むだけで draw 自体は成立する)。
	pipeline.buildDx12PointFilterResources(
		device, vsBlob.Get(), psBlob.Get(),
		layout, static_cast<UINT>(std::size(layout)));

	pipeline.m_valid = true;
	return pipeline;
}

// ─────────────────────────────────────────────────────────────────────────────
// pixel-grid 用 point-filter root signature + PSO の eager 構築
// ─────────────────────────────────────────────────────────────────────────────
//
// createFromDx12 から base 2D PSO 構築直後に呼ばれる。base と同じ shader / blend /
// 入力 layout を保ったまま、root sig の static sampler s0 だけ LINEAR → POINT に
// 差し替えた variant を作る。失敗時はメンバを nullptr のままにし、
// submitPixelGridDx12 が linear PSO へフォールバックする (機能としては成立)。

namespace detail
{
/// @brief point-filter 用の root signature description を組み立てる
/// @details base 2D root sig と同じ 3 root params (VS CBV / PS CBV / SRV table)
///          を作り、static sampler の Filter だけ POINT に差し替える。
///          引数の配列は呼び出し側でストレージを保持する必要がある (関数 return
///          後も D3D12_ROOT_SIGNATURE_DESC が指したまま)。
inline D3D12_ROOT_SIGNATURE_DESC
makeDx12PointRootSigDesc(
	D3D12_DESCRIPTOR_RANGE&     srvRange,
	D3D12_ROOT_PARAMETER (&params)[3],
	D3D12_STATIC_SAMPLER_DESC&  sampler)
{
	srvRange                                   = {};
	srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors                    = 1;
	srvRange.BaseShaderRegister                = 0;
	srvRange.RegisterSpace                     = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	for (auto& p : params) p = {};
	params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
	params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
	params[2].ParameterType                       =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges   = &srvRange;
	params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	sampler                  = {};
	sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT; ///< ← linear との差分はここだけ
	sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters     = 3;
	rsd.pParameters       = params;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers   = &sampler;
	rsd.Flags             =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	return rsd;
}

/// @brief root signature description をシリアライズして root signature を生成
inline Microsoft::WRL::ComPtr<ID3D12RootSignature>
createDx12PointRootSig(ID3D12Device* device,
                        const D3D12_ROOT_SIGNATURE_DESC& rsd)
{
	Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
	if (FAILED(D3D12SerializeRootSignature(
			&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) ||
		!sigBlob)
	{
		MITIRU_LOG_WARN("RenderPipeline2D",
			"point-filter SerializeRootSignature failed; "
			"pixel-grid Point will fall back to Linear");
		return {};
	}

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
	if (FAILED(device->CreateRootSignature(
			0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
			IID_PPV_ARGS(&rootSig))) ||
		!rootSig)
	{
		MITIRU_LOG_WARN("RenderPipeline2D",
			"point-filter CreateRootSignature failed; "
			"pixel-grid Point will fall back to Linear");
		return {};
	}
	return rootSig;
}
} // namespace detail

inline void RenderPipeline2D::buildDx12PointFilterResources(
	ID3D12Device* device,
	ID3DBlob* vsBlob, ID3DBlob* psBlob,
	const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount)
{
	if (!device || !vsBlob || !psBlob || !layout || layoutCount == 0)
	{
		MITIRU_LOG_WARN("RenderPipeline2D",
			"point-filter eager build skipped: invalid inputs; "
			"pixel-grid Point will fall back to Linear");
		return;
	}

	D3D12_DESCRIPTOR_RANGE    srvRange{};
	D3D12_ROOT_PARAMETER      params[3]{};
	D3D12_STATIC_SAMPLER_DESC sampler{};
	const D3D12_ROOT_SIGNATURE_DESC rsd =
		detail::makeDx12PointRootSigDesc(srvRange, params, sampler);

	auto rootSig = detail::createDx12PointRootSig(device, rsd);
	if (!rootSig)
	{
		return; // エラーは helper 側で既にログ済み
	}

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	try
	{
		pso = buildDx12Pso(
			device, rootSig.Get(), vsBlob, psBlob, layout, layoutCount);
	}
	catch (...)
	{
		pso.Reset();
	}
	if (!pso)
	{
		MITIRU_LOG_WARN("RenderPipeline2D",
			"point-filter PSO build failed; "
			"pixel-grid Point will fall back to Linear");
		return;
	}

	m_dx12PointRootSig  = std::move(rootSig);
	m_dx12PointPipeline = std::move(pso);
}

inline void RenderPipeline2D::submitBatchDx12(
	const std::vector<Vertex2D>& vertices,
	const std::vector<std::uint32_t>& indices)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue)
	{
		return;
	}

	/// ring slot を確保し、その slot の前回 GPU 完了だけ待つ (直前ではない)
	const int s = acquireDx12Slot();

	/// uUseTexture = 0 を明示する (直前の textured batch / pixel-grid から漏れた
	/// 1 で頂点カラー描画がテクスチャサンプルされるのを防ぐ。ADR 0009)。
	/// slot s 専用 CB を使うので前 GPU 読み取りとは race しない。
	{
		const float psOff[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		updateCbDx12(m_dx12PsCb[s].Get(), psOff, sizeof(psOff));
	}

	const auto vbSize = static_cast<std::uint32_t>(
		vertices.size() * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(
		indices.size() * sizeof(std::uint32_t));

	updateDx12Buffer(
		m_dx12VertexBuffer[s], m_dx12VbCapacity[s],
		vertices.data(), vbSize);
	updateDx12Buffer(
		m_dx12IndexBuffer[s], m_dx12IbCapacity[s],
		indices.data(), ibSize);

	/// 現在のバックバッファ RTV を取得する (device->beginFrame が
	/// 既に RENDER_TARGET 状態への barrier を発行している前提)
	auto* swapChain = m_dx12Device->getSwapChain();
	if (!swapChain) return;
	auto* rt = dynamic_cast<gfx::Dx12RenderTarget*>(
		swapChain->backBuffer());
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	/// コマンドリストを記録する (slot s の allocator で Reset)
	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), m_dx12Pipeline.Get());

	m_dx12Cl->SetGraphicsRootSignature(m_dx12RootSig.Get());
	m_dx12Cl->SetPipelineState(m_dx12Pipeline.Get());

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12PsCb[s]->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { m_dx12SrvHeap.Get() };
	m_dx12Cl->SetDescriptorHeaps(1, heaps);
	m_dx12Cl->SetGraphicsRootDescriptorTable(
		2, m_dx12SrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = { 0, 0,
		static_cast<LONG>(viewportWidth()),
		static_cast<LONG>(viewportHeight()) };
	m_dx12Cl->RSSetScissorRects(1, &sci);

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_dx12VertexBuffer[s]->GetGPUVirtualAddress();
	vbv.SizeInBytes = vbSize;
	vbv.StrideInBytes = sizeof(Vertex2D);
	m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_dx12IndexBuffer[s]->GetGPUVirtualAddress();
	ibv.SizeInBytes = ibSize;
	ibv.Format = DXGI_FORMAT_R32_UINT;
	m_dx12Cl->IASetIndexBuffer(&ibv);

	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_dx12Cl->DrawIndexedInstanced(
		static_cast<UINT>(indices.size()), 1, 0, 0, 0);

	m_dx12Cl->Close();

	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);

	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[s] = m_dx12FenceValue;
}

inline void RenderPipeline2D::submitStyledBatchDx12(
	const std::vector<StyledVertex2D>& vertices,
	const std::vector<std::uint32_t>& indices,
	const StyleConstants& style,
	std::string_view vsSource,
	std::string_view psSource,
	Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso)
{
	if (!m_dx12Cl || !m_dx12Queue) return;

	/// Rect/Circle のどちらかを識別して PSO を準備する
	const bool isRect = (&cachedPso == &m_dx12SdfRectPso);
	if (isRect)
	{
		ensureDx12SdfResources(
			vsSource, psSource,
			m_dx12SdfRectVsBlob, m_dx12SdfRectPsBlob,
			m_dx12SdfRectPso);
	}
	else
	{
		ensureDx12SdfResources(
			vsSource, psSource,
			m_dx12SdfCircleVsBlob, m_dx12SdfCirclePsBlob,
			m_dx12SdfCirclePso);
	}

	/// ring slot を確保し、その slot の前回 GPU 完了だけ待つ
	const int s = acquireDx12Slot();

	/// スタイル定数を更新 (slot s 専用 CB)
	updateCbDx12(m_dx12SdfStyleCb[s].Get(), &style, sizeof(StyleConstants));

	/// VB / IB 更新 (slot s 専用)
	const auto vbSize = static_cast<std::uint32_t>(
		vertices.size() * sizeof(StyledVertex2D));
	const auto ibSize = static_cast<std::uint32_t>(
		indices.size() * sizeof(std::uint32_t));
	updateDx12Buffer(
		m_dx12SdfVertexBuffer[s], m_dx12SdfVbCapacity[s],
		vertices.data(), vbSize);
	updateDx12Buffer(
		m_dx12SdfIndexBuffer[s], m_dx12SdfIbCapacity[s],
		indices.data(), ibSize);

	/// RTV 取得
	auto* swapChain = m_dx12Device->getSwapChain();
	if (!swapChain) return;
	auto* rt = dynamic_cast<gfx::Dx12RenderTarget*>(
		swapChain->backBuffer());
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	auto* pso = isRect ? m_dx12SdfRectPso.Get() : m_dx12SdfCirclePso.Get();

	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), pso);

	m_dx12Cl->SetGraphicsRootSignature(m_dx12SdfRootSig.Get());
	m_dx12Cl->SetPipelineState(pso);

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12SdfStyleCb[s]->GetGPUVirtualAddress());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = { 0, 0,
		static_cast<LONG>(viewportWidth()),
		static_cast<LONG>(viewportHeight()) };
	m_dx12Cl->RSSetScissorRects(1, &sci);

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_dx12SdfVertexBuffer[s]->GetGPUVirtualAddress();
	vbv.SizeInBytes = vbSize;
	vbv.StrideInBytes = sizeof(StyledVertex2D);
	m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_dx12SdfIndexBuffer[s]->GetGPUVirtualAddress();
	ibv.SizeInBytes = ibSize;
	ibv.Format = DXGI_FORMAT_R32_UINT;
	m_dx12Cl->IASetIndexBuffer(&ibv);

	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_dx12Cl->DrawIndexedInstanced(
		static_cast<UINT>(indices.size()), 1, 0, 0, 0);

	m_dx12Cl->Close();

	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);

	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[s] = m_dx12FenceValue;
}

inline void RenderPipeline2D::ensureDx12SdfResources(
	std::string_view vsSource, std::string_view psSource,
	Microsoft::WRL::ComPtr<ID3DBlob>& cachedVs,
	Microsoft::WRL::ComPtr<ID3DBlob>& cachedPs,
	Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso)
{
	auto* device = m_dx12NativeDevice.Get();

	/// ルートシグネチャ (共通、SDF は全種で同じレイアウト)
	/// 0: VS CBV b0 (projection)
	/// 1: PS CBV b1 (style constants — DX11 と同じスロット)
	if (!m_dx12SdfRootSig)
	{
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rsd = {};
		rsd.NumParameters = 2;
		rsd.pParameters = params;
		rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
		if (FAILED(D3D12SerializeRootSignature(
				&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
				&sigBlob, &errBlob)))
		{
			throw std::runtime_error(
				"RenderPipeline2D: SDF SerializeRootSignature failed");
		}
		if (FAILED(device->CreateRootSignature(
				0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
				IID_PPV_ARGS(&m_dx12SdfRootSig))))
		{
			throw std::runtime_error(
				"RenderPipeline2D: SDF CreateRootSignature failed");
		}
	}

	/// StyleConstants CB (Rect/Circle 共用、slot 別)
	if (!m_dx12SdfStyleCb[0])
	{
		const std::uint32_t cbSize =
			(sizeof(StyleConstants) + 255) & ~std::uint32_t{255};
		for (int i = 0; i < kDx12Ring; ++i)
			m_dx12SdfStyleCb[i] = createUploadBufferDx12(device, cbSize);
	}

	/// VB / IB (Rect/Circle 共用、slot 別)
	if (!m_dx12SdfVertexBuffer[0])
	{
		for (int i = 0; i < kDx12Ring; ++i)
		{
			m_dx12SdfVertexBuffer[i] = createUploadBufferDx12(device, 65536);
			m_dx12SdfVbCapacity[i] = 65536;
		}
	}
	if (!m_dx12SdfIndexBuffer[0])
	{
		for (int i = 0; i < kDx12Ring; ++i)
		{
			m_dx12SdfIndexBuffer[i] = createUploadBufferDx12(device, 32768);
			m_dx12SdfIbCapacity[i] = 32768;
		}
	}

	/// シェーダー + PSO (初回のみ)
	if (!cachedPso)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
		D3DCompile(
			vsSource.data(), vsSource.size(),
			nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
			0, 0, &cachedVs, &errBlob);
		D3DCompile(
			psSource.data(), psSource.size(),
			nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
			0, 0, &cachedPs, &errBlob);
		if (!cachedVs || !cachedPs)
		{
			throw std::runtime_error(
				"RenderPipeline2D: SDF D3DCompile failed");
		}

		/// StyledVertex2D: pos(2) + localUV(2) + color(4) + shapeRect(4) = 48 byte
		const D3D12_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
			  0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
			  0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,
			  0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
			  0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		cachedPso = buildDx12Pso(
			device, m_dx12SdfRootSig.Get(),
			cachedVs.Get(), cachedPs.Get(),
			layout, static_cast<UINT>(std::size(layout)));
	}
}

inline Microsoft::WRL::ComPtr<ID3D12Resource>
RenderPipeline2D::createUploadBufferDx12(ID3D12Device* device, std::uint32_t sizeBytes)
{
	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC rd = {};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = sizeBytes;
	rd.Height = 1;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_UNKNOWN;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> buf;
	if (FAILED(device->CreateCommittedResource(
			&hp, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&buf))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateCommittedResource (upload) failed");
	}
	return buf;
}

inline void RenderPipeline2D::updateCbDx12(ID3D12Resource* cb, const void* data, size_t bytes)
{
	if (!cb || !data) return;
	void* mapped = nullptr;
	D3D12_RANGE r = {0, 0};
	if (FAILED(cb->Map(0, &r, &mapped))) return;
	std::memcpy(mapped, data, bytes);
	D3D12_RANGE w = {0, bytes};
	cb->Unmap(0, &w);
}

inline void RenderPipeline2D::updateDx12Buffer(
	Microsoft::WRL::ComPtr<ID3D12Resource>& buf,
	std::uint32_t& capacity,
	const void* data, std::uint32_t bytes)
{
	if (bytes > capacity)
	{
		std::uint32_t newCap = std::max(bytes, capacity * 2);
		buf = createUploadBufferDx12(m_dx12NativeDevice.Get(), newCap);
		capacity = newCap;
	}
	void* mapped = nullptr;
	D3D12_RANGE r = {0, 0};
	if (FAILED(buf->Map(0, &r, &mapped))) return;
	std::memcpy(mapped, data, bytes);
	D3D12_RANGE w = {0, bytes};
	buf->Unmap(0, &w);
}

inline Microsoft::WRL::ComPtr<ID3D12PipelineState>
RenderPipeline2D::buildDx12Pso(ID3D12Device* device,
	ID3D12RootSignature* rootSig,
	ID3DBlob* vs, ID3DBlob* ps,
	const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
	psd.pRootSignature = rootSig;
	psd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	psd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

	auto& rtb = psd.BlendState.RenderTarget[0];
	rtb.BlendEnable = TRUE;
	rtb.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rtb.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rtb.BlendOp = D3D12_BLEND_OP_ADD;
	rtb.SrcBlendAlpha = D3D12_BLEND_ONE;
	rtb.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	rtb.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rtb.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psd.SampleMask = UINT_MAX;

	psd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psd.RasterizerState.FrontCounterClockwise = FALSE;
	psd.RasterizerState.DepthClipEnable = TRUE;

	psd.DepthStencilState.DepthEnable = FALSE;
	psd.DepthStencilState.StencilEnable = FALSE;

	psd.InputLayout = { layout, layoutCount };
	psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psd.NumRenderTargets = 1;
	psd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psd.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	if (FAILED(device->CreateGraphicsPipelineState(
			&psd, IID_PPV_ARGS(&pso))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateGraphicsPipelineState failed");
	}
	return pso;
}

inline void RenderPipeline2D::waitDx12Fence()
{
	if (!m_dx12Fence || !m_dx12FenceEvent) return;
	if (m_dx12Fence->GetCompletedValue() < m_dx12FenceValue)
	{
		m_dx12Fence->SetEventOnCompletion(
			m_dx12FenceValue, m_dx12FenceEvent);
		WaitForSingleObject(m_dx12FenceEvent, INFINITE);
	}
}

inline void RenderPipeline2D::waitForDx12Slot(int slot)
{
	if (!m_dx12Fence || !m_dx12FenceEvent) return;
	// この slot を最後に使った submit の完了だけ待つ。target==0 は未使用 slot。
	const UINT64 target = m_dx12SlotSignal[slot];
	if (target != 0 && m_dx12Fence->GetCompletedValue() < target)
	{
		m_dx12Fence->SetEventOnCompletion(target, m_dx12FenceEvent);
		WaitForSingleObject(m_dx12FenceEvent, INFINITE);
	}
}

inline int RenderPipeline2D::acquireDx12Slot()
{
	const int s = m_dx12Slot;
	waitForDx12Slot(s);
	m_dx12Slot = (s + 1) % kDx12Ring;
	return s;
}

} // namespace mitiru::render

#endif // _WIN32
