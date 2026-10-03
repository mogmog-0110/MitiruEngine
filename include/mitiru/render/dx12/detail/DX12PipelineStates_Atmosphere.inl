// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 物理ベースの空と空気遠近 (Hillaire 2020)。LUT と空気遠近の froxel を compute で作り、空は主パスの深度が
// 空いている所 (深度 1) に MSAA のまま描く。空気遠近と体積フォグは resolve の後の 1 枚の合成で掛ける
// (DX12PipelineStates_Fog.inl)。パイプラインは空かフォグを最初に有効にしたフレームで作る。

public:

/// @brief 空と空気遠近の指定。enabled = false (既定) なら何もしない
void setSky(const SkySettings& sky) override
{
	m_sky = sky;
	m_sky.toonBands = std::clamp(m_sky.toonBands, 0, 8);
	m_sky.metersPerUnit = (m_sky.metersPerUnit > 0.0f) ? m_sky.metersPerUnit : 1.0f;
	m_sky.aerialPerspectiveScale = std::max(m_sky.aerialPerspectiveScale, 0.0f);
	applySkyToLight();
}

[[nodiscard]] const SkySettings& sky() const noexcept { return m_sky; }

private:

SkySettings  m_sky{};
sgc::Colorf  m_lightBaseColor = Light{}.color;   ///< ゲームが渡した主光源の色 (空が色を決める前)
sgc::Vec3f   m_skyAmbientSunDir{0.0f, 0.0f, 0.0f};        ///< 環境光を最後に計算した太陽の向き
sgc::Colorf  m_skyAmbientUp{0.0f, 0.0f, 0.0f, 1.0f};
sgc::Colorf  m_skyAmbientDown{0.0f, 0.0f, 0.0f, 1.0f};
AtmosphereParams m_atmoLutParams{};
bool         m_atmoLutDirty = true;
bool         m_atmoTried = false;

/// 記述子の組。1 組 = SRV 6 枚 (t0..t5) + UAV 1 枚 (u0)
static constexpr UINT kAtmoSrvPerSet = 6;
static constexpr UINT kAtmoSetSize = kAtmoSrvPerSet + 1;
static constexpr UINT kAtmoSetTransmittance = 0;
static constexpr UINT kAtmoSetMultiScatter = 1;
static constexpr UINT kAtmoSetSkyView = 2;
static constexpr UINT kAtmoSetAerial = 3;
static constexpr UINT kAtmoSetSkyDraw = 4;
static constexpr UINT kAtmoSetFogInject = 5;      ///< 5, 6 = 履歴 0 / 1 へ書く
static constexpr UINT kAtmoSetFogIntegrate = 7;   ///< 7, 8 = 注入 0 / 1 を読む
static constexpr UINT kAtmoSetComposite = 9;
static constexpr UINT kAtmoSetCount = 10;
static constexpr UINT kAtmoApSlices = 32;

gfx::GpuResource             m_atmoTransmittance;   ///< 256x64 RGBA16F
gfx::GpuResource             m_atmoMultiScatter;    ///< 32x32 RGBA16F
gfx::GpuResource             m_atmoSkyView;         ///< 192x108 RGBA16F
gfx::GpuResource             m_atmoAerial;          ///< 32^3 RGBA16F
ComPtr<ID3D12DescriptorHeap> m_atmoHeap;
ComPtr<ID3D12RootSignature>  m_atmoComputeRS;       ///< b0..b3 + 光 t8 + ビット集合 t9 + SRV 表 + UAV 表
ComPtr<ID3D12RootSignature>  m_atmoGraphicsRS;      ///< b0, b1 + SRV 表
ComPtr<ID3D12PipelineState>  m_atmoTransmittancePSO;
ComPtr<ID3D12PipelineState>  m_atmoMultiScatterPSO;
ComPtr<ID3D12PipelineState>  m_atmoSkyViewPSO;
ComPtr<ID3D12PipelineState>  m_atmoAerialPSO;
ComPtr<ID3D12PipelineState>  m_skyDrawPSO;
D3D12_GPU_VIRTUAL_ADDRESS    m_atmoFrameCB = 0;

