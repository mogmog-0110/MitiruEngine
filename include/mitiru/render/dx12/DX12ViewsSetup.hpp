// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12Views.hpp の後に include)。副ビューの描画先と貼り付けの PSO
//
// 描画先の形式と標本数は主ビューと同じにして、forward の PSO (DX12PipelineStates_ForwardRender.inl) をそのまま使う。

/// @brief 副ビューの LDR を出力画面へそのまま貼る PS (tonemap の root sig と全画面三角形の VS を使う)。
///        LDR は sRGB の値が入っていて、_UNORM で読んで _UNORM の backbuffer へ書くので値は変わらない
static constexpr const char* kViewCompositePs = R"hlsl(
Texture2D<float4> g_view : register(t0);
SamplerState      g_samp : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(g_view.Sample(g_samp, input.TexCoord).rgb, 1.0);
}
)hlsl";

void ensureViewCompositePipeline()
{
	if (m_viewCompositePSO || !m_tonemapVS || !m_tonemapRootSig) { return; }
	try
	{
		m_viewCompositePS = gfx::Dx12Shader::createPixelShader(kViewCompositePs, "PSMain");
	}
	catch (const std::exception&)
	{
		debug::warnOnce("dx12.view.composite", "副ビューの貼り付けのシェーダーを作れない — compositeView は描かない");
		return;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_tonemapRootSig.Get();
	pd.VS = m_tonemapVS->shaderBytecode();
	pd.PS = m_viewCompositePS->shaderBytecode();
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = FALSE;
	pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pd.SampleDesc.Count = 1;
	(void)m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_viewCompositePSO.ReleaseAndGetAddressOf()));
}

[[nodiscard]] bool createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool shaderVisible,
                                  ComPtr<ID3D12DescriptorHeap>& out)
{
	D3D12_DESCRIPTOR_HEAP_DESC d = {};
	d.Type = type;
	d.NumDescriptors = count;
	d.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&d, IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
}

[[nodiscard]] bool createViewTexture(const View3D& v, DXGI_FORMAT format, UINT samples, D3D12_RESOURCE_FLAGS flags,
                                     D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, gfx::GpuResource& out)
{
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = static_cast<UINT64>(v.desc.width);
	d.Height = static_cast<UINT>(v.desc.height);
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = format;
	d.SampleDesc.Count = samples;
	d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	d.Flags = flags;
	return SUCCEEDED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, d, state, clear, out));
}

/// @brief MSAA の色 (FP16)・法線・深度、resolve 先の HDR、tonemap 先の LDR と、それぞれの記述子を作る
[[nodiscard]] bool createViewTargets(View3D& v)
{
	D3D12_CLEAR_VALUE depthClear = {};
	depthClear.Format = DXGI_FORMAT_D32_FLOAT;
	depthClear.DepthStencil.Depth = 1.0f;
	const auto rt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const bool resources =
		createViewTexture(v, DXGI_FORMAT_R16G16B16A16_FLOAT, MSAA_SAMPLE_COUNT, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, v.color) &&
		createViewTexture(v, DXGI_FORMAT_R8G8B8A8_UNORM, MSAA_SAMPLE_COUNT, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, v.normal) &&
		createViewTexture(v, DXGI_FORMAT_R32_TYPELESS, MSAA_SAMPLE_COUNT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		                  D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, v.depth) &&
		createViewTexture(v, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST,
		                  nullptr, v.hdr) &&
		createViewTexture(v, DXGI_FORMAT_R8G8B8A8_TYPELESS, 1, rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, v.ldr);
	const bool heaps =
		resources && createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, v.colorRtv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, v.normalRtv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, v.ldrRtv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, v.dsv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewSrvCount, true, v.srv);
	if (!heaps) { return false; }
	writeViewDescriptors(v);
	v.texture.adopt(v.ldr, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, v.desc.width, v.desc.height);
	return true;
}

void writeViewDescriptors(const View3D& v)
{
	D3D12_RENDER_TARGET_VIEW_DESC color = {};
	color.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	color.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
	m_d3dDevice->CreateRenderTargetView(v.color.Get(), &color, v.colorRtv->GetCPUDescriptorHandleForHeapStart());
	color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_d3dDevice->CreateRenderTargetView(v.normal.Get(), &color, v.normalRtv->GetCPUDescriptorHandleForHeapStart());
	D3D12_RENDER_TARGET_VIEW_DESC ldr = {};
	ldr.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ldr.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateRenderTargetView(v.ldr.Get(), &ldr, v.ldrRtv->GetCPUDescriptorHandleForHeapStart());
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
	m_d3dDevice->CreateDepthStencilView(v.depth.Get(), &dsv, v.dsv->GetCPUDescriptorHandleForHeapStart());

	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpu = v.srv->GetCPUDescriptorHandleForHeapStart();
	const auto srvAt = [&](UINT slot, ID3D12Resource* res, DXGI_FORMAT format) {
		D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = format;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Texture2D.MipLevels = 1;
		D3D12_CPU_DESCRIPTOR_HANDLE h = cpu;
		h.ptr += static_cast<SIZE_T>(slot) * inc;
		m_d3dDevice->CreateShaderResourceView(res, &s, h);
	};
	// 空きの枠は null SRV。tonemap の遮蔽と bloom は CB で切ってあり、読まれない
	srvAt(0, v.hdr.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	srvAt(1, nullptr, DXGI_FORMAT_R32_FLOAT);
	srvAt(2, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
	srvAt(3, v.ldr.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	srvAt(4, nullptr, DXGI_FORMAT_R32_FLOAT);
	srvAt(5, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
}
