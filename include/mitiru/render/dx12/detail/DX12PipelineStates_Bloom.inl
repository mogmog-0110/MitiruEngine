// Renderer3D_DX12 のクラス本体の一部。DX12PipelineStates.hpp から include される


// ─────────────────────────────────────────────────────────────
//  bloom (v41、ADR 0043): HDR → 1/2 (しきい値) → 1/4 → 1/2 に戻して加算 → tonemap が t2 で読む
//  root sig と VS は tonemap のもの (SRV t0..t2 + CBV b0、s0 = linear/clamp、フルスクリーン三角形) を共用する
// ─────────────────────────────────────────────────────────────

/// @brief bloom の PSO 2 本 (縮小 / 戻し) を作り、テクスチャも用意する。createTonemapPipeline の後に呼ぶ
void createBloomPipelines()
{
	if (!m_bloomDownPS || !m_bloomUpPS || !m_tonemapVS || !m_tonemapRootSig) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_tonemapRootSig.Get();
	psoDesc.VS             = m_tonemapVS->shaderBytecode();
	psoDesc.InputLayout.NumElements        = 0;
	psoDesc.InputLayout.pInputElementDescs = nullptr;
	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable   = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 1;
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;

	psoDesc.PS = m_bloomDownPS->shaderBytecode();
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_bloomDownPSO.GetAddressOf())))) return;
	psoDesc.PS = m_bloomUpPS->shaderBytecode();
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_bloomUpPSO.GetAddressOf())))) return;

	createBloomResources();
}

/// @brief bloom テクスチャ 3 枚 (1/2、1/4、1/2) と RTV/SRV を viewport から作る。resize でも呼ぶ。
/// @details SRV heap は 3 スロット × 3 組: 組 0 = {HDR, -, -} → tex0、組 1 = {tex0, -, -} → tex1、
///          組 2 = {tex1, tex0, -} → tex2。最後に tex2 を tonemap の SRV heap のスロット 2 へ貼る。
///          テクスチャは常に PSR 状態で待つ
void createBloomResources()
{
	if (!m_d3dDevice || !m_hdrIntermediateBuffer || !m_hdrIntermediateSrvHeap) return;

	for (auto& t : m_bloomTex) { t.Reset(); }
	m_bloomRtvHeap.Reset();
	m_bloomSrvHeap.Reset();

	const UINT fullW = static_cast<UINT>(m_config.viewportWidth);
	const UINT fullH = static_cast<UINT>(m_config.viewportHeight);
	const UINT sizes[3][2] = {
		{ std::max<UINT>(1, fullW / 2), std::max<UINT>(1, fullH / 2) },
		{ std::max<UINT>(1, fullW / 4), std::max<UINT>(1, fullH / 4) },
		{ std::max<UINT>(1, fullW / 2), std::max<UINT>(1, fullH / 2) },
	};


	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	for (int i = 0; i < 3; ++i)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width            = sizes[i][0];
		desc.Height           = sizes[i][1];
		desc.DepthOrArraySize = 1;
		desc.MipLevels        = 1;
		desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		HRESULT hr = gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, m_bloomTex[i]);
		if (FAILED(hr) || !m_bloomTex[i]) { for (auto& t : m_bloomTex) { t.Reset(); } return; }
		m_bloomTex[i]->SetName(i == 0 ? L"Renderer3D bloom half A" : (i == 1 ? L"Renderer3D bloom quarter" : L"Renderer3D bloom half B"));
		m_bloomTexSize[i][0] = sizes[i][0];
		m_bloomTexSize[i][1] = sizes[i][1];
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 3;
	rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_bloomRtvHeap.GetAddressOf()));
	if (!m_bloomRtvHeap) return;
	const UINT rtvInc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int i = 0; i < 3; ++i)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
		rtv.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
		rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += rtvInc * static_cast<SIZE_T>(i);
		m_d3dDevice->CreateRenderTargetView(m_bloomTex[i].Get(), &rtv, h);
	}

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 9;
	srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(m_bloomSrvHeap.GetAddressOf()));
	if (!m_bloomSrvHeap) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels       = 1;
	srv.Texture2D.MostDetailedMip = 0;

	ID3D12Resource* groups[3][3] = {
		{ m_hdrIntermediateBuffer.Get(), nullptr,            nullptr },
		{ m_bloomTex[0].Get(),           nullptr,            nullptr },
		{ m_bloomTex[1].Get(),           m_bloomTex[0].Get(), nullptr },
	};
	const UINT srvInc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (int group = 0; group < 3; ++group)
	{
		for (int slot = 0; slot < 3; ++slot)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE h = m_bloomSrvHeap->GetCPUDescriptorHandleForHeapStart();
			h.ptr += srvInc * static_cast<SIZE_T>(group * 3 + slot);
			m_d3dDevice->CreateShaderResourceView(groups[group][slot], &srv, h);
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tonemapSlot = m_hdrIntermediateSrvHeap->GetCPUDescriptorHandleForHeapStart();
	tonemapSlot.ptr += srvInc * 2;
	m_d3dDevice->CreateShaderResourceView(m_bloomTex[2].Get(), &srv, tonemapSlot);
}

