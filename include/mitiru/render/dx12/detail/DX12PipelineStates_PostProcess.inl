// Class-body chunk for Renderer3D_DX12 - included via DX12PipelineStates.hpp


// ─────────────────────────────────────────────────────────────
//  MSAA Resolve + HDR tonemap (ENG-105 v2 + ENG-106)
//  MSAA color (FP16) → HDR intermediate (FP16 single-sample) → backbuffer (LDR via tonemap)
// ─────────────────────────────────────────────────────────────

/// @brief MSAA color RT を HDR intermediate に Resolve する
/// @details ENG-106 で backbuffer 直接 resolve から HDR intermediate 経由に変更。
///          MSAA color (FP16) → ResolveSubresource → HDR intermediate (FP16)
///          → applyTonemap で backbuffer (LDR) に焼く。
///          MSAA color:     RENDER_TARGET → RESOLVE_SOURCE → RENDER_TARGET
///          HDR intermediate: RESOLVE_DEST → (この後 SRV に遷移する)
void resolveMSAAColorToHDR()
{
	if (!m_msaaColorBuffer || !m_hdrIntermediateBuffer) return;
	if (!m_device) return;

	D3D12_RESOURCE_BARRIER pre[1] = {};
	pre[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	pre[0].Transition.pResource   = m_msaaColorBuffer.Get();
	pre[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	pre[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
	pre[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, pre);
	// HDR intermediate は createHDRIntermediate で RESOLVE_DEST 状態に置く / 前フレームの
	// applyTonemap 終了時にも RESOLVE_DEST に戻している前提。

	m_graphicsCmdList->ResolveSubresource(
		m_hdrIntermediateBuffer.Get(), 0,
		m_msaaColorBuffer.Get(), 0,
		DXGI_FORMAT_R16G16B16A16_FLOAT);

	D3D12_RESOURCE_BARRIER post[1] = {};
	post[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	post[0].Transition.pResource   = m_msaaColorBuffer.Get();
	post[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
	post[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	post[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, post);
}

/// @brief Tonemap PSO (root sig + PSO) を生成する (ENG-106)
/// @details createFXAAPipelines と同じ root sig 構造: [0] SRV table, [1] CBV b0,
///          static sampler s0 (linear/clamp). Backbuffer (LDR R8G8B8A8) に書く。
void createTonemapPipeline()
{
	if (!m_tonemapVS || !m_tonemapPS) return;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors     = 1;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	params[0].DescriptorTable.pDescriptorRanges   = &srvRange;
	params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].Descriptor.ShaderRegister = 0;
	params[1].Descriptor.RegisterSpace  = 0;
	params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxAnisotropy    = 1;
	sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD           = 0.0f;
	sampler.MaxLOD           = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister   = 0;
	sampler.RegisterSpace    = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters     = 2;
	rootDesc.pParameters       = params;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers   = &sampler;
	rootDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> serialized, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errorBlob.GetAddressOf());
	if (FAILED(hr)) return;

	hr = m_d3dDevice->CreateRootSignature(
		0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(m_tonemapRootSig.GetAddressOf()));
	if (FAILED(hr)) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature                  = m_tonemapRootSig.Get();
	psoDesc.VS                              = m_tonemapVS->shaderBytecode();
	psoDesc.PS                              = m_tonemapPS->shaderBytecode();
	psoDesc.InputLayout.NumElements         = 0;
	psoDesc.InputLayout.pInputElementDescs  = nullptr;
	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable    = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask
		= D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable   = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 1;
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;

	m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_tonemapPSO.GetAddressOf()));
}

/// @brief HDR intermediate を tonemap して backbuffer に焼く (ENG-106)
/// @details resolveMSAAColorToHDR() の直後に呼ぶ。
///          HDR intermediate: RESOLVE_DEST → PIXEL_SHADER_RESOURCE
///          backbuffer:       RENDER_TARGET (そのまま) — RTV bind
///          終了後: HDR intermediate を RESOLVE_DEST に戻す (次フレーム用)
void applyTonemap()
{
	if (!m_tonemapPSO || !m_tonemapRootSig) return;
	if (!m_hdrIntermediateBuffer || !m_hdrIntermediateSrvHeap) return;
	if (!m_device) return;

	auto* swapChain = m_device->getSwapChain();
	if (!swapChain) return;
	auto* bb = static_cast<gfx::Dx12RenderTarget*>(swapChain->backBuffer());
	if (!bb || !bb->nativeResource()) return;

	// HDR intermediate を PS で読めるよう SRV 状態へ遷移
	D3D12_RESOURCE_BARRIER pre = {};
	pre.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	pre.Transition.pResource   = m_hdrIntermediateBuffer.Get();
	pre.Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	pre.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	pre.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &pre);

	// backbuffer を RT として bind (resolveMSAAColorToHDR 前から RENDER_TARGET の想定)
	auto rtv = bb->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width    = static_cast<float>(m_config.viewportWidth);
	vp.Height   = static_cast<float>(m_config.viewportHeight);
	vp.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &vp);

	D3D12_RECT scissor = {};
	scissor.right  = static_cast<LONG>(m_config.viewportWidth);
	scissor.bottom = static_cast<LONG>(m_config.viewportHeight);
	m_graphicsCmdList->RSSetScissorRects(1, &scissor);

	m_graphicsCmdList->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_tonemapPSO.Get());

	ID3D12DescriptorHeap* heaps[] = { m_hdrIntermediateSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(
		0, m_hdrIntermediateSrvHeap->GetGPUDescriptorHandleForHeapStart());

	// CB (exposure / gamma) を ring buffer に書き出して CBV root として bind
	const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = uploadTonemapCB();
	if (cbAddr != 0)
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbAddr);
	}

	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	// HDR intermediate を RESOLVE_DEST に戻して次フレームに備える
	D3D12_RESOURCE_BARRIER post = {};
	post.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	post.Transition.pResource   = m_hdrIntermediateBuffer.Get();
	post.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	post.Transition.StateAfter  = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	post.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &post);
}

