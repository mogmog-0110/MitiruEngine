// Class-body chunk for Renderer3D_DX12 - included via DX12PipelineStates.hpp


// ─────────────────────────────────────────────────────────────
//  メインPSO（トゥーンシェーディング）
// ─────────────────────────────────────────────────────────────

/// @brief メインPSO（トゥーンシェーディング）を生成する
/// @details 背面カリング、深度テスト有効、アルファブレンド有効
void createMainPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

	/// ルートシグネチャ
	psoDesc.pRootSignature = m_rootSignature.Get();

	/// シェーダー
	psoDesc.VS = m_toonVS->shaderBytecode();
	psoDesc.PS = m_toonPS->shaderBytecode();

	/// 入力レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayout(inputLayout, inputCount);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = inputCount;

	/// ラスタライザ: 背面カリング
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	psoDesc.RasterizerState.SlopeScaledDepthBias =
		D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;
	psoDesc.RasterizerState.MultisampleEnable = FALSE;
	psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
	psoDesc.RasterizerState.ForcedSampleCount = 0;
	psoDesc.RasterizerState.ConservativeRaster =
		D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	/// ブレンド: アルファブレンド（SrcAlpha, InvSrcAlpha）
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	/// 深度ステンシル: 深度テスト・書き込み有効
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	/// サンプルマスク・トポロジ・フォーマット
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 2;
	// ENG-106 HDR: RT0 (scene color) は FP16 で書き出して tonemap で 0..1 に。
	// RT1 (normal) は表示には載らず post-process でのみ使うので R8 のまま。
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.BlendState.RenderTarget[1] = psoDesc.BlendState.RenderTarget[0];
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	// 4x MSAA — MRT 全 RT + depth と sample count を揃える (ENG-105 v2)
	psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	psoDesc.SampleDesc.Quality = 0;

	HRESULT hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_mainPSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateGraphicsPipelineState (main) failed");
	}

	/// マルチライト Phong PSO（PS のみ差し替え）
	psoDesc.PS = m_multiLightPS->shaderBytecode();
	hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_multiLightPSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateGraphicsPipelineState (multi-light) failed");
	}

	/// ShaderMode 別 PSO（VS / RasterizerState / RT 構成は main と同じ、PS だけ差替え）
	auto createModeVariant = [&](const std::optional<gfx::Dx12Shader>& ps,
	                             ComPtr<ID3D12PipelineState>& out,
	                             const char* tag)
	{
		if (!ps) return;
		psoDesc.PS = ps->shaderBytecode();
		HRESULT h = m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(out.GetAddressOf()));
		if (FAILED(h))
		{
			throw std::runtime_error(
				std::string("Renderer3D_DX12: CreateGraphicsPipelineState (")
				+ tag + ") failed");
		}
	};
	createModeVariant(m_phongPS, m_phongPSO, "phong");
	createModeVariant(m_unlitPS, m_unlitPSO, "unlit");
	createModeVariant(m_flatPS,  m_flatPSO,  "flat");
}

// ─────────────────────────────────────────────────────────────
//  アウトラインPSO
// ─────────────────────────────────────────────────────────────

/// @brief アウトラインPSOを生成する
/// @details 前面カリング（背面膨張アウトライン）、深度テスト有効・書き込み無効
void createOutlinePSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

	/// ルートシグネチャ
	psoDesc.pRootSignature = m_rootSignature.Get();

	/// シェーダー
	psoDesc.VS = m_outlineVS->shaderBytecode();
	psoDesc.PS = m_outlinePS->shaderBytecode();

	/// 入力レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayout(inputLayout, inputCount);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = inputCount;

	/// ラスタライザ: 前面カリング（背面だけ描画 -> アウトラインに見える）
	/// DepthBias: アウトラインの背面をメインパスの前面より奥に押し出す
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.RasterizerState.DepthBias = 5000;
	psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;
	psoDesc.RasterizerState.MultisampleEnable = FALSE;
	psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
	psoDesc.RasterizerState.ForcedSampleCount = 0;
	psoDesc.RasterizerState.ConservativeRaster =
		D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	/// ブレンド: 不透明
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	/// 深度ステンシル: 深度テスト有効、深度書き込み無効
	/// アウトラインはメインパスのメッシュの背後に隠れるべきだが、
	/// 深度バッファを汚さないことで正しいオクルージョンを維持する
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	/// サンプルマスク・トポロジ・フォーマット
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	// ENG-106 HDR: back-extrusion outline は MSAA color (FP16) に書き込む
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	// 4x MSAA — 共有 depth と sample count を揃える (ENG-105 v2)
	psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	psoDesc.SampleDesc.Quality = 0;

	HRESULT hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_outlinePSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateGraphicsPipelineState (outline) failed");
	}
}

