// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// Effekseer のエフェクト (.efkefc /.efk)。ゲームからは drawModel(path, 位置, 回転, 倍率, key, 経過秒)
// で渡される (アニメ付きモデルと同じ「時刻を渡して描く」入口)。ModuleApi にも Screen にも何も足さない。
// 描くのは不透明と半透明 (OIT) の後、MSAA の resolve の前: 深度で壁に隠れ、HDR のまま tonemap される。
// 副ビューの間に積んだエフェクトはそのビューにだけ出る。進めるのは主ビューのパスで 1 回だけ。
// Effekseer は自分の PSO で色だけを書くので、FSR の反応マスクは「描く前に取った色」との差から作る
// (FidelityFX の GenerateReactiveMask と同じ考え方)。差を取るのは FSR を使っていてエフェクトがある時だけ。

/// @brief drawModel に渡されたのが Effekseer のエフェクトか (拡張子で見る)
[[nodiscard]] static bool isEffekseerPath(const char* path) noexcept
{
	if (path == nullptr) { return false; }
	const std::string_view p(path);
	const auto endsWith = [&p](std::string_view ext) {
		return p.size() >= ext.size() && p.substr(p.size() - ext.size()) == ext;
	};
	return endsWith(".efkefc") || endsWith(".efk");
}

/// @return エフェクトとして受け取ったら true (Effekseer の無いビルドでは受け取って捨て、1 度だけ知らせる)
bool queueEffekseer(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale,
                    const char* key, float ageSec)
{
	if (!isEffekseerPath(path)) { return false; }
#if defined(MITIRU_HAS_EFFEKSEER)
	if (m_effekseer)
	{
		m_effekseer->draw(path, position, rotYDeg, scale, key, ageSec, currentPass());
		m_effekseerQueued = m_effekseerQueued || m_activeView == nullptr;
	}
#else
	(void)position; (void)rotYDeg; (void)scale; (void)key; (void)ageSec;
	debug::warnOnceFix("render.effekseer.missing", std::string("Effekseer のエフェクトを描けない: ") + path,
		"このビルドには Effekseer が入っていない", "-DMITIRU_WITH_EFFEKSEER=ON で configure し直す");
#endif
	return true;
}

/// @brief FSR の入力を作った後に呼ぶ。エフェクトが変えた画素を反応マスクへ MAX で重ねる (Effekseer の無いビルドでは何もしない)
void drawEffekseerReactive()
{
#if defined(MITIRU_HAS_EFFEKSEER)
	const bool snapshot = m_efkSnapshotTaken;
	m_efkSnapshotTaken = false;
	if (!snapshot || !fsrActive() || !m_efkReactivePSO) { return; }
	auto* cl = m_graphicsCmdList.Get();
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT base = (m_frameCursor % FRAME_COUNT) * 2;
	auto cpu = m_efkHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(base) * inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	m_d3dDevice->CreateShaderResourceView(m_msaaColorBuffer.Get(), &sv, cpu);
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(m_efkSnapshot.Get(), &sv, nextDescriptor(cpu, 1));
	auto gpu = m_efkHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(base) * inc;

	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto rtv = m_fsrRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setFullViewport();
	cl->SetGraphicsRootSignature(m_efkReactiveRS.Get());
	cl->SetPipelineState(m_efkReactivePSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_efkHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, gpu);
	const float maxReactive = kFsrMaxReactive;
	cl->SetGraphicsRoot32BitConstants(1, 1, &maxReactive, 0);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
#endif
}

#if defined(MITIRU_HAS_EFFEKSEER)
static_assert(fx::EffekseerRuntime::kMaxPasses >= kViewPassCount, "Effekseer の描く先が副ビューの数に足りない");
std::unique_ptr<fx::EffekseerRuntime> m_effekseer;
bool                                  m_effekseerQueued = false;   ///< このフレームに積んだエフェクトがある
bool                                  m_efkSnapshotTaken = false;  ///< 描く前の色を写した (反応マスクを作る)
gfx::GpuResource                      m_efkSnapshot;               ///< 描く前の色 (MSAA を resolve した 1 標本)
ComPtr<ID3D12RootSignature>           m_efkReactiveRS;
ComPtr<ID3D12PipelineState>           m_efkReactivePSO;
ComPtr<ID3D12DescriptorHeap>          m_efkHeap;                   ///< フレームごとに { 今の MSAA の色, 写し }

