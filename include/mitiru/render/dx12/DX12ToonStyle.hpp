// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。
// 描画ごとのトゥーンの段・線と止め絵 (ADR 0079)。段は描画ごとの CbDrawEx に載せ、線と止め絵は法線 RT の a に詰めた
// 描画の印から、その描画の線の指定と「色のまま残すか」を引く。印を 1 つでも使ったフレームは、場面の線も含めて
// 種と線の 2 パスで引く (種は境の手前側の画素、線は種からの距離で覆う割合を決める)。使わないフレームは今までの線のまま。

public:

void setToonStyle(const ToonStyle* style) override
{
	if (style == nullptr)
	{
		m_toonDrawRamp = ToonRampGpu{};
		m_toonDrawAccent = ToonAccentGpu{};
		m_toonDrawMetal = ToonMetalGpu{};
		m_toonDrawStyled = false;
		return;
	}
	const int slot = m_toonSlots.slotFor(*style);
	if (m_toonSlots.overflowed())
	{
		debug::warnOnce("dx12.toon.slots", "線の指定は 1 フレームに 7 個までなので、超えた分は場面の線で描きます");
	}
	m_toonDrawRamp = makeToonRampGpu(*style, slot);
	m_toonDrawAccent = makeToonAccentGpu(*style);
	m_toonDrawMetal = (style->bands >= 1) ? makeToonMetalGpu(*style) : ToonMetalGpu{};
	if (style->bands == 0 && style->metal.amount > 0.0f)
	{
		debug::verboseOnce("dx12.toon.metal.bands",
		                   "金属は段のあるトゥーン (bands 1 以上) にだけ効きます");
	}
	m_toonDrawStyled = true;
}

void setImpactFrame(const ImpactFrame& frame) override { m_impactFrame = clampImpactFrame(frame); }

/// @brief このフレームに使った線の印の数 (場面の印 0 を含む)。診断とテスト用
[[nodiscard]] int toonSlotCount() const noexcept { return m_toonSlots.count(); }

private:

/// 線の種を探す深さの段差 (割合) と、折れ目とみなす面の向きの差 (cos)
static constexpr float kToonLineDepthStep = 0.04f;
static constexpr float kToonLineCreaseCos = 0.8f;
static constexpr int kToonLineMaxRadius = 8;

ToonSlotTable  m_toonSlots;
ToonRampGpu    m_toonDrawRamp{};
ToonAccentGpu  m_toonDrawAccent{};
ToonMetalGpu   m_toonDrawMetal{};
bool           m_toonDrawStyled = false;
ImpactFrame    m_impactFrame{};
bool           m_clodStyleSeen = false;   ///< このフレームに clod の物を積んだか
ToonRampGpu    m_clodStyleRamp{};         ///< 最初に clod の物を積んだ時の段と印

ComPtr<ID3D12RootSignature>  m_toonLineRS;
ComPtr<ID3D12PipelineState>  m_toonSeedPSO;
ComPtr<ID3D12PipelineState>  m_toonLinePSO;
ComPtr<ID3D12DescriptorHeap> m_toonLineHeap;   ///< フレームごとに [深度, 法線, 種]
ComPtr<ID3D12DescriptorHeap> m_toonSeedRtvHeap;
gfx::GpuResource             m_toonSeed;        ///< 種 (R16G16: 距離と印)。主ビューの内部の大きさ
UINT                         m_toonSeedW = 0;
UINT                         m_toonSeedH = 0;
ComPtr<ID3D12RootSignature>  m_impactRS;
ComPtr<ID3D12PipelineState>  m_impactPSO;
ComPtr<ID3D12DescriptorHeap> m_impactHeap;     ///< フレームごとに [出力の写し, 法線]

void beginFrameToonStyle() noexcept
{
	m_toonSlots.reset();
	m_toonDrawRamp = ToonRampGpu{};
	m_toonDrawAccent = ToonAccentGpu{};
	m_toonDrawMetal = ToonMetalGpu{};
	m_toonDrawStyled = false;
	m_impactFrame = ImpactFrame{};
	m_clodStyleSeen = false;
	m_clodStyleRamp = ToonRampGpu{};
}

