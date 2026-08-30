#pragma once

/// @file Renderer3D_DX12_Setup_impl.hpp
/// @brief Renderer3D_DX12 の初期化・リサイズ・破棄の実装本体（Renderer3D_DX12.hpp から機械的分割）

#include <mitiru/render/Renderer3D_DX12.hpp>

#ifdef _WIN32

namespace mitiru::render
{

/// @brief レンダラーを初期化する
/// @param device Dx12Deviceへのポインタ（外部で管理・ライフタイム保証）
/// @param cfg レンダラー設定
inline void Renderer3D_DX12::initialize(gfx::Dx12Device* device, const Config& cfg)
{
	if (!device)
	{
		throw std::runtime_error(
			"Renderer3D_DX12: device is null");
	}

	m_device = device;
	m_d3dDevice = device->nativeDevice();
	m_config = cfg;
	m_sceneAmbient = cfg.defaultAmbient;

	// 段階的初期化（デバッグ用）
	try {
		createCommandResources();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createCommandResources: ") + e.what());
	}

	// per-frame UPLOAD ring buffer: drawMesh 毎の CreateCommittedResource を撤廃
	// 8 MiB × FRAME_COUNT。CB / VB / IB / staging を 1 リングに集約。
	if (!m_uploadRing.initialize(m_d3dDevice, FRAME_COUNT,
	                             UINT64{8} * 1024 * 1024))
	{
		throw std::runtime_error(
			"DX12 Dx12UploadRing initialize failed");
	}

	// アルベド SRV 用 shader-visible heap (capacity 256。フレーム内 draw 数の上限)
	try {
		createAlbedoSrvHeap();
	} catch (const std::exception& e) {
		throw std::runtime_error(
			std::string("DX12 createAlbedoSrvHeap: ") + e.what());
	}

	// シャドウマップを初期化（描画/サンプリングは設定 ON 時のみ実施）
	if (!m_shadowMap.initialize(m_d3dDevice,
	                            m_directionalShadow.config().mapSize))
	{
		throw std::runtime_error("DX12 Dx12ShadowMap initialize failed");
	}

	try {
		compileShaders();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 compileShaders: ") + e.what());
	}
	try {
		createRootSignature();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createRootSignature: ") + e.what());
	}
	// シャドウパス用 PSO (depth-only, no PS)。
	// VS (m_toonVS) と root signature を流用するため、compileShaders /
	// createRootSignature の後でなければ早期 return して PSO が作られない
	// (= renderShadowPass が毎フレーム no-op になり影が一切出ない)。
	try {
		createShadowPSO();
	} catch (const std::exception& e) {
		throw std::runtime_error(
			std::string("DX12 createShadowPSO: ") + e.what());
	}
	try {
		createMainPSO();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createMainPSO: ") + e.what());
	}
	try {
		createDepthBuffer();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createDepthBuffer: ") + e.what());
	}
	try {
		createOutlinePostProcess();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createOutlinePostProcess: ") + e.what());
	}
	try {
		createFXAAPipelines();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createFXAAPipelines: ") + e.what());
	}
	try {
		createTonemapPipeline();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createTonemapPipeline: ") + e.what());
	}
	// D3D12 InfoQueue を確保し、runtime 検証エラーを毎フレーム
	// ファイルへダンプする (ENG-105 v2 MSAA debug)。Debug layer が
	// 無効でも QueryInterface は通る (メッセージが来ないだけ)。
	m_d3dDevice->QueryInterface(IID_PPV_ARGS(m_infoQueue.GetAddressOf()));

	// 半透明 OIT (accum/reveal MSAA RT + 透明 PSO)。root sig / depth / MSAA 確定後。
	try {
		createOitResources();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createOitResources: ") + e.what());
	}

	// clod 世界ジオメトリパス (SM6.6 が無い環境では supported()==false で縮退)
	m_clod.waitIdle = [this] { if (m_device != nullptr) { m_device->waitForGpu(); } };
	if (m_clod.initialize(m_d3dDevice, FRAME_COUNT))
	{
		createClodInjectPso();
	}

	m_initialized = true;
}

/// @brief clod inject の root sig + PSO (fullscreen、SV_Depth 書きで depth-tested 合成)
inline void Renderer3D_DX12::createClodInjectPso()
{
	static constexpr const char* kInjectHlsl = R"(
Texture2D<float4> ClodColor : register(t0);
StructuredBuffer<uint2> VisBuf : register(t1);
cbuffer CB : register(b0) { uint2 dims; }
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint vid : SV_VertexID)
{
	float2 uv = float2((vid << 1) & 2, vid & 2);
	VSOut o;
	o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
	o.pos.y = -o.pos.y;
	return o;
}
struct PSOut { float4 color : SV_Target; float depth : SV_Depth; };
PSOut PSMain(VSOut i)
{
	uint2 p = uint2(i.pos.xy);
	uint2 v = VisBuf[p.y * dims.x + p.x];
	if ((v.x | v.y) == 0) { discard; }
	PSOut o;
	o.color = float4(ClodColor.Load(int3(p, 0)).rgb, 1.0);
	o.depth = 1.0 - float(v.y >> 2) / 1073741823.0;
	return o;
}
)";
	// root: [0] = SRV table (t0,t1)、[1] = 32bit 定数 (画面寸法)
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 2;
	range.BaseShaderRegister = 0;
	D3D12_ROOT_PARAMETER prm[2] = {};
	prm[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	prm[0].DescriptorTable.NumDescriptorRanges = 1;
	prm[0].DescriptorTable.pDescriptorRanges = &range;
	prm[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	prm[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	prm[1].Constants.ShaderRegister = 0;
	prm[1].Constants.Num32BitValues = 2;
	prm[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 2;
	rsd.pParameters = prm;
	ComPtr<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
	{
		return;
	}
	if (FAILED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	                                            IID_PPV_ARGS(&m_clodInjectRS))))
	{
		return;
	}

	const auto vs = gfx::Dx12Shader::createVertexShader(kInjectHlsl, "VSMain");
	const auto ps = gfx::Dx12Shader::createPixelShader(kInjectHlsl, "PSMain");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_clodInjectRS.Get();
	pd.VS = { vs.bytecode().data(), vs.bytecode().size() };
	pd.PS = { ps.bytecode().data(), ps.bytecode().size() };
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_clodInjectPSO))))
	{
		m_clodInjectPSO.Reset();
		return;
	}

	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = 2;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_clodInjectHeap));
}

