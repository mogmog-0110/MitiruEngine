#pragma once

/// @file Renderer3D_DX12_Setup_impl.hpp
/// @brief Renderer3D_DX12 の初期化・リサイズ・破棄の実装本体（Renderer3D_DX12.hpp から機械的分割）

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/debug/BootTrace.hpp>
#include <mitiru/render/HdrEnvironment.hpp>
#include <mitiru/render/Renderer3D_DX12.hpp>

#ifdef _WIN32

namespace mitiru::render
{

/// @brief レンダラーを初期化する
/// @param device Dx12Device へのポインタ（外部で管理・ライフタイム保証）
/// @param cfg レンダラー設定
inline void Renderer3D_DX12::initialize(gfx::Dx12Device* device, const Config& cfg)
{
	if (!device)
	{
		throw std::runtime_error(
			"Renderer3D_DX12: device is null");
	}
	discardDrawRun();
	m_drawMerge = env::value("MITIRU_DRAW_MERGE") != "0";   // 切ると、まとめた絵とまとめない絵を同じビルドで比べられる

	m_device = device;
	m_d3dDevice = device->nativeDevice();
	m_config = cfg;
	m_needs = cfg.needs;
	m_outputWidth = cfg.viewportWidth;
	m_outputHeight = cfg.viewportHeight;
	m_sceneAmbient = cfg.defaultAmbient;

	// 段階的初期化（デバッグ用）
	try {
		createCommandResources();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createCommandResources: ") + e.what());
	}

	mitiru::debug::boottrace::mark("r3d_upload_ring");
	// per-frame UPLOAD ring buffer: drawMesh 毎の CreateCommittedResource を撤廃
	// CB / VB / IB / staging を 1 リングに集約。最初のページは 1 MiB で、足りない場面だけ 8 MiB のページを足す
	// (3 面ぶんを最初から抱えると、使わない場面にも作業メモリとプライベートバイトを約 48 MB 払わせた)
	if (!m_uploadRing.initialize(m_d3dDevice, FRAME_COUNT,
	                             UINT64{8} * 1024 * 1024, UINT64{1} * 1024 * 1024))
	{
		throw std::runtime_error(
			"DX12 Dx12UploadRing initialize failed");
	}

	mitiru::debug::boottrace::mark("r3d_albedo_heap");
	// アルベド SRV 用 shader-visible heap (capacity 256。フレーム内 draw 数の上限)
	try {
		createAlbedoSrvHeap();
	} catch (const std::exception& e) {
		throw std::runtime_error(
			std::string("DX12 createAlbedoSrvHeap: ") + e.what());
	}

	mitiru::debug::boottrace::mark("r3d_shadow_maps");
	// シャドウマップを初期化する（描画/サンプリングは設定 ON 時のみ実施）
	applyShadowCascadeMode();
	if (!m_shadowMap.initialize(m_d3dDevice,
	                            m_directionalShadow.config().mapSize))
	{
		throw std::runtime_error("DX12 Dx12ShadowMap initialize failed");
	}
	// カスケード 1 (遠距離、B13)。setCascadedShadowEnabled(false) の間は未使用のままだが、
	// 場面の表 (ensureSceneTable) が毎フレーム t2 を埋めるため常に初期化しておく。
	// 2 列のアトラス: 左 = カスケード 1、右 = カスケード 2 (3 カスケード時のみ描く)。SRV は t2 の 1 枚のまま。
	if (!m_shadowMapFar.initialize(m_d3dDevice,
	                               m_directionalShadow.config().farMapSize, 2))
	{
		throw std::runtime_error("DX12 Dx12ShadowMap (far cascade) initialize failed");
	}
	// 影を落とすスポットのアトラス。作れなければスポットは影なしで描く (場面の表は白で埋める)
	(void)m_spotShadowAtlas.initialize(m_d3dDevice, kSpotShadowMapSize, kMaxSpotShadows);
	if (pointShadowDeclared()) { (void)createPointShadowAtlas(); }

	mitiru::debug::boottrace::mark("r3d_compile_shaders");
	try {
		compileShaders();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 compileShaders: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_root_sig");
	try {
		createRootSignature();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createRootSignature: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_shadow_pso");
	// シャドウパス用 PSO (depth-only, no PS)。
	// VS (m_toonVS) と root signature を流用するため、compileShaders /
	// createRootSignature の後でなければ早期 return して PSO が作られない
	// (= 影の深度パスが毎フレーム no-op になり影が一切出ない)。
	try {
		createShadowPSO();
		createShadowCommandList();
	} catch (const std::exception& e) {
		throw std::runtime_error(
			std::string("DX12 createShadowPSO: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_main_pso");
	try {
		createMainPSO();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createMainPSO: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_silhouette_pso");
	// 作れなければ影絵だけを描かない (透かしそのものは PS の中なので残る)
	createSilhouettePso();
	mitiru::debug::boottrace::mark("r3d_depth_buffer");
	try {
		createDepthBuffer();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createDepthBuffer: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_outline");
	try {
		createOutlinePostProcess();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createOutlinePostProcess: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_occlusion_resolve");
	// オクルージョン min-depth resolve。m_outlinePostVS（フルスクリーン三角形 VS）と
	// m_depthSRVHeap（深度 SRV スロット 0）の両方に依存するため、
	// createOutlinePostProcess の後で呼ぶ。失敗しても致命的ではない
	// （既定 OFF の setOcclusionCullingEnabled を呼ばなければ影響しない）ので
	// try/catch で握り潰さず、生成失敗は関数内部で早期 return するだけにする。
	createOcclusionResolveResources();
	mitiru::debug::boottrace::mark("r3d_fxaa");
	try {
		createFXAAPipelines();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createFXAAPipelines: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_tonemap");
	try {
		createTonemapPipeline();
	} catch (const std::exception& e) {
		throw std::runtime_error(std::string("DX12 createTonemapPipeline: ") + e.what());
	}
	mitiru::debug::boottrace::mark("r3d_ssao");
	// SSAO (v40)。outline post の root sig/VS と tonemap の SRV heap の両方に依存するので最後。
	// 既定 OFF なので失敗は関数内の早期 return に任せる (setAmbientOcclusion が no-op になるだけ)。
	// 新体制では ambient_occlusion を宣言したときだけ作る (4 MiB と起動の 3 ms を、使わないゲームに払わせない)
	if (m_needs.wants(features::Feature::AmbientOcclusion)) { createSsaoPipelines(); }
	// 自動露出と接触影は、宣言したときだけ初期化で作る。宣言しない旧体制は、最初に頼まれたフレームで作る (全員に払わせない)
	if (m_needs.declaredMode && m_needs.wants(features::Feature::AutoExposure)) { createAutoExposureResources(); }
	if (m_needs.declaredMode && m_needs.wants(features::Feature::ContactShadow)) { createContactShadowPipeline(); }
	mitiru::debug::boottrace::mark("r3d_bloom");
	// bloom (v41)。tonemap の root sig / VS / SRV heap に依存する。既定 OFF なので失敗は早期 return に任せる
	createBloomPipelines();
	mitiru::debug::boottrace::mark("r3d_dof");
	// 被写界深度 (v44)。tonemap の root sig / VS と FXAA の intermediate に依存する。既定 OFF
	createDofPipeline();
	mitiru::debug::boottrace::mark("r3d_temporal");
	// TAA・動きベクトル・動きのぼけ・剣筋。outline post の VS と FXAA の intermediate に依存する。既定 OFF。
	// TAA と動きベクトルは 59 MiB の履歴と動きの絵を持つので、新体制では宣言した行 (動きのぼけと内部解像度の宣言も引き込む) だけが作る
	if (m_needs.wants(features::Feature::TaaMotionVectors)) { createTemporalPipelines(); }
	if (m_needs.wants(features::Feature::MotionBlur)) { createMotionBlurPipelines(); }
	mitiru::debug::boottrace::mark("r3d_trails_toon");
	createTrailPipelines();
	createHitFeelPipeline();
	createToonPipelines();

	mitiru::debug::boottrace::mark("r3d_oit");
	// 半透明 OIT (accum/reveal MSAA RT + 透明 PSO)。root sig / depth / MSAA 確定後。
	// 新体制で oit を宣言していなければ作らず、半透明のメッシュは不透明と同じ経路で描く (100 MiB を使わないゲームに払わせない)
	if (m_needs.wants(features::Feature::Oit))
	{
		try {
			createOitResources();
		} catch (const std::exception& e) {
			throw std::runtime_error(std::string("DX12 createOitResources: ") + e.what());
		}
	}

	mitiru::debug::boottrace::mark("r3d_clod");
	// clod 世界ジオメトリパス (SM6.6 が無い環境では supported()==false で縮退)。新体制では clod を宣言したときだけ作る
	m_clod.waitIdle = [this] { if (m_device != nullptr) { m_device->waitForGpu(); } };
	m_clod.writeFrameSrvs = [this](D3D12_CPU_DESCRIPTOR_HANDLE cpu) { writeClodFrameSrvs(cpu); };
	if (m_needs.wants(features::Feature::Clod)) { createClodRuntime(); }
#if defined(MITIRU_HAS_EFFEKSEER)
	if (m_needs.wants(features::Feature::Effekseer)) { createEffekseerRuntime(); }
#endif
	mitiru::debug::boottrace::mark("r3d_timers");
	// 測れない環境 (タイムスタンプ非対応のキュー) でも描画は続ける
	(void)m_frameTimer.init(m_d3dDevice, device->commandQueue(), FRAME_COUNT);
	(void)m_passTimeline.init(m_d3dDevice, device->commandQueue(), FRAME_COUNT);
	mitiru::debug::boottrace::mark("r3d_cluster_lights");
	// 局所光の割り当て。作れなくても主光源だけで描ける
	createClusteredLightResources();
	mitiru::debug::boottrace::mark("r3d_decals_particles");
	// デカールと GPU パーティクル。作れなければそれぞれ描かないだけで、3D は続ける
	createDecalResources();
	createParticlePipelines();

	mitiru::debug::boottrace::mark("r3d_descriptor_tables");
	m_initialized = true;
	registerDescriptorTables();
	mitiru::debug::boottrace::mark("r3d_done");
}

/// @brief clod の固定バッファとパイプライン。SM6.6 が無い GPU では supported() が false のまま縮退する
inline void Renderer3D_DX12::createClodRuntime()
{
	if (m_clod.initialize(m_d3dDevice, FRAME_COUNT))
	{
		createClodInjectPso();
	}
}

inline bool Renderer3D_DX12::featureCreated(features::Feature f) const noexcept
{
	using features::Feature;
	switch (f)
	{
	case Feature::Oit: return m_oit.isInitialized() && m_oitTransparentPSO;
	case Feature::Clod: return m_clod.supported();
	case Feature::TaaMotionVectors: return velocityReady() && m_taaPSO;
	case Feature::MotionBlur: return m_mbTileMaxPSO && m_mbTileMax;
	case Feature::AmbientOcclusion: return m_ssaoPSO && m_ssaoTex[0];
	case Feature::AutoExposure: return m_autoExposurePSO && m_autoExposureTex;
	case Feature::ContactShadow: return m_contactPSO && m_ssaoTex[0];
	case Feature::PointShadow: return m_pointShadowAtlas.isInitialized();
	case Feature::Effekseer:
#if defined(MITIRU_HAS_EFFEKSEER)
		return m_effekseer != nullptr;
#else
		return false;
#endif
	default: return true;
	}
}

inline void Renderer3D_DX12::declareFeatures(const features::RenderNeeds& needs)
{
	if (!m_needs.declaredMode || !needs.declaredMode) { return; }
	if (!m_needsPending) { m_pendingNeeds = m_needs; }
	m_pendingNeeds.declared |= needs.declared;
	m_needsPending = true;
}

/// beginFrame の頭 (描画の外) で、増えた宣言のぶんを作る。GPU が前のフレームを読み終えるのを待ってから作り、記述子の組は登録簿が書き替える
inline void Renderer3D_DX12::applyPendingNeeds()
{
	if (!m_needsPending) { return; }
	m_needsPending = false;
	using features::Feature;
	const features::RenderNeeds before = m_needs;
	m_needs = m_pendingNeeds;
	const auto grew = [&](Feature f) { return !before.wants(f) && m_needs.wants(f); };
	// 作れなかった機能は宣言を取り消す。作った物として扱うと、使われたときに未宣言の知らせも出ず、作り直すこともできない
	const auto create = [&](Feature f, const auto& make) {
		if (!grew(f)) { return; }
		try { make(); }
		catch (const std::exception& e)
		{
			m_needs.declared.reset(static_cast<std::size_t>(f));
			console::verbosef("増えた宣言の機能 %s を作れませんでした (%s)", features::key(f), e.what());
			return;
		}
		// 作る関数の多くは前提が欠けると例外でなく黙って戻るので、できた物で確かめる
		if (!featureCreated(f))
		{
			m_needs.declared.reset(static_cast<std::size_t>(f));
			console::verbosef("増えた宣言の機能 %s を作れませんでした", features::key(f));
		}
	};
	if (m_device != nullptr) { m_device->waitForGpu(); }
	create(Feature::AmbientOcclusion, [&] { createSsaoPipelines(); });
	create(Feature::AutoExposure, [&] { createAutoExposureResources(); });
	create(Feature::ContactShadow, [&] { createContactShadowPipeline(); });
	create(Feature::TaaMotionVectors, [&] { createTemporalPipelines(); });
	create(Feature::MotionBlur, [&] {
		createMotionBlurPipelines();
		if (!m_mbTileMax && m_temporalSrvHeap) { createMotionBlurResources(); }
	});
	create(Feature::Oit, [&] { createOitResources(); });
	create(Feature::Clod, [&] { createClodRuntime(); });
	create(Feature::PointShadow, [&] { (void)createPointShadowAtlas(); });
#if defined(MITIRU_HAS_EFFEKSEER)
	create(Feature::Effekseer, [&] { createEffekseerRuntime(); });
#endif
	features::setActiveNeeds(m_needs);   // 知らせの側が、作ってある宣言 (減らない和集合) を読む
	// 宣言が足りない間に落とした頼みを、作れた今から当て直す。倍率と画質の段は次の beginFrame の applyRequestedQuality が当てる
	if (grew(Feature::RenderScale)) { m_appliedRequestedRenderScale = -1.0f; }
	setAntiAliasing(m_requestedAa);
	setMotionBlur(m_requestedMotionBlur);
	(void)refreshDescriptorTables(dx12::RefreshPhase::Lazy);
}

/// @brief clod inject の root sig + PSO (fullscreen、SV_Depth 書きで depth-tested 合成)
inline void Renderer3D_DX12::createClodInjectPso()
{
	static constexpr const char* kInjectHlsl = R"(
Texture2D<float4> ClodColor : register(t0);
StructuredBuffer<uint2> VisBuf : register(t1);
cbuffer CB : register(b0) { uint2 dims; float slot; }
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint vid : SV_VertexID)
{
	float2 uv = float2((vid << 1) & 2, vid & 2);
	VSOut o;
	o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
	o.pos.y = -o.pos.y;
	return o;
}
struct PSOut { float4 color : SV_Target0; float4 normal : SV_Target1; float depth : SV_Depth; };
PSOut PSMain(VSOut i)
{
	uint2 p = uint2(i.pos.xy);
	uint2 v = VisBuf[p.y * dims.x + p.x];
	if ((v.x | v.y) == 0) { discard; }
	PSOut o;
	o.color = float4(ClodColor.Load(int3(p, 0)).rgb, 1.0);
	// 法線は書かない (長さ 0 なので線は折れ目を探さない)。a は描画の印 (DX12ToonShaders.hpp。N·V は 0)
	o.normal = float4(0.5, 0.5, 0.5, slot * 32.0 / 255.0);
	o.depth = 1.0 - float(v.y >> 2) / 1073741823.0;
	return o;
}
)";
	// root: [0] = SRV table (t0,t1)、[1] = 32bit 定数 (画面寸法と描画の印)
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
	prm[1].Constants.Num32BitValues = 3;
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
	pd.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 2;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pd.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
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
	(void)gfx::compileDx12Shader(DX12_DEFAULT_VS_3D, "VSMain", "vs_5_0", 0,
		vs.GetAddressOf(), err.GetAddressOf());
	(void)gfx::compileDx12Shader(dx12LitPixelShader(LitShade::PhongOit), "PSMain", "ps_5_0", 0,
		ps.GetAddressOf(), err.GetAddressOf());
	if (!vs || !ps)
	{
		throw std::runtime_error("createOitResources: shader compile (transparent) failed");
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

/// @brief 出力 (バックバッファ) の大きさを変える。3D の内部解像度はこれに setRenderScale の倍率を掛けた大きさ
inline void Renderer3D_DX12::resize(float width, float height)
{
	discardDrawRun();
	if (width <= 0.0f || height <= 0.0f)
	{
		return;
	}
	if (m_initialized && width == m_outputWidth && height == m_outputHeight)
	{
		return;
	}
	m_outputWidth = width;
	m_outputHeight = height;
	applyRenderSize();
}

/// @brief 内部解像度の大きさの資源を作り替える。GPU の完了は呼び出し側が待つ。
///        記述子は書かない。それを写す組は登録簿 (DX12DescriptorTableList.hpp) が世代の違いで書き替える
inline void Renderer3D_DX12::rebuildViewportResources()
{
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
	if (m_smaaReady)
	{
		m_smaaReady = createSmaaTargets();
		m_smaaFailed = !m_smaaReady;
	}
	/// SSAO のテクスチャ (viewport サイズ)
	createSsaoResources();
	/// bloom の 1/2・1/4 解像テクスチャも viewport に従う
	createBloomResources();
	/// 被写界深度のヒープ
	createDofResources();
	/// 動きベクトルと TAA の履歴も viewport の大きさ。履歴は捨てる
	createTemporalResources();
	/// オクルージョン resolve RT + readback もビューポートサイズ依存のため作り直す
	m_occlusionResolveTex.Reset();
	m_occlusionResolveRtvHeap.Reset();
	for (auto& rb : m_occlusionReadback) { rb.Reset(); }
	for (auto& pending : m_occlusionReadbackPending) { pending = false; }
	createOcclusionResolveResources();
	/// 色コピーバッファ (backbuffer サイズ) + モード 3/4 SRV ヒープを再生成する
	m_colorCopyBuffer.Reset();
	m_colorEdgeSRVHeap.Reset();
	m_depthColorSRVHeap.Reset();
	createColorCopyBuffer();
	/// OIT accum/reveal を新サイズで再生成する (composite PSO は流用)
	m_oit.resize(static_cast<UINT>(m_config.viewportWidth), static_cast<UINT>(m_config.viewportHeight));
}

/// @brief リソースを破棄する
inline void Renderer3D_DX12::destroy()
{
	discardDrawRun();
	if (!m_initialized)
	{
		return;
	}

	/// 読み込みの前半を待ってから GPU 処理の完了を待ち、リソースを解放する
	discardStreaming();
	if (m_device)
	{
		m_device->waitForGpu();
	}
#if defined(MITIRU_HAS_EFFEKSEER)
	m_effekseer.reset();
#endif

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
	m_occlusionResolvePSO.Reset();
	m_occlusionResolveRootSig.Reset();
	m_occlusionResolveTex.Reset();
	m_occlusionResolveRtvHeap.Reset();
	for (auto& rb : m_occlusionReadback) { rb.Reset(); }
	for (auto& pending : m_occlusionReadbackPending) { pending = false; }

	m_initialized = false;
}

/// @brief キューブマップ skybox をセットする（DX12）
/// @details HDR の環境の間は、入れる前の空として預かるだけで、外したときに戻す。
inline void Renderer3D_DX12::setSkybox(const Cubemap& cubemap)
{
	flushDrawRun();
	if (!m_environmentHdrName.empty()) { m_skyboxBeforeHdr = cubemap; return; }
	installSkybox(cubemap);
}

/// @details テクスチャ部分（TextureCube + upload + SRV）のみリセットし、
///          PSO / root signature / VB / IB / CB は再利用する。
///          これにより 1/2/3 のような頻繁な variant 切替で
///          shader compile + PSO 作成が行われない（「もっさり」防止）。
inline void Renderer3D_DX12::installSkybox(const Cubemap& cubemap)
{
	flushDrawRun();
	m_skyboxCubemap = cubemap;
	m_skyboxIsHdr = false;
	m_skyboxHdr.reset();
	// テクスチャまわりだけリセットする。pipeline は流用する
	m_skyboxTextureReady = false;
	m_skyboxNeedsUpload  = false;
	// 投入済みのフレームは空を最初の不透明 draw で描いている。ここで消すと、場面が変わった直後に GPU が
	// 解放済みのヒープとテクスチャを読んで止まる (#79: つづきから → 事務所の空で DEVICE_HUNG)
	// テクスチャとアップロードバッファは GpuResource が自分で完了を待つ。ヒープだけ預ける。
	if (m_device)
	{
		m_device->deferRelease(m_skyboxSrvHeap);
	}
	m_skyboxTexture.Reset();
	m_skyboxUpload.Reset();
	m_skyboxSrvHeap.Reset();
	if (cubemap.valid())
	{
		m_skyboxEnabled = true;
	}
}

// 空は画像の幅に見合う面 (正距円筒の幅の 1/4)、環境光は同じ面を畳み込む。どちらも GPU へ写すまで元を共有する。
// 明るさは画像のまま持ち、intensity は描くときに掛ける (空は skybox の定数、環境光は IblParams.y)
inline void Renderer3D_DX12::setEnvironmentHdr(const char* name, float intensity, float yawDeg)
{
	flushDrawRun();   // 溜めた描画は今の空と明るさで描き終える (溜めが無ければ何もしない)
	const bool off = name == nullptr || name[0] == '\0';
	if (off)
	{
		if (m_environmentHdrName.empty()) { return; }
		m_environmentHdrName.clear();
		m_environmentHdrImage.reset();
		m_environmentHdrImageName.clear();
		m_environmentHdrIntensity = 1.0f;
		installEnvironment(m_environmentBeforeHdr);
		installSkybox(m_skyboxBeforeHdr);
		m_environmentBeforeHdr = Cubemap{};
		m_skyboxBeforeHdr = Cubemap{};
		return;
	}
	if (name == m_environmentHdrName)
	{
		// 明るさだけ変わったときは、読み直しも畳み込みも要らない
		if (yawDeg == m_environmentHdrYaw)
		{
			m_environmentHdrIntensity = intensity;
			return;
		}
	}
	if (m_environmentHdrImageName != name)
	{
		const auto bytes = vfs::readAsset(name);
		auto image = bytes ? decodeRadianceHdr(*bytes) : std::nullopt;
		if (!image)
		{
			debug::warnOnce(std::string("dx12.hdr_environment.") + name,
			                std::string("HDR の環境 ") + name +
			                    " を読めないので、環境は今のままです (Radiance の .hdr を、assets からの相対か絶対のパスで)");
			return;
		}
		m_environmentHdrImage = std::make_shared<const EquirectImage>(std::move(*image));
		m_environmentHdrImageName = name;
	}
	const bool entering = m_environmentHdrName.empty();
	if (entering)
	{
		m_environmentBeforeHdr = m_pbrEnvironmentCubemap;
		m_skyboxBeforeHdr = m_skyboxCubemap;
	}
	const auto cube = std::make_shared<const HdrCubemap>(
		hdrCubemapFromEquirect(*m_environmentHdrImage, skyFaceSizeFor(m_environmentHdrImage->width), yawDeg));
	m_environmentHdrName = name;
	m_environmentHdrYaw = yawDeg;
	m_environmentHdrIntensity = intensity;
	installEnvironment(Cubemap{});
	installSkybox(Cubemap{});
	m_pbrEnvironmentIsHdr = true;
	m_pbrEnvironmentHdr = cube;
	m_skyboxIsHdr = true;
	m_skyboxHdr = cube;
	if (entering) { m_skyboxEnabled = true; }   // 入れた後の setSkyboxEnabled(false) は、向きや画像を替えても保つ
}

} // namespace mitiru::render

#endif // _WIN32
