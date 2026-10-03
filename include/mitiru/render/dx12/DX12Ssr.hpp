// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。画面の反射 (SSR) の履歴
//
// 前方の描画の PBR と水面の PS が、前のフレームの深度の階層 (HZB) を前のフレームの画面で辿り、当たった所の前のフレームの
// 色を映す (DX12LightingProbeShaders.hpp の traceSsr)。この断片は、空気遠近を合成した HDR と MSAA の深度から、次のフレームが
// 辿る HZB と色の mip を compute で作る。GPU は同じキューで順に流すので、資源は 1 組で足りる。

public:

/// @brief 画面の反射を設定する (host の既定)。既定は無効。無効の間は HZB を作らない。ゲームが SceneLook の indirect を
///        頼んでいる間はそちらが上に乗り、設定画面の上限 (QualityCaps::screenSpaceReflections) が切っていれば効かない
void setScreenSpaceReflections(const ScreenSpaceReflectionSettings& s) noexcept
{
	m_hostSsr = s;
	refreshIndirectLighting();
}

/// @brief 今使っている画面の反射 (host の既定 + ゲームの頼み + 設定画面の上限)
[[nodiscard]] const ScreenSpaceReflectionSettings& screenSpaceReflections() const noexcept { return m_ssr; }

private:

void applySsrSettings(const ScreenSpaceReflectionSettings& s) noexcept
{
	m_ssr = s;
	m_ssr.maxRoughness = std::clamp(s.maxRoughness, 0.0f, 1.0f);
	m_ssr.thickness = std::max(s.thickness, 0.01f);
	m_ssr.maxDistance = std::max(s.maxDistance, 0.1f);
	m_ssr.maxSteps = std::clamp(s.maxSteps, 4, 256);
	m_ssr.intensity = std::max(s.intensity, 0.0f);
	if (!s.enabled) { m_ssrHistoryValid = false; }
}

ScreenSpaceReflectionSettings m_hostSsr;

static constexpr UINT kSsrColorMips = 7;
static constexpr UINT kSsrMaxHzbMips = 14;

ScreenSpaceReflectionSettings m_ssr;
gfx::GpuResource             m_ssrHzb;          ///< R32_FLOAT、段 0 は内部解像度
gfx::GpuResource             m_ssrColor;        ///< R16G16B16A16_FLOAT、kSsrColorMips 段
UINT                         m_ssrWidth = 0;
UINT                         m_ssrHeight = 0;
UINT                         m_ssrHzbMips = 0;
UINT                         m_ssrColorMipCount = 0;
ComPtr<ID3D12DescriptorHeap> m_ssrHeap;         ///< shader-visible。dispatch ごとに { t0, u0 } の 2 枚
const void*                  m_ssrHeapKey[2] = {nullptr, nullptr};   ///< 表が指す深度と HDR (作り直しの検知)
ComPtr<ID3D12RootSignature>  m_ssrRootSig;
ComPtr<ID3D12PipelineState>  m_ssrHzbFirstPso;
ComPtr<ID3D12PipelineState>  m_ssrHzbDownPso;
ComPtr<ID3D12PipelineState>  m_ssrColorCopyPso;
ComPtr<ID3D12PipelineState>  m_ssrColorDownPso;
bool                         m_ssrPipelineTried = false;
bool                         m_ssrInShaderState = false;   ///< 資源が PS / CS で読める状態か (false = UAV)
bool                         m_ssrHistoryValid = false;
glm::mat4                    m_ssrPrevViewProj{1.0f};
float                        m_ssrDepthA = 0.0f;   ///< 前フレームの射影の z の係数。距離 = B / (深度 + A)
float                        m_ssrDepthB = 0.0f;
float                        m_ssrNear = 0.1f;

