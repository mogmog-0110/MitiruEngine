// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// XeGTAO (external/XeGTAO) による環境遮蔽。SSAO と同じ setAmbientOcclusion の強さと半径で動き、
// 結果を SSAO のテクスチャ 0 へ写すので tonemap は同じ t1 を読む。パイプラインは GTAO を選んだ
// 最初のフレームで作る (FXC で 4 本 1〜2 秒かかるので、使わないゲームの起動を遅らせない)。

public:

/// @brief 環境遮蔽の方式。setAmbientOcclusion で有効にしたときだけ効く
void setAmbientOcclusionMethod(AmbientOcclusionMethod method) noexcept override { m_aoMethod = method; }

[[nodiscard]] AmbientOcclusionMethod ambientOcclusionMethod() const noexcept { return m_aoMethod; }

private:

AmbientOcclusionMethod m_aoMethod = AmbientOcclusionMethod::Ssao;
bool m_gtaoTried = false;   ///< パイプライン作りを試したか (失敗したら SSAO へ戻し、毎フレームは試さない)

/// 記述子の表。SRV 2 枚 + UAV 5 枚を 1 組にして、パスごとに組を変える
static constexpr UINT kGtaoSrvPerSet = 2;
static constexpr UINT kGtaoUavPerSet = 5;
static constexpr UINT kGtaoSetSize = kGtaoSrvPerSet + kGtaoUavPerSet;
static constexpr UINT kGtaoSetPrefilter = 0;
static constexpr UINT kGtaoSetMain = 1;
static constexpr UINT kGtaoSetDenoiseAB = 2;   ///< 項 A を読んで B へ書く
static constexpr UINT kGtaoSetDenoiseBA = 3;
static constexpr UINT kGtaoSetCount = 4;

ComPtr<ID3D12RootSignature>  m_gtaoRootSig;
ComPtr<ID3D12PipelineState>  m_gtaoDepthCopyPSO;
ComPtr<ID3D12PipelineState>  m_gtaoPrefilterPSO;
ComPtr<ID3D12PipelineState>  m_gtaoMainPSO;
ComPtr<ID3D12PipelineState>  m_gtaoDenoisePSO;
ComPtr<ID3D12PipelineState>  m_gtaoDenoiseFinalPSO;
gfx::GpuResource             m_gtaoNdcDepth;        ///< 主パスの深度の sample 0 (R32F)
gfx::GpuResource             m_gtaoWorkingDepth;    ///< 視空間の深度、mip 5 段 (R32F)
gfx::GpuResource             m_gtaoTerm[2];         ///< 遮蔽の項 (R8_UINT)。A と B を交互に使う
gfx::GpuResource             m_gtaoEdges;           ///< 縁 (R8_UNORM)
ComPtr<ID3D12DescriptorHeap> m_gtaoHeap;
ComPtr<ID3D12DescriptorHeap> m_gtaoRtvHeap;
UINT                         m_gtaoW = 0;
UINT                         m_gtaoH = 0;

/// @brief SSAO か GTAO を記録する。drawSsaoPasses と同じ位置 (深度が DEPTH_WRITE) で呼ぶ
void drawAmbientOcclusionPasses()
{
	if (m_aoMethod == AmbientOcclusionMethod::Gtao && ensureGtao())
	{
		drawGtaoPasses();
		return;
	}
	drawSsaoPasses();
}

[[nodiscard]] bool ensureGtao()
{
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	if (m_gtaoMainPSO && m_gtaoW == w && m_gtaoH == h) { return true; }
	if (m_gtaoTried && !m_gtaoMainPSO) { return false; }
	m_gtaoTried = true;
	if (!m_gtaoMainPSO && !createGtaoPipelines())
	{
		m_gtaoMainPSO.Reset();
		debug::verboseOnce("dx12.gtao.pipeline", "GTAO のシェーダーを作れなかったので、代わりに SSAO で描きます。");
		return false;
	}
	return createGtaoResources(w, h);
}