/// @brief 半透明 OIT のリソースを生成する (WeightedBlendedOIT + 透明 PSO)。
/// @details accum/reveal は MSAA color と同じ 4x。透明 PSO は main VS + WBOIT PS、
///          main root signature を流用、深度は読み取り専用 (DepthWrite=ZERO) で不透明を
///          遮蔽する。composite 先は MSAA color (FP16 HDR) なので tonemap 前に重なる。
inline void Renderer3D_DX12::createOitResources()
{
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	if (w == 0 || h == 0) { return; }

	// accum(RGBA16F)/reveal(R16F) を MSAA で。composite 先 = MSAA color (FP16)。
	m_oit.initialize(m_d3dDevice, w, h,
	                 DXGI_FORMAT_R16G16B16A16_FLOAT, MSAA_SAMPLE_COUNT);

	// 透明ジオメトリ PSO: main VS + WBOIT PS。
	Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, err;
	D3DCompile(DX12_DEFAULT_VS_3D, std::strlen(DX12_DEFAULT_VS_3D), nullptr, nullptr,
	           nullptr, "VSMain", "vs_5_0", 0, 0, vs.GetAddressOf(), err.GetAddressOf());
	D3DCompile(DX12_OIT_TRANSPARENT_PS_3D, std::strlen(DX12_OIT_TRANSPARENT_PS_3D), nullptr,
	           nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, ps.GetAddressOf(), err.GetAddressOf());
	if (!vs || !ps)
	{
		throw std::runtime_error("createOitResources: D3DCompile (transparent) failed");
	}

	const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
	pd.pRootSignature = m_rootSignature.Get();
	pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
	dx12::WeightedBlendedOIT::fillAccumulateBlend(pd.BlendState);
	pd.SampleMask = UINT_MAX;
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;   // 透明は両面見せる
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;  // 読み取り専用
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.InputLayout = {layout, 4};
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 2;
	pd.RTVFormats[0] = dx12::WeightedBlendedOIT::accumFormat();
	pd.RTVFormats[1] = dx12::WeightedBlendedOIT::revealFormat();
	pd.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&pd, IID_PPV_ARGS(m_oitTransparentPSO.GetAddressOf()))))
	{
		throw std::runtime_error("createOitResources: CreateGraphicsPipelineState (transparent) failed");
	}
}