/// @brief bloom の 3 パスを記録する。resolveMSAAColorToHDR の直後 (HDR intermediate が RESOLVE_DEST) に呼ぶ。
///        applyTonemap の前提 (RESOLVE_DEST で受け取る) を崩さないよう、終わりに戻す
void drawBloomPasses()
{
	m_bloomAppliedThisFrame = false;
	if (!m_bloomEnabled || m_bloomStrength <= 0.0f) return;
	if (!m_bloomDownPSO || !m_bloomUpPSO || !m_bloomSrvHeap || !m_bloomRtvHeap) return;
	if (!m_bloomTex[0] || !m_bloomTex[1] || !m_bloomTex[2] || !m_hdrIntermediateBuffer) return;

	D3D12_RESOURCE_BARRIER hdr = {};
	hdr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	hdr.Transition.pResource   = m_hdrIntermediateBuffer.Get();
	hdr.Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	hdr.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	hdr.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &hdr);

	m_graphicsCmdList->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { m_bloomSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);

	const float fullW = m_config.viewportWidth;
	const float fullH = m_config.viewportHeight;
	runBloomPass(m_bloomDownPSO.Get(), 0, 0, uploadBloomCB(1.0f / fullW, 1.0f / fullH, true));
	runBloomPass(m_bloomDownPSO.Get(), 1, 1,
	             uploadBloomCB(1.0f / static_cast<float>(m_bloomTexSize[0][0]), 1.0f / static_cast<float>(m_bloomTexSize[0][1]), false));
	runBloomPass(m_bloomUpPSO.Get(), 2, 2,
	             uploadBloomCB(1.0f / static_cast<float>(m_bloomTexSize[1][0]), 1.0f / static_cast<float>(m_bloomTexSize[1][1]), false));

	hdr.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	hdr.Transition.StateAfter  = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	m_graphicsCmdList->ResourceBarrier(1, &hdr);
	m_bloomAppliedThisFrame = true;
}

/// @brief 1 パス分: tex[target] を RT にして描き、PSR に戻す。srvGroup は createBloomResources の組番号
void runBloomPass(ID3D12PipelineState* pso, int target, int srvGroup, D3D12_GPU_VIRTUAL_ADDRESS cb)
{
	D3D12_RESOURCE_BARRIER toRt = {};
	toRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toRt.Transition.pResource   = m_bloomTex[target].Get();
	toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	toRt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &toRt);

	D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(m_bloomTexSize[target][0]), static_cast<float>(m_bloomTexSize[target][1]), 0.0f, 1.0f };
	D3D12_RECT     sr{ 0, 0, static_cast<LONG>(m_bloomTexSize[target][0]), static_cast<LONG>(m_bloomTexSize[target][1]) };
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * static_cast<SIZE_T>(target);
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	D3D12_GPU_DESCRIPTOR_HANDLE table = m_bloomSrvHeap->GetGPUDescriptorHandleForHeapStart();
	table.ptr += m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	           * static_cast<UINT64>(srvGroup * 3);
	m_graphicsCmdList->SetPipelineState(pso);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(0, table);
	if (cb != 0) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cb); }
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toRt.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	m_graphicsCmdList->ResourceBarrier(1, &toRt);
}

/// @brief CbBloom (b0) を ring buffer に置く。texel は読む側 (t0) のサイズの逆数
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadBloomCB(float texelW, float texelH, bool applyThreshold)
{
	struct alignas(256) CbBloom256
	{
		float texelSize[2];
		float threshold;
		float applyThreshold;
		float _trailing[60]{};
	};
	CbBloom256 cb{};
	cb.texelSize[0]   = texelW;
	cb.texelSize[1]   = texelH;
	cb.threshold      = m_bloomThreshold;
	cb.applyThreshold = applyThreshold ? 1.0f : 0.0f;
	auto a = m_uploadRing.upload(&cb, sizeof(CbBloom256), 256);
	return a.valid() ? a.gpuAddr : 0;
}
