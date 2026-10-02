#pragma once
// RenderPipeline2D.hpp からのみ include する。

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

	/// ── 基本 2D ルートシグネチャ ──
	/// 0: VS CBV b0、projection 4x4
	/// 1: PS CBV b0、uUseTexture float4
	/// 2: PS SRV table t0、albedo
	/// s0: static sampler、linear clamp
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

	/// ── シェーダーのコンパイル ──
	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> compileErr;
	(void)gfx::compileDx12Shader(DEFAULT_VS_2D, "VSMain", "vs_5_0", 0, &vsBlob, &compileErr);
	(void)gfx::compileDx12Shader(DEFAULT_PS_2D, "PSMain", "ps_5_0", 0, &psBlob, &compileErr);
	if (!vsBlob || !psBlob)
	{
		throw std::runtime_error(
			"RenderPipeline2D: shader compile (default 2D) failed");
	}
	pipeline.m_dx12VsBlob = vsBlob;
	pipeline.m_dx12PsBlob = psBlob;

	/// ── 基本 2D PSO ──
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

	/// 2D の中間 RT 用に 4x MSAA PSO を先に構築する。
	/// 4x MSAA が使えない場合は null のままにし、submit 側で 1x に切り替える。
	pipeline.m_dx12PipelineMsaa = tryBuildDx12PsoMsaa(
		device, pipeline.m_dx12RootSig.Get(),
		vsBlob.Get(), psBlob.Get(), layout,
		static_cast<UINT>(std::size(layout)));

	/// ── テクスチャ未使用時の null SRV を持つ SRV heap ──
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

	/// ── slot ごとの upload heap バッファ ──
	constexpr std::uint32_t INITIAL_VB = 65536;
	constexpr std::uint32_t INITIAL_IB = 32768;
	const float psConst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < kDx12Ring; ++i)
	{
		pipeline.m_dx12VertexBuffer[i] = createUploadBufferDx12(device, INITIAL_VB);
		pipeline.m_dx12IndexBuffer[i]  = createUploadBufferDx12(device, INITIAL_IB);
		pipeline.m_dx12VbCapacity[i]   = INITIAL_VB;
		pipeline.m_dx12IbCapacity[i]   = INITIAL_IB;
		/// PS CB は 256 バイト境界にそろえる。
		pipeline.m_dx12PsCb[i] = createUploadBufferDx12(device, 256);
		pipeline.updateCbDx12(pipeline.m_dx12PsCb[i].Get(), psConst, sizeof(psConst));
	}

	/// projection CB は全 slot で共有し、resize のときだけ更新する。
	pipeline.m_dx12VsCb = createUploadBufferDx12(device, 256);
	const auto ortho = OrthoMatrix::create(screenWidth, screenHeight);
	pipeline.updateCbDx12(
		pipeline.m_dx12VsCb.Get(), ortho.m, sizeof(ortho.m));

	/// ── ring allocator、command list、fence ──
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

	/// ── pixel-grid 用 point-filter root signature と PSO ──
	/// draw() 内での遅延初期化を避けるため、ここで構築する。
	/// 構築できない場合は linear PSO に切り替わり、pixel-art の表示だけがにじむ。
	pipeline.buildDx12PointFilterResources(
		device, vsBlob.Get(), psBlob.Get(),
		layout, static_cast<UINT>(std::size(layout)));

	pipeline.m_valid = true;
	return pipeline;
}

// ── pixel-grid 用 point-filter root signature と PSO の構築 ──
// createFromDx12 から基本 2D PSO の構築直後に呼ぶ。
// static sampler s0 の Filter だけを LINEAR から POINT に変える。

namespace detail
{
/// @brief point-filter 用の root signature description を組み立てる
/// @details 戻り値は引数の配列を指すため、使用が終わるまで配列を保持する。
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

	/// point-filter 用に 4x MSAA PSO も構築する。
	/// 構築できない場合は null のままにし、submit 側で 1x に切り替える。
	m_dx12PointPipelineMsaa = tryBuildDx12PsoMsaa(
		device, rootSig.Get(), vsBlob, psBlob, layout, layoutCount);

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

	/// ring slot を確保し、その slot を前回使った GPU 処理の完了だけを待つ。
	const int s = acquireDx12Slot();

