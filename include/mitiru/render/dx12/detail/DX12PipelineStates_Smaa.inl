// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// SMAA 1x。辺の検出、重みの計算、近傍の混ぜの 3 パスで、FXAA と同じく最終色 (LDR) の写しを読んで書き戻す。
// 辺と重みの RT は大きさが出力と同じで、4K では約 48 MiB になるので、SMAA を選んで最初に掛けるフレームで作る。

ComPtr<ID3D12RootSignature> m_smaaRootSig;
ComPtr<ID3D12PipelineState> m_smaaEdgePSO;
ComPtr<ID3D12PipelineState> m_smaaWeightPSO;
ComPtr<ID3D12PipelineState> m_smaaBlendPSO;
gfx::GpuResource m_smaaEdges;
gfx::GpuResource m_smaaWeights;
dx12::Dx12Texture2D m_smaaArea;
dx12::Dx12Texture2D m_smaaSearch;
ComPtr<ID3D12DescriptorHeap> m_smaaSrvHeap;
ComPtr<ID3D12DescriptorHeap> m_smaaRtvHeap;
bool m_smaaReady = false;
bool m_smaaFailed = false;   ///< 表が無い、または作れなかった。選び直されるまで再び試さない

static constexpr UINT kSmaaSlotColor = 0;
static constexpr UINT kSmaaSlotEdges = 1;
static constexpr UINT kSmaaSlotBlend = 2;
static constexpr UINT kSmaaSlotArea = 3;
static constexpr UINT kSmaaSlotSearch = 4;
static constexpr UINT kSmaaSlotCount = 5;

[[nodiscard]] bool smaaWanted() const noexcept { return m_aaMode == AntiAliasing3D::MsaaSmaa; }

/// @brief SMAA を選んでいて、まだ作っていなければ作る。表の転送は開いているフレームのリストに積む
void prepareSmaa()
{
	if (!smaaWanted() || m_smaaReady || m_smaaFailed) { return; }
	if (createSmaa()) { return; }
	m_smaaFailed = true;
	console::verbosef(smaaTables().available() ? "SMAA を作れなかったので、FXAA で描きます"
	                                            : "SMAA の表がこのビルドに無いので、FXAA で描きます");
}

[[nodiscard]] bool createSmaa()
{
	if (!m_outlinePostVS || !m_graphicsCmdList || !m_fxaaIntermediate) { return false; }
	const SmaaTables tables = smaaTables();
	if (!tables.available()) { return false; }
	if (!createSmaaRootSignature() || !createSmaaPipelines() || !uploadSmaaTables(tables)) { return false; }
	if (!createSmaaTargets()) { return false; }
	m_smaaReady = true;
	return true;
}

[[nodiscard]] bool createSmaaRootSignature()
{
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = kSmaaSlotCount;
	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	params[0].DescriptorTable.pDescriptorRanges = &range;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	const D3D12_STATIC_SAMPLER_DESC samplers[2] = {
		clampSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, 0), clampSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, 1)};
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = params;
	rd.NumStaticSamplers = 2;
	rd.pStaticSamplers = samplers;
	return serializeRootSig(rd, m_smaaRootSig);
}

[[nodiscard]] bool createSmaaPso(const char* entry, DXGI_FORMAT rtv, ComPtr<ID3D12PipelineState>& out)
{
	try
	{
		const auto ps = gfx::Dx12Shader::createPixelShader(smaaPassSource(entry), "PSMain");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_smaaRootSig.Get(), ps);
		pd.RTVFormats[0] = rtv;
		return SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
	}
	catch (const std::exception& e)
	{
		console::verbosef("SMAA のシェーダーのコンパイルに失敗しました (%s)", e.what());
		return false;
	}
}

[[nodiscard]] bool createSmaaPipelines()
{
	return createSmaaPso(DX12_SMAA_EDGE_PS, DXGI_FORMAT_R8G8_UNORM, m_smaaEdgePSO) &&
	       createSmaaPso(DX12_SMAA_WEIGHT_PS, DXGI_FORMAT_R8G8B8A8_UNORM, m_smaaWeightPSO) &&
	       createSmaaPso(DX12_SMAA_BLEND_PS, DXGI_FORMAT_R8G8B8A8_UNORM, m_smaaBlendPSO);
}

/// @brief 面積 (RG8) と探索 (R8) を RGBA8 にして上げる。線形の値なので sRGB にしない
[[nodiscard]] bool uploadSmaaTables(const SmaaTables& tables)
{
	const auto upload = [&](dx12::Dx12Texture2D& dst, const std::uint8_t* src, int w, int h, int channels) {
		MipImage image;
		image.format = TextureFormat::Rgba8;
		image.width = static_cast<std::uint32_t>(w);
		image.height = static_cast<std::uint32_t>(h);
		std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
		for (std::size_t i = 0; i < static_cast<std::size_t>(w) * static_cast<std::size_t>(h); ++i)
		{
			for (int c = 0; c < channels; ++c) { px[i * 4 + static_cast<std::size_t>(c)] = src[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]; }
		}
		image.mips.push_back(std::move(px));
		return dst.uploadMip(m_d3dDevice, m_graphicsCmdList.Get(), image, m_frameTempResources);
	};
	return upload(m_smaaArea, tables.area, kSmaaAreaWidth, kSmaaAreaHeight, 2) &&
	       upload(m_smaaSearch, tables.search, kSmaaSearchWidth, kSmaaSearchHeight, 1);
}

