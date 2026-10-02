// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 動きのぼけ: タイル最大 → 近傍最大 → 集め。AA の後に backbuffer (LDR) を 1 回書き直す。

gfx::GpuResource            m_mbTileMax;       ///< タイル解像度の R16G16F
gfx::GpuResource            m_mbNeighborMax;   ///< 同じ大きさ
UINT                        m_mbTilesW = 0;
UINT                        m_mbTilesH = 0;
ComPtr<ID3D12PipelineState> m_mbTileMaxPSO;
ComPtr<ID3D12PipelineState> m_mbNeighborMaxPSO;
ComPtr<ID3D12PipelineState> m_mbGatherPSO;

/// @brief 3 本の PSO を作る。createTemporalPipelines の後に呼ぶ (root sig と VS を共用する)
void createMotionBlurPipelines()
{
	if (!m_temporalRootSig || !m_outlinePostVS) { return; }
	const std::string common = std::string("#define TILE ") + std::to_string(kMotionBlurTile) + "\n" +
	                           DX12_MOTION_BLUR_COMMON_HLSL;
	const auto tilePS = gfx::Dx12Shader::createPixelShader(common + DX12_MOTION_BLUR_TILE_MAX_PS, "PSMain");
	const auto neighborPS = gfx::Dx12Shader::createPixelShader(common + DX12_MOTION_BLUR_NEIGHBOR_MAX_PS, "PSMain");
	const auto gatherPS = gfx::Dx12Shader::createPixelShader(common + DX12_MOTION_BLUR_GATHER_PS, "PSMain");

	const auto make = [this](const gfx::Dx12Shader& ps, DXGI_FORMAT format, ComPtr<ID3D12PipelineState>& out) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_temporalRootSig.Get(), ps);
		pd.RTVFormats[0] = format;
		if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())))) { out.Reset(); }
	};
	make(tilePS, DXGI_FORMAT_R16G16_FLOAT, m_mbTileMaxPSO);
	make(neighborPS, DXGI_FORMAT_R16G16_FLOAT, m_mbNeighborMaxPSO);
	make(gatherPS, DXGI_FORMAT_R8G8B8A8_UNORM, m_mbGatherPSO);
}

/// @brief タイル解像度の 2 枚を作る。createTemporalResources から (viewport が決まった後に) 呼ぶ
void createMotionBlurResources()
{
	const auto tile = static_cast<UINT>(kMotionBlurTile);
	m_mbTilesW = (static_cast<UINT>(m_config.viewportWidth) + tile - 1) / tile;
	m_mbTilesH = (static_cast<UINT>(m_config.viewportHeight) + tile - 1) / tile;
	constexpr auto kRt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (!createTemporalTexture(m_mbTilesW, m_mbTilesH, DXGI_FORMAT_R16G16_FLOAT, kRt, L"Renderer3D motion blur tile max",
	                           m_mbTileMax) ||
	    !createTemporalTexture(m_mbTilesW, m_mbTilesH, DXGI_FORMAT_R16G16_FLOAT, kRt,
	                           L"Renderer3D motion blur neighbor max", m_mbNeighborMax))
	{
		m_mbTileMax.Reset();
		m_mbNeighborMax.Reset();
	}
}

void writeMotionBlurViews()
{
	if (!m_mbTileMax || !m_mbNeighborMax) { return; }
	D3D12_RENDER_TARGET_VIEW_DESC rv = {};
	rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rv.Format = DXGI_FORMAT_R16G16_FLOAT;
	m_d3dDevice->CreateRenderTargetView(m_mbTileMax.Get(), &rv, temporalRtv(kTemporalRtvTileMax));
	m_d3dDevice->CreateRenderTargetView(m_mbNeighborMax.Get(), &rv, temporalRtv(kTemporalRtvNeighborMax));

	constexpr auto kRg = DXGI_FORMAT_R16G16_FLOAT;
	constexpr auto kRgba = DXGI_FORMAT_R8G8B8A8_UNORM;
	writeTexSrv(kTemporalTableTileMax, 0, m_velocityTex.Get(), kRg);
	writeTexSrv(kTemporalTableNeighborMax, 0, m_mbTileMax.Get(), kRg);
	for (UINT slot = 1; slot < kTemporalTableSize; ++slot)
	{
		writeTexSrv(kTemporalTableTileMax, slot, nullptr, kRgba);
		writeTexSrv(kTemporalTableNeighborMax, slot, nullptr, kRgba);
	}
	writeTexSrv(kTemporalTableMotionBlur, 0, m_fxaaIntermediate.Get(), kRgba);
	writeTexSrv(kTemporalTableMotionBlur, 1, m_velocityTex.Get(), kRg);
	writeTexSrv(kTemporalTableMotionBlur, 2, m_mbNeighborMax.Get(), kRg);
	writeMsaaDepthSrv(kTemporalTableMotionBlur, 3);
}

[[nodiscard]] bool motionBlurReady() const noexcept
{
	return m_motionBlurStrength > 0.0f && m_mbTileMaxPSO && m_mbNeighborMaxPSO && m_mbGatherPSO && m_mbTileMax &&
	       m_mbNeighborMax && velocityReady() && m_fxaaIntermediate;
}

/// @brief AA の後に呼ぶ
void drawMotionBlurPass()
{
	auto* bb = sceneColorTarget();
	if (!motionBlurReady() || !backBufferMatchesViewport(bb)) { return; }

	struct alignas(256) CbMotionBlur
	{
		float screenSize[2];
		float halfExposure;
		float nearZ;
		float farZ;
		float pad[3];
	};
	CbMotionBlur cb{};
	cb.screenSize[0] = m_config.viewportWidth;
	cb.screenSize[1] = m_config.viewportHeight;
	cb.halfExposure = 0.5f * m_motionBlurStrength;
	cb.nearZ = m_clodCamera.nearClip();
	cb.farZ = m_clodCamera.farClip();

	drawMotionBlurTilePass(m_mbTileMax, kTemporalRtvTileMax, m_mbTileMaxPSO.Get(), kTemporalTableTileMax, cb);
	drawMotionBlurTilePass(m_mbNeighborMax, kTemporalRtvNeighborMax, m_mbNeighborMaxPSO.Get(),
	                       kTemporalTableNeighborMax, cb);

	beginBackBufferCopy(bb->nativeResource());
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = bb->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();
	drawTemporalFullscreen(m_mbGatherPSO.Get(), kTemporalTableMotionBlur, &cb, sizeof(cb));
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	endBackBufferCopy();
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

template <typename Cb>
void drawMotionBlurTilePass(gfx::GpuResource& target, UINT rtvIndex, ID3D12PipelineState* pso, UINT table, const Cb& cb)
{
	temporalBarrier(target.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto rtv = temporalRtv(rtvIndex);
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(m_mbTilesW), static_cast<float>(m_mbTilesH), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(m_mbTilesW), static_cast<LONG>(m_mbTilesH)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
	drawTemporalFullscreen(pso, table, &cb, sizeof(cb));
	temporalBarrier(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
