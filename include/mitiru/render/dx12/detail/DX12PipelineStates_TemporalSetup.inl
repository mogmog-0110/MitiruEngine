// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// フレームをまたぐ後処理 (TAA・動きベクトル・動きのぼけ) の設定、状態、資源の生成。
// パスの記録は DX12PipelineStates_TemporalPasses.inl と DX12PipelineStates_MotionBlur.inl。

public:

/// @brief 3D の AA の方式を選ぶ。Taa 以外へ切り替えると履歴とずらしの相を捨てる
void setAntiAliasing(AntiAliasing3D mode) noexcept override
{
	if (mode != m_aaMode) { resetTemporalHistory(); }
	m_aaMode = mode;
}

[[nodiscard]] AntiAliasing3D antiAliasing() const noexcept { return m_aaMode; }

/// @brief 履歴を捨てる。カメラが別の場所へ飛んだフレーム (場面の切り替え) の頭で呼ぶ
void resetTemporalHistory() noexcept override
{
	m_taaHistoryValid = false;
	m_taaFrameIndex = 0;
	m_prevViewProjValid = false;
	m_motionHistory.clear();
	m_upscaleHistoryValid = false;
	resetViewTemporalHistories();
}

/// @brief 動きのぼけ。strength はシャッターの開いている割合 (0 = 無効、1 = 1 フレームの移動ぶん)
void setMotionBlur(float strength) noexcept override
{
	m_motionBlurStrength = (strength < 0.0f) ? 0.0f : ((strength > 1.0f) ? 1.0f : strength);
}

[[nodiscard]] float motionBlur() const noexcept { return m_motionBlurStrength; }

/// @brief 動きベクトルを作るフレームか (TAA か動きのぼけか TAAU が有効)
[[nodiscard]] bool motionVectorsActive() const noexcept
{
	return m_aaMode == AntiAliasing3D::Taa || m_motionBlurStrength > 0.0f || upscaleActive();
}

/// @brief このフレームの射影のずらし (NDC)。TAA でなければ 0
[[nodiscard]] JitterOffset projectionJitterNdc() const noexcept { return m_jitterNdc; }

/// @brief 後処理の GPU 時間を測るか。測ると postGpuMilliseconds が数フレーム遅れて埋まる
void setPostGpuTimingEnabled(bool enabled) noexcept { m_postTimingEnabled = enabled; }

[[nodiscard]] double postGpuMilliseconds(PostGpuPass pass) const noexcept
{
	return m_postTimer.milliseconds(static_cast<std::uint32_t>(pass));
}

private:

AntiAliasing3D m_aaMode = AntiAliasing3D::MsaaFxaa;
float          m_motionBlurStrength = 0.0f;

/// 射影のずらし。m_projMatrix はずらし込み、m_projNoJitter / m_viewProjNoJitter は抜いたもの。
/// 抜いた方は動きベクトルと worldToScreen が使う (HUD の位置が画素の中で震えないように)
JitterOffset  m_jitterNdc{};
std::uint32_t m_taaFrameIndex = 0;   ///< 履歴を作り直してからのフレーム数 = ずらしの相
glm::mat4     m_projNoJitter{1.0f};
glm::mat4     m_viewProjNoJitter{1.0f};
glm::mat4     m_prevViewProjNoJitter{1.0f};
bool          m_cameraEverSet = false;
bool          m_prevViewProjValid = false;

bool m_taaHistoryValid = false;
int  m_taaHistoryWrite = 0;   ///< 今フレームの TAA が書く履歴 (もう片方を読む)

