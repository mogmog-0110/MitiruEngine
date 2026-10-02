// Renderer3D_DX12 のクラス本体の一部。DX12PipelineStates.hpp から include される


// ─────────────────────────────────────────────────────────────
//  被写界深度 (v44、ADR 0046): tonemap・outline の後、FXAA の前に backbuffer (LDR) を 1 回だけ書き直す
//  色の写しは FXAA の intermediate を借りる (どちらも backbuffer と同じ大きさで、FXAA は後で写し直す)
// ─────────────────────────────────────────────────────────────

/// @brief DoF の PSO を作る。createTonemapPipeline と createFXAAPipelines の後に呼ぶ
void createDofPipeline()
{
	if (!m_dofPS || !m_tonemapVS || !m_tonemapRootSig) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_tonemapRootSig.Get();
	psoDesc.VS             = m_tonemapVS->shaderBytecode();
	psoDesc.PS             = m_dofPS->shaderBytecode();
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
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_dofPSO.GetAddressOf())))) return;

	createDofResources();
}

/// @brief SRV heap {色の写し, 深度, null} を作る。写しも深度も resize で作り直されるので、その後にも呼ぶ
void createDofResources()
{
	m_dofSrvHeap.Reset();
	if (!m_d3dDevice || !m_fxaaIntermediate || !m_depthBuffer) return;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 3;
	heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_dofSrvHeap.GetAddressOf())))) return;

	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_dofSrvHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv = {};
	colorSrv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
	colorSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
	colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	colorSrv.Texture2D.MipLevels     = 1;
	m_d3dDevice->CreateShaderResourceView(m_fxaaIntermediate.Get(), &colorSrv, h);

	h.ptr += inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
	depthSrv.Format                  = DXGI_FORMAT_R32_FLOAT;
	depthSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &depthSrv, h);

	h.ptr += inc;
	m_d3dDevice->CreateShaderResourceView(nullptr, &colorSrv, h);
}

[[nodiscard]] bool dofReady() const noexcept
{
	return m_dofStrength > 0.0f && m_dofPSO && m_dofSrvHeap && m_fxaaIntermediate && m_depthBuffer;
}

/// @brief backbuffer を写しへ取り、深度で決めた半径でぼかして backbuffer へ書き戻す
void drawDofPass()
{
	if (!dofReady()) return;
	auto* bb = sceneColorTarget();
	if (!bb) return;
	{
		// lo-fi の低解像 RT へ出している間は写しと大きさが合わない (FXAA と同じ理由で飛ばす)
		const auto d = bb->nativeResource()->GetDesc();
		if (static_cast<float>(d.Width)  != m_config.viewportWidth ||
		    static_cast<float>(d.Height) != m_config.viewportHeight) { return; }
	}

	dofTransition(bb->nativeResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_graphicsCmdList->CopyResource(m_fxaaIntermediate.Get(), bb->nativeResource());
	dofTransition(bb->nativeResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	dofTransition(m_fxaaIntermediate.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	dofTransition(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	auto rtv = bb->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	D3D12_VIEWPORT vp{ 0.0f, 0.0f, m_config.viewportWidth, m_config.viewportHeight, 0.0f, 1.0f };
	D3D12_RECT     sr{ 0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight) };
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
	m_graphicsCmdList->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_dofPSO.Get());
	ID3D12DescriptorHeap* heaps[] = { m_dofSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(0, m_dofSrvHeap->GetGPUDescriptorHandleForHeapStart());
	const D3D12_GPU_VIRTUAL_ADDRESS cb = uploadDofCB();
	if (cb != 0) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cb); }
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	dofTransition(m_fxaaIntermediate.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	dofTransition(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

void dofTransition(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource   = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter  = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

/// @brief CbDof (b0)。ぼけ半径の上限は 720p の画素で受け取り、実際の高さに合わせて伸ばす
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadDofCB()
{
	struct alignas(256) CbDof256
	{
		float texelSize[2];
		float viewRayScale[2];
		float start;
		float invRange;
		float maxRadiusPx;
		float nearZ;
		float farZ;
		float _pad[3];
	};
	const float p00 = std::fabs(m_projMatrix[0][0]);
	const float p11 = std::fabs(m_projMatrix[1][1]);
	const float end = (m_dofEnd > m_dofStart) ? m_dofEnd : m_dofStart + 1.0f;
	CbDof256 cb{};
	cb.texelSize[0]    = 1.0f / m_config.viewportWidth;
	cb.texelSize[1]    = 1.0f / m_config.viewportHeight;
	cb.viewRayScale[0] = (p00 > 1e-6f) ? 1.0f / p00 : 1.0f;
	cb.viewRayScale[1] = (p11 > 1e-6f) ? 1.0f / p11 : 1.0f;
	cb.start           = m_dofStart;
	cb.invRange        = 1.0f / (end - m_dofStart);
	cb.maxRadiusPx     = m_dofStrength * m_config.viewportHeight / 720.0f;
	cb.nearZ           = m_clodCamera.nearClip();
	cb.farZ            = m_clodCamera.farClip();
	auto a = m_uploadRing.upload(&cb, sizeof(CbDof256), 256);
	return a.valid() ? a.gpuAddr : 0;
}