[[nodiscard]] bool createGtaoRootSignature()
{
	D3D12_DESCRIPTOR_RANGE srv = {};
	srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srv.NumDescriptors = kGtaoSrvPerSet;
	D3D12_DESCRIPTOR_RANGE uav = {};
	uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uav.NumDescriptors = kGtaoUavPerSet;
	D3D12_ROOT_PARAMETER p[3] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[1].DescriptorTable.NumDescriptorRanges = 1;
	p[1].DescriptorTable.pDescriptorRanges = &srv;
	p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[2].DescriptorTable.NumDescriptorRanges = 1;
	p[2].DescriptorTable.pDescriptorRanges = &uav;
	D3D12_STATIC_SAMPLER_DESC s = clampSampler(D3D12_FILTER_MIN_MAG_MIP_POINT, 0);
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 3;
	rd.pParameters = p;
	rd.NumStaticSamplers = 1;
	rd.pStaticSamplers = &s;
	return serializeRootSig(rd, m_gtaoRootSig);
}

[[nodiscard]] bool createGtaoComputePso(const std::string& source, ComPtr<ID3D12PipelineState>& out)
{
	UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
	flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	ComPtr<ID3DBlob> cs, err;
	if (FAILED(gfx::compileDx12Shader(source, "CSMain", "cs_5_0", flags, cs.GetAddressOf(), err.GetAddressOf())))
	{
		if (err) { console::verbosef("GTAO のシェーダーのコンパイルに失敗しました (%s)。", static_cast<const char*>(err->GetBufferPointer())); }
		return false;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_gtaoRootSig.Get();
	pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	return SUCCEEDED(m_d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
}

[[nodiscard]] bool createGtaoPipelines()
{
	if (!m_temporalRootSig || !m_outlinePostVS || !createGtaoRootSignature()) { return false; }
	const auto copyPS = gfx::Dx12Shader::createPixelShader(DX12_GTAO_DEPTH_COPY_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_temporalRootSig.Get(), copyPS);
	pd.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_gtaoDepthCopyPSO.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	return createGtaoComputePso(gtaoPassSource(DX12_GTAO_PREFILTER_CS), m_gtaoPrefilterPSO) &&
	       createGtaoComputePso(gtaoPassSource(DX12_GTAO_MAIN_CS), m_gtaoMainPSO) &&
	       createGtaoComputePso(gtaoPassSource(DX12_GTAO_DENOISE_CS, "#define GTAO_FINAL 0\n"), m_gtaoDenoisePSO) &&
	       createGtaoComputePso(gtaoPassSource(DX12_GTAO_DENOISE_CS, "#define GTAO_FINAL 1\n"), m_gtaoDenoiseFinalPSO);
}

[[nodiscard]] bool createGtaoTexture(UINT w, UINT h, UINT16 mips, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                     D3D12_RESOURCE_STATES state, const wchar_t* name, gfx::GpuResource& out)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = mips;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = flags;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = format;
	const bool rt = (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, state, rt ? &clear : nullptr, out)) ||
	    !out)
	{
		return false;
	}
	out->SetName(name);
	return true;
}

/// @brief viewport の大きさの資源と記述子を作り直す
[[nodiscard]] bool createGtaoResources(UINT w, UINT h)
{
	if (m_device != nullptr) { m_device->waitForGpu(); }
	m_gtaoW = 0;
	m_gtaoH = 0;
	constexpr auto kUav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	constexpr auto kRest = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	const bool ok =
		createGtaoTexture(w, h, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, kRest,
		                  L"Renderer3D GTAO NDC depth", m_gtaoNdcDepth) &&
		createGtaoTexture(w, h, XE_GTAO_DEPTH_MIP_LEVELS, DXGI_FORMAT_R32_FLOAT, kUav, kRest,
		                  L"Renderer3D GTAO working depth", m_gtaoWorkingDepth) &&
		createGtaoTexture(w, h, 1, DXGI_FORMAT_R8_UINT, kUav, kRest, L"Renderer3D GTAO term A", m_gtaoTerm[0]) &&
		createGtaoTexture(w, h, 1, DXGI_FORMAT_R8_UINT, kUav, kRest, L"Renderer3D GTAO term B", m_gtaoTerm[1]) &&
		createGtaoTexture(w, h, 1, DXGI_FORMAT_R8_UNORM, kUav, kRest, L"Renderer3D GTAO edges", m_gtaoEdges);
	if (!ok || !createGtaoHeaps()) { return false; }
	writeGtaoViews();
	m_gtaoW = w;
	m_gtaoH = h;
	return true;
}

