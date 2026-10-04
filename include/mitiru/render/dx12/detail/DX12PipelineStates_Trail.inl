// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 剣筋の帯。drawTrail は点を写して積むだけで、帯の頂点は endFrame のパスで upload ring へ直に作る。
// 副ビューの間に積んだ筋はそのビューにだけ出る。

public:

/// @brief 剣筋やダッシュの筋を積む。点は呼び出しの間だけ読み、写しを持つ
/// @details 1 フレームに積める点は kMaxTrailPointsPerFrame まで。超えた筋は描かない (warnOnce)。
///          帯は endFrame の時点のカメラへ向ける。beginFrame より前に積んだ筋は捨てる。
void drawTrail(std::span<const TrailPoint> points, const TrailStyle& style = {})
{
	if (!m_frameActive || points.size() < 2) { return; }
	if (m_trailPoints.size() + points.size() > kMaxTrailPointsPerFrame)
	{
		debug::warnOnce("dx12.trail.budget", "剣筋の点は 1 フレームに " + std::to_string(kMaxTrailPointsPerFrame) +
		                                         " 個までなので、超えた分の筋は描きません。drawTrail に渡す点を減らしてください。");
		return;
	}
	m_trailBatches.push_back({static_cast<uint32_t>(m_trailPoints.size()), static_cast<uint32_t>(points.size()), style,
	                          currentPass()});
	m_trailPoints.insert(m_trailPoints.end(), points.begin(), points.end());
}

/// @brief ゲーム DLL の Screen::drawTrail から来る形 (ABI v48)。上と同じ上限で、写しへ直に変換して積む
void drawTrail(const TrailPointPod* points, int count, const TrailStylePod& style) override
{
	if (!m_frameActive || points == nullptr || count < 2) { return; }
	const auto n = static_cast<std::size_t>(count);
	if (m_trailPoints.size() + n > kMaxTrailPointsPerFrame)
	{
		debug::warnOnce("dx12.trail.budget", "剣筋の点は 1 フレームに " + std::to_string(kMaxTrailPointsPerFrame) +
		                                         " 個までなので、超えた分の筋は描きません。drawTrail に渡す点を減らしてください。");
		return;
	}
	m_trailBatches.push_back({static_cast<uint32_t>(m_trailPoints.size()), static_cast<uint32_t>(n), toTrailStyle(style),
	                          currentPass()});
	for (std::size_t i = 0; i < n; ++i) { m_trailPoints.push_back(toTrailPoint(points[i])); }
}

private:

static constexpr std::size_t kMaxTrailPointsPerFrame = 16384;

struct TrailBatch
{
	uint32_t   first = 0;
	uint32_t   count = 0;
	TrailStyle style;
	int        pass = 0;   ///< 積んだビュー (0 = 主ビュー)
};
std::vector<TrailPoint>     m_trailPoints;
std::vector<TrailBatch>     m_trailBatches;
ComPtr<ID3D12RootSignature> m_trailRootSig;
ComPtr<ID3D12PipelineState> m_trailAdditivePSO;
ComPtr<ID3D12PipelineState> m_trailAlphaPSO;

void createTrailPipelines()
{
	m_trailPoints.reserve(1024);
	m_trailBatches.reserve(64);
	m_trailDrawn.reserve(64);
	D3D12_ROOT_PARAMETER prm = {};
	prm.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	prm.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 1;
	rd.pParameters = &prm;
	rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	if (!serializeRootSig(rd, m_trailRootSig)) { return; }

	const auto vs = gfx::Dx12Shader::createVertexShader(DX12_TRAIL_HLSL, "VSMain");
	const auto ps = gfx::Dx12Shader::createPixelShader(DX12_TRAIL_HLSL, "PSMain");
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
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pd.SampleDesc.Count = MSAA_SAMPLE_COUNT;
	auto& rt = pd.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
	                           D3D12_COLOR_WRITE_ENABLE_BLUE;
	rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.SrcBlend = rt.DestBlend = D3D12_BLEND_ONE;
	rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_trailAdditivePSO.ReleaseAndGetAddressOf()))))
	{
		m_trailAdditivePSO.Reset();
	}
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_trailAlphaPSO.ReleaseAndGetAddressOf()))))
	{
		m_trailAlphaPSO.Reset();
	}
}

/// @brief 今のビューに積んだ筋を MSAA の HDR 色へ描く。不透明と半透明の後、resolve の前 (深度が DEPTH_WRITE) で呼ぶ。
///        FSR の間は同じ帯を反応マスクへも描く。積んだ筋は副ビューの仕上げの後に clearTrails で捨てる
void drawTrailPass()
{
	const int pass = currentPass();
	const bool any = std::any_of(m_trailBatches.begin(), m_trailBatches.end(),
	                             [pass](const TrailBatch& b) { return b.pass == pass; });
	if (!any || !m_trailRootSig || !m_msaaColorRtvHeap || !m_dsvHeap) { return; }
	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	setFullViewport();
	cl->SetGraphicsRootSignature(m_trailRootSig.Get());
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	for (const TrailBatch& batch : m_trailBatches)
	{
		if (batch.pass == pass) { drawTrailBatch(batch); }
	}
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	drawTrailReactive();
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}

void drawTrailBatch(const TrailBatch& batch)
{
	ID3D12PipelineState* pso =
		(batch.style.blend == TrailBlend::Additive) ? m_trailAdditivePSO.Get() : m_trailAlphaPSO.Get();
	if (pso == nullptr) { return; }
	const std::size_t vertexCount = static_cast<std::size_t>(batch.count) * 2;
	auto verts = m_uploadRing.allocate(vertexCount * sizeof(TrailVertex), 16);
	if (!verts.valid()) { return; }
	const std::span<const TrailPoint> points(m_trailPoints.data() + batch.first, batch.count);
	const std::size_t written = buildTrailRibbon(
		points, m_cameraPosition, batch.style, std::span(static_cast<TrailVertex*>(verts.cpuPtr), vertexCount));
	if (written < 4) { return; }

	struct alignas(256) CbTrail
	{
		float viewProj[4][4];
		float edgeSoftness;
		float additive;
		float maxReactive;
		float pad;
	};
	CbTrail cb{};
	toColumnMajor(cb.viewProj, m_projMatrix * m_viewMatrix);
	cb.edgeSoftness = batch.style.edgeSoftness;
	cb.additive = (batch.style.blend == TrailBlend::Additive) ? 1.0f : 0.0f;
	cb.maxReactive = kFsrMaxReactive;
	const auto c = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!c.valid()) { return; }

	auto* cl = m_graphicsCmdList.Get();
	cl->SetPipelineState(pso);
	cl->SetGraphicsRootConstantBufferView(0, c.gpuAddr);
	const D3D12_VERTEX_BUFFER_VIEW vbv{verts.gpuAddr, static_cast<UINT>(written * sizeof(TrailVertex)),
	                                   sizeof(TrailVertex)};
	cl->IASetVertexBuffers(0, 1, &vbv);
	cl->DrawInstanced(static_cast<UINT>(written), 1, 0, 0);
	++m_drawCallCount;
	if (fsrActive()) { m_trailDrawn.push_back({vbv, c.gpuAddr}); }
}

void clearTrails() noexcept
{
	m_trailPoints.clear();
	m_trailBatches.clear();
	m_trailDrawn.clear();
}