/// @brief 主光源の色を GPU の線形の作業色にする (m_light.color はこの後は線形)。空が有効で driveSunLight なら、
///        カメラ位置で減衰した太陽の色にする。環境光も同じ時に決め直す
void applySkyToLight()
{
	const sgc::Colorf base = linearColor(m_lightBaseColor);
	if (!m_sky.enabled || !m_sky.driveSunLight) { m_light.color = base; }
	if (!m_sky.enabled) { return; }
	const sgc::Vec3f sunDir = (m_light.direction * -1.0f).normalized();
	const float alt = atmosphere::cameraAltitudeKm(m_sky, m_cameraPosition.y);
	if (m_sky.driveSunLight)
	{
		const sgc::Vec3f t = atmosphere::sunTransmittance(m_sky.atmosphere, alt, sunDir);
		m_light.color = {base.r * t.x, base.g * t.y, base.b * t.z, base.a};
	}
	if (m_sky.driveAmbient && (sunDir - m_skyAmbientSunDir).lengthSquared() > 1e-6f)
	{
		const auto amb = atmosphere::skyAmbient(m_sky, alt, sunDir);
		m_skyAmbientUp = {amb.sky.x, amb.sky.y, amb.sky.z, 1.0f};
		m_skyAmbientDown = {amb.ground.x, amb.ground.y, amb.ground.z, 1.0f};
		m_skyAmbientSunDir = sunDir;
	}
}

/// @brief 半球の環境光を空が決めているか
[[nodiscard]] bool skyDrivesAmbient() const noexcept { return m_sky.enabled && m_sky.driveAmbient; }

/// @brief setHemisphereAmbient の上下。両方が真っ黒なら半球を使わない指定なので、上下とも平坦な環境光
[[nodiscard]] bool hemisphereAmbientSet() const noexcept
{
	return (m_ambientSky.r + m_ambientSky.g + m_ambientSky.b + m_ambientGround.r + m_ambientGround.g + m_ambientGround.b) > 0.0f;
}

/// @brief 上からの環境光 (線形)。空が決めた値は元から線形、ゲームが書いた色は線形にする
[[nodiscard]] sgc::Colorf hemisphereAmbientSky() const noexcept
{
	if (skyDrivesAmbient()) { return m_skyAmbientUp; }
	return m_linHemiSky(hemisphereAmbientSet() ? m_ambientSky : m_sceneAmbient);
}

[[nodiscard]] sgc::Colorf hemisphereAmbientGround() const noexcept
{
	if (skyDrivesAmbient()) { return m_skyAmbientDown; }
	return m_linHemiGround(hemisphereAmbientSet() ? m_ambientGround : m_sceneAmbient);
}

[[nodiscard]] bool atmosphereActive() const noexcept { return m_sky.enabled; }

/// @brief 空とフォグの共通の root sig、記述子の heap を作る (最初に要ったフレームで 1 回)
[[nodiscard]] bool ensureAtmosphereCommon()
{
	if (m_atmoComputeRS && m_atmoGraphicsRS && m_atmoHeap) { return true; }
	if (m_atmoTried) { return false; }
	m_atmoTried = true;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kAtmoSetCount * kAtmoSetSize;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_atmoHeap.ReleaseAndGetAddressOf()))) ||
	    !createAtmosphereRootSignatures())
	{
		m_atmoHeap.Reset();
		debug::warnOnce("dx12.atmosphere.init", "空とフォグの root signature を作れない — 空もフォグも描かない");
		return false;
	}
	for (UINT set = 0; set < kAtmoSetCount; ++set) { clearAtmosphereSet(set); }
	return true;
}

