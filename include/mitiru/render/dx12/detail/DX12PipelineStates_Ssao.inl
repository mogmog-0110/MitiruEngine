// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される


// ─────────────────────────────────────────────────────────────
//  SSAO (v40、ADR 0042): 生成 → 横ぼかし → 縦ぼかし → tonemap が t1 で読む
//  root sig と VS は outline post のもの (SRV t0..t2 + CBV b0、フルスクリーン三角形) を共用する
// ─────────────────────────────────────────────────────────────

/// @brief SSAO の PSO 2 本 (生成 / ぼかし) を作り、テクスチャも用意する。createOutlinePostProcess の後に呼ぶ
void createSsaoPipelines()
{
	if (!m_ssaoPS || !m_ssaoBlurPS || !m_outlinePostVS || !m_outlinePostRootSig) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_outlinePostRootSig.Get();
	psoDesc.VS             = m_outlinePostVS->shaderBytecode();
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
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8_UNORM;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;

	psoDesc.PS = m_ssaoPS->shaderBytecode();
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_ssaoPSO.GetAddressOf())))) return;
	psoDesc.PS = m_ssaoBlurPS->shaderBytecode();
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_ssaoBlurPSO.GetAddressOf())))) return;

	createSsaoResources();
}

/// @brief AO テクスチャ 2 枚 (ping-pong、R8) と RTV/SRV を viewport サイズで作る。resize でも呼ぶ。
/// @details SRV heap は 3 スロット × 2 組: 組 0 = {深度, 法線, tex1}、組 1 = {深度, 法線, tex0}。
///          生成は組 0 に書いて tex0 へ、横ぼかしは組 1 (tex0 を読む) → tex1、縦ぼかしは組 0 (tex1) → tex0。
///          最後に tex0 を tonemap の SRV heap のスロット 1 へ貼る。テクスチャは常に PSR 状態で待つ
void createSsaoResources()
{
	if (!m_d3dDevice || !m_depthBuffer || !m_normalBuffer || !m_hdrIntermediateSrvHeap) return;

	for (auto& t : m_ssaoTex) { t.Reset(); }
	m_ssaoRtvHeap.Reset();
	m_ssaoSrvHeap.Reset();

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width            = static_cast<UINT64>(m_config.viewportWidth);
	desc.Height           = static_cast<UINT>(m_config.viewportHeight);
	desc.DepthOrArraySize = 1;
	desc.MipLevels        = 1;
	desc.Format           = DXGI_FORMAT_R8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format   = DXGI_FORMAT_R8_UNORM;
	clear.Color[0] = 1.0f;

	for (int i = 0; i < 2; ++i)
	{
		HRESULT hr = gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, m_ssaoTex[i]);
		if (FAILED(hr) || !m_ssaoTex[i]) { m_ssaoTex[0].Reset(); m_ssaoTex[1].Reset(); return; }
		m_ssaoTex[i]->SetName(i == 0 ? L"Renderer3D SSAO 0" : L"Renderer3D SSAO 1");
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_ssaoRtvHeap.GetAddressOf()));
	if (!m_ssaoRtvHeap) return;
	const UINT rtvInc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int i = 0; i < 2; ++i)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
		rtv.Format        = DXGI_FORMAT_R8_UNORM;
		rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_ssaoRtvHeap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += rtvInc * static_cast<SIZE_T>(i);
		m_d3dDevice->CreateRenderTargetView(m_ssaoTex[i].Get(), &rtv, h);
	}

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 6;
	srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(m_ssaoSrvHeap.GetAddressOf()));
	if (!m_ssaoSrvHeap) return;

	const UINT srvInc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
	depthSrv.Format                  = DXGI_FORMAT_R32_FLOAT;
	depthSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv = depthSrv;
	normalSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	D3D12_SHADER_RESOURCE_VIEW_DESC aoSrv = {};
	aoSrv.Format                    = DXGI_FORMAT_R8_UNORM;
	aoSrv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
	aoSrv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	aoSrv.Texture2D.MipLevels       = 1;
	aoSrv.Texture2D.MostDetailedMip = 0;

	for (int group = 0; group < 2; ++group)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_ssaoSrvHeap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += srvInc * static_cast<SIZE_T>(group * 3);
		m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &depthSrv, h);
		h.ptr += srvInc;
		m_d3dDevice->CreateShaderResourceView(m_normalBuffer.Get(), &normalSrv, h);
		h.ptr += srvInc;
		m_d3dDevice->CreateShaderResourceView(m_ssaoTex[group == 0 ? 1 : 0].Get(), &aoSrv, h);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tonemapSlot = m_hdrIntermediateSrvHeap->GetCPUDescriptorHandleForHeapStart();
	tonemapSlot.ptr += srvInc;
	m_d3dDevice->CreateShaderResourceView(m_ssaoTex[0].Get(), &aoSrv, tonemapSlot);
}