/// 動きベクトル用に、不透明の描画を前フレームと突き合わせる
struct MotionDraw
{
	const Mesh*     mesh = nullptr;         ///< VB/IB のキャッシュを引く鍵
	sgc::Mat4f      world;
	ID3D12Resource* skinnedVertices = nullptr;   ///< GPU スキニングの出力 (剛体なら nullptr)
	uint32_t        skinSlot = 0;
	uint64_t        frame = 0;
	bool            cameraLocked = false;   ///< setMotionVectorCaster(false) の間の描画。画面上で止まって見える
	bool            drawn = true;           ///< false = 視錐台で落ちたか半透明。対の順番にだけ数え、動きは描かない
};
FrameMotionHistory<MotionDraw> m_motionHistory;

/// 資源。SRV の表は 4 枚ずつで、番号は kTemporalTable* (動きのぼけの表もここに並ぶ)
static constexpr UINT kTemporalTableSize = 4;
static constexpr UINT kTemporalTableVelocity = 0;
static constexpr UINT kTemporalTableTaa = 1;          ///< 1 = 履歴 0 へ書く、2 = 履歴 1 へ書く
static constexpr UINT kTemporalTableTileMax = 3;
static constexpr UINT kTemporalTableNeighborMax = 4;
static constexpr UINT kTemporalTableMotionBlur = 5;
static constexpr UINT kTemporalTableCount = 6;
static constexpr UINT kTemporalRtvVelocityObject = 0;
static constexpr UINT kTemporalRtvVelocity = 1;
static constexpr UINT kTemporalRtvHistory = 2;        ///< 2 と 3
static constexpr UINT kTemporalRtvTileMax = 4;
static constexpr UINT kTemporalRtvNeighborMax = 5;
static constexpr UINT kTemporalRtvCount = 6;

gfx::GpuResource             m_velocityObjectTex;   ///< 物の動き (R16G16F)
gfx::GpuResource             m_velocityDepth;       ///< 物の描き直しの深度 (single-sample)
gfx::GpuResource             m_velocityTex;         ///< 画面全体の動き (R16G16F)
gfx::GpuResource             m_taaHistory[2];       ///< RGBA16F。8 bit だと 0.1 ずつの混ぜで端数が消え、収束が止まる
ComPtr<ID3D12DescriptorHeap> m_temporalRtvHeap;
ComPtr<ID3D12DescriptorHeap> m_temporalDsvHeap;
ComPtr<ID3D12DescriptorHeap> m_temporalSrvHeap;
ComPtr<ID3D12RootSignature>  m_temporalRootSig;     ///< 全画面パス: SRV t0..t3 + CBV b0
ComPtr<ID3D12RootSignature>  m_velocityObjectRootSig;
ComPtr<ID3D12PipelineState>  m_velocityObjectPSO;
ComPtr<ID3D12PipelineState>  m_velocityResolvePSO;
ComPtr<ID3D12PipelineState>  m_taaPSO;

dx12::Dx12GpuTimer m_postTimer;
bool               m_postTimingEnabled = false;

/// @brief temporal の root sig と PSO を作り、資源を用意する。outline post の VS と FXAA の写しの後に呼ぶ
void createTemporalPipelines()
{
	if (!m_outlinePostVS) { return; }
	if (!createTemporalRootSignatures()) { return; }
	createVelocityObjectPSO();

	const auto resolvePS = gfx::Dx12Shader::createPixelShader(DX12_VELOCITY_RESOLVE_PS, "PSMain");
	const auto taaPS = gfx::Dx12Shader::createPixelShader(DX12_TAA_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_temporalRootSig.Get(), resolvePS);
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_velocityResolvePSO.ReleaseAndGetAddressOf()))))
	{
		m_velocityResolvePSO.Reset();
	}
	pd = fullscreenPsoDesc(m_temporalRootSig.Get(), taaPS);
	pd.NumRenderTargets = 2;
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pd.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_taaPSO.ReleaseAndGetAddressOf()))))
	{
		m_taaPSO.Reset();
	}
	if (m_device != nullptr) { (void)m_postTimer.init(m_d3dDevice, m_device->commandQueue(), FRAME_COUNT); }
	createTemporalResources();
}