[[nodiscard]] bool createAtmosphereRootSignatures()
{
	D3D12_DESCRIPTOR_RANGE srv = {};
	srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srv.NumDescriptors = kAtmoSrvPerSet;
	D3D12_DESCRIPTOR_RANGE uav = {};
	uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uav.NumDescriptors = 1;
	uav.OffsetInDescriptorsFromTableStart = 0;
	D3D12_ROOT_PARAMETER p[8] = {};
	for (UINT i = 0; i < 4; ++i)
	{
		p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		p[i].Descriptor.ShaderRegister = i;
	}
	p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	p[4].Descriptor.ShaderRegister = 8;
	p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	p[5].Descriptor.ShaderRegister = 9;
	p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[6].DescriptorTable.NumDescriptorRanges = 1;
	p[6].DescriptorTable.pDescriptorRanges = &srv;
	p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[7].DescriptorTable.NumDescriptorRanges = 1;
	p[7].DescriptorTable.pDescriptorRanges = &uav;
	D3D12_STATIC_SAMPLER_DESC samplers[2] = {clampSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0),
	                                         clampSampler(D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, 1)};
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	for (auto& s : samplers) { s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; }
	D3D12_ROOT_SIGNATURE_DESC cd = {};
	cd.NumParameters = 8;
	cd.pParameters = p;
	cd.NumStaticSamplers = 2;
	cd.pStaticSamplers = samplers;
	if (!serializeRootSig(cd, m_atmoComputeRS)) { return false; }

	D3D12_ROOT_PARAMETER g[3] = {};
	g[0] = p[0];
	g[1] = p[1];
	g[2] = p[6];
	for (auto& gp : g) { gp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; }
	D3D12_ROOT_SIGNATURE_DESC gd = {};
	gd.NumParameters = 3;
	gd.pParameters = g;
	gd.NumStaticSamplers = 2;
	gd.pStaticSamplers = samplers;
	return serializeRootSig(gd, m_atmoGraphicsRS);
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE atmoCpu(UINT set, UINT slot) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_atmoHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(set * kAtmoSetSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE atmoGpu(UINT set, UINT slot) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE h = m_atmoHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<UINT64>(set * kAtmoSetSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

/// @brief SRV を組の枠へ書く。dim は TEXTURE2D / TEXTURE3D / TEXTURE2DMS。res が null なら null 記述子
void writeAtmoSrv(UINT set, UINT slot, ID3D12Resource* res, DXGI_FORMAT format, D3D12_SRV_DIMENSION dim)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = format;
	sd.ViewDimension = dim;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (dim == D3D12_SRV_DIMENSION_TEXTURE2D) { sd.Texture2D.MipLevels = 1; }
	if (dim == D3D12_SRV_DIMENSION_TEXTURE3D) { sd.Texture3D.MipLevels = 1; }
	m_d3dDevice->CreateShaderResourceView(res, &sd, atmoCpu(set, slot));
}

void writeAtmoUav(UINT set, ID3D12Resource* res, D3D12_UAV_DIMENSION dim, UINT depth)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	ud.ViewDimension = dim;
	if (dim == D3D12_UAV_DIMENSION_TEXTURE3D) { ud.Texture3D.WSize = depth; }
	m_d3dDevice->CreateUnorderedAccessView(res, nullptr, &ud, atmoCpu(set, kAtmoSrvPerSet));
}

/// @brief 組を null 記述子で埋める。シェーダーが宣言する型と次元に合わせる (t0〜t2 は 2D、t3/t4 は影、t5 は 3D)
void clearAtmosphereSet(UINT set)
{
	constexpr auto k2D = D3D12_SRV_DIMENSION_TEXTURE2D;
	constexpr auto kRgba = DXGI_FORMAT_R16G16B16A16_FLOAT;
	for (UINT slot = 0; slot < 3; ++slot) { writeAtmoSrv(set, slot, nullptr, kRgba, k2D); }
	writeAtmoSrv(set, 3, nullptr, DXGI_FORMAT_R32_FLOAT, k2D);
	writeAtmoSrv(set, 4, nullptr, DXGI_FORMAT_R32_FLOAT, k2D);
	writeAtmoSrv(set, 5, nullptr, kRgba, D3D12_SRV_DIMENSION_TEXTURE3D);
	writeAtmoUav(set, nullptr, D3D12_UAV_DIMENSION_TEXTURE2D, 0);
}

