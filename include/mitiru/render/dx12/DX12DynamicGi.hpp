// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。動く光の GI (DDGI、ADR 0074)
//
// 焼いた光と同じ並びの格子 (SH L2 の float4 x 7 と距離の地図) を GPU に持ち、フレームの頭で前のフレームに描いた物から
// レイの場面 (DX12DynamicGiScene.hpp) を作って、TraceCS と UpdateCS (tools/ddgi_shaders/ddgi.hlsl) で更新する。
// 前方の描画と clod は writeIndirectSrvs がこの格子を指すので、焼いた光と同じ HLSL で引く。格子の形と最初の値は
// 読み込んだ焼いた光から取る。DXR 1.1 の無い GPU と設定画面の上限が Off の間は何もせず、焼いた光のまま描く。
// 描画だけが読み、GameMemory には触れない。

public:

/// @brief 履歴の混ぜ方と、面の中に埋まったプローブの見分け方 (既定は gi::DynamicGiTuning)
void setDynamicGiTuning(const gi::DynamicGiTuning& t) noexcept { m_ddgiTuning = t; }
[[nodiscard]] const gi::DynamicGiTuning& dynamicGiTuning() const noexcept { return m_ddgiTuning; }

/// @brief レイが何にも当たらなかった向きの空の放射輝度 (線形)。外すと半球の環境光 (setHemisphereAmbient か環境光の色) を使う
void setDynamicGiSky(const gi::BakeSky& sky) noexcept
{
	m_ddgiSky = sky;
	m_ddgiSkySet = true;
}
void clearDynamicGiSky() noexcept { m_ddgiSkySet = false; }

/// @brief この GPU で DDGI を使えるか (DXR 1.1、SM 6.6)。初めて呼んだ時に調べ、使えなければ詳しく出す実行で 1 度だけ知らせる
[[nodiscard]] bool dynamicGiSupported() { return ensureDdgiPipeline(); }
/// @brief このフレームの描画が DDGI の格子を引いているか
[[nodiscard]] bool dynamicGiActive() const noexcept { return m_ddgiVolume.ready; }
[[nodiscard]] std::uint32_t dynamicGiProbeCount() const noexcept { return m_ddgiVolume.ready ? m_ddgiVolume.probeCount : 0; }
[[nodiscard]] std::uint32_t dynamicGiRaysPerProbe() const noexcept { return m_ddgiVolume.ready ? m_ddgiVolume.rays : 0; }
/// @brief 直前の更新で TLAS に積んだ物の数
[[nodiscard]] std::uint32_t dynamicGiInstanceCount() const noexcept { return static_cast<std::uint32_t>(m_ddgiInstanceDescs.size()); }

/// @brief DXR の無い GPU と同じ扱いにする (焼いた光へ戻る経路の試験用)
void simulateNoRaytracingForTest() noexcept
{
	m_ddgiCaps = DdgiCaps::Unsupported;
	releaseDdgiVolume();
}

private:

enum class DdgiCaps : std::uint8_t { Unknown, Supported, Unsupported };

/// 格子 1 つぶんの GPU の資源
struct DdgiVolumeGpu
{
	const gi::ProbeVolume* source = nullptr;   ///< 種にした焼いた格子 (変わったら作り直す)
	gi::ProbeGridDesc grid;
	std::uint32_t probeCount = 0;
	std::uint32_t tiles = 1;
	std::uint32_t atlasWidth = 1;
	std::uint32_t atlasHeight = 1;
	std::uint32_t rays = 0;
	std::uint32_t density = 1;
	dx12::StagedBuffer probes;        ///< float4 x 7 / プローブ。UAV にもなる
	dx12::StagedBuffer state;         ///< float4 / プローブ。x = 裏面に当たったレイの割合の履歴
	PendingTexture visibility;        ///< R32G32_FLOAT の距離の地図。UAV にもなる
	gfx::GpuResource rayBuffer;       ///< float4 / レイ
	std::uint32_t visSrv = 0;
	std::uint32_t visUav = 0;
	bool uploaded = false;
	bool history = false;             ///< 放射照度に前の値がある
	bool visHistory = false;          ///< 距離の地図に前の値がある
	bool ready = false;               ///< このフレームの描画が引く
};