	/// 直前の描画から値が残らないよう、uUseTexture を 0 に戻す。
	/// slot 専用 CB のため、前回の GPU 読み取りとは競合しない。
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

	/// beginFrame が RENDER_TARGET 状態への barrier を発行済みであることを前提とする。
	auto* rt = dx12RenderTarget();
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	/// RTV と SampleDesc.Count が一致する PSO を選ぶ。
	ID3D12PipelineState* pso = m_dx12Pipeline.Get();
	if (rt->sampleCount() == static_cast<int>(gfx::Dx12MsaaTarget::kSampleCount))
	{
		if (!m_dx12PipelineMsaa) return; // MSAA 変種が無いのに MSAA RT — 安全にスキップ
		pso = m_dx12PipelineMsaa.Get();
	}

	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), pso);

	m_dx12Cl->SetGraphicsRootSignature(m_dx12RootSig.Get());
	m_dx12Cl->SetPipelineState(pso);

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12PsCb[s]->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { m_dx12SrvHeap.Get() };
	m_dx12Cl->SetDescriptorHeaps(1, heaps);
	m_dx12Cl->SetGraphicsRootDescriptorTable(
		2, m_dx12SrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// viewport の TopLeftX と TopLeftY に中央寄せの offset を入れ、scissor も合わせる。
	D3D12_VIEWPORT vp = {};
	vp.TopLeftX = viewportOffsetX();
	vp.TopLeftY = viewportOffsetY();
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = {
		static_cast<LONG>(viewportOffsetX()),
		static_cast<LONG>(viewportOffsetY()),
		static_cast<LONG>(viewportOffsetX() + viewportWidth()),
		static_cast<LONG>(viewportOffsetY() + viewportHeight()) };
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

	const bool isRect = (&cachedPso == &m_dx12SdfRectPso);
	if (isRect)
	{
		ensureDx12SdfResources(
			vsSource, psSource,
			m_dx12SdfRectVsBlob, m_dx12SdfRectPsBlob,
			m_dx12SdfRectPso, m_dx12SdfRectPsoMsaa);
	}
	else
	{
		ensureDx12SdfResources(
			vsSource, psSource,
			m_dx12SdfCircleVsBlob, m_dx12SdfCirclePsBlob,
			m_dx12SdfCirclePso, m_dx12SdfCirclePsoMsaa);
	}

	/// ring slot を確保し、その slot を前回使った GPU 処理の完了だけを待つ。
	const int s = acquireDx12Slot();

	updateCbDx12(m_dx12SdfStyleCb[s].Get(), &style, sizeof(StyleConstants));

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

	auto* rt = dx12RenderTarget();
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	/// RTV のサンプル数に合わせて 1x または MSAA PSO を選ぶ。
	ID3D12PipelineState* pso = isRect ? m_dx12SdfRectPso.Get()
	                                  : m_dx12SdfCirclePso.Get();
	if (rt->sampleCount() == static_cast<int>(gfx::Dx12MsaaTarget::kSampleCount))
	{
		pso = isRect ? m_dx12SdfRectPsoMsaa.Get()
		             : m_dx12SdfCirclePsoMsaa.Get();
		if (!pso) return; // MSAA 変種が無いのに MSAA RT — 安全にスキップ
	}

	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), pso);

	m_dx12Cl->SetGraphicsRootSignature(m_dx12SdfRootSig.Get());
	m_dx12Cl->SetPipelineState(pso);

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12SdfStyleCb[s]->GetGPUVirtualAddress());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// viewport の TopLeftX と TopLeftY に中央寄せの offset を入れ、scissor も合わせる。
	D3D12_VIEWPORT vp = {};
	vp.TopLeftX = viewportOffsetX();
	vp.TopLeftY = viewportOffsetY();
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = {
		static_cast<LONG>(viewportOffsetX()),
		static_cast<LONG>(viewportOffsetY()),
		static_cast<LONG>(viewportOffsetX() + viewportWidth()),
		static_cast<LONG>(viewportOffsetY() + viewportHeight()) };
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPso,
	Microsoft::WRL::ComPtr<ID3D12PipelineState>& cachedPsoMsaa)
{
	auto* device = m_dx12NativeDevice.Get();

	/// SDF 共通のルートシグネチャ
	/// 0: VS CBV b0、projection
	/// 1: PS CBV b1、DX11 と同じ style constants
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

	if (!m_dx12SdfStyleCb[0])
	{
		const std::uint32_t cbSize =
			(sizeof(StyleConstants) + 255) & ~std::uint32_t{255};
		for (int i = 0; i < kDx12Ring; ++i)
			m_dx12SdfStyleCb[i] = createUploadBufferDx12(device, cbSize);
	}

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

	if (!cachedPso)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
		(void)gfx::compileDx12Shader(vsSource, "VSMain", "vs_5_0", 0, &cachedVs, &errBlob);
		(void)gfx::compileDx12Shader(psSource, "PSMain", "ps_5_0", 0, &cachedPs, &errBlob);
		if (!cachedVs || !cachedPs)
		{
			throw std::runtime_error(
				"RenderPipeline2D: SDF shader compile failed");
		}

		/// StyledVertex2D は pos 2、localUV 2、color 4、shapeRect 4 の計 48 バイト。
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

		/// 1x PSO と同時に 4x MSAA PSO も構築する。
		/// 4x MSAA が使えない場合は null のままにする。
		cachedPsoMsaa = tryBuildDx12PsoMsaa(
			device, m_dx12SdfRootSig.Get(),
			cachedVs.Get(), cachedPs.Get(),
			layout, static_cast<UINT>(std::size(layout)));
	}
}