[[nodiscard]] bool createAtmoTexture(UINT w, UINT h, UINT d, const wchar_t* name, gfx::GpuResource& out)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = (d > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = static_cast<UINT16>(d);
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc,
	                                  D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, out)) || !out)
	{
		return false;
	}
	out->SetName(name);
	return true;
}

[[nodiscard]] static std::string atmoShaderSource(const char* body)
{
	return std::string(DX12_ATMOSPHERE_COMMON_HLSL) + DX12_ATMOSPHERE_MARCH_HLSL + DX12_ATMOSPHERE_TOON_HLSL + body;
}

[[nodiscard]] bool createAtmoComputePso(const std::string& src, ComPtr<ID3D12PipelineState>& out)
{
	ComPtr<ID3DBlob> cs, err;
	if (FAILED(gfx::compileDx12Shader(src, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, cs.GetAddressOf(),
	                                  err.GetAddressOf())))
	{
		if (err) { std::fprintf(stderr, "[mitiru] atmosphere compile: %s\n", static_cast<const char*>(err->GetBufferPointer())); }
		return false;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_atmoComputeRS.Get();
	pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	return SUCCEEDED(m_d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
}

/// @brief 空の LUT と空気遠近の資源・パイプラインを作る (空を最初に有効にしたフレームで 1 回)
[[nodiscard]] bool ensureAtmosphere()
{
	if (m_skyDrawPSO) { return true; }
	if (!ensureAtmosphereCommon() || m_atmoTransmittancePSO) { return false; }
	const bool ok =
		createAtmoTexture(256, 64, 1, L"Renderer3D sky transmittance LUT", m_atmoTransmittance) &&
		createAtmoTexture(32, 32, 1, L"Renderer3D sky multi-scattering LUT", m_atmoMultiScatter) &&
		createAtmoTexture(192, 108, 1, L"Renderer3D sky-view LUT", m_atmoSkyView) &&
		createAtmoTexture(kAtmoApSlices, kAtmoApSlices, kAtmoApSlices, L"Renderer3D aerial perspective", m_atmoAerial) &&
		createAtmoComputePso(atmoShaderSource(DX12_ATMOSPHERE_TRANSMITTANCE_CS), m_atmoTransmittancePSO) &&
		createAtmoComputePso(atmoShaderSource(DX12_ATMOSPHERE_MULTISCATTER_CS), m_atmoMultiScatterPSO) &&
		createAtmoComputePso(atmoShaderSource(DX12_ATMOSPHERE_SKYVIEW_CS), m_atmoSkyViewPSO) &&
		createAtmoComputePso(atmoShaderSource(DX12_ATMOSPHERE_AERIAL_CS), m_atmoAerialPSO) && createSkyDrawPso();
	if (!ok)
	{
		m_skyDrawPSO.Reset();
		debug::warnOnce("dx12.atmosphere.pipeline", "空のシェーダーか LUT を作れない — 空と空気遠近は描かない");
		return false;
	}
	writeAtmosphereViews();
	m_atmoLutDirty = true;
	return true;
}

[[nodiscard]] bool createSkyDrawPso()
{
	const std::string src = atmoShaderSource(DX12_SKY_DRAW_HLSL);
	const auto vs = gfx::Dx12Shader::createVertexShader(src, "VSMain");
	const auto ps = gfx::Dx12Shader::createPixelShader(src, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_atmoGraphicsRS.Get();
	pd.VS = vs.shaderBytecode();
	pd.PS = ps.shaderBytecode();
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	// 何も描かれていない所 (深度 1 のまま) だけに描く
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	return SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_skyDrawPSO.ReleaseAndGetAddressOf())));
}