/// @brief 全画面パスの PSO の共通部分 (VS は outline post のフルスクリーン三角形、深度なし、RT 1 枚)
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC fullscreenPsoDesc(ID3D12RootSignature* rootSig,
                                                                   const gfx::Dx12Shader& ps) const
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = rootSig;
	pd.VS = m_outlinePostVS->shaderBytecode();
	pd.PS = ps.shaderBytecode();
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = FALSE;
	for (auto& rt : pd.BlendState.RenderTarget) { rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; }
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
	pd.SampleDesc.Count = 1;
	return pd;
}

[[nodiscard]] static D3D12_STATIC_SAMPLER_DESC clampSampler(D3D12_FILTER filter, UINT reg) noexcept
{
	D3D12_STATIC_SAMPLER_DESC s = {};
	s.Filter = filter;
	s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.MaxAnisotropy = 1;
	s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	s.MaxLOD = D3D12_FLOAT32_MAX;
	s.ShaderRegister = reg;
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return s;
}

[[nodiscard]] bool serializeRootSig(const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out)
{
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) { return false; }
	return SUCCEEDED(m_d3dDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
	                                                  IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
}

[[nodiscard]] bool createTemporalRootSignatures()
{
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = kTemporalTableSize;
	D3D12_ROOT_PARAMETER post[2] = {};
	post[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	post[0].DescriptorTable.NumDescriptorRanges = 1;
	post[0].DescriptorTable.pDescriptorRanges = &range;
	post[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	post[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	post[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	const D3D12_STATIC_SAMPLER_DESC samplers[2] = {
		clampSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0), clampSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, 1)};
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = post;
	rd.NumStaticSamplers = 2;
	rd.pStaticSamplers = samplers;
	if (!serializeRootSig(rd, m_temporalRootSig)) { return false; }

	D3D12_ROOT_PARAMETER obj[2] = {};
	for (UINT i = 0; i < 2; ++i)
	{
		obj[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		obj[i].Descriptor.ShaderRegister = i;
		obj[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	}
	D3D12_ROOT_SIGNATURE_DESC od = {};
	od.NumParameters = 2;
	od.pParameters = obj;
	od.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	return serializeRootSig(od, m_velocityObjectRootSig);
}

/// @brief 物の描き直しの PSO。頂点は slot 0 が今の位置、slot 1 が前の位置 (剛体は同じ VB を 2 回渡す)
void createVelocityObjectPSO()
{
	const auto vs = gfx::Dx12Shader::createVertexShader(DX12_VELOCITY_OBJECT_HLSL, "VSMain");
	const auto ps = gfx::Dx12Shader::createPixelShader(DX12_VELOCITY_OBJECT_HLSL, "PSMain");
	const D3D12_INPUT_ELEMENT_DESC layout[2] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 7, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_velocityObjectRootSig.Get();
	pd.VS = vs.shaderBytecode();
	pd.PS = ps.shaderBytecode();
	pd.InputLayout = {layout, 2};
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = 1;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_velocityObjectPSO.ReleaseAndGetAddressOf()))))
	{
		m_velocityObjectPSO.Reset();
	}
}

[[nodiscard]] bool createTemporalTexture(UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                         const wchar_t* name, gfx::GpuResource& out)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = flags;
	D3D12_CLEAR_VALUE clear = {};
	const bool depth = (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0;
	clear.Format = depth ? DXGI_FORMAT_D32_FLOAT : format;
	clear.DepthStencil.Depth = 1.0f;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc,
	                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, out)) || !out)
	{
		return false;
	}
	out->SetName(name);
	return true;
}