/// ddgi.hlsl の CbDdgi と同じ並び (448 byte)
struct DdgiConstants
{
	float giOrigin[4];
	float giSpacing[4];
	std::uint32_t giDims[4];
	float giParams[4];
	float unusedRefl[16];
	float unusedSsr[16];
	float unusedSsrMatrix[16];
	float cameraPos[4];
	float rot[3][4];
	std::uint32_t rayParams[4];
	float sunDir[4];
	float sunColor[4];
	float skyZenith[4];
	float skyHorizon[4];
	float skyGround[4];
	float blend[4];
	float misc[4];
};
static_assert(sizeof(DdgiConstants) == 448);

/// ddgi.hlsl の DdgiLight と同じ並び (48 byte)
struct DdgiLightGpu
{
	float posRange[4];
	float colorScale[4];
	float dirOffset[4];
};
static_assert(sizeof(DdgiLightGpu) == 48);

static constexpr std::uint32_t kDdgiFlagSun = 1u;
static constexpr std::uint32_t kDdgiFlagHistory = 2u;
static constexpr std::uint32_t kDdgiFlagVisHistory = 4u;
static constexpr std::uint32_t kDdgiMaxProbes = 65535;          ///< UpdateCS は 1 プローブ 1 group
static constexpr std::uint32_t kDdgiMaxRaysPerFrame = 1u << 22;  ///< TraceCS の group の数の上限 (65535 x 64)

DdgiCaps m_ddgiCaps = DdgiCaps::Unknown;
ComPtr<ID3D12Device5> m_ddgiDevice;
ComPtr<ID3D12GraphicsCommandList4> m_ddgiList;
ComPtr<ID3D12RootSignature> m_ddgiRS;
ComPtr<ID3D12PipelineState> m_ddgiTracePso;
ComPtr<ID3D12PipelineState> m_ddgiUpdatePso;
gi::DynamicGiTuning m_ddgiTuning;
gi::BakeSky m_ddgiSky;
bool m_ddgiSkySet = false;
std::shared_ptr<const gi::ProbeVolume> m_ddgiBakeVolume;   ///< 読み込んだ焼いた格子 (DDGI の格子の形と種)
std::vector<LocalLight> m_ddgiLights;                      ///< 前のフレームの局所光 (beginFrame で m_localLights と入れ替える)
std::vector<DdgiLightGpu> m_ddgiLightScratch;
DdgiVolumeGpu m_ddgiVolume;

// NOLINTNEXTLINE(google-build-namespaces) intentional in-class .inl include
#include <mitiru/render/dx12/DX12DynamicGiScene.hpp> // NOLINT(build/include)

/// @brief ゲームか host が DDGI を頼んでいて、使える条件がそろっているか (使えるかの GPU の確認は ensureDdgiPipeline)
[[nodiscard]] bool dynamicGiWanted() const noexcept
{
	return m_giSettings.enabled && m_giSettings.dynamic && m_qualityCaps.dynamicGi != DynamicGiQuality::Off &&
	       m_ddgiBakeVolume != nullptr && m_ddgiCaps != DdgiCaps::Unsupported;
}

/// @brief DDGI に要る GPU の機能のうち、足りないもの。そろっていれば nullptr
[[nodiscard]] const char* dynamicGiUnsupportedReason()
{
	if (FAILED(m_d3dDevice->QueryInterface(IID_PPV_ARGS(m_ddgiDevice.ReleaseAndGetAddressOf()))) ||
	    FAILED(m_graphicsCmdList.As(&m_ddgiList)))
	{
		return "DXR の入口 (ID3D12Device5) がありません";
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
	if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5))) ||
	    o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
	{
		return "レイトレーシング (DXR 1.1) に対応していません";
	}
	D3D12_FEATURE_DATA_SHADER_MODEL sm = {D3D_SHADER_MODEL_6_6};
	if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) || sm.HighestShaderModel < D3D_SHADER_MODEL_6_6)
	{
		return "シェーダーモデル 6.6 に対応していません";
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
	if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))) ||
	    o.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3 || !o.TypedUAVLoadAdditionalFormats)
	{
		return "記述子の heap を直接引く機能か、R32G32 の UAV の読み込みに対応していません";
	}
	return nullptr;
}