void writeAtmosphereViews()
{
	constexpr auto k2D = D3D12_SRV_DIMENSION_TEXTURE2D;
	constexpr auto kRgba = DXGI_FORMAT_R16G16B16A16_FLOAT;
	constexpr auto kU2D = D3D12_UAV_DIMENSION_TEXTURE2D;
	writeAtmoUav(kAtmoSetTransmittance, m_atmoTransmittance.Get(), kU2D, 0);
	writeAtmoSrv(kAtmoSetMultiScatter, 0, m_atmoTransmittance.Get(), kRgba, k2D);
	writeAtmoUav(kAtmoSetMultiScatter, m_atmoMultiScatter.Get(), kU2D, 0);
	for (const UINT set : {kAtmoSetSkyView, kAtmoSetAerial, kAtmoSetSkyDraw, kAtmoSetFogInject, kAtmoSetFogInject + 1})
	{
		writeAtmoSrv(set, 0, m_atmoTransmittance.Get(), kRgba, k2D);
		writeAtmoSrv(set, 1, m_atmoMultiScatter.Get(), kRgba, k2D);
	}
	writeAtmoUav(kAtmoSetSkyView, m_atmoSkyView.Get(), kU2D, 0);
	writeAtmoUav(kAtmoSetAerial, m_atmoAerial.Get(), D3D12_UAV_DIMENSION_TEXTURE3D, kAtmoApSlices);
	writeAtmoSrv(kAtmoSetSkyDraw, 2, m_atmoSkyView.Get(), kRgba, k2D);
	writeCompositeViews();
}

/// @brief このフレームの CbAtmosphere を ring に置く。空もフォグも読む
void uploadAtmosphereFrameCB()
{
	struct alignas(256) CbAtmosphere
	{
		float invViewProj[4][4];
		float cameraKm[4];
		float sunDir[4];
		float rayleigh[4];
		float mie[4];
		float ozone[4];
		float ground[4];
		float radii[4];
		float ap[4];
		float cameraWorld[4];
		float screen[4];
	};
	const AtmosphereParams& a = m_sky.atmosphere;
	const sgc::Vec3f sun = (m_light.direction * -1.0f).normalized();
	const float alt = atmosphere::cameraAltitudeKm(m_sky, m_cameraPosition.y);
	CbAtmosphere cb{};
	toColumnMajor(cb.invViewProj, glm::inverse(m_viewProjNoJitter));
	const float km[4] = {0.0f, a.bottomRadiusKm + alt, 0.0f, m_sky.metersPerUnit * 0.001f};
	const float sd[4] = {sun.x, sun.y, sun.z, m_sky.sunIlluminance};
	const float ry[4] = {a.rayleighScattering.x, a.rayleighScattering.y, a.rayleighScattering.z, a.rayleighScaleHeightKm};
	const float mi[4] = {a.mieScattering, a.mieExtinction, a.mieScaleHeightKm, a.mieG};
	const float oz[4] = {a.ozoneAbsorption.x, a.ozoneAbsorption.y, a.ozoneAbsorption.z, a.ozoneCenterKm};
	const float gr[4] = {a.groundAlbedo.x, a.groundAlbedo.y, a.groundAlbedo.z, a.ozoneHalfWidthKm};
	const float rd[4] = {a.bottomRadiusKm, a.topRadiusKm, m_sky.sunDiskAngularRadius, static_cast<float>(m_sky.toonBands)};
	const bool apOn = m_sky.enabled && m_sky.aerialPerspectiveScale > 0.0f;
	const float ap[4] = {32.0f, m_sky.aerialPerspectiveScale, 0.0f, apOn ? 1.0f : 0.0f};
	const float cw[4] = {m_cameraPosition.x, m_cameraPosition.y, m_cameraPosition.z, m_sky.groundHeight};
	const float sc[4] = {m_config.viewportWidth, m_config.viewportHeight, 1.0f / m_config.viewportWidth,
	                     1.0f / m_config.viewportHeight};
	std::memcpy(cb.cameraKm, km, sizeof(km));
	std::memcpy(cb.sunDir, sd, sizeof(sd));
	std::memcpy(cb.rayleigh, ry, sizeof(ry));
	std::memcpy(cb.mie, mi, sizeof(mi));
	std::memcpy(cb.ozone, oz, sizeof(oz));
	std::memcpy(cb.ground, gr, sizeof(gr));
	std::memcpy(cb.radii, rd, sizeof(rd));
	std::memcpy(cb.ap, ap, sizeof(ap));
	std::memcpy(cb.cameraWorld, cw, sizeof(cw));
	std::memcpy(cb.screen, sc, sizeof(sc));
	const auto alloc = m_uploadRing.upload(&cb, sizeof(cb), 256);
	m_atmoFrameCB = alloc.valid() ? alloc.gpuAddr : 0;
}