/// @brief ビューポートサイズを変更する
/// @param width 新しい幅
/// @param height 新しい高さ
inline void Renderer3D_DX12::resize(float width, float height)
{
	if (width <= 0.0f || height <= 0.0f)
	{
		return;
	}

	if (m_initialized &&
	    width == m_config.viewportWidth && height == m_config.viewportHeight)
	{
		return;
	}

	m_config.viewportWidth = width;
	m_config.viewportHeight = height;

	if (m_initialized)
	{
		/// GPU が参照中のリソースを破棄しないよう完了を待つ
		if (m_device)
		{
			m_device->waitForGpu();
		}

		/// 深度 + 法線 + MSAA color バッファを再生成する (ENG-105 v2)
		m_depthBuffer.Reset();
		m_dsvHeap.Reset();
		m_normalBuffer.Reset();
		m_normalRTVHeap.Reset();
		m_msaaColorBuffer.Reset();
		m_msaaColorRtvHeap.Reset();
		createDepthBuffer();
		/// FXAA intermediate (backbuffer サイズ) を再生成する
		createFXAAIntermediate();
		/// outline post の深度/法線 SRV を再生成後のリソースへ貼り直す
		updateOutlinePostSRVs();
		/// 色コピーバッファ (backbuffer サイズ) + モード3/4 SRV ヒープを再生成する
		m_colorCopyBuffer.Reset();
		m_colorEdgeSRVHeap.Reset();
		m_depthColorSRVHeap.Reset();
		createColorCopyBuffer();
		/// OIT accum/reveal を新サイズで再生成する (composite PSO は流用)
		m_oit.resize(static_cast<UINT>(width), static_cast<UINT>(height));
	}
}

/// @brief リソースを破棄する
inline void Renderer3D_DX12::destroy()
{
	if (!m_initialized)
	{
		return;
	}

	/// GPU処理の完了を待ってからリソースを解放する
	if (m_device)
	{
		m_device->waitForGpu();
	}

	m_frameTempResources.clear();
	for (auto& v : m_perFrameTempResources) v.clear();
	m_graphicsCmdList.Reset();

	for (uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		m_commandAllocators[i].Reset();
	}

	m_mainPSO.Reset();
	m_outlinePostPSO.Reset();
	for (auto& p : m_outlinePostPSOs) p.Reset();
	m_fresnelMainPSO.Reset();
	m_outlinePostRootSig.Reset();
	m_colorCopyBuffer.Reset();
	m_colorEdgeSRVHeap.Reset();
	m_depthColorSRVHeap.Reset();
	m_rootSignature.Reset();
	m_depthBuffer.Reset();
	m_dsvHeap.Reset();

	m_initialized = false;
}

/// @brief キューブマップ skybox をセットする（DX12）
/// @details テクスチャ部分（TextureCube + upload + SRV）のみリセットし、
///          PSO / root signature / VB / IB / CB は再利用する。
///          これにより 1/2/3 のような頻繁な variant 切替で
///          shader compile + PSO 作成が走らない（「もっさり」防止）。
inline void Renderer3D_DX12::setSkybox(const Cubemap& cubemap)
{
	m_skyboxCubemap = cubemap;
	// テクスチャまわりだけリセット。pipeline は流用
	m_skyboxTextureReady = false;
	m_skyboxNeedsUpload  = false;
	m_skyboxTexture.Reset();
	m_skyboxUpload.Reset();
	m_skyboxSrvHeap.Reset();
	if (cubemap.valid())
	{
		m_skyboxEnabled = true;
	}
}

} // namespace mitiru::render

#endif // _WIN32