/// @brief viewport の大きさの資源と RTV/DSV/SRV を作り直す。resize からも呼ぶ (FXAA の写しと深度の後)
void createTemporalResources()
{
	m_taaHistoryValid = false;
	if (!m_d3dDevice || !m_depthBuffer || !m_fxaaIntermediate) { return; }
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	constexpr auto kRt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const bool ok =
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, kRt, L"Renderer3D velocity (objects)", m_velocityObjectTex) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		                      L"Renderer3D velocity depth", m_velocityDepth) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, kRt, L"Renderer3D velocity", m_velocityTex) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D TAA history 0", m_taaHistory[0]) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D TAA history 1", m_taaHistory[1]);
	if (!ok || !createTemporalHeaps()) { m_velocityTex.Reset(); return; }
	createMotionBlurResources();
	writeTemporalViews();
}

[[nodiscard]] bool createTemporalHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv = {};
	rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv.NumDescriptors = kTemporalRtvCount;
	D3D12_DESCRIPTOR_HEAP_DESC dsv = {};
	dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv.NumDescriptors = 1;
	D3D12_DESCRIPTOR_HEAP_DESC srv = {};
	srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv.NumDescriptors = kTemporalTableCount * kTemporalTableSize;
	srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (m_device != nullptr)
	{
		m_device->deferRelease(m_temporalSrvHeap);
	}
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(m_temporalRtvHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(m_temporalDsvHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&srv, IID_PPV_ARGS(m_temporalSrvHeap.ReleaseAndGetAddressOf())));
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE temporalRtv(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_temporalRtvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return h;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE temporalSrvCpu(UINT table, UINT slot) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_temporalSrvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(table * kTemporalTableSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE temporalTable(UINT table) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE h = m_temporalSrvHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<UINT64>(table * kTemporalTableSize) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

/// @brief 2D テクスチャの SRV を表の 1 枠へ書く。res が null なら format の null SRV
void writeTexSrv(UINT table, UINT slot, ID3D12Resource* res, DXGI_FORMAT format)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = format;
	sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sd.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(res, &sd, temporalSrvCpu(table, slot));
}

void writeMsaaDepthSrv(UINT table, UINT slot)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_R32_FLOAT;
	sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &sd, temporalSrvCpu(table, slot));
}

void writeTemporalViews()
{
	D3D12_RENDER_TARGET_VIEW_DESC rv = {};
	rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rv.Format = DXGI_FORMAT_R16G16_FLOAT;
	m_d3dDevice->CreateRenderTargetView(m_velocityObjectTex.Get(), &rv, temporalRtv(kTemporalRtvVelocityObject));
	m_d3dDevice->CreateRenderTargetView(m_velocityTex.Get(), &rv, temporalRtv(kTemporalRtvVelocity));
	rv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_d3dDevice->CreateRenderTargetView(m_taaHistory[0].Get(), &rv, temporalRtv(kTemporalRtvHistory));
	m_d3dDevice->CreateRenderTargetView(m_taaHistory[1].Get(), &rv, temporalRtv(kTemporalRtvHistory + 1));
	D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
	dv.Format = DXGI_FORMAT_D32_FLOAT;
	dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateDepthStencilView(m_velocityDepth.Get(), &dv, m_temporalDsvHeap->GetCPUDescriptorHandleForHeapStart());

	writeMsaaDepthSrv(kTemporalTableVelocity, 0);
	writeTexSrv(kTemporalTableVelocity, 1, m_velocityObjectTex.Get(), DXGI_FORMAT_R16G16_FLOAT);
	writeTexSrv(kTemporalTableVelocity, 2, m_velocityDepth.Get(), DXGI_FORMAT_R32_FLOAT);
	writeTexSrv(kTemporalTableVelocity, 3, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM);
	for (UINT write = 0; write < 2; ++write)
	{
		const UINT table = kTemporalTableTaa + write;
		writeTexSrv(table, 0, m_fxaaIntermediate.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
		writeTexSrv(table, 1, m_taaHistory[1 - write].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
		writeTexSrv(table, 2, m_velocityTex.Get(), DXGI_FORMAT_R16G16_FLOAT);
		writeMsaaDepthSrv(table, 3);
	}
	writeMotionBlurViews();
}