/// @brief root sig と 4 つの PSO を作る (1 回だけ試す)。作れなければ false で、画面の反射は効かない
bool ensureSsrPipelines()
{
	if (m_ssrRootSig) { return true; }
	if (m_ssrPipelineTried || m_d3dDevice == nullptr) { return false; }
	m_ssrPipelineTried = true;
	static const D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
	                                                 {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1}};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[0].DescriptorTable = {2, ranges};
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	p[1].Constants = {0, 0, 4};
	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 2;
	rsd.pParameters = p;
	ComPtr<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
	    FAILED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	                                            IID_PPV_ARGS(m_ssrRootSig.ReleaseAndGetAddressOf()))))
	{
		debug::warnOnce("dx12.ssr.init", "画面の反射のルートシグネチャを作れない — 画面の反射は効かない");
		return false;
	}
	const bool ok = makeSsrPso(DX12_SSR_HZB_FIRST_CS, m_ssrHzbFirstPso) && makeSsrPso(DX12_SSR_HZB_DOWN_CS, m_ssrHzbDownPso) &&
	                makeSsrPso(DX12_SSR_COLOR_COPY_CS, m_ssrColorCopyPso) && makeSsrPso(DX12_SSR_COLOR_DOWN_CS, m_ssrColorDownPso);
	if (!ok)
	{
		m_ssrRootSig.Reset();
		debug::warnOnce("dx12.ssr.init", "画面の反射の compute を作れない — 画面の反射は効かない");
	}
	return ok;
}

[[nodiscard]] bool makeSsrPso(const char* body, ComPtr<ID3D12PipelineState>& out)
{
	ComPtr<ID3DBlob> cs, err;
	const std::string src = std::string(DX12_SSR_COMMON_HLSL) + body;
	if (FAILED(gfx::compileDx12Shader(src, "CSMain", "cs_5_0", 0, cs.GetAddressOf(), err.GetAddressOf()))) { return false; }
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_ssrRootSig.Get();
	pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	return SUCCEEDED(m_d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
}

/// @brief 内部解像度に合わせて HZB と色の mip を作る。大きさが変わったら古いものはこのフレームの後に捨てる
bool ensureSsrResources()
{
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	if (m_ssrHzb && m_ssrWidth == w && m_ssrHeight == h) { return true; }
	if (m_ssrHzb) { m_frameTempResources.push_back(std::move(m_ssrHzb)); }
	if (m_ssrColor) { m_frameTempResources.push_back(std::move(m_ssrColor)); }
	m_ssrHistoryValid = false;
	m_ssrInShaderState = false;
	UINT mips = 1;
	while (mips < kSsrMaxHzbMips && ((std::max(w, h) >> mips) > 0)) { ++mips; }
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = w;
	d.Height = h;
	d.DepthOrArraySize = 1;
	d.MipLevels = static_cast<UINT16>(mips);
	d.Format = DXGI_FORMAT_R32_FLOAT;
	d.SampleDesc.Count = 1;
	d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_RESOURCE_DESC c = d;
	c.MipLevels = static_cast<UINT16>(std::min(mips, kSsrColorMips));
	c.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	if (w == 0 || h == 0 ||
	    FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, m_ssrHzb)) ||
	    FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, c, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, m_ssrColor)))
	{
		m_ssrHzb.Reset();
		m_ssrColor.Reset();
		return false;
	}
	m_ssrHzb->SetName(L"Renderer3D SSR HZB");
	m_ssrColor->SetName(L"Renderer3D SSR colour");
	m_ssrWidth = w;
	m_ssrHeight = h;
	m_ssrHzbMips = mips;
	m_ssrColorMipCount = c.MipLevels;
	m_ssrHeapKey[0] = m_ssrHeapKey[1] = nullptr;
	return true;
}