/// @brief Tonemap CB (b0) を ring buffer から確保して GPU virtual address を返す
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadTonemapCB()
{
	struct alignas(256) CbTonemap256
	{
		float exposure;
		float gamma;
		float _pad0;
		float _pad1;
		float _trailing[60]{}; // 256 B 境界
	};
	CbTonemap256 cb{};
	cb.exposure = m_tonemapExposure;
	cb.gamma    = m_tonemapGamma;
	auto a = m_uploadRing.upload(&cb, sizeof(CbTonemap256), 256);
	return a.valid() ? a.gpuAddr : 0;
}

// ─────────────────────────────────────────────────────────────
//  FXAA ポストプロセス AA (ENG-104)
// ─────────────────────────────────────────────────────────────

/// @brief FXAA リソース (root sig / PSO / intermediate buffer / SRV) を生成する
/// @details createOutlinePostProcess() の後、createDepthBuffer() の後に呼ぶ。
///          intermediate は backbuffer サイズのコピー先。outline 後の backbuffer を
///          ここに CopyResource で焼いて、FXAA PS が SRV 経由で読む。
void createFXAAPipelines()
{
	if (!m_fxaaPS || !m_outlinePostVS) return;

	// ─── ルートシグネチャ ───────────────────────────────────────
	// [0] descriptor table { t0 } — シーン色テクスチャ
	// [1] CBV b0                  — FXAAParams
	// static sampler s0           — linear / clamp
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors     = 1;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER params[2] = {};
	params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	params[0].DescriptorTable.pDescriptorRanges   = &srvRange;
	params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[1].Descriptor.ShaderRegister = 0;
	params[1].Descriptor.RegisterSpace  = 0;
	params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC linearSampler = {};
	linearSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	linearSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.MaxAnisotropy    = 1;
	linearSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
	linearSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	linearSampler.MinLOD           = 0.0f;
	linearSampler.MaxLOD           = D3D12_FLOAT32_MAX;
	linearSampler.ShaderRegister   = 0;
	linearSampler.RegisterSpace    = 0;
	linearSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters     = 2;
	rootDesc.pParameters       = params;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers   = &linearSampler;
	rootDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> serialized, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errorBlob.GetAddressOf());
	if (FAILED(hr)) return;

	hr = m_d3dDevice->CreateRootSignature(
		0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(m_fxaaRootSig.GetAddressOf()));
	if (FAILED(hr)) return;

	// ─── PSO ────────────────────────────────────────────────────
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_fxaaRootSig.Get();
	psoDesc.VS             = m_outlinePostVS->shaderBytecode();
	psoDesc.PS             = m_fxaaPS->shaderBytecode();
	psoDesc.InputLayout.NumElements        = 0;
	psoDesc.InputLayout.pInputElementDescs = nullptr;

	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;

	psoDesc.BlendState.RenderTarget[0].BlendEnable    = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask
		= D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState.DepthEnable   = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 1;
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;

	hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_fxaaPSO.GetAddressOf()));
	if (FAILED(hr)) return;

	// ─── intermediate buffer + SRV heap ─────────────────────────
	createFXAAIntermediate();
}