void writeDrawToon(DX12CbDrawEx& cb) const noexcept
{
	cb.toonRamp = m_toonDrawRamp;
	cb.toonAccent = m_toonDrawAccent;
	cb.toonMetal = m_toonDrawMetal;
}

/// @brief clod の物を積んだ時の段と印を覚える。clod は 1 回の resolve で全部を塗るので、フレームの最初の指定を使う
void noteClodToonStyle()
{
	if (!m_clodStyleSeen)
	{
		m_clodStyleSeen = true;
		m_clodStyleRamp = m_toonDrawRamp;
		return;
	}
	if (std::memcmp(&m_clodStyleRamp, &m_toonDrawRamp, sizeof(ToonRampGpu)) != 0)
	{
		debug::verboseOnce("dx12.toon.clod", "clod の物は 1 フレームに 1 つの段と線で描くので、最初の drawModel の時の toonStyle3D を使います。");
	}
}

/// @brief clod の resolve に渡す段。描画ごとの段が無ければ場面の段を同じ形にして渡す
[[nodiscard]] ToonRampGpu clodToonRamp() const noexcept
{
	if (m_clodStyleRamp.params[0] > 0.5f) { return m_clodStyleRamp; }
	ToonRampGpu g = sceneToonRampGpu(m_toonBands, m_toonSoftness, linearColor(m_toonShadowTint), linearColor(m_toonMidTint));
	g.params[3] = m_clodStyleRamp.params[3];
	return g;
}

[[nodiscard]] bool toonLinesActive() const noexcept
{
	return m_toonSlots.count() > 1 && m_toonSeedPSO && m_toonLinePSO && m_toonLineHeap;
}

void createToonPipelines()
{
	if (!m_outlinePostVS) { return; }
	try
	{
		createToonLinePipelines();
		createImpactFramePipeline();
	}
	catch (const std::exception& e)
	{
		console::verbosef("トゥーンの線と止め絵のシェーダーを作れなかったので、今までの線で描きます (%s)。", e.what());
		m_toonSeedPSO.Reset();
		m_impactPSO.Reset();
	}
}

void createToonLinePipelines()
{
	D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[0].DescriptorTable = {1, &range};
	p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = p;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3 * FRAME_COUNT, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
	D3D12_DESCRIPTOR_HEAP_DESC rh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
	if (!serializeRootSig(rd, m_toonLineRS) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_toonLineHeap.ReleaseAndGetAddressOf()))) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&rh, IID_PPV_ARGS(m_toonSeedRtvHeap.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	const std::string common = std::string(DX12_TOON_SLOT_HLSL) + DX12_TOON_LINE_COMMON_HLSL;
	const auto seed = gfx::Dx12Shader::createPixelShader(common + DX12_TOON_LINE_SEED_PS, "PSMain");
	const auto line = gfx::Dx12Shader::createPixelShader(common + DX12_TOON_LINE_DRAW_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_toonLineRS.Get(), seed);
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_toonSeedPSO.ReleaseAndGetAddressOf()))))
	{
		m_toonSeedPSO.Reset();
		return;
	}
	pd = fullscreenPsoDesc(m_toonLineRS.Get(), line);
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	// 出力は lerp(下の色, 線の色, a)。a に線の色の割合を掛けてあるので、色を読まずに下の色と混ぜられる
	auto& rt = pd.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_ZERO;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_toonLinePSO.ReleaseAndGetAddressOf()))))
	{
		m_toonLinePSO.Reset();
	}
}

void createImpactFramePipeline()
{
	D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[1].DescriptorTable = {1, &range};
	p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = p;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 * FRAME_COUNT, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
	if (!serializeRootSig(rd, m_impactRS) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_impactHeap.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	const auto ps = gfx::Dx12Shader::createPixelShader(std::string(DX12_TOON_SLOT_HLSL) + DX12_IMPACT_FRAME_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_impactRS.Get(), ps);
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_impactPSO.ReleaseAndGetAddressOf()))))
	{
		m_impactPSO.Reset();
	}
}