// ─────────────────────────────────────────────────────────────
//  深度バッファ・法線バッファ
// ─────────────────────────────────────────────────────────────

/// @brief 深度バッファとDSVデスクリプタヒープを生成する
void createDepthBuffer()
{
	/// DSVデスクリプタヒープの生成
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = m_d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_dsvHeap.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateDescriptorHeap (DSV) failed");
	}

	/// 深度バッファリソースの生成 — 4x MSAA + TYPELESS
	/// TYPELESS にすることで DSV (D32_FLOAT) と SRV (R32_FLOAT) を両方
	/// 作れる (ENG-105 v2)。MSAA 化に伴って format compatibility が厳しく
	/// なる可能性があるため、TYPELESS で safe path に倒す。
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Alignment = 0;
	depthDesc.Width = static_cast<UINT64>(m_config.viewportWidth);
	depthDesc.Height = static_cast<UINT>(m_config.viewportHeight);
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	hr = m_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(m_depthBuffer.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"Renderer3D_DX12: CreateCommittedResource (depth) failed");
	}

	/// DSV (MSAA TEXTURE2DMS、format = D32_FLOAT を明示)
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
	dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;

	m_d3dDevice->CreateDepthStencilView(
		m_depthBuffer.Get(),
		&dsvDesc,
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	/// 法線バッファ（MRT RT1）の生成 — 4x MSAA
	D3D12_RESOURCE_DESC normalDesc = {};
	normalDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	normalDesc.Width = static_cast<UINT64>(m_config.viewportWidth);
	normalDesc.Height = static_cast<UINT>(m_config.viewportHeight);
	normalDesc.DepthOrArraySize = 1;
	normalDesc.MipLevels = 1;
	normalDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	normalDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	normalDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	normalDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE normalClear = {};
	normalClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	hr = m_d3dDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &normalDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &normalClear,
		IID_PPV_ARGS(m_normalBuffer.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error("Renderer3D_DX12: normal buffer failed");
	}

	D3D12_DESCRIPTOR_HEAP_DESC normalRtvHeapDesc = {};
	normalRtvHeapDesc.NumDescriptors = 1;
	normalRtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_d3dDevice->CreateDescriptorHeap(
		&normalRtvHeapDesc, IID_PPV_ARGS(m_normalRTVHeap.GetAddressOf()));

	// MSAA テクスチャ用 RTV は ViewDimension を明示する
	D3D12_RENDER_TARGET_VIEW_DESC normalRtvDesc = {};
	normalRtvDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
	normalRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
	m_d3dDevice->CreateRenderTargetView(
		m_normalBuffer.Get(), &normalRtvDesc,
		m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart());

	// ── MSAA color RT (ENG-105 v2 / ENG-106 HDR) ──────────────
	// メインパスで bind するための 4x MSAA color RT。outline / FXAA 前に
	// ResolveSubresource で HDR intermediate に焼く (旧 backbuffer 直接ではない)。
	// ENG-106 で HDR FP16 化 — ハイライト >1.0 を保持して tonemap で 0..1 に
	// 圧縮する。skybox / scene PSO の RTVFormats[0] も同フォーマットに揃える。
	D3D12_RESOURCE_DESC msaaColorDesc = normalDesc;  // 同じサイズ/サンプル数
	msaaColorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	D3D12_CLEAR_VALUE msaaClear = {};
	msaaClear.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
	msaaClear.Color[0] = 0.0f;
	msaaClear.Color[1] = 0.0f;
	msaaClear.Color[2] = 0.0f;
	msaaClear.Color[3] = 1.0f;

	hr = m_d3dDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &msaaColorDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &msaaClear,
		IID_PPV_ARGS(m_msaaColorBuffer.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error("Renderer3D_DX12: MSAA color buffer failed");
	}

	D3D12_DESCRIPTOR_HEAP_DESC msaaRtvHeapDesc = {};
	msaaRtvHeapDesc.NumDescriptors = 1;
	msaaRtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_d3dDevice->CreateDescriptorHeap(
		&msaaRtvHeapDesc, IID_PPV_ARGS(m_msaaColorRtvHeap.GetAddressOf()));

	D3D12_RENDER_TARGET_VIEW_DESC msaaRtvDesc = {};
	msaaRtvDesc.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
	msaaRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
	m_d3dDevice->CreateRenderTargetView(
		m_msaaColorBuffer.Get(), &msaaRtvDesc,
		m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart());

	// ── HDR intermediate (ENG-106) — single-sample FP16 ─────────
	// MSAA color の Resolve 先。tonemap PS が SRV としてサンプリングして
	// backbuffer に焼く。Outline モード3/4 の color-copy も "tonemap 後の
	// backbuffer" を読むので、HDR intermediate に直接アクセスする必要は無い。
	createHDRIntermediate();
}

