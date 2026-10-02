// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// froxel の体積フォグと、空気遠近とフォグを resolve 済みの HDR に掛ける合成。froxel は内部解像度の 8 画素四方 ×
// 64 枚。注入は主光源の影 (カスケード) と froxel の光の一覧 (前方の描画と同じもの) で照らし、TAA と同じ相で
// 標本をずらして前フレームの froxel と混ぜる。資源の組と root sig は空と共有する (DX12PipelineStates_Atmosphere.inl)。

public:

/// @brief 体積フォグの指定。enabled = false (既定) なら何もしない。距離フォグ (setFog) とは別に掛かる
void setVolumetricFog(const VolumetricFogSettings& fogSettings) override
{
	if (fogSettings.enabled != m_fog.enabled || fogSettings.range != m_fog.range) { m_fogHistoryValid = false; }
	m_fog = fogSettings;
	m_fog.range = std::max(m_fog.range, 1.0f);
	m_fog.temporalBlend = std::clamp(m_fog.temporalBlend, 0.02f, 1.0f);
	m_fog.anisotropy = std::clamp(m_fog.anisotropy, -0.95f, 0.95f);
}

[[nodiscard]] const VolumetricFogSettings& volumetricFog() const noexcept { return m_fog; }

private:

VolumetricFogSettings        m_fog{};
gfx::GpuResource             m_fogInject[2];     ///< 注入 (rgb = 散乱、a = 消散)。今フレームが書く方と履歴
gfx::GpuResource             m_fogIntegrated;    ///< 積分 (rgb = 散乱、a = 透過率)
ComPtr<ID3D12PipelineState>  m_fogInjectPSO;
ComPtr<ID3D12PipelineState>  m_fogIntegratePSO;
ComPtr<ID3D12PipelineState>  m_fogCompositePSO;
UINT                         m_fogDims[3] = {};
int                          m_fogWrite = 0;
bool                         m_fogHistoryValid = false;
bool                         m_fogTried = false;
std::uint32_t                m_fogFrameIndex = 0;
glm::mat4                    m_fogPrevViewProj{1.0f};
sgc::Vec3f                   m_fogPrevCamera{};

[[nodiscard]] static std::string fogShaderSource(const char* body)
{
	return std::string(DX12_ATMOSPHERE_COMMON_HLSL) + DX12_ATMOSPHERE_TOON_HLSL + DX12_FOG_COMMON_HLSL + body;
}

[[nodiscard]] bool createFogPipelines()
{
	return createAtmoComputePso(fogShaderSource(DX12_FOG_INJECT_CS), m_fogInjectPSO) &&
	       createAtmoComputePso(fogShaderSource(DX12_FOG_INTEGRATE_CS), m_fogIntegratePSO);
}

/// @brief 合成の PSO。HDR の写し (single-sample RGBA16F) へ dst × a + src で混ぜる
[[nodiscard]] bool createFogCompositePso()
{
	const auto ps = gfx::Dx12Shader::createPixelShader(fogShaderSource(DX12_FOG_COMPOSITE_HLSL), "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_atmoGraphicsRS.Get(), ps);
	auto& bl = pd.BlendState.RenderTarget[0];
	bl.BlendEnable = TRUE;
	bl.SrcBlend = D3D12_BLEND_ONE;
	bl.DestBlend = D3D12_BLEND_SRC_ALPHA;
	bl.BlendOp = D3D12_BLEND_OP_ADD;
	bl.SrcBlendAlpha = D3D12_BLEND_ZERO;
	bl.DestBlendAlpha = D3D12_BLEND_ONE;
	bl.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	return SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_fogCompositePSO.ReleaseAndGetAddressOf())));
}

/// @brief フォグの資源を内部解像度の froxel の数で用意する。初回はパイプラインも作る
[[nodiscard]] bool ensureVolumetricFog()
{
	if (!ensureAtmosphereCommon() || !ensureAerialComposite()) { return false; }
	if (!m_fogInjectPSO)
	{
		if (m_fogTried) { return false; }
		m_fogTried = true;
		if (!createFogPipelines())
		{
			m_fogInjectPSO.Reset();
			debug::warnOnce("dx12.fog.pipeline", "体積フォグのシェーダーを作れない — フォグは描かない");
			return false;
		}
	}
	const UINT w = static_cast<UINT>(fog::froxelCount(static_cast<int>(m_config.viewportWidth)));
	const UINT h = static_cast<UINT>(fog::froxelCount(static_cast<int>(m_config.viewportHeight)));
	if (m_fogIntegrated && m_fogDims[0] == w && m_fogDims[1] == h) { return true; }
	return createFogResources(w, h);
}