/// @brief 種の RT を主ビューの内部の大きさに合わせる。古い RT は GPU が読み終わるまで一時資源として持つ
[[nodiscard]] bool ensureToonSeed(UINT w, UINT h)
{
	if (m_toonSeed && m_toonSeedW == w && m_toonSeedH == h) { return true; }
	if (m_toonSeed) { m_frameTempResources.push_back(std::move(m_toonSeed)); }
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                  nullptr, m_toonSeed)) || !m_toonSeed)
	{
		m_toonSeed.Reset();
		return false;
	}
	m_toonSeed->SetName(L"Renderer3D toon line seeds");
	m_d3dDevice->CreateRenderTargetView(m_toonSeed.Get(), nullptr, m_toonSeedRtvHeap->GetCPUDescriptorHandleForHeapStart());
	m_toonSeedW = w;
	m_toonSeedH = h;
	return true;
}

/// @brief 印 0 の線 (場面の線)。今までの線は 1 画素ずらした所と比べるので、太さはずらす幅の 2 倍の画素になる
[[nodiscard]] ToonLineSlot sceneToonLine() const noexcept
{
	ToonLineSlot l;
	if (!m_config.enableOutline || m_outlineMode == OutlineMode::Fresnel) { return l; }
	l.width = 2.0f * static_cast<float>(std::max(static_cast<int>(m_outlineWidthPx), 1));
	l.farWidth = l.width;
	l.mix = std::clamp(m_outlineDarken, 0.0f, 1.0f);
	l.fadeStart = m_outlineFadeNear;
	l.fadeEnd = m_outlineFadeFar;
	l.fadeMin = m_outlineFadeMin;
	return l;
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadToonLineCB()
{
	struct alignas(256) CbToonLine
	{
		float screen[4];
		float depth[4];
		float color[kToonSlotCount][4];
		float width[kToonSlotCount][4];
		float fade[kToonSlotCount][4];
	};
	CbToonLine cb{};
	const float scale = m_config.viewportHeight / 1080.0f;
	float widest = 0.0f;
	for (int i = 0; i < m_toonSlots.count(); ++i)
	{
		const ToonLineSlot l = (i == 0) ? sceneToonLine() : m_toonSlots.slot(i);
		const float k = (i == 0) ? 1.0f : scale;   // 場面の線は今までどおり今の画面の画素で数える
		for (int c = 0; c < 3; ++c) { cb.color[i][c] = l.color[c]; }
		cb.color[i][3] = l.mix;
		cb.width[i][0] = l.width * k;
		cb.width[i][1] = l.farWidth * k;
		cb.width[i][2] = l.farDistance;
		cb.fade[i][0] = l.fadeStart;
		cb.fade[i][1] = l.fadeEnd;
		cb.fade[i][2] = l.fadeMin;
		widest = std::max({widest, cb.width[i][0], cb.width[i][1]});
	}
	cb.screen[0] = m_config.viewportWidth;
	cb.screen[1] = m_config.viewportHeight;
	cb.screen[3] = static_cast<float>(std::min(static_cast<int>(std::ceil(widest * 0.5f + 0.5f)), kToonLineMaxRadius));
	cb.depth[0] = m_clodCamera.nearClip();
	cb.depth[1] = m_clodCamera.farClip();
	cb.depth[2] = kToonLineDepthStep;
	cb.depth[3] = kToonLineCreaseCos;
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief このフレームの区画に深度・法線・種の SRV を書き、表の先頭を返す
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE writeToonLineViews()
{
	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT base = (m_frameCursor % FRAME_COUNT) * 3;
	auto cpu = m_toonLineHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(base) * inc;
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R32_FLOAT;
	m_d3dDevice->CreateShaderResourceView(m_depthBuffer.Get(), &sv, cpu);
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_d3dDevice->CreateShaderResourceView(m_normalBuffer.Get(), &sv, nextDescriptor(cpu, 1));
	m_d3dDevice->CreateShaderResourceView(m_toonSeed.Get(), nullptr, nextDescriptor(cpu, 2));
	auto gpu = m_toonLineHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(base) * inc;
	return gpu;
}

/// @brief トーンマップの後、AA の前に呼ぶ (今までの線と同じ所)。深度は DEPTH_WRITE、法線は RENDER_TARGET で受けて返す
void drawToonLinePass()
{
	auto* bb = sceneColorTarget();
	const UINT w = static_cast<UINT>(m_config.viewportWidth);
	const UINT h = static_cast<UINT>(m_config.viewportHeight);
	if (bb == nullptr || !ensureToonSeed(w, h)) { return; }
	const auto cb = uploadToonLineCB();
	if (cb == 0) { return; }
	const auto table = writeToonLineViews();
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_normalBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_toonSeed.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_toonLineRS.Get());
	ID3D12DescriptorHeap* heaps[] = {m_toonLineHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, table);
	cl->SetGraphicsRootConstantBufferView(1, cb);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const auto seedRtv = m_toonSeedRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &seedRtv, FALSE, nullptr);
	cl->SetPipelineState(m_toonSeedPSO.Get());
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_toonSeed.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto out = bb->rtvHandle();
	cl->OMSetRenderTargets(1, &out, FALSE, nullptr);
	cl->SetPipelineState(m_toonLinePSO.Get());
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_normalBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}