[[nodiscard]] bool createDdgiRootSignature()
{
	D3D12_DESCRIPTOR_RANGE srv = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 40, 0, 0};
	D3D12_DESCRIPTOR_RANGE uav = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, 0};
	D3D12_ROOT_PARAMETER p[10] = {};
	const auto root = [&p](int i, D3D12_ROOT_PARAMETER_TYPE type, UINT reg) {
		p[i].ParameterType = type;
		p[i].Descriptor.ShaderRegister = reg;
	};
	root(0, D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
	root(1, D3D12_ROOT_PARAMETER_TYPE_SRV, 0);    // TLAS
	root(2, D3D12_ROOT_PARAMETER_TYPE_SRV, 1);    // InstanceInfo
	root(3, D3D12_ROOT_PARAMETER_TYPE_SRV, 2);    // DdgiLight
	root(4, D3D12_ROOT_PARAMETER_TYPE_SRV, 39);   // 前のフレームの格子 (g_giProbes)
	p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[5].DescriptorTable = {1, &srv};             // g_giVisibility
	root(6, D3D12_ROOT_PARAMETER_TYPE_UAV, 0);    // レイ
	root(7, D3D12_ROOT_PARAMETER_TYPE_UAV, 1);    // 格子へ書く
	p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[8].DescriptorTable = {1, &uav};             // 距離の地図へ書く
	root(9, D3D12_ROOT_PARAMETER_TYPE_UAV, 3);    // 裏面の割合の履歴
	D3D12_STATIC_SAMPLER_DESC s = {};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.MaxLOD = D3D12_FLOAT32_MAX;
	s.ShaderRegister = 1;
	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 10;
	rsd.pParameters = p;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers = &s;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
	ComPtr<ID3DBlob> sig, err;
	return SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) &&
	       SUCCEEDED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	                                                  IID_PPV_ARGS(m_ddgiRS.ReleaseAndGetAddressOf())));
}

[[nodiscard]] bool createDdgiPipelines()
{
	const auto pso = [this](const std::uint8_t* blob, std::size_t size, ComPtr<ID3D12PipelineState>& out) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
		d.pRootSignature = m_ddgiRS.Get();
		d.CS = {blob, size};
		return SUCCEEDED(m_d3dDevice->CreateComputePipelineState(&d, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
	};
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kDdgiHeapSlots;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!createDdgiRootSignature() || !pso(ddgi::kDdgiTrace, ddgi::kDdgiTraceSize, m_ddgiTracePso) ||
	    !pso(ddgi::kDdgiUpdate, ddgi::kDdgiUpdateSize, m_ddgiUpdatePso) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_ddgiHeap.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	// 0 番は「持っていない」の印にするので配らない
	m_ddgiFreeSlots.clear();
	for (std::uint32_t i = kDdgiHeapSlots - 1; i >= 1; --i) { m_ddgiFreeSlots.push_back(i); }
	return true;
}

/// @brief 初めて頼まれた時に GPU を調べて PSO を作る。使えなければ焼いた光のままにして 1 度だけ知らせる
[[nodiscard]] bool ensureDdgiPipeline()
{
	if (m_ddgiCaps != DdgiCaps::Unknown) { return m_ddgiCaps == DdgiCaps::Supported; }
	const char* why = dynamicGiUnsupportedReason();
	if (why == nullptr && !createDdgiPipelines()) { why = "DDGI のシェーダーを作れませんでした"; }
	if (why != nullptr)
	{
		m_ddgiCaps = DdgiCaps::Unsupported;
		debug::verboseOnce("dx12.ddgi.unsupported", std::string("この GPU は") + why +
		                   "。動く光の GI は使わず、焼いた光のまま描きます。");
		return false;
	}
	m_ddgiCaps = DdgiCaps::Supported;
	return true;
}

/// @brief UAV にもなる DEFAULT のバッファと、中身を詰めた転送元
[[nodiscard]] bool stageDdgiBuffer(const void* data, std::size_t bytes, dx12::StagedBuffer& out)
{
	out = {};
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COMMON, out.buffer,
	                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, out.staging)))
	{
		return false;
	}
	void* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(out.staging->Map(0, &noRead, &mapped))) { return false; }
	std::memcpy(mapped, data, bytes);
	out.staging->Unmap(0, nullptr);
	out.bytes = bytes;
	return true;
}