[[nodiscard]] bool createGtaoHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kGtaoSetCount * kGtaoSetSize;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	D3D12_DESCRIPTOR_HEAP_DESC rd = {};
	rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rd.NumDescriptors = 1;
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_gtaoHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&rd, IID_PPV_ARGS(m_gtaoRtvHeap.ReleaseAndGetAddressOf())));
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE gtaoCpu(UINT set, UINT slot) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_gtaoHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(set * kGtaoSetSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gtaoGpu(UINT set, UINT slot) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE h = m_gtaoHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<UINT64>(set * kGtaoSetSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

void writeGtaoSrv(UINT set, UINT slot, ID3D12Resource* res, DXGI_FORMAT format, UINT mips)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = format;
	sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sd.Texture2D.MipLevels = mips;
	m_d3dDevice->CreateShaderResourceView(res, &sd, gtaoCpu(set, slot));
}

void writeGtaoUav(UINT set, UINT slot, ID3D12Resource* res, DXGI_FORMAT format, UINT mip)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = format;
	ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	ud.Texture2D.MipSlice = mip;
	m_d3dDevice->CreateUnorderedAccessView(res, nullptr, &ud, gtaoCpu(set, kGtaoSrvPerSet + slot));
}

void writeGtaoViews()
{
	constexpr auto kR32 = DXGI_FORMAT_R32_FLOAT;
	constexpr auto kU8 = DXGI_FORMAT_R8_UINT;
	constexpr auto kUn8 = DXGI_FORMAT_R8_UNORM;
	for (UINT set = 0; set < kGtaoSetCount; ++set)
	{
		for (UINT slot = 0; slot < kGtaoUavPerSet; ++slot) { writeGtaoUav(set, slot, nullptr, kU8, 0); }
	}
	writeGtaoSrv(kGtaoSetPrefilter, 0, m_gtaoNdcDepth.Get(), kR32, 1);
	writeGtaoSrv(kGtaoSetPrefilter, 1, nullptr, kR32, 1);
	for (UINT mip = 0; mip < XE_GTAO_DEPTH_MIP_LEVELS; ++mip)
	{
		writeGtaoUav(kGtaoSetPrefilter, mip, m_gtaoWorkingDepth.Get(), kR32, mip);
	}
	writeGtaoSrv(kGtaoSetMain, 0, m_gtaoWorkingDepth.Get(), kR32, XE_GTAO_DEPTH_MIP_LEVELS);
	writeGtaoSrv(kGtaoSetMain, 1, m_gtaoNdcDepth.Get(), kR32, 1);
	writeGtaoUav(kGtaoSetMain, 0, m_gtaoTerm[0].Get(), kU8, 0);
	writeGtaoUav(kGtaoSetMain, 1, m_gtaoEdges.Get(), kUn8, 0);
	for (UINT ab = 0; ab < 2; ++ab)
	{
		const UINT set = (ab == 0) ? kGtaoSetDenoiseAB : kGtaoSetDenoiseBA;
		writeGtaoSrv(set, 0, m_gtaoTerm[ab].Get(), kU8, 1);
		writeGtaoSrv(set, 1, m_gtaoEdges.Get(), kUn8, 1);
		writeGtaoUav(set, 0, m_gtaoTerm[1 - ab].Get(), kU8, 0);
	}
	D3D12_RENDER_TARGET_VIEW_DESC rv = {};
	rv.Format = kR32;
	rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateRenderTargetView(m_gtaoNdcDepth.Get(), &rv, m_gtaoRtvHeap->GetCPUDescriptorHandleForHeapStart());
}

/// @brief XeGTAO の定数。射影は主パスと同じずらし込み (深度と同じ射影で視空間へ戻す)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadGtaoCB(int denoisePasses)
{
	XeGTAO::GTAOSettings settings;
	settings.QualityLevel = 2;
	settings.DenoisePasses = denoisePasses;
	settings.Radius = m_aoRadius;
	settings.FinalValuePower = XE_GTAO_DEFAULT_FINAL_VALUE_POWER * m_aoStrength;
	struct alignas(256) CbGtao
	{
		XeGTAO::GTAOConstants consts;
	};
	CbGtao cb{};
	// glm は列優先で持つので、そのままの並びが XeGTAO の言う「行優先 (行ベクトル)」の行列になる
	const unsigned temporalIndex = (m_aaMode == AntiAliasing3D::Taa) ? m_taaFrameIndex : 0u;
	XeGTAO::GTAOUpdateConstants(cb.consts, static_cast<int>(m_gtaoW), static_cast<int>(m_gtaoH), settings,
	                            glm::value_ptr(m_projMatrix), true, temporalIndex);
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

void gtaoBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	temporalBarrier(res, before, after);
}

