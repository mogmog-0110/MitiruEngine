// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// FSR 3.1 で内部解像度の絵を出力へ戻す (ADR 0058)。置き場所は TAAU と同じ (動きのぼけの後、tonemap 済みの LDR)。
// FSR に渡す 1 標本の深度と反応マスクは、半透明の後・剣筋の前に作る。剣筋は自分の帯を反応マスクへ MAX で足す。
// FSR がビルドに無いか作れない時は fsrActive() が false で、TAAU が受け持つ。

public:

/// @brief 直前のフレームの FSR の反応マスク (0..1、内部解像度) を読む。診断とテスト用で GPU の完了を待つ。FSR でなければ空
[[nodiscard]] std::vector<float> readFsrReactiveForDiagnostics()
{
	if (!fsrActive() || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	const auto raw = gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), m_fsrReactive.Get(),
	                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 1);
	std::vector<float> out(raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i) { out[i] = static_cast<float>(raw[i]) / 255.0f; }
	return out;
}

private:

/// 反応マスクの上限。1 にすると履歴を全く使わず、その画素だけ揺れる
static constexpr float kFsrMaxReactive = 0.9f;
/// FSR に渡すフレームの間隔。実時間を渡すと同じ入力から同じ絵にならない
static constexpr float kFsrFrameTimeMs = 1000.0f / 60.0f;

dx12::Fsr3Upscaler           m_fsr;
gfx::GpuResource             m_fsrDepth;      ///< 内部解像度の 1 標本の深度 (R32、DSV にもなる)
gfx::GpuResource             m_fsrReactive;   ///< 内部解像度の反応マスク (R8)
gfx::GpuResource             m_fsrOutput;     ///< 出力解像度 (RGBA8、UAV)。鮮鋭化の PSO でバックバッファへ写す
ComPtr<ID3D12DescriptorHeap> m_fsrRtvHeap;
ComPtr<ID3D12DescriptorHeap> m_fsrDsvHeap;
ComPtr<ID3D12PipelineState>  m_fsrInputsPSO;
ComPtr<ID3D12PipelineState>  m_trailReactivePSO;

struct TrailDrawn
{
	D3D12_VERTEX_BUFFER_VIEW  vbv{};
	D3D12_GPU_VIRTUAL_ADDRESS cb = 0;
};
std::vector<TrailDrawn> m_trailDrawn;   ///< このフレームに描いた帯 (反応マスクへもう一度描く)

/// @brief FSR の入力と反応マスクを作るか。FSR の資源は主ビューの大きさなので、副ビューの仕上げの間は作らない
[[nodiscard]] bool fsrActive() const noexcept
{
	return m_activeView == nullptr && m_fsr.ready() && m_fsrOutput && m_fsrInputsPSO;
}

[[nodiscard]] bool ensureFsrPipelines()
{
	if (m_fsrInputsPSO && m_trailReactivePSO) { return true; }
	const auto inputs = gfx::Dx12Shader::createPixelShader(DX12_FSR_INPUTS_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_temporalRootSig.Get(), inputs);
	pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_fsrInputsPSO.ReleaseAndGetAddressOf()))))
	{
		m_fsrInputsPSO.Reset();
		return false;
	}
	return createTrailReactivePSO();
}

/// @brief 剣筋の帯を反応マスクへ描く PSO。色の PSO と同じ VS と深度テストで、被覆率を MAX で重ねる
[[nodiscard]] bool createTrailReactivePSO()
{
	if (!m_trailRootSig) { return false; }
	const auto vs = gfx::Dx12Shader::createVertexShader(DX12_TRAIL_HLSL, "VSMain");
	const auto ps = gfx::Dx12Shader::createPixelShader(DX12_TRAIL_HLSL, "PSReactive");
	const D3D12_INPUT_ELEMENT_DESC layout[3] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_trailRootSig.Get();
	pd.VS = vs.shaderBytecode();
	pd.PS = ps.shaderBytecode();
	pd.InputLayout = {layout, 3};
	pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pd.RasterizerState.DepthClipEnable = TRUE;
	pd.DepthStencilState.DepthEnable = TRUE;
	pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pd.SampleMask = UINT_MAX;
	pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pd.NumRenderTargets = 1;
	pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = 1;
	auto& rt = pd.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = rt.DestBlend = rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_MAX;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
	return SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_trailReactivePSO.ReleaseAndGetAddressOf())));
}

void releaseFsrResources() noexcept
{
	m_fsr.destroy();
	m_fsrDepth.Reset();
	m_fsrReactive.Reset();
	m_fsrOutput.Reset();
}

/// @brief FSR を選んでいれば文脈と入出力を作る。作れなければ理由を 1 回だけ出し、TAAU に任せる (GPU は待った後で呼ぶ)
[[nodiscard]] bool createFsrResources(UINT iw, UINT ih, UINT ow, UINT oh)
{
	releaseFsrResources();
	if (m_upscaler != Upscaler3D::Fsr3) { return false; }
	if (!ensureFsrPipelines() || !m_fsr.create(m_d3dDevice, iw, ih, ow, oh))
	{
		debug::warnOnce("dx12.fsr3.unavailable", "FSR 3.1 を使えない — TAAU で戻す: " +
		                                             (m_fsr.error().empty() ? std::string("PSO を作れない") : m_fsr.error()));
		releaseFsrResources();
		return false;
	}
	const bool ok =
		createTemporalTexture(iw, ih, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		                      L"Renderer3D FSR depth", m_fsrDepth) &&
		createTemporalTexture(iw, ih, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
		                      L"Renderer3D FSR reactive", m_fsrReactive) &&
		createFsrOutput(ow, oh) && createFsrHeaps();
	if (!ok)
	{
		debug::warnOnce("dx12.fsr3.resources", "FSR 3.1 の入出力を作れない — TAAU で戻す");
		releaseFsrResources();
		return false;
	}
	writeFsrViews();
	return true;
}