/// @brief res が無ければ何もしない (副ビューは遠距離の影のアトラスをカスケードを使う時だけ作る)
void atmoBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	if (res != nullptr) { temporalBarrier(res, before, after); }
}

/// @brief 1 本の compute を流す。書く資源は ALL_SHADER_RESOURCE ↔ UNORDERED_ACCESS を往復する
void dispatchAtmo(ID3D12PipelineState* pso, UINT set, ID3D12Resource* target, UINT gx, UINT gy, UINT gz)
{
	auto* cl = m_graphicsCmdList.Get();
	atmoBarrier(target, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cl->SetPipelineState(pso);
	cl->SetComputeRootDescriptorTable(6, atmoGpu(set, 0));
	cl->SetComputeRootDescriptorTable(7, atmoGpu(set, kAtmoSrvPerSet));
	cl->Dispatch(gx, gy, gz);
	atmoBarrier(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

/// @brief compute の root 引数のうち、全パスで共通の CB と光のバッファを張る
void bindAtmosphereCompute()
{
	auto* cl = m_graphicsCmdList.Get();
	ID3D12DescriptorHeap* heaps[] = {m_atmoHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetComputeRootSignature(m_atmoComputeRS.Get());
	cl->SetComputeRootConstantBufferView(0, m_atmoFrameCB);
}

/// @brief LUT (組成が変わった時だけ) → 空の見え方 → 空気遠近の froxel
void recordAtmosphereLuts()
{
	if (m_atmoLutDirty || std::memcmp(&m_atmoLutParams, &m_sky.atmosphere, sizeof(AtmosphereParams)) != 0)
	{
		dispatchAtmo(m_atmoTransmittancePSO.Get(), kAtmoSetTransmittance, m_atmoTransmittance.Get(), 32, 8, 1);
		dispatchAtmo(m_atmoMultiScatterPSO.Get(), kAtmoSetMultiScatter, m_atmoMultiScatter.Get(), 4, 4, 1);
		m_atmoLutParams = m_sky.atmosphere;
		m_atmoLutDirty = false;
	}
	dispatchAtmo(m_atmoSkyViewPSO.Get(), kAtmoSetSkyView, m_atmoSkyView.Get(), 24, 14, 1);
	dispatchAtmo(m_atmoAerialPSO.Get(), kAtmoSetAerial, m_atmoAerial.Get(), kAtmoApSlices / 4, kAtmoApSlices / 4,
	             kAtmoApSlices / 4);
}

/// @brief 主パスの何も無い所へ空を描く (MSAA の色と深度、深度は読むだけ)
void drawSkyIntoScene()
{
	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	setFullViewport();
	cl->SetGraphicsRootSignature(m_atmoGraphicsRS.Get());
	cl->SetPipelineState(m_skyDrawPSO.Get());
	cl->SetGraphicsRootConstantBufferView(0, m_atmoFrameCB);
	cl->SetGraphicsRootConstantBufferView(1, m_atmoFrameCB);
	cl->SetGraphicsRootDescriptorTable(2, atmoGpu(kAtmoSetSkyDraw, 0));
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}

/// @brief endFrame で不透明 (clod を含む) の後、半透明の前に呼ぶ。空・空気遠近・フォグの compute と空の描画
void drawAtmospherePasses()
{
	m_atmoFrameCB = 0;
	const bool sky = m_sky.enabled && ensureAtmosphere() && ensureAerialComposite();
	const bool fog = m_fog.enabled && ensureVolumetricFog();
	if (!sky && !fog) { return; }
	uploadAtmosphereFrameCB();
	if (m_atmoFrameCB == 0) { return; }
	timePostPass(PostGpuPass::Sky, [&] {
		if (!sky) { return; }
		bindAtmosphereCompute();
		recordAtmosphereLuts();
		drawSkyIntoScene();
	});
	timePostPass(PostGpuPass::VolumetricFog, [&] {
		if (fog) { recordVolumetricFog(); }
	});
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}