void copyMainDepthForGtao()
{
	auto* cl = m_graphicsCmdList.Get();
	gtaoBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gtaoBarrier(m_gtaoNdcDepth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto rtv = m_gtaoRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();
	// 主パスの深度 SRV は velocity の表の 0 番に貼ってある
	const float pad[4] = {};
	drawTemporalFullscreen(m_gtaoDepthCopyPSO.Get(), kTemporalTableVelocity, pad, sizeof(pad));
	gtaoBarrier(m_gtaoNdcDepth.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gtaoBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void dispatchGtao(ID3D12PipelineState* pso, UINT set, UINT groupsX, UINT groupsY)
{
	auto* cl = m_graphicsCmdList.Get();
	cl->SetPipelineState(pso);
	cl->SetComputeRootDescriptorTable(1, gtaoGpu(set, 0));
	cl->SetComputeRootDescriptorTable(2, gtaoGpu(set, kGtaoSrvPerSet));
	cl->Dispatch(groupsX, groupsY, 1);
}

/// @brief 深度の写し → 視空間の深度の mip → 遮蔽の項と縁 → 縁を保つぼかし → SSAO のテクスチャ 0 へ写す
void drawGtaoPasses()
{
	m_aoAppliedThisFrame = false;
	if (!m_aoEnabled || m_aoStrength <= 0.0f || !m_ssaoTex[0]) { return; }
	auto* cl = m_graphicsCmdList.Get();
	copyMainDepthForGtao();

	// TAA が重ねてくれるならぼかしは 1 回で足りる。止まった絵だけなら 2 回で模様を消す
	const int denoisePasses = (m_aaMode == AntiAliasing3D::Taa) ? 1 : 2;
	const D3D12_GPU_VIRTUAL_ADDRESS cb = uploadGtaoCB(denoisePasses);
	if (cb == 0) { return; }
	constexpr auto kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	constexpr auto kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	cl->SetComputeRootSignature(m_gtaoRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = {m_gtaoHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetComputeRootConstantBufferView(0, cb);

	gtaoBarrier(m_gtaoWorkingDepth.Get(), kRead, kUav);
	dispatchGtao(m_gtaoPrefilterPSO.Get(), kGtaoSetPrefilter, (m_gtaoW + 15) / 16, (m_gtaoH + 15) / 16);
	gtaoBarrier(m_gtaoWorkingDepth.Get(), kUav, kRead);

	gtaoBarrier(m_gtaoTerm[0].Get(), kRead, kUav);
	gtaoBarrier(m_gtaoEdges.Get(), kRead, kUav);
	dispatchGtao(m_gtaoMainPSO.Get(), kGtaoSetMain, (m_gtaoW + 7) / 8, (m_gtaoH + 7) / 8);
	gtaoBarrier(m_gtaoEdges.Get(), kUav, kRead);
	gtaoBarrier(m_gtaoTerm[0].Get(), kUav, kRead);

	int src = 0;
	for (int pass = 0; pass < denoisePasses; ++pass)
	{
		const int dst = 1 - src;
		gtaoBarrier(m_gtaoTerm[dst].Get(), kRead, kUav);
		dispatchGtao((pass + 1 == denoisePasses) ? m_gtaoDenoiseFinalPSO.Get() : m_gtaoDenoisePSO.Get(),
		             (src == 0) ? kGtaoSetDenoiseAB : kGtaoSetDenoiseBA, ((m_gtaoW + 1) / 2 + 7) / 8, (m_gtaoH + 7) / 8);
		gtaoBarrier(m_gtaoTerm[dst].Get(), kUav, kRead);
		src = dst;
	}

	// 値は 0..255 の uint。同じ R8 の型の仲間なので、そのまま SSAO の R8_UNORM へ写せば 0..1 で読める
	gtaoBarrier(m_gtaoTerm[src].Get(), kRead, D3D12_RESOURCE_STATE_COPY_SOURCE);
	gtaoBarrier(m_ssaoTex[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	cl->CopyResource(m_ssaoTex[0].Get(), m_gtaoTerm[src].Get());
	gtaoBarrier(m_ssaoTex[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gtaoBarrier(m_gtaoTerm[src].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kRead);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
	m_aoAppliedThisFrame = true;
}