/// @brief 距離の地図の SRV (前のフレームを読む) と UAV (書く) を m_ddgiHeap に置く
[[nodiscard]] bool writeDdgiVisibilityViews(DdgiVolumeGpu& v)
{
	if (!takeDdgiSlot(v.visSrv) || !takeDdgiSlot(v.visUav)) { return false; }
	D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
	s.Format = DXGI_FORMAT_R32G32_FLOAT;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(v.visibility.texture.Get(), &s, ddgiCpuSlot(v.visSrv));
	D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
	u.Format = DXGI_FORMAT_R32G32_FLOAT;
	u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateUnorderedAccessView(v.visibility.texture.Get(), nullptr, &u, ddgiCpuSlot(v.visUav));
	return true;
}

/// @brief 種の格子から GPU の資源を作る。中身の転送は uploadDdgiVolume
[[nodiscard]] bool createDdgiVolume(const gi::DynamicGiSeed& seed, std::uint32_t rays, DdgiVolumeGpu& v)
{
	const gi::ProbeVolume& vol = seed.volume;
	v.grid = vol.grid;
	v.probeCount = vol.grid.count();
	v.tiles = vol.tilesPerRow;
	v.atlasWidth = vol.atlasWidth();
	v.atlasHeight = vol.atlasHeight();
	v.rays = rays;
	if (v.probeCount > kDdgiMaxProbes || std::uint64_t{v.probeCount} * rays > kDdgiMaxRaysPerFrame) { return false; }
	const std::vector<float> packed = packProbeVolume(vol);
	std::vector<float> state(static_cast<std::size_t>(v.probeCount) * 4, 0.0f);
	for (std::uint32_t i = 0; i < v.probeCount; ++i) { state[static_cast<std::size_t>(i) * 4] = vol.valid[i] != 0 ? 0.0f : 1.0f; }
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = v.atlasWidth;
	d.Height = v.atlasHeight;
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = DXGI_FORMAT_R32G32_FLOAT;
	d.SampleDesc.Count = 1;
	d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return stageDdgiBuffer(packed.data(), packed.size() * sizeof(float), v.probes) &&
	       stageDdgiBuffer(state.data(), state.size() * sizeof(float), v.state) &&
	       stageTexture(d, reinterpret_cast<const std::uint8_t*>(vol.visibility.data()), vol.visibility.size() * sizeof(float),
	                    v.visibility) &&
	       SUCCEEDED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, std::uint64_t{v.probeCount} * rays * 16,
	                                      D3D12_RESOURCE_STATE_COMMON, v.rayBuffer,
	                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) &&
	       writeDdgiVisibilityViews(v);
}