/// @brief HDR intermediate buffer (single-sample FP16) を生成する
/// @details createDepthBuffer の最後で呼ばれる。resize でも再呼出される。
void createHDRIntermediate()
{
	if (!m_d3dDevice) return;

	m_hdrIntermediateBuffer.Reset();
	m_hdrIntermediateRtvHeap.Reset();
	m_hdrIntermediateSrvHeap.Reset();

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width              = static_cast<UINT64>(m_config.viewportWidth);
	desc.Height             = static_cast<UINT>(m_config.viewportHeight);
	desc.DepthOrArraySize   = 1;
	desc.MipLevels          = 1;
	desc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count   = 1;
	desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
	clear.Color[0] = clear.Color[1] = clear.Color[2] = 0.0f;
	clear.Color[3] = 1.0f;

	HRESULT hr = m_d3dDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_RESOLVE_DEST, &clear,
		IID_PPV_ARGS(m_hdrIntermediateBuffer.GetAddressOf()));
	if (FAILED(hr) || !m_hdrIntermediateBuffer) return;

	// RTV heap (1 slot)
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(m_hdrIntermediateRtvHeap.GetAddressOf()));
	if (m_hdrIntermediateRtvHeap)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
		rtv.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
		rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_d3dDevice->CreateRenderTargetView(
			m_hdrIntermediateBuffer.Get(), &rtv,
			m_hdrIntermediateRtvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// SRV heap (shader-visible, 1 slot) — tonemap PS が t0 で読む
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3dDevice->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(m_hdrIntermediateSrvHeap.GetAddressOf()));
	if (m_hdrIntermediateSrvHeap)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels       = 1;
		srv.Texture2D.MostDetailedMip = 0;
		m_d3dDevice->CreateShaderResourceView(
			m_hdrIntermediateBuffer.Get(), &srv,
			m_hdrIntermediateSrvHeap->GetCPUDescriptorHandleForHeapStart());
	}
}

// ─────────────────────────────────────────────────────────────
//  ポストプロセス アウトライン
// ─────────────────────────────────────────────────────────────