/// @brief 出力と同じ大きさの辺と重みの RT と、3 パスが共有する記述子を作る。resize でも呼ぶ (GPU の完了は呼び手が待つ)
[[nodiscard]] bool createSmaaTargets()
{
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	constexpr auto kRt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	constexpr auto kRest = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (!createGtaoTexture(w, h, 1, DXGI_FORMAT_R8G8_UNORM, kRt, kRest, L"Renderer3D SMAA edges", m_smaaEdges) ||
	    !createGtaoTexture(w, h, 1, DXGI_FORMAT_R8G8B8A8_UNORM, kRt, kRest, L"Renderer3D SMAA weights", m_smaaWeights))
	{
		return false;
	}
	if (!m_smaaSrvHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC sd = {};
		sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sd.NumDescriptors = kSmaaSlotCount;
		sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(&sd, IID_PPV_ARGS(m_smaaSrvHeap.ReleaseAndGetAddressOf())))) { return false; }
	}
	if (!m_smaaRtvHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC rd = {};
		rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rd.NumDescriptors = 2;
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(&rd, IID_PPV_ARGS(m_smaaRtvHeap.ReleaseAndGetAddressOf())))) { return false; }
	}
	m_smaaArea.createSRV(m_d3dDevice, smaaSrvCpu(kSmaaSlotArea));
	m_smaaSearch.createSRV(m_d3dDevice, smaaSrvCpu(kSmaaSlotSearch));
	writeSmaaSrv(kSmaaSlotEdges, m_smaaEdges.Get(), DXGI_FORMAT_R8G8_UNORM);
	writeSmaaSrv(kSmaaSlotBlend, m_smaaWeights.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	const UINT rtvStep = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_smaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_d3dDevice->CreateRenderTargetView(m_smaaEdges.Get(), nullptr, rtv);
	rtv.ptr += rtvStep;
	m_d3dDevice->CreateRenderTargetView(m_smaaWeights.Get(), nullptr, rtv);
	return true;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE smaaSrvCpu(UINT slot) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_smaaSrvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(slot) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

void writeSmaaSrv(UINT slot, ID3D12Resource* res, DXGI_FORMAT format)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(res, &srv, smaaSrvCpu(slot));
}

/// @brief 色の写しは resize で作り直されるので、パスの前に毎フレーム張り直す
void drawSmaaPass()
{
	prepareSmaa();
	if (!m_smaaReady) { drawFXAAPass(); return; }
	auto* bb = sceneColorTarget();
	if (bb == nullptr || !backBufferMatchesViewport(bb)) { return; }

	writeSmaaSrv(kSmaaSlotColor, m_fxaaIntermediate.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	beginBackBufferCopy(bb->nativeResource());

	auto* cl = m_graphicsCmdList.Get();
	const UINT rtvStep = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE edgesRtv = m_smaaRtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE weightsRtv = edgesRtv;
	weightsRtv.ptr += rtvStep;
	const float w = m_config.viewportWidth;
	const float h = m_config.viewportHeight;
	const float metrics[4] = {1.0f / w, 1.0f / h, w, h};

	const auto pass = [&](ID3D12PipelineState* pso, ID3D12Resource* target, D3D12_CPU_DESCRIPTOR_HANDLE rtv, bool clear) {
		if (target != nullptr) { temporalBarrier(target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET); }
		cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		if (clear)
		{
			const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			cl->ClearRenderTargetView(rtv, zero, 0, nullptr);
		}
		setTargetViewport(w, h);
		drawSmaaFullscreen(pso, metrics);
		if (target != nullptr) { temporalBarrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); }
	};
	pass(m_smaaEdgePSO.Get(), m_smaaEdges.Get(), edgesRtv, true);
	pass(m_smaaWeightPSO.Get(), m_smaaWeights.Get(), weightsRtv, true);
	pass(m_smaaBlendPSO.Get(), nullptr, bb->rtvHandle(), false);
	endBackBufferCopy();
}

void drawSmaaFullscreen(ID3D12PipelineState* pso, const float (&metrics)[4])
{
	auto* cl = m_graphicsCmdList.Get();
	cl->SetGraphicsRootSignature(m_smaaRootSig.Get());
	cl->SetPipelineState(pso);
	ID3D12DescriptorHeap* heaps[] = {m_smaaSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, m_smaaSrvHeap->GetGPUDescriptorHandleForHeapStart());
	const auto a = m_uploadRing.upload(metrics, sizeof(metrics), 256);
	if (a.valid()) { cl->SetGraphicsRootConstantBufferView(1, a.gpuAddr); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}