/// @brief SSAO の 3 パスを記録する。resolve/tonemap の直前、深度が DEPTH_WRITE・法線 RT が RENDER_TARGET の位置で呼ぶ
void drawSsaoPasses()
{
	m_aoAppliedThisFrame = false;
	if (!m_aoEnabled || m_aoStrength <= 0.0f) return;
	if (!m_ssaoPSO || !m_ssaoBlurPSO || !m_ssaoSrvHeap || !m_ssaoRtvHeap) return;
	if (!m_ssaoTex[0] || !m_ssaoTex[1] || !m_depthBuffer || !m_normalBuffer) return;

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource   = m_depthBuffer.Get();
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource   = m_normalBuffer.Get();
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(2, barriers);

	D3D12_VIEWPORT vp{ 0.0f, 0.0f, m_config.viewportWidth, m_config.viewportHeight, 0.0f, 1.0f };
	D3D12_RECT     sr{ 0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight) };
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
	m_graphicsCmdList->SetGraphicsRootSignature(m_outlinePostRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { m_ssaoSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);

	runSsaoPass(m_ssaoPSO.Get(),     0, 0, uploadSsaoCB(0.0f, 0.0f));
	runSsaoPass(m_ssaoBlurPSO.Get(), 1, 1, uploadSsaoCB(1.0f, 0.0f));
	runSsaoPass(m_ssaoBlurPSO.Get(), 0, 0, uploadSsaoCB(0.0f, 1.0f));

	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_graphicsCmdList->ResourceBarrier(2, barriers);

	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_aoAppliedThisFrame = true;
}

/// @brief 1 パス分: tex[target] を RT にして描き、PSR に戻す。srvGroup は createSsaoResources の組番号
void runSsaoPass(ID3D12PipelineState* pso, int target, int srvGroup, D3D12_GPU_VIRTUAL_ADDRESS cb)
{
	D3D12_RESOURCE_BARRIER toRt = {};
	toRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toRt.Transition.pResource   = m_ssaoTex[target].Get();
	toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	toRt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &toRt);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_ssaoRtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * static_cast<SIZE_T>(target);
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	D3D12_GPU_DESCRIPTOR_HANDLE table = m_ssaoSrvHeap->GetGPUDescriptorHandleForHeapStart();
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

/// @brief CbSsao (b0) を ring buffer に置く。行列は VS と同じ column-major memcpy (mul(M, v) 前提)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadSsaoCB(float blurDirX, float blurDirY)
{
	struct alignas(256) CbSsao256
	{
		float proj[4][4];
		float invProj[4][4];
		float view[4][4];
		float texelSize[2];
		float screenSize[2];
		float radius;
		float strength;
		float nearZ;
		float farZ;
		float blurDir[2];
		float _pad[2];
	};
	CbSsao256 cb{};
	toColumnMajor(cb.proj, m_projMatrix);
	toColumnMajor(cb.invProj, glm::inverse(m_projMatrix));
	toColumnMajor(cb.view, m_viewMatrix);
	cb.texelSize[0]  = 1.0f / m_config.viewportWidth;
	cb.texelSize[1]  = 1.0f / m_config.viewportHeight;
	cb.screenSize[0] = m_config.viewportWidth;
	cb.screenSize[1] = m_config.viewportHeight;
	cb.radius   = m_aoRadius;
	cb.strength = m_aoStrength;
	cb.nearZ    = m_clodCamera.nearClip();
	cb.farZ     = m_clodCamera.farClip();
	cb.blurDir[0] = blurDirX;
	cb.blurDir[1] = blurDirY;
	auto a = m_uploadRing.upload(&cb, sizeof(CbSsao256), 256);
	return a.valid() ? a.gpuAddr : 0;
}