/// @brief 種の値を GPU へ写す (作った最初のフレームだけ)
void uploadDdgiVolume(DdgiVolumeGpu& v)
{
	if (v.uploaded) { return; }
	auto* cl = m_graphicsCmdList.Get();
	dx12::recordStagedCopies(cl, v.probes);
	dx12::detail::recordTransition(cl, v.probes.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	dx12::recordStagedCopies(cl, v.state);
	dx12::detail::recordTransition(cl, v.state.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	recordPendingCopies(v.visibility);
	for (gfx::GpuResource* r : {&v.probes.staging, &v.state.staging, &v.visibility.staging})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	v.uploaded = true;
}

void releaseDdgiVolume()
{
	DdgiVolumeGpu& v = m_ddgiVolume;
	for (gfx::GpuResource* r : {&v.probes.buffer, &v.probes.staging, &v.state.buffer, &v.state.staging, &v.visibility.texture,
	                            &v.visibility.staging, &v.rayBuffer})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	if (v.visSrv != 0) { retireDdgiSlot(v.visSrv); }
	if (v.visUav != 0) { retireDdgiSlot(v.visUav); }
	m_ddgiVolume = DdgiVolumeGpu{};
}

/// @brief 焼いた格子と段に合う格子を用意する。焼いた光か段が変わったら作り直し、焼いた値から始める
[[nodiscard]] bool ensureDdgiVolume(const gi::DynamicGiPreset& preset)
{
	DdgiVolumeGpu& v = m_ddgiVolume;
	if (v.source == m_ddgiBakeVolume.get() && v.rays == preset.raysPerProbe && v.density == preset.density && v.probes.buffer)
	{
		return true;
	}
	releaseDdgiVolume();
	const gi::DynamicGiSeed seed = gi::seedDynamicGi(*m_ddgiBakeVolume, preset.density);
	if (!createDdgiVolume(seed, preset.raysPerProbe, v))
	{
		debug::verboseOnce("dx12.ddgi.volume", "動く光の GI の格子を GPU に置けなかったので (プローブとレイが多すぎるか、メモリが足りない)、"
		                   "焼いた光のまま描きます。焼く時の格子を粗くするか、設定画面の段を下げてください。");
		releaseDdgiVolume();
		return false;
	}
	v.source = m_ddgiBakeVolume.get();
	v.density = preset.density;
	v.history = true;
	v.visHistory = seed.hasVisibility;
	return true;
}

[[nodiscard]] static DdgiLightGpu packDdgiLight(const LocalLight& l) noexcept
{
	const LocalLightGpu g = packLocalLight(l, ClusterView{});
	DdgiLightGpu out{};
	for (int i = 0; i < 3; ++i)
	{
		out.posRange[i] = g.positionWS[i];
		out.colorScale[i] = g.color[i];
		out.dirOffset[i] = g.directionWS[i];
	}
	out.posRange[3] = g.range;
	out.colorScale[3] = g.spotScale;
	out.dirOffset[3] = g.spotOffset;
	return out;
}

/// @brief 当たった面を照らす局所光 (前のフレームの光と、setLights の点光源・スポット)
void collectDdgiLights()
{
	appendLegacyLights(m_ddgiLights);
	m_ddgiLightScratch.clear();
	for (const LocalLight& l : m_ddgiLights)
	{
		if (m_ddgiLightScratch.size() >= m_ddgiTuning.maxLocalLights) { break; }
		if (l.intensity > 0.0f && l.range > 0.0f) { m_ddgiLightScratch.push_back(packDdgiLight(l)); }
	}
}

void fillDdgiSky(DdgiConstants& c) const
{
	gi::BakeSky sky = m_ddgiSky;
	if (!m_ddgiSkySet)
	{
		const sgc::Colorf up = hemisphereAmbientSky();
		const sgc::Colorf down = hemisphereAmbientGround();
		sky.zenith = sky.horizon = {up.r, up.g, up.b};
		sky.ground = {down.r, down.g, down.b};
	}
	const gi::Vec3 v[3] = {sky.zenith, sky.horizon, sky.ground};
	float* out[3] = {c.skyZenith, c.skyHorizon, c.skyGround};
	for (int i = 0; i < 3; ++i)
	{
		out[i][0] = v[i].x;
		out[i][1] = v[i].y;
		out[i][2] = v[i].z;
	}
}

[[nodiscard]] DdgiConstants makeDdgiConstants() const
{
	const DdgiVolumeGpu& v = m_ddgiVolume;
	const gi::DynamicGiTuning& t = m_ddgiTuning;
	DdgiConstants c{};
	const float origin[3] = {v.grid.origin.x, v.grid.origin.y, v.grid.origin.z};
	const float spacing[3] = {v.grid.spacing.x, v.grid.spacing.y, v.grid.spacing.z};
	for (int a = 0; a < 3; ++a)
	{
		c.giOrigin[a] = origin[a];
		c.giSpacing[a] = spacing[a];
		c.giDims[a] = v.grid.dims[a];
	}
	c.giOrigin[3] = v.history ? 1.0f : 0.0f;
	c.giSpacing[3] = 1.0f;
	c.giDims[3] = v.tiles;
	c.giParams[0] = m_giSettings.normalBias > 0.0f ? m_giSettings.normalBias : gi::defaultProbeBias(v.grid);
	c.giParams[2] = 1.0f / static_cast<float>(v.atlasWidth);
	c.giParams[3] = 1.0f / static_cast<float>(v.atlasHeight);
	const auto rot = gi::frameRayRotation(m_frameCounter);
	for (int r = 0; r < 3; ++r)
	{
		for (int k = 0; k < 3; ++k) { c.rot[r][k] = rot[static_cast<std::size_t>(r * 3 + k)]; }
	}
	const bool sun = m_light.type == LightType::Directional && m_light.intensity > 0.0f;
	c.rayParams[0] = v.rays;
	c.rayParams[1] = v.probeCount;
	c.rayParams[2] = static_cast<std::uint32_t>(m_ddgiLightScratch.size());
	c.rayParams[3] = (sun ? kDdgiFlagSun : 0u) | (v.history ? kDdgiFlagHistory : 0u) | (v.visHistory ? kDdgiFlagVisHistory : 0u);
	const sgc::Vec3f dir = m_light.direction.normalized();
	c.sunDir[0] = dir.x;
	c.sunDir[1] = dir.y;
	c.sunDir[2] = dir.z;
	c.sunColor[0] = m_light.color.r * m_light.intensity;
	c.sunColor[1] = m_light.color.g * m_light.intensity;
	c.sunColor[2] = m_light.color.b * m_light.intensity;
	fillDdgiSky(c);
	c.blend[0] = t.hysteresis;
	c.blend[1] = t.fastHysteresis;
	c.blend[2] = t.changeThreshold;
	c.blend[3] = gi::visibilityMaxDistance(v.grid);
	c.misc[0] = t.backfaceInvalid;
	c.misc[1] = t.visibilitySharpness;
	c.misc[2] = t.backfaceHistory;
	c.misc[3] = t.lightShadowMargin;
	return c;
}

/// @brief レイを飛ばして格子を更新する。格子と距離の地図は前後で描画が読む状態に戻す
void dispatchDynamicGi()
{
	DdgiVolumeGpu& v = m_ddgiVolume;
	auto* cl = m_graphicsCmdList.Get();
	const DdgiConstants constants = makeDdgiConstants();
	const auto cb = m_uploadRing.upload(&constants, sizeof(constants), 256);
	const DdgiInstanceGpu dummyInfo{};
	const DdgiLightGpu dummyLight{};
	const auto infos = m_ddgiInstanceInfos.empty() ? m_uploadRing.upload(&dummyInfo, sizeof(dummyInfo), 256)
	                                               : m_uploadRing.upload(m_ddgiInstanceInfos.data(),
	                                                                     m_ddgiInstanceInfos.size() * sizeof(DdgiInstanceGpu), 256);
	const auto lights = m_ddgiLightScratch.empty() ? m_uploadRing.upload(&dummyLight, sizeof(dummyLight), 256)
	                                               : m_uploadRing.upload(m_ddgiLightScratch.data(),
	                                                                     m_ddgiLightScratch.size() * sizeof(DdgiLightGpu), 256);
	if (!cb.valid() || !infos.valid() || !lights.valid()) { return; }
	// heap を直接引く root signature は、heap を張ってから張る
	ID3D12DescriptorHeap* heaps[] = {m_ddgiHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetComputeRootSignature(m_ddgiRS.Get());
	cl->SetComputeRootConstantBufferView(0, cb.gpuAddr);
	cl->SetComputeRootShaderResourceView(1, m_ddgiTlas->GetGPUVirtualAddress());
	cl->SetComputeRootShaderResourceView(2, infos.gpuAddr);
	cl->SetComputeRootShaderResourceView(3, lights.gpuAddr);
	cl->SetComputeRootShaderResourceView(4, v.probes.buffer->GetGPUVirtualAddress());
	cl->SetComputeRootDescriptorTable(5, ddgiGpuSlot(v.visSrv));
	cl->SetComputeRootUnorderedAccessView(6, v.rayBuffer->GetGPUVirtualAddress());
	cl->SetComputeRootUnorderedAccessView(7, v.probes.buffer->GetGPUVirtualAddress());
	cl->SetComputeRootDescriptorTable(8, ddgiGpuSlot(v.visUav));
	cl->SetComputeRootUnorderedAccessView(9, v.state.buffer->GetGPUVirtualAddress());
	cl->SetPipelineState(m_ddgiTracePso.Get());
	cl->Dispatch((v.probeCount * v.rays + 63) / 64, 1, 1);
	constexpr auto kRead = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	constexpr auto kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	D3D12_RESOURCE_BARRIER b[3] = {};
	b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	b[0].UAV.pResource = v.rayBuffer.Get();
	b[1] = transitionBarrier(v.probes.buffer.Get(), kRead, kUav);
	b[2] = transitionBarrier(v.visibility.texture.Get(), kRead, kUav);
	cl->ResourceBarrier(3, b);
	cl->SetPipelineState(m_ddgiUpdatePso.Get());
	cl->Dispatch(v.probeCount, 1, 1);
	b[0] = transitionBarrier(v.probes.buffer.Get(), kUav, kRead);
	b[1] = transitionBarrier(v.visibility.texture.Get(), kUav, kRead);
	cl->ResourceBarrier(2, b);
	v.history = true;
	v.visHistory = true;
}

[[nodiscard]] static D3D12_RESOURCE_BARRIER transitionBarrier(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = r;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	return b;
}

/// @brief beginFrame で、局所光を空にする前に呼ぶ。前のフレームの光と描いた物を DDGI の側へ移す
void beginFrameDynamicGi()
{
	std::swap(m_ddgiLights, m_localLights);
	std::swap(m_ddgiCastersPrev, m_ddgiCasters);
	m_ddgiCasters.clear();
	m_ddgiRecordedPrev = m_ddgiRecording;
	m_ddgiRecording = dynamicGiWanted();
}

/// @brief beginFrame で、最初の描画より前に呼ぶ。頼まれていれば格子を更新し、このフレームの描画が引けるようにする
void updateDynamicGi()
{
	m_ddgiVolume.ready = false;
	if (!dynamicGiWanted())
	{
		if (m_ddgiVolume.probes.buffer) { releaseDdgiVolume(); }
		if (!m_ddgiMeshes.empty()) { releaseDdgiScene(); }
		return;
	}
	const gi::DynamicGiPreset preset = gi::dynamicGiPreset(m_qualityCaps.dynamicGi);
	if (!ensureDdgiPipeline() || !ensureDdgiVolume(preset)) { return; }
	MITIRU_ZONE_NAMED("Render::Dx12::DynamicGi");
	uploadDdgiVolume(m_ddgiVolume);
	// 描いた物を残し始めたばかりのフレームは場面が空なので、空だけを見た値で格子を崩さないよう更新しない
	if (m_ddgiRecordedPrev && buildDdgiScene())
	{
		collectDdgiLights();
		dispatchDynamicGi();
	}
	m_ddgiVolume.ready = true;
}

/// @brief 格子と距離の地図を読み戻し用のバッファへ写す命令を list に積む
void recordDdgiReadback(ID3D12GraphicsCommandList* list, ID3D12Resource* probes, ID3D12Resource* vis,
                        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp) const
{
	const DdgiVolumeGpu& v = m_ddgiVolume;
	constexpr auto kRead = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	constexpr auto kSrc = D3D12_RESOURCE_STATE_COPY_SOURCE;
	D3D12_RESOURCE_BARRIER b[2] = {transitionBarrier(v.probes.buffer.Get(), kRead, kSrc),
	                               transitionBarrier(v.visibility.texture.Get(), kRead, kSrc)};
	list->ResourceBarrier(2, b);
	list->CopyBufferRegion(probes, 0, v.probes.buffer.Get(), 0, std::uint64_t{v.probeCount} * gi::kProbeFloat4s * 16);
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = vis;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = fp;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = v.visibility.texture.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	b[0] = transitionBarrier(v.probes.buffer.Get(), kSrc, kRead);
	b[1] = transitionBarrier(v.visibility.texture.Get(), kSrc, kRead);
	list->ResourceBarrier(2, b);
}

/// @brief 読み戻した格子 (float4 x 7 / プローブ) と距離の地図を ProbeVolume にする
void unpackDdgiReadback(const float* probes, const std::uint8_t* vis, std::uint32_t rowPitch, gi::ProbeVolume& out) const
{
	const DdgiVolumeGpu& v = m_ddgiVolume;
	out.allocate(v.grid);
	for (std::uint32_t i = 0; i < v.probeCount; ++i)
	{
		const float* p = probes + static_cast<std::size_t>(i) * gi::kProbeFloat4s * 4;
		for (int k = 0; k < gi::kShCoefficients; ++k) { out.irradiance[i].c[k] = {p[k * 3], p[k * 3 + 1], p[k * 3 + 2]}; }
		out.valid[i] = p[27] > 0.5f ? 1 : 0;
	}
	const std::size_t rowFloats = static_cast<std::size_t>(v.atlasWidth) * 2;
	for (std::uint32_t y = 0; y < v.atlasHeight; ++y)
	{
		std::memcpy(out.visibility.data() + y * rowFloats, vis + static_cast<std::size_t>(y) * rowPitch, rowFloats * sizeof(float));
	}
}

public:

/// @brief DDGI の格子を CPU へ読み戻す。GPU の完了を待つので、フレームの外で呼ぶ (試験と診断用)
[[nodiscard]] bool readDynamicGiVolume(gi::ProbeVolume& out)
{
	const DdgiVolumeGpu& v = m_ddgiVolume;
	if (!v.ready || m_cmdListOpen || m_device == nullptr) { return false; }
	m_device->waitForGpu();
	const D3D12_RESOURCE_DESC td = v.visibility.texture->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
	UINT rows = 0;
	UINT64 rowBytes = 0, visBytes = 0;
	m_d3dDevice->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &rowBytes, &visBytes);
	const std::uint64_t probeBytes = std::uint64_t{v.probeCount} * gi::kProbeFloat4s * 16;
	ComPtr<ID3D12CommandAllocator> alloc;
	ComPtr<ID3D12GraphicsCommandList> list;
	gfx::GpuResource probeRb, visRb;
	if (FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
	    FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))) ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, probeBytes, D3D12_RESOURCE_STATE_COPY_DEST, probeRb)) ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, visBytes, D3D12_RESOURCE_STATE_COPY_DEST, visRb)))
	{
		return false;
	}
	recordDdgiReadback(list.Get(), probeRb.Get(), visRb.Get(), fp);
	list->Close();
	ID3D12CommandList* lists[] = {list.Get()};
	m_device->commandQueue()->ExecuteCommandLists(1, lists);
	m_device->waitForGpu();
	void* probes = nullptr;
	void* vis = nullptr;
	if (FAILED(probeRb->Map(0, nullptr, &probes)) || FAILED(visRb->Map(0, nullptr, &vis))) { return false; }
	unpackDdgiReadback(static_cast<const float*>(probes), static_cast<const std::uint8_t*>(vis) + fp.Offset, fp.Footprint.RowPitch, out);
	probeRb->Unmap(0, nullptr);
	visRb->Unmap(0, nullptr);
	return true;
}

private:
