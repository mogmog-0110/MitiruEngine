// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// GPU パーティクルの資源と PSO。粒のプールは全エミッターで 1 本 (kParticlePoolSize 個) を区画に分けて使う。
// 刻みの compute と描画の PSO は root sig を分け、深度・法線・VFX テクスチャは自分の heap のフレームの区画に置く。

private:

/// 自分の heap の 1 フレームぶん: [0] 深度 [1] 法線 (刻みの表) / [2] 深度 [3] VFX の色 (描画の表)
static constexpr UINT kParticleHeapPerFrame = 4;

gfx::GpuResource             m_particlePool;
ComPtr<ID3D12RootSignature>  m_particleStepRS;
ComPtr<ID3D12RootSignature>  m_particleDrawRS;
ComPtr<ID3D12PipelineState>  m_particleStepPSO;
ComPtr<ID3D12PipelineState>  m_particleAdditivePSO;
ComPtr<ID3D12PipelineState>  m_particleAlphaPSO;
ComPtr<ID3D12PipelineState>  m_particleReactivePSO;
ComPtr<ID3D12DescriptorHeap> m_particleHeap;

void createParticlePipelines()
{
	m_particleQueue.reserve(16);
	m_particleInstances.reserve(16);
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, UINT64{sizeof(ParticleGpu)} * kParticlePoolSize,
	                                D3D12_RESOURCE_STATE_COMMON, m_particlePool, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) ||
	    !createParticleRootSigs() || !createParticleStepPso() || !createParticleDrawPsos() || !createParticleHeap())
	{
		debug::warnOnce("dx12.particles.init", "GPU パーティクルの資源を作れない — 粒は描かない");
		m_particleStepPSO.Reset();
		return;
	}
	m_particlePool->SetName(L"Renderer3D particle pool");
}

[[nodiscard]] bool particlesReady() const noexcept
{
	return m_particlePool && m_particleStepPSO && m_particleAdditivePSO && m_particleAlphaPSO && m_particleHeap;
}

[[nodiscard]] bool createParticleRootSigs()
{
	D3D12_DESCRIPTOR_RANGE stepRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
	D3D12_ROOT_PARAMETER sp[5] = {};
	sp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	sp[0].Descriptor.ShaderRegister = 0;
	sp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	sp[1].Descriptor.ShaderRegister = 1;
	sp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	sp[2].Constants = {2, 0, 2};
	sp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	sp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	sp[4].DescriptorTable = {1, &stepRange};
	D3D12_ROOT_SIGNATURE_DESC sd = {};
	sd.NumParameters = 5;
	sd.pParameters = sp;
	if (!serializeRootSig(sd, m_particleStepRS)) { return false; }

	D3D12_DESCRIPTOR_RANGE drawRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0};
	D3D12_ROOT_PARAMETER dp[7] = {};
	const UINT cbRegs[3] = {0, 1, 3};
	const int cbSlots[3] = {0, 1, 4};
	for (int i = 0; i < 3; ++i)
	{
		dp[cbSlots[i]].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		dp[cbSlots[i]].Descriptor.ShaderRegister = cbRegs[i];
	}
	dp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	dp[2].Descriptor.ShaderRegister = 0;
	dp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	dp[3].DescriptorTable = {1, &drawRange};
	dp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	dp[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	dp[5].Descriptor.ShaderRegister = 3;
	dp[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	dp[6].Descriptor.ShaderRegister = 4;
	D3D12_STATIC_SAMPLER_DESC samp = {};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.MaxLOD = D3D12_FLOAT32_MAX;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC dd = {};
	dd.NumParameters = 7;
	dd.pParameters = dp;
	dd.NumStaticSamplers = 1;
	dd.pStaticSamplers = &samp;
	return serializeRootSig(dd, m_particleDrawRS);
}

[[nodiscard]] bool createParticleStepPso()
{
	const std::string src = std::string(DX12_PARTICLE_COMMON_HLSL) + DX12_PARTICLE_STEP_CS;
	ComPtr<ID3DBlob> cs, err;
	if (FAILED(gfx::compileDx12Shader(src.c_str(), "CSMain", "cs_5_0", 0, cs.GetAddressOf(), err.GetAddressOf())))
	{
		if (err) { std::fprintf(stderr, "[mitiru][particles] %s\n", static_cast<const char*>(err->GetBufferPointer())); }
		return false;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_particleStepRS.Get();
	pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	return SUCCEEDED(m_d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(m_particleStepPSO.ReleaseAndGetAddressOf())));
}

/// @brief 描画の PSO の共通部。深度は束縛しない (PS が深度を読んで隠れを決める)
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePsoDesc(const gfx::Dx12Shader& vs, const gfx::Dx12Shader& ps) const
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_particleDrawRS.Get();
	pd.VS = vs.shaderBytecode();
	pd.PS = ps.shaderBytecode();
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pd.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	return pd;
}

[[nodiscard]] bool createParticleDrawPsos()
{
	const std::string src = std::string(DX12_PARTICLE_COMMON_HLSL) + DX12_PARTICLE_DRAW_HLSL;
	const auto vs = gfx::Dx12Shader::createVertexShader(src, "VSMain");
	const auto add = gfx::Dx12Shader::createPixelShader(src, "PSAdditive");
	const auto alpha = gfx::Dx12Shader::createPixelShader(src, "PSAlpha");
	const auto reactive = gfx::Dx12Shader::createPixelShader(src, "PSReactive");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = particlePsoDesc(vs, add);
	auto& rt = pd.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = rt.DestBlend = D3D12_BLEND_ONE;
	rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_particleAdditivePSO.ReleaseAndGetAddressOf()))))
	{
		return false;
	}

	pd = particlePsoDesc(vs, alpha);
	dx12::WeightedBlendedOIT::fillAccumulateBlend(pd.BlendState);
	pd.NumRenderTargets = 2;
	pd.RTVFormats[0] = dx12::WeightedBlendedOIT::accumFormat();
	pd.RTVFormats[1] = dx12::WeightedBlendedOIT::revealFormat();
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_particleAlphaPSO.ReleaseAndGetAddressOf()))))
	{
		return false;
	}

	// 反応マスク (FSR の間だけ使う。作れなくても粒は描く)
	pd = particlePsoDesc(vs, reactive);
	pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	pd.SampleDesc.Count = 1;
	auto& rr = pd.BlendState.RenderTarget[0];
	rr.BlendEnable = TRUE;
	rr.SrcBlend = rr.DestBlend = rr.SrcBlendAlpha = rr.DestBlendAlpha = D3D12_BLEND_ONE;
	rr.BlendOp = rr.BlendOpAlpha = D3D12_BLEND_OP_MAX;
	rr.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_particleReactivePSO.ReleaseAndGetAddressOf()))))
	{
		m_particleReactivePSO.Reset();
	}
	return true;
}

[[nodiscard]] bool createParticleHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kParticleHeapPerFrame * FRAME_COUNT * kViewPassCount;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_particleHeap.ReleaseAndGetAddressOf())));
}

/// @brief このフレームと今のビューの区画に深度・法線・VFX の色を書き、刻みの表と描画の表の先頭を返す
void writeParticleViews(D3D12_GPU_DESCRIPTOR_HANDLE& stepTable, D3D12_GPU_DESCRIPTOR_HANDLE& drawTable)
{
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT region = (m_frameCursor % FRAME_COUNT) * kViewPassCount + static_cast<UINT>(currentPass());
	const UINT base = region * kParticleHeapPerFrame;
	auto cpu = m_particleHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(base) * inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R32_FLOAT;
	m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &sv, cpu);
	m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &sv, nextDescriptor(cpu, 2));
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_d3dDevice->CreateShaderResourceView(m_normalBuffer.Get(), &sv, nextDescriptor(cpu, 1));
	D3D12_SHADER_RESOURCE_VIEW_DESC tv = {};
	tv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	tv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	tv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	tv.Texture2DArray.MipLevels = kVfxMipCount;
	tv.Texture2DArray.ArraySize = static_cast<UINT>(kVfxMaxLayers);
	m_d3dDevice->CreateShaderResourceView(m_vfxTexture.Get(), &tv, nextDescriptor(cpu, 3));
	stepTable = m_particleHeap->GetGPUDescriptorHandleForHeapStart();
	stepTable.ptr += static_cast<UINT64>(base) * inc;
	drawTable = stepTable;
	drawTable.ptr += static_cast<UINT64>(2) * inc;
}