inline gfx::GpuResource
RenderPipeline2D::createUploadBufferDx12(ID3D12Device* device, std::uint32_t sizeBytes)
{
	gfx::GpuResource buf;
	if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, sizeBytes,
		D3D12_RESOURCE_STATE_GENERIC_READ, buf)))
	{
		throw std::runtime_error(
			"RenderPipeline2D: GPU allocation (upload) failed");
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
	gfx::GpuResource& buf,
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
	const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
	UINT sampleCount)
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
	/// sampleCount が 1 より大きい場合は quadrilateral line AA を有効にする。
	/// 塗り三角形の edge AA は MSAA RT なら常に有効になる。
	psd.RasterizerState.MultisampleEnable = (sampleCount > 1) ? TRUE : FALSE;

	psd.DepthStencilState.DepthEnable = FALSE;
	psd.DepthStencilState.StencilEnable = FALSE;

	psd.InputLayout = { layout, layoutCount };
	psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psd.NumRenderTargets = 1;
	psd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psd.SampleDesc.Count = sampleCount;
	psd.SampleDesc.Quality = 0;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	if (FAILED(device->CreateGraphicsPipelineState(
			&psd, IID_PPV_ARGS(&pso))))
	{
		throw std::runtime_error(
			"RenderPipeline2D: CreateGraphicsPipelineState failed");
	}
	return pso;
}

inline Microsoft::WRL::ComPtr<ID3D12PipelineState>
RenderPipeline2D::tryBuildDx12PsoMsaa(ID3D12Device* device,
	ID3D12RootSignature* rootSig,
	ID3DBlob* vs, ID3DBlob* ps,
	const D3D12_INPUT_ELEMENT_DESC* layout, UINT layoutCount)
{
	if (!device || !rootSig || !vs || !ps || !layout || layoutCount == 0)
	{
		return {};
	}
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	try
	{
		pso = buildDx12Pso(device, rootSig, vs, ps, layout, layoutCount,
			gfx::Dx12MsaaTarget::kSampleCount);
	}
	catch (...)
	{
		pso.Reset();
	}
	return pso;
}

inline void RenderPipeline2D::waitDx12Fence()
{
	if (!m_dx12Fence || !m_dx12FenceEvent) return;
	(void)gfx::waitForFenceOrReport(
		m_dx12Fence.Get(), m_dx12FenceValue, m_dx12FenceEvent, "RenderPipeline2D drain");
}

inline void RenderPipeline2D::waitForDx12Slot(int slot)
{
	if (!m_dx12Fence || !m_dx12FenceEvent) return;
	// この slot を最後に使った submit の完了だけを待つ。
	// target が 0 の slot は未使用。
	const UINT64 target = m_dx12SlotSignal[slot];
	if (target != 0)
	{
		(void)gfx::waitForFenceOrReport(m_dx12Fence.Get(), target, m_dx12FenceEvent, "RenderPipeline2D slot");
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