/// @brief ポストプロセスアウトライン用のリソースを生成する
void createOutlinePostProcess()
{
	if (!m_outlinePostVS || !m_outlinePostPS) return;

	/// SRVヒープ（深度 + 法線 + ダミーの3スロット）を生成する
	/// ルートシグネチャが3スロット要求するため、未使用でも3つ確保する
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3dDevice->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(m_depthSRVHeap.GetAddressOf()));

	auto srvIncrementSize = m_d3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto srvStart = m_depthSRVHeap->GetCPUDescriptorHandleForHeapStart();

	// MSAA 4x (ENG-105 v2): depth/normal は MSAA テクスチャ → SRV も TEXTURE2DMS
	/// スロット0: 深度SRV (MSAA)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_d3dDevice->CreateShaderResourceView(
		m_depthBuffer.Get(), &srvDesc, srvStart);

	/// スロット1: 法線SRV (MSAA)
	D3D12_SHADER_RESOURCE_VIEW_DESC normalSrvDesc = {};
	normalSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	normalSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	normalSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	D3D12_CPU_DESCRIPTOR_HANDLE normalSrvHandle = srvStart;
	normalSrvHandle.ptr += srvIncrementSize;
	m_d3dDevice->CreateShaderResourceView(
		m_normalBuffer.Get(), &normalSrvDesc, normalSrvHandle);

	/// スロット2: nullダミー（モード0-2では使用しない）
	D3D12_CPU_DESCRIPTOR_HANDLE dummySlot = srvStart;
	dummySlot.ptr += srvIncrementSize * 2;
	D3D12_SHADER_RESOURCE_VIEW_DESC dummySrv = {};
	dummySrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	dummySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	dummySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	dummySrv.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(
		nullptr, &dummySrv, dummySlot);

	/// ルートシグネチャ: SRV(t0,t1,t2) + CBV(b0)
	/// 3スロットにすることで色バッファ付きモードにも対応する
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 3;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER postParams[2] = {};
	postParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postParams[0].DescriptorTable.NumDescriptorRanges = 1;
	postParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
	postParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	postParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	postParams[1].Descriptor.ShaderRegister = 0;
	postParams[1].Descriptor.RegisterSpace = 0;
	postParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters = 2;
	rootSigDesc.pParameters = postParams;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> serialized, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errorBlob.GetAddressOf());
	if (FAILED(hr)) return;

	hr = m_d3dDevice->CreateRootSignature(
		0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(m_outlinePostRootSig.GetAddressOf()));
	if (FAILED(hr)) return;

	/// 色バッファコピー用リソースを生成する（モード3,4で使用）
	createColorCopyBuffer();

	/// ポストプロセスPSO共通設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_outlinePostRootSig.Get();
	psoDesc.VS = m_outlinePostVS->shaderBytecode();
	psoDesc.InputLayout.NumElements = 0;
	psoDesc.InputLayout.pInputElementDescs = nullptr;

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;

	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;

	/// モード0: 深度Sobel（既存シェーダー）
	psoDesc.PS = m_outlinePostPS->shaderBytecode();
	m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_outlinePostPSO.GetAddressOf()));

	/// モード1: 深度Laplacian
	if (m_outlinePostPS_Laplacian)
	{
		psoDesc.PS = m_outlinePostPS_Laplacian->shaderBytecode();
		m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_outlinePostPSOs[1].GetAddressOf()));
	}

	/// モード2: 深度Sobel + NdotV
	if (m_outlinePostPS_DepthNdotV)
	{
		psoDesc.PS = m_outlinePostPS_DepthNdotV->shaderBytecode();
		m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_outlinePostPSOs[2].GetAddressOf()));
	}

	/// モード3: 色エッジ
	if (m_outlinePostPS_ColorEdge)
	{
		psoDesc.PS = m_outlinePostPS_ColorEdge->shaderBytecode();
		m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_outlinePostPSOs[3].GetAddressOf()));
	}

	/// モード4: 深度+色 複合
	if (m_outlinePostPS_DepthColor)
	{
		psoDesc.PS = m_outlinePostPS_DepthColor->shaderBytecode();
		m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_outlinePostPSOs[4].GetAddressOf()));
	}

	/// モード5: Fresnel PSO（メインパスのPS差し替え）
	createFresnelMainPSO();
}

// ─────────────────────────────────────────────────────────────
//  色バッファコピーリソース
// ─────────────────────────────────────────────────────────────