/// @brief 線のパス。印を使ったフレームは種と線の 2 パス、使わないフレームは今までの線
void drawOutlinePasses()
{
	if (toonLinesActive())
	{
		drawToonLinePass();
		return;
	}
	if (m_config.enableOutline && m_outlinePostPSO && m_depthSRVHeap) { drawPostProcessOutline(); }
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadImpactCB(const ImpactFrame& f)
{
	struct alignas(256) CbImpact
	{
		float light[4];
		float dark[4];
		float params[4];
		float keep[2][4];
	};
	CbImpact cb{};
	for (int i = 0; i < 3; ++i)
	{
		cb.light[i] = f.lightColor[i];
		cb.dark[i] = f.darkColor[i];
	}
	cb.light[3] = f.threshold;
	cb.dark[3] = f.amount;
	cb.params[0] = static_cast<float>(f.mode);
	cb.params[1] = m_config.viewportWidth;
	cb.params[2] = m_config.viewportHeight;
	const std::uint32_t keep = m_toonSlots.keepMask();
	for (int i = 0; i < kToonSlotCount; ++i) { cb.keep[i / 4][i % 4] = ((keep >> i) & 1u) ? 1.0f : 0.0f; }
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief アップスケールの後、当たりの演出の前に呼ぶ。出力 (RENDER_TARGET) を写して止め絵を掛け、書き戻す
void drawImpactFramePass()
{
	const ImpactFrame f = m_impactFrame;
	m_impactFrame = ImpactFrame{};
	auto* bb = (m_device != nullptr) ? m_device->currentBackBuffer() : nullptr;
	if (!impactFrameActive(f) || !m_impactPSO || bb == nullptr || bb->nativeResource() == nullptr) { return; }
	const D3D12_RESOURCE_DESC bbDesc = bb->nativeResource()->GetDesc();
	if (bbDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || !ensureHitFeelCopy(bbDesc)) { return; }
	const auto cb = uploadImpactCB(f);
	if (cb == 0) { return; }
	copyBackBufferForHitFeel(bb->nativeResource());

	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT base = (m_frameCursor % FRAME_COUNT) * 2;
	auto cpu = m_impactHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(base) * inc;
	m_d3dDevice->CreateShaderResourceView(m_hitFeelCopy.Get(), nullptr, cpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_d3dDevice->CreateShaderResourceView(m_normalBuffer.Get(), &sv, nextDescriptor(cpu, 1));
	auto gpu = m_impactHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(base) * inc;

	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_normalBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = bb->rtvHandle();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(bbDesc.Width), static_cast<float>(bbDesc.Height), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(bbDesc.Width), static_cast<LONG>(bbDesc.Height)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_impactRS.Get());
	cl->SetPipelineState(m_impactPSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_impactHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootConstantBufferView(0, cb);
	cl->SetGraphicsRootDescriptorTable(1, gpu);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_normalBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}