/// @brief FXAA intermediate buffer (backbuffer サイズ) と SRV を生成する
/// @details resize() でも再呼び出しされる。intermediate は COPY_DEST 状態で開始。
void createFXAAIntermediate()
{
	if (!m_d3dDevice) return;

	m_fxaaIntermediate.Reset();
	m_fxaaSrvHeap.Reset();

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width              = static_cast<UINT64>(m_config.viewportWidth);
	desc.Height             = static_cast<UINT>(m_config.viewportHeight);
	desc.DepthOrArraySize   = 1;
	desc.MipLevels          = 1;
	desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count   = 1;
	desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = m_d3dDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(m_fxaaIntermediate.GetAddressOf()));
	if (FAILED(hr) || !m_fxaaIntermediate) return;

	// shader-visible SRV heap (1 slot)
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = m_d3dDevice->CreateDescriptorHeap(
		&heapDesc, IID_PPV_ARGS(m_fxaaSrvHeap.GetAddressOf()));
	if (FAILED(hr) || !m_fxaaSrvHeap) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels       = 1;
	srv.Texture2D.MostDetailedMip = 0;
	m_d3dDevice->CreateShaderResourceView(
		m_fxaaIntermediate.Get(), &srv,
		m_fxaaSrvHeap->GetCPUDescriptorHandleForHeapStart());
}

/// @brief FXAA ポストプロセスを実行する (outline 描画後・overlay2D 前に呼ばれる)
/// @details
///   1. backbuffer → intermediate に CopyResource
///   2. intermediate を PIXEL_SHADER_RESOURCE 状態に遷移、backbuffer を RENDER_TARGET に戻す
///   3. FXAA PSO + root sig をバインドし、CBV (rcpFrame / quality) をアップロード
///   4. フルスクリーン三角形 (3 vertices, no VB) を draw → backbuffer に FXAA 適用
///   5. intermediate を COPY_DEST 状態に戻して次フレームに備える
void drawFXAAPass()
{
	if (!m_fxaaEnabled)            return;
	if (!m_fxaaPSO || !m_fxaaRootSig) return;
	if (!m_fxaaIntermediate || !m_fxaaSrvHeap) return;

	auto* swapChainPost = m_device->getSwapChain();
	if (!swapChainPost) return;
	auto* bbPost = static_cast<gfx::Dx12RenderTarget*>(swapChainPost->backBuffer());
	if (!bbPost) return;

	// ─── 1. backbuffer → COPY_SOURCE ───────────────────────────
	D3D12_RESOURCE_BARRIER preCopy = {};
	preCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	preCopy.Transition.pResource   = bbPost->nativeResource();
	preCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	preCopy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
	preCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &preCopy);

	m_graphicsCmdList->CopyResource(m_fxaaIntermediate.Get(), bbPost->nativeResource());

	// ─── 2. backbuffer → RENDER_TARGET, intermediate → PSR ──────
	D3D12_RESOURCE_BARRIER postCopy[2] = {};
	postCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	postCopy[0].Transition.pResource   = bbPost->nativeResource();
	postCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	postCopy[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	postCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	postCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	postCopy[1].Transition.pResource   = m_fxaaIntermediate.Get();
	postCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	postCopy[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	postCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(2, postCopy);

	// ─── 3. RT を backbuffer のみに ────────────────────────────
	auto rtv = bbPost->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	// viewport / scissor をフルバックバッファに
	D3D12_VIEWPORT vp{ 0.0f, 0.0f,
		m_config.viewportWidth, m_config.viewportHeight,
		0.0f, 1.0f };
	D3D12_RECT     sr{ 0, 0,
		static_cast<LONG>(m_config.viewportWidth),
		static_cast<LONG>(m_config.viewportHeight) };
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);

	// PSO / root sig / heap
	m_graphicsCmdList->SetPipelineState(m_fxaaPSO.Get());
	m_graphicsCmdList->SetGraphicsRootSignature(m_fxaaRootSig.Get());

	ID3D12DescriptorHeap* heaps[] = { m_fxaaSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(
		0, m_fxaaSrvHeap->GetGPUDescriptorHandleForHeapStart());

	// ─── 4. CBV (FXAAParams) ───────────────────────────────────
	struct alignas(256) CbFXAA
	{
		float rcpFrameX, rcpFrameY;
		float subpixQuality;
		float edgeThreshold;
		float edgeThresholdMin;
		float pad0, pad1, pad2;
	};
	CbFXAA cb;
	cb.rcpFrameX        = 1.0f / m_config.viewportWidth;
	cb.rcpFrameY        = 1.0f / m_config.viewportHeight;
	cb.subpixQuality    = m_fxaaSubpixQuality;
	cb.edgeThreshold    = m_fxaaEdgeThreshold;
	cb.edgeThresholdMin = m_fxaaEdgeThresholdMin;
	cb.pad0 = cb.pad1 = cb.pad2 = 0.0f;

	const auto cbAlloc = m_uploadRing.upload(&cb, sizeof(CbFXAA), 256);
	if (cbAlloc.valid())
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbAlloc.gpuAddr);
	}

	// ─── 5. フルスクリーン三角形を描画 ─────────────────────────
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	// ─── 6. intermediate を COPY_DEST に戻して次フレーム準備 ────
	D3D12_RESOURCE_BARRIER finalBarrier = {};
	finalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	finalBarrier.Transition.pResource   = m_fxaaIntermediate.Get();
	finalBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	finalBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
	finalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &finalBarrier);

	// メインの root sig に戻す
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