/// @brief dispatch ごとの { t0, u0 } を書く。深度・HDR・自分の資源が作り直された時だけ (GPU が読んでいる表は書き換えない)
bool ensureSsrDescriptors()
{
	const void* key[2] = {m_depthBuffer.Get(), m_hdrIntermediateBuffer.Get()};
	if (m_ssrHeap && m_ssrHeapKey[0] == key[0] && m_ssrHeapKey[1] == key[1]) { return true; }
	if (!m_ssrHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.NumDescriptors = 2 * (kSsrMaxHzbMips + kSsrColorMips);
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_ssrHeap.ReleaseAndGetAddressOf())))) { return false; }
	}
	UINT slot = 0;
	writeSsrPair(slot++, m_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2DMS, 0, m_ssrHzb.Get(), 0);
	for (UINT m = 1; m < m_ssrHzbMips; ++m)
	{
		writeSsrPair(slot++, m_ssrHzb.Get(), DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D, m - 1, m_ssrHzb.Get(), m);
	}
	writeSsrPair(slot++, m_hdrIntermediateBuffer.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D, 0,
	             m_ssrColor.Get(), 0);
	for (UINT m = 1; m < m_ssrColorMipCount; ++m)
	{
		writeSsrPair(slot++, m_ssrColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D, m - 1, m_ssrColor.Get(), m);
	}
	m_ssrHeapKey[0] = key[0];
	m_ssrHeapKey[1] = key[1];
	return true;
}

void writeSsrPair(UINT slot, ID3D12Resource* src, DXGI_FORMAT format, D3D12_SRV_DIMENSION dim, UINT srcMip, ID3D12Resource* dst,
                  UINT dstMip)
{
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_ssrHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * 2 * inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Format = format;
	sv.ViewDimension = dim;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (dim == D3D12_SRV_DIMENSION_TEXTURE2D)
	{
		sv.Texture2D.MostDetailedMip = srcMip;
		sv.Texture2D.MipLevels = 1;
	}
	m_d3dDevice->CreateShaderResourceView(src, &sv, cpu);
	cpu.ptr += inc;
	D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
	uv.Format = dst == m_ssrHzb.Get() ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uv.Texture2D.MipSlice = dstMip;
	m_d3dDevice->CreateUnorderedAccessView(dst, nullptr, &uv, cpu);
}