[[nodiscard]] bool createFsrOutput(UINT w, UINT h)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                  nullptr, m_fsrOutput)) || !m_fsrOutput)
	{
		return false;
	}
	m_fsrOutput->SetName(L"Renderer3D FSR output");
	return true;
}

[[nodiscard]] bool createFsrHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv = {};
	rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv.NumDescriptors = 1;
	D3D12_DESCRIPTOR_HEAP_DESC dsv = {};
	dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv.NumDescriptors = 1;
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(m_fsrRtvHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(m_fsrDsvHeap.ReleaseAndGetAddressOf())));
}

void writeFsrViews()
{
	m_d3dDevice->CreateRenderTargetView(m_fsrReactive.Get(), nullptr, m_fsrRtvHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
	dv.Format = DXGI_FORMAT_D32_FLOAT;
	dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateDepthStencilView(m_fsrDepth.Get(), &dv, m_fsrDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

/// @brief 鮮鋭化の PSO が FSR の出力を写す表と、入力を作るパスの表を書く (writeUpscaleViews から)
void writeFsrTables()
{
	constexpr auto kRgba8 = DXGI_FORMAT_R8G8B8A8_UNORM;
	writeUpscaleSrv(kUpscaleTableFsr, 0, m_fsrOutput.Get(), kRgba8);
	for (UINT slot = 1; slot < kTemporalTableSize; ++slot) { writeUpscaleSrv(kUpscaleTableFsr, slot, nullptr, kRgba8); }
	writeUpscaleSrv(kUpscaleTableFsrInputs, 0, m_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, true);
	writeUpscaleSrv(kUpscaleTableFsrInputs, 1, m_oit.revealSampledResource(), dx12::WeightedBlendedOIT::revealFormat());
	for (UINT slot = 2; slot < kTemporalTableSize; ++slot) { writeUpscaleSrv(kUpscaleTableFsrInputs, slot, nullptr, kRgba8); }
}

/// @brief 半透明の後・剣筋の前に呼ぶ。1 標本の深度と半透明の反応マスクを作る (主パスの深度は DEPTH_WRITE で受けて返す)
void drawFsrInputs()
{
	if (!fsrActive()) { return; }
	temporalBarrier(m_fsrDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = m_fsrRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_fsrDsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	const bool oitDrawn = !m_transparentCommands.empty() && m_oitTransparentPSO;
	const float cb[4] = {oitDrawn ? 1.0f : 0.0f, kFsrMaxReactive, 0.0f, 0.0f};
	drawUpscaleFullscreen(m_fsrInputsPSO.Get(), kUpscaleTableFsrInputs, cb, sizeof(cb), m_config.viewportWidth,
	                      m_config.viewportHeight);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_fsrDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

/// @brief このフレームの剣筋を反応マスクへもう一度描く (drawTrailPass が色を描いた後)
void drawTrailReactive()
{
	if (!fsrActive() || !m_trailReactivePSO || m_trailDrawn.empty()) { return; }
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_fsrDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const auto rtv = m_fsrRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_fsrDsvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	setFullViewport();
	cl->SetGraphicsRootSignature(m_trailRootSig.Get());
	cl->SetPipelineState(m_trailReactivePSO.Get());
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	for (const TrailDrawn& d : m_trailDrawn)
	{
		cl->SetGraphicsRootConstantBufferView(0, d.cb);
		cl->IASetVertexBuffers(0, 1, &d.vbv);
		cl->DrawInstanced(d.vbv.SizeInBytes / d.vbv.StrideInBytes, 1, 0, 0);
	}
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	temporalBarrier(m_fsrDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_fsrReactive.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/// @brief FSR で m_fsrOutput へ戻す。内部解像度の LDR は PIXEL_SHADER_RESOURCE で受け取る
[[nodiscard]] bool recordFsr()
{
	const float iw = m_config.viewportWidth;
	const float ih = m_config.viewportHeight;
	constexpr auto kPsr = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	dx12::Fsr3Dispatch d;
	d.commandList = m_graphicsCmdList.Get();
	d.color = {m_sceneLdr.Get(), kPsr};
	d.depth = {m_fsrDepth.Get(), kPsr};
	d.motionVectors = {m_velocityTex.Get(), kPsr};
	d.reactive = {m_fsrReactive.Get(), kPsr};
	d.output = {m_fsrOutput.Get(), kPsr};
	d.jitterX = m_jitterNdc.x * iw * 0.5f;
	d.jitterY = -m_jitterNdc.y * ih * 0.5f;
	d.motionScaleX = iw;
	d.motionScaleY = ih;
	d.renderWidth = static_cast<std::uint32_t>(iw);
	d.renderHeight = static_cast<std::uint32_t>(ih);
	d.sharpness = m_upscaleSharpness;
	d.frameTimeMs = kFsrFrameTimeMs;
	d.cameraNear = m_clodCamera.nearClip();
	d.cameraFar = m_clodCamera.farClip();
	d.fovY = m_clodCamera.fov();
	d.reset = !m_upscaleHistoryValid;
	if (m_fsr.dispatch(d)) { return true; }
	debug::warnOnce("dx12.fsr3.dispatch", "FSR 3.1 の dispatch に失敗 — このフレームは引き伸ばすだけ: " + m_fsr.error());
	return false;
}