[[nodiscard]] bool createFogResources(UINT w, UINT h)
{
	if (m_device != nullptr) { m_device->waitForGpu(); }
	constexpr UINT d = fog::kFogSlices;
	const bool ok = createAtmoTexture(w, h, d, L"Renderer3D fog inject 0", m_fogInject[0]) &&
	                createAtmoTexture(w, h, d, L"Renderer3D fog inject 1", m_fogInject[1]) &&
	                createAtmoTexture(w, h, d, L"Renderer3D fog integrated", m_fogIntegrated);
	if (!ok)
	{
		m_fogIntegrated.Reset();
		return false;
	}
	m_fogDims[0] = w;
	m_fogDims[1] = h;
	m_fogDims[2] = d;
	m_fogHistoryValid = false;
	writeFogViews();
	return true;
}

void writeFogViews()
{
	constexpr auto k3D = D3D12_SRV_DIMENSION_TEXTURE3D;
	constexpr auto kRgba = DXGI_FORMAT_R16G16B16A16_FLOAT;
	for (UINT i = 0; i < 2; ++i)
	{
		const UINT inject = kAtmoSetFogInject + i;
		writeAtmoSrv(inject, 3, m_shadowMap.nativeResource(), DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D);
		writeAtmoSrv(inject, 4, m_shadowMapFar.nativeResource(), DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D);
		writeAtmoSrv(inject, 5, m_fogInject[1 - i].Get(), kRgba, k3D);
		writeAtmoUav(inject, m_fogInject[i].Get(), D3D12_UAV_DIMENSION_TEXTURE3D, fog::kFogSlices);
		const UINT integrate = kAtmoSetFogIntegrate + i;
		writeAtmoSrv(integrate, 5, m_fogInject[i].Get(), kRgba, k3D);
		writeAtmoUav(integrate, m_fogIntegrated.Get(), D3D12_UAV_DIMENSION_TEXTURE3D, fog::kFogSlices);
	}
	writeCompositeViews();
}

/// @brief 合成の組 (主パスの深度、空気遠近、フォグ)。どれかを作り替えたら呼ぶ。無いものは null 記述子
void writeCompositeViews()
{
	if (!m_atmoHeap) { return; }
	constexpr auto k3D = D3D12_SRV_DIMENSION_TEXTURE3D;
	constexpr auto kRgba = DXGI_FORMAT_R16G16B16A16_FLOAT;
	writeAtmoSrv(kAtmoSetComposite, 0, m_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2DMS);
	writeAtmoSrv(kAtmoSetComposite, 1, m_atmoAerial.Get(), kRgba, k3D);
	writeAtmoSrv(kAtmoSetComposite, 2, m_fogIntegrated.Get(), kRgba, k3D);
}

[[nodiscard]] bool ensureAerialComposite()
{
	if (m_fogCompositePSO) { return true; }
	if (!ensureAtmosphereCommon() || !createFogCompositePso())
	{
		debug::warnOnce("dx12.fog.composite", "空気遠近とフォグの合成を作れない — 掛けない");
		return false;
	}
	writeCompositeViews();
	return true;
}