/// @brief エフェクトの前後の色の差を反応マスクにする PS。tonemap の手前の HDR なので x / (1 + x) で縮めてから比べる
static constexpr const char* kEffekseerReactivePS = R"hlsl(
Texture2DMS<float4, 4> g_after  : register(t0);
Texture2D<float4>      g_before : register(t1);
cbuffer CbReactive : register(b0) { float MaxReactive; };
struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
float4 PSMain(PSInput input) : SV_TARGET
{
    int2 p = int2(input.Position.xy);
    float3 after = (g_after.Load(p, 0).rgb + g_after.Load(p, 1).rgb + g_after.Load(p, 2).rgb + g_after.Load(p, 3).rgb) * 0.25;
    float3 before = g_before.Load(int3(p, 0)).rgb;
    float3 d = abs(after / (1.0 + after) - before / (1.0 + before));
    return min(saturate(max(d.r, max(d.g, d.b)) * 4.0), MaxReactive).xxxx;
}
)hlsl";

/// @brief initialize から (FSR の有無に関わらず作っておく。作れなければ反応マスクに足さないだけ)
void createEffekseerReactivePipeline()
{
	if (!m_outlinePostVS) { return; }
	D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[0].DescriptorTable = {1, &range};
	p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	p[1].Constants = {0, 0, 1};
	p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = p;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = 2 * FRAME_COUNT;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!serializeRootSig(rd, m_efkReactiveRS) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_efkHeap.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	const auto ps = gfx::Dx12Shader::createPixelShader(kEffekseerReactivePS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_efkReactiveRS.Get(), ps);
	pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	auto& rt = pd.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = rt.DestBlend = rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_MAX;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_efkReactivePSO.ReleaseAndGetAddressOf()))))
	{
		m_efkReactivePSO.Reset();
	}
}

/// @brief エフェクトを描く前の色を 1 標本へ resolve して取っておく (MSAA の色は RENDER_TARGET で受けて返す)
void snapshotBeforeEffekseer()
{
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	if (!m_efkSnapshot || m_efkSnapshot->GetDesc().Width != w || m_efkSnapshot->GetDesc().Height != h)
	{
		if (m_efkSnapshot) { m_frameTempResources.push_back(std::move(m_efkSnapshot)); }
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = w;
		desc.Height = h;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc,
		                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, m_efkSnapshot)) || !m_efkSnapshot)
		{
			m_efkSnapshot.Reset();
			return;
		}
	}
	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
	temporalBarrier(m_efkSnapshot.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	m_graphicsCmdList->ResolveSubresource(m_efkSnapshot.Get(), 0, m_msaaColorBuffer.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
	temporalBarrier(m_efkSnapshot.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_efkSnapshotTaken = true;
}

/// @brief initialize から。作れなくても 3D 全体は止めず、エフェクトだけ描かない
void createEffekseerRuntime()
{
	fx::EffekseerTarget target;
	target.colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	target.depthFormat = DXGI_FORMAT_D32_FLOAT;
	target.sampleCount = static_cast<int>(MSAA_SAMPLE_COUNT);
	target.framesInFlight = static_cast<int>(FRAME_COUNT);
	std::string error;
	m_effekseer = fx::EffekseerRuntime::create(m_d3dDevice, m_device->commandQueue(), target, error);
	if (!m_effekseer) { std::fprintf(stderr, "[mitiru][effekseer] 使えない: %s\n", error.c_str()); }
	createEffekseerReactivePipeline();
}

/// @brief endFrame (主ビュー) と副ビューの仕上げから。今のビューの MSAA HDR + depth に束縛し直して記録する
///        (Effekseer は自分の PSO とヒープを積む)。主ビューのパスでエフェクトを進める
void renderEffekseerPass()
{
	const bool queued = m_effekseerQueued && m_activeView == nullptr;
	if (m_activeView == nullptr) { m_effekseerQueued = false; }
	if (!m_effekseer || !m_graphicsCmdList || !m_msaaColorRtvHeap || !m_dsvHeap) { return; }
	if (m_activeView == nullptr) { m_effekseer->advance(); }
	if (queued && fsrActive() && m_efkReactivePSO) { snapshotBeforeEffekseer(); }
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	D3D12_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &vp);
	const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_config.viewportWidth), static_cast<LONG>(m_config.viewportHeight)};
	m_graphicsCmdList->RSSetScissorRects(1, &scissor);

	fx::EffectCamera cam;
	cam.eye = m_clodCamera.position();
	cam.target = m_clodCamera.target();
	cam.up = m_clodCamera.up();
	cam.fovYRad = m_clodCamera.fov();
	cam.aspect = m_clodCamera.aspectRatio();
	cam.nearZ = m_clodCamera.nearClip();
	cam.farZ = m_clodCamera.farClip();
	m_effekseer->render(m_graphicsCmdList.Get(), cam, currentPass());
}
#endif