void ssrBarrier(ID3D12Resource* res, UINT sub, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = sub;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

/// @brief 1 段ぶん dispatch する。書いた段は次の段が読めるよう SRV へ遷移する
void ssrDispatch(ID3D12PipelineState* pso, UINT slot, UINT dstW, UINT dstH, UINT srcW, UINT srcH, ID3D12Resource* dst, UINT dstMip)
{
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_ssrHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot) * 2 * inc;
	const UINT size[4] = {dstW, dstH, srcW, srcH};
	m_graphicsCmdList->SetPipelineState(pso);
	m_graphicsCmdList->SetComputeRootDescriptorTable(0, gpu);
	m_graphicsCmdList->SetComputeRoot32BitConstants(1, 4, size, 0);
	m_graphicsCmdList->Dispatch((dstW + 7) / 8, (dstH + 7) / 8, 1);
	ssrBarrier(dst, dstMip, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

/// @brief endFrame で空気遠近の合成の後に呼ぶ。次のフレームが辿る HZB と色の mip を作り、このフレームの射影を覚える
void buildSsrHistory()
{
	if (!m_ssr.enabled || m_activeView != nullptr || !m_depthBuffer || !m_hdrIntermediateBuffer) { return; }
	if (!ensureSsrPipelines() || !ensureSsrResources() || !ensureSsrDescriptors()) { return; }
	if (m_ssrInShaderState)
	{
		ssrBarrier(m_ssrHzb.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
		           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ssrBarrier(m_ssrColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
		           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	ID3D12DescriptorHeap* heaps[] = {m_ssrHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetComputeRootSignature(m_ssrRootSig.Get());
	ssrBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_DEPTH_WRITE,
	           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	UINT slot = 0;
	ssrDispatch(m_ssrHzbFirstPso.Get(), slot++, m_ssrWidth, m_ssrHeight, m_ssrWidth, m_ssrHeight, m_ssrHzb.Get(), 0);
	ssrBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
	           D3D12_RESOURCE_STATE_DEPTH_WRITE);
	for (UINT m = 1; m < m_ssrHzbMips; ++m)
	{
		ssrDispatch(m_ssrHzbDownPso.Get(), slot++, mipExtent(m_ssrWidth, m), mipExtent(m_ssrHeight, m), mipExtent(m_ssrWidth, m - 1),
		            mipExtent(m_ssrHeight, m - 1), m_ssrHzb.Get(), m);
	}
	ssrBarrier(m_hdrIntermediateBuffer.Get(), 0, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ssrDispatch(m_ssrColorCopyPso.Get(), slot++, m_ssrWidth, m_ssrHeight, m_ssrWidth, m_ssrHeight, m_ssrColor.Get(), 0);
	ssrBarrier(m_hdrIntermediateBuffer.Get(), 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	for (UINT m = 1; m < m_ssrColorMipCount; ++m)
	{
		ssrDispatch(m_ssrColorDownPso.Get(), slot++, mipExtent(m_ssrWidth, m), mipExtent(m_ssrHeight, m), mipExtent(m_ssrWidth, m - 1),
		            mipExtent(m_ssrHeight, m - 1), m_ssrColor.Get(), m);
	}
	m_ssrInShaderState = true;
	m_ssrHistoryValid = true;
	m_ssrPrevViewProj = m_viewProjNoJitter;
	m_ssrDepthA = m_projNoJitter[2][2];
	m_ssrDepthB = m_projNoJitter[3][2];
	m_ssrNear = m_clodCamera.nearClip();
}

[[nodiscard]] static UINT mipExtent(UINT size, UINT mip) noexcept { return std::max(1u, size >> mip); }

/// @brief 場面の表の t42 (HZB) と t43 (色) を書く。前のフレームの履歴が無ければ null
void writeSsrSrvs(D3D12_CPU_DESCRIPTOR_HANDLE cpu) const
{
	const bool ready = m_ssr.enabled && m_ssrHistoryValid && m_ssrInShaderState;
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R32_FLOAT;
	sv.Texture2D.MipLevels = ready ? m_ssrHzbMips : 1;
	m_d3dDevice->CreateShaderResourceView(ready ? m_ssrHzb.Get() : nullptr, &sv, cpu);
	cpu.ptr += m_albedoSrvIncrement;
	sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	sv.Texture2D.MipLevels = ready ? m_ssrColorMipCount : 1;
	m_d3dDevice->CreateShaderResourceView(ready ? m_ssrColor.Get() : nullptr, &sv, cpu);
}

/// @brief CbCluster の Ssr* を埋める。副ビューは主ビューの履歴と画面が違うので使わない
void fillSsrCB(DX12CbCluster& cb, bool mainView) const
{
	const bool on = mainView && m_ssr.enabled && m_ssrHistoryValid && m_ssrInShaderState;
	cb.ssrParams[0] = on ? 1.0f : 0.0f;
	cb.ssrParams[1] = m_ssr.maxRoughness;
	cb.ssrParams[2] = m_ssr.thickness;
	cb.ssrParams[3] = m_ssr.intensity;
	cb.ssrScreen[0] = static_cast<float>(m_ssrWidth);
	cb.ssrScreen[1] = static_cast<float>(m_ssrHeight);
	cb.ssrScreen[2] = static_cast<float>(m_ssrHzbMips);
	cb.ssrScreen[3] = static_cast<float>(m_ssrColorMipCount);
	cb.ssrRay[0] = m_ssr.maxDistance;
	cb.ssrRay[1] = static_cast<float>(m_ssr.maxSteps);
	cb.ssrRay[2] = static_cast<float>(m_taaFrameIndex % 64u);
	cb.ssrRay[3] = (m_aaMode == AntiAliasing3D::Taa || upscaleActive()) ? 1.0f : 0.0f;
	cb.ssrDepth[0] = m_ssrDepthA;
	cb.ssrDepth[1] = m_ssrDepthB;
	cb.ssrDepth[2] = m_ssrNear;
	std::memcpy(cb.ssrPrevViewProj, &m_ssrPrevViewProj, sizeof(cb.ssrPrevViewProj));
}