/// @brief 色バッファコピー用リソースとSRVヒープを生成する
void createColorCopyBuffer()
{
	/// 色コピーバッファ（バックバッファと同サイズ）
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC colorDesc = {};
	colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	colorDesc.Width = static_cast<UINT64>(m_config.viewportWidth);
	colorDesc.Height = static_cast<UINT>(m_config.viewportHeight);
	colorDesc.DepthOrArraySize = 1;
	colorDesc.MipLevels = 1;
	colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	colorDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	m_d3dDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(m_colorCopyBuffer.GetAddressOf()));
	if (!m_colorCopyBuffer) return;

	auto srvIncrSize = m_d3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	/// モード3用SRVヒープ: [0]=色コピー, [1]=法線, [2]=ダミー
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 3;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_d3dDevice->CreateDescriptorHeap(
			&desc, IID_PPV_ARGS(m_colorEdgeSRVHeap.GetAddressOf()));

		if (m_colorEdgeSRVHeap)
		{
			auto start = m_colorEdgeSRVHeap->GetCPUDescriptorHandleForHeapStart();

			// t0: 色コピーバッファ
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MipLevels = 1;
			m_d3dDevice->CreateShaderResourceView(
				m_colorCopyBuffer.Get(), &srvDesc, start);

			// t1: 法線バッファ (MSAA 4x — ENG-105 v2)
			D3D12_CPU_DESCRIPTOR_HANDLE slot1 = start;
			slot1.ptr += srvIncrSize;
			D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv = {};
			normalSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			normalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			normalSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			m_d3dDevice->CreateShaderResourceView(
				m_normalBuffer.Get(), &normalSrv, slot1);

			// t2: nullスロット
			D3D12_CPU_DESCRIPTOR_HANDLE slot2 = start;
			slot2.ptr += srvIncrSize * 2;
			m_d3dDevice->CreateShaderResourceView(
				nullptr, &srvDesc, slot2);
		}
	}

	/// モード4用SRVヒープ: [0]=深度, [1]=法線, [2]=色コピー
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 3;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_d3dDevice->CreateDescriptorHeap(
			&desc, IID_PPV_ARGS(m_depthColorSRVHeap.GetAddressOf()));

		if (m_depthColorSRVHeap)
		{
			auto start = m_depthColorSRVHeap->GetCPUDescriptorHandleForHeapStart();

			// t0: 深度バッファ (MSAA 4x — ENG-105 v2)
			D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
			depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
			depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			m_d3dDevice->CreateShaderResourceView(
				m_depthBuffer.Get(), &depthSrv, start);

			// t1: 法線バッファ (MSAA 4x — ENG-105 v2)
			D3D12_CPU_DESCRIPTOR_HANDLE slot1 = start;
			slot1.ptr += srvIncrSize;
			D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv = {};
			normalSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			normalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			normalSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			m_d3dDevice->CreateShaderResourceView(
				m_normalBuffer.Get(), &normalSrv, slot1);

			// t2: 色コピーバッファ
			D3D12_CPU_DESCRIPTOR_HANDLE slot2 = start;
			slot2.ptr += srvIncrSize * 2;
			D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv = {};
			colorSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			colorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			colorSrv.Texture2D.MipLevels = 1;
			m_d3dDevice->CreateShaderResourceView(
				m_colorCopyBuffer.Get(), &colorSrv, slot2);
		}
	}
}

// ─────────────────────────────────────────────────────────────
//  Fresnel付きメインPSO
// ─────────────────────────────────────────────────────────────

/// @brief Fresnel付きメインPSOを生成する（モード5用）
void createFresnelMainPSO()
{
	if (!m_toonVS || !m_fresnelToonPS) return;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = m_toonVS->shaderBytecode();
	psoDesc.PS = m_fresnelToonPS->shaderBytecode();

	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayoutInternal(inputLayout, inputCount);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = inputCount;

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState.RenderTarget[1] = psoDesc.BlendState.RenderTarget[0];

	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 2;
	// ENG-106 HDR: RT0 = MSAA color FP16, RT1 = normal R8
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	// 4x MSAA — 共有 depth/normal の sample count に揃える (ENG-105 v2)
	psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNT;

	m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_fresnelMainPSO.GetAddressOf()));
}