/// @brief このフレームの CbFog。フォグが無効でも合成が読むので、空だけのフレームでも置く
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadFogCB()
{
	struct alignas(256) CbFog
	{
		float prevViewProj[4][4];
		float vec[8][4];   // dims, density, albedo, sun, ambientUp, ambientDown, jitter, prevCamera
	};
	CbFog cb{};
	toColumnMajor(cb.prevViewProj, m_fogPrevViewProj);
	const float sunK = m_light.intensity * m_fog.sunScale;
	const sgc::Colorf up = hemisphereAmbientSky();
	const sgc::Colorf down = hemisphereAmbientGround();
	const float amb = m_fog.ambientScale;
	const JitterOffset j = temporalJitter(m_fogFrameIndex);
	const float zj = haltonRadicalInverse(m_fogFrameIndex % 16 + 1, 5);
	const float v[8][4] = {
		{static_cast<float>(m_fogDims[0]), static_cast<float>(m_fogDims[1]), static_cast<float>(m_fogDims[2]), m_fog.range},
		{m_fog.density, m_fog.heightFalloff, m_fog.baseHeight, m_fog.constantDensity},
		{m_fog.albedo.r, m_fog.albedo.g, m_fog.albedo.b, m_fog.anisotropy},
		{m_light.color.r * sunK, m_light.color.g * sunK, m_light.color.b * sunK, (m_fog.shadows && m_shadowEnabled) ? 1.0f : 0.0f},
		{up.r * amb, up.g * amb, up.b * amb, m_fog.localLightScale},
		{down.r * amb, down.g * amb, down.b * amb, m_fogHistoryValid ? 1.0f : 0.0f},
		{j.x + 0.5f, j.y + 0.5f, zj, m_fog.temporalBlend},
		{m_fogPrevCamera.x, m_fogPrevCamera.y, m_fogPrevCamera.z, m_fog.enabled ? 1.0f : 0.0f},
	};
	std::memcpy(cb.vec, v, sizeof(v));
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief 注入 → 積分。影マップは前方の描画が読む PIXEL_SHADER_RESOURCE から、compute でも読める状態へ一時的に移す
void recordVolumetricFog()
{
	auto* cl = m_graphicsCmdList.Get();
	const D3D12_GPU_VIRTUAL_ADDRESS fogCb = uploadFogCB();
	const D3D12_GPU_VIRTUAL_ADDRESS shadowCb = uploadShadowCB();
	if (fogCb == 0 || shadowCb == 0 || !m_clusterFrameAlloc.valid() || !m_clusterMasks || !m_lightBufferAlloc.valid())
	{
		return;
	}
	bindAtmosphereCompute();
	cl->SetComputeRootConstantBufferView(1, fogCb);
	cl->SetComputeRootConstantBufferView(2, shadowCb);
	cl->SetComputeRootConstantBufferView(3, m_clusterFrameAlloc.gpuAddr);
	cl->SetComputeRootShaderResourceView(4, m_lightBufferAlloc.gpuAddr);
	cl->SetComputeRootShaderResourceView(5, m_clusterMasks->GetGPUVirtualAddress());
	constexpr auto kPsr = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	constexpr auto kAll = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	atmoBarrier(m_shadowMap.nativeResource(), kPsr, kAll);
	atmoBarrier(m_shadowMapFar.nativeResource(), kPsr, kAll);
	const int write = m_fogWrite;
	dispatchAtmo(m_fogInjectPSO.Get(), kAtmoSetFogInject + static_cast<UINT>(write), m_fogInject[write].Get(),
	             (m_fogDims[0] + 3) / 4, (m_fogDims[1] + 3) / 4, m_fogDims[2] / 4);
	dispatchAtmo(m_fogIntegratePSO.Get(), kAtmoSetFogIntegrate + static_cast<UINT>(write), m_fogIntegrated.Get(),
	             (m_fogDims[0] + 7) / 8, (m_fogDims[1] + 7) / 8, 1);
	atmoBarrier(m_shadowMapFar.nativeResource(), kAll, kPsr);
	atmoBarrier(m_shadowMap.nativeResource(), kAll, kPsr);
	m_fogWrite = 1 - write;
	m_fogHistoryValid = true;
	m_fogPrevViewProj = m_viewProjNoJitter;
	m_fogPrevCamera = m_cameraPosition;
	++m_fogFrameIndex;
}

/// @brief resolve の直後 (HDR の写しが RESOLVE_DEST) に呼ぶ。空気遠近とフォグを HDR に掛ける
void drawAerialFogComposite()
{
	const bool ap = m_sky.enabled && m_skyDrawPSO && m_sky.aerialPerspectiveScale > 0.0f;
	const bool fogOn = m_fog.enabled && m_fogIntegrated && m_fogHistoryValid;
	if ((!ap && !fogOn) || !m_fogCompositePSO || m_atmoFrameCB == 0 || !m_hdrIntermediateRtvHeap) { return; }
	const D3D12_GPU_VIRTUAL_ADDRESS fogCb = uploadFogCB();
	if (fogCb == 0) { return; }
	auto* cl = m_graphicsCmdList.Get();
	atmoBarrier(m_hdrIntermediateBuffer.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
	atmoBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = m_hdrIntermediateRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();
	ID3D12DescriptorHeap* heaps[] = {m_atmoHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootSignature(m_atmoGraphicsRS.Get());
	cl->SetPipelineState(m_fogCompositePSO.Get());
	cl->SetGraphicsRootConstantBufferView(0, m_atmoFrameCB);
	cl->SetGraphicsRootConstantBufferView(1, fogCb);
	cl->SetGraphicsRootDescriptorTable(2, atmoGpu(kAtmoSetComposite, 0));
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	atmoBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	atmoBarrier(m_hdrIntermediateBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}