// ─────────────────────────────────────────────────────────────
//  2Dオーバーレイ描画
// ─────────────────────────────────────────────────────────────

/// @brief 2Dオーバーレイを描画する（endFrame()内、Presentバリア前に呼ばれる）
/// @details SpriteBatchとShapeRendererの蓄積データをOverlay2DVertexに変換し描画する。
void renderOverlay2D()
{
	if (!m_overlayScreen || !m_overlay2DPSO || !m_overlay2DRootSig)
	{
		return;
	}

	/// SpriteBatchとShapeRendererから頂点とインデックスを収集する
	const auto& spriteBatchVerts = m_overlayScreen->spriteBatch().vertices();
	const auto& spriteBatchIndices = m_overlayScreen->spriteBatch().indices();
	const auto& shapeVerts = m_overlayScreen->shapeRenderer().vertices();
	const auto& shapeIndices = m_overlayScreen->shapeRenderer().indices();

	const std::size_t totalVerts = spriteBatchVerts.size() + shapeVerts.size();
	const std::size_t totalIndices = spriteBatchIndices.size() + shapeIndices.size();

	if (totalVerts == 0 || totalIndices == 0)
	{
		return;
	}

	/// Overlay2DVertex配列を構築する
	std::vector<Overlay2DVertex> vertices;
	vertices.reserve(totalVerts);

	for (const auto& v : spriteBatchVerts)
	{
		vertices.push_back({
			v.position.x, v.position.y,
			v.texCoord.x, v.texCoord.y,
			v.color.r, v.color.g, v.color.b, v.color.a
		});
	}

	const auto shapeBaseIndex = static_cast<std::uint32_t>(spriteBatchVerts.size());
	for (const auto& v : shapeVerts)
	{
		vertices.push_back({
			v.position.x, v.position.y,
			v.texCoord.x, v.texCoord.y,
			v.color.r, v.color.g, v.color.b, v.color.a
		});
	}

	/// インデックス配列を構築する（ShapeRendererのインデックスはオフセット）
	std::vector<std::uint32_t> indices;
	indices.reserve(totalIndices);
	indices.insert(indices.end(), spriteBatchIndices.begin(), spriteBatchIndices.end());
	for (auto idx : shapeIndices)
	{
		indices.push_back(idx + shapeBaseIndex);
	}

	/// MRTを解除してバックバッファのみにする
	auto* swapChain = m_device->getSwapChain();
	auto* backBuffer = static_cast<gfx::Dx12RenderTarget*>(swapChain->backBuffer());
	auto rtvHandle = backBuffer->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	/// 2DオーバーレイPSOとルートシグネチャをバインドする
	m_graphicsCmdList->SetPipelineState(m_overlay2DPSO.Get());
	m_graphicsCmdList->SetGraphicsRootSignature(m_overlay2DRootSig.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/// ビューポートとシザーを再設定する
	D3D12_VIEWPORT viewport = {};
	viewport.Width = m_config.viewportWidth;
	viewport.Height = m_config.viewportHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = {};
	scissor.right = static_cast<LONG>(m_config.viewportWidth);
	scissor.bottom = static_cast<LONG>(m_config.viewportHeight);
	m_graphicsCmdList->RSSetScissorRects(1, &scissor);

	/// 正射影行列を生成してCBVにアップロードする
	/// 左上(0,0) -> 右下(W,H)、クリップ空間[-1,1]にマッピング
	struct alignas(256) CbOverlay2D
	{
		float ortho[4][4]{};
	};
	CbOverlay2D cb;
	const float W = m_config.viewportWidth;
	const float H = m_config.viewportHeight;
	/// 列優先(column-major)正射影行列
	/// x' = 2/W * x - 1
	/// y' = -2/H * y + 1  (上が+1、下が-1)
	/// z' = 0
	cb.ortho[0][0] = 2.0f / W;
	cb.ortho[1][1] = -2.0f / H;
	cb.ortho[2][2] = 0.0f;
	cb.ortho[3][3] = 1.0f;
	cb.ortho[3][0] = -1.0f;
	cb.ortho[3][1] = 1.0f;

	const auto cbAlloc = m_uploadRing.upload(&cb, sizeof(CbOverlay2D), 256);
	if (!cbAlloc.valid()) return;
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbAlloc.gpuAddr);

	/// 頂点バッファを ring 経由で確保
	const UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(Overlay2DVertex));
	const auto vbAlloc = m_uploadRing.upload(vertices.data(), vbSize, 4);
	if (!vbAlloc.valid()) return;

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vbAlloc.gpuAddr;
	vbv.SizeInBytes    = vbSize;
	vbv.StrideInBytes  = sizeof(Overlay2DVertex);
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	/// インデックスバッファを ring 経由で確保
	const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
	const auto ibAlloc = m_uploadRing.upload(indices.data(), ibSize, 4);
	if (!ibAlloc.valid()) return;

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = ibAlloc.gpuAddr;
	ibv.SizeInBytes    = ibSize;
	ibv.Format         = DXGI_FORMAT_R32_UINT;
	m_graphicsCmdList->IASetIndexBuffer(&ibv);

	/// 描画実行
	m_graphicsCmdList->DrawIndexedInstanced(
		static_cast<UINT>(indices.size()), 1, 0, 0, 0);
}

// ─────────────────────────────────────────────────────────────
//  描画ヘルパー
// ─────────────────────────────────────────────────────────────

/// @brief トランスフォーム定数バッファを ring buffer 経由でアップロードする
/// @param worldTransform ワールド変換行列
/// @return CBV にバインドする GPU アドレス（0 で失敗）
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadTransformCB(
	const sgc::Mat4f& worldTransform)
{
	DX12CbTransform cb;
	toColumnMajor(cb.world, toGlm(worldTransform));
	toColumnMajor(cb.view, m_viewMatrix);
	toColumnMajor(cb.projection, m_projMatrix);

	auto a = m_uploadRing.upload(&cb, sizeof(DX12CbTransform), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief ライティング定数バッファを ring buffer 経由でアップロードする
/// @param material マテリアル情報
/// @return CBV にバインドする GPU アドレス（0 で失敗）
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadLightingCB(
	const Material& material)
{
	DX12CbLighting cb;

	/// ライト方向
	cb.lightDir[0] = m_light.direction.x;
	cb.lightDir[1] = m_light.direction.y;
	cb.lightDir[2] = m_light.direction.z;
	cb.lightDir[3] = 0.0f;

	/// ライト色（強度を乗算）
	cb.lightColor[0] = m_light.color.r * m_light.intensity;
	cb.lightColor[1] = m_light.color.g * m_light.intensity;
	cb.lightColor[2] = m_light.color.b * m_light.intensity;
	cb.lightColor[3] = 1.0f;

	/// アンビエント色
	cb.ambientColor[0] = m_sceneAmbient.r;
	cb.ambientColor[1] = m_sceneAmbient.g;
	cb.ambientColor[2] = m_sceneAmbient.b;
	cb.ambientColor[3] = 1.0f;

	/// カメラ位置
	cb.cameraPos[0] = m_cameraPosition.x;
	cb.cameraPos[1] = m_cameraPosition.y;
	cb.cameraPos[2] = m_cameraPosition.z;
	cb.cameraPos[3] = 1.0f;

	/// マテリアル拡散色
	cb.materialDiffuse[0] = material.diffuse.r;
	cb.materialDiffuse[1] = material.diffuse.g;
	cb.materialDiffuse[2] = material.diffuse.b;
	cb.materialDiffuse[3] = material.diffuse.a;

	/// マテリアル鏡面反射色
	cb.materialSpecular[0] = material.specular.r;
	cb.materialSpecular[1] = material.specular.g;
	cb.materialSpecular[2] = material.specular.b;
	cb.materialSpecular[3] = material.specular.a;

	/// マテリアル光沢度
	cb.materialShininess = material.shininess;

	auto a = m_uploadRing.upload(&cb, sizeof(DX12CbLighting), 256);
	return a.valid() ? a.gpuAddr : 0;
}
