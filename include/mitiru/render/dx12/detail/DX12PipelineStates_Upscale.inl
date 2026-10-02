// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 3D の内部解像度と出力解像度の分離 (TAAU / FSR 3.1)。m_config.viewportWidth/Height は内部解像度で、3D の資源は全部
// その大きさで作る。倍率が 1 未満の間 (と NativeAA の間) は tonemap から動きのぼけまでを内部解像度の LDR (m_sceneLdr) に描き、
// TAAU か FSR が出力解像度へ戻し、鮮鋭化してバックバッファへ書く。倍率 1 では m_sceneLdr を作らず、今までと同じ経路を通る。

public:

/// @brief 3D の内部解像度の倍率 (0.5..1.0、1/20 刻み)。1 未満なら AA の方式に関係なく TAAU で出力へ戻す。
///        変えると内部の資源を作り替える (GPU の完了を待つ) ので、毎フレーム細かく揺らす使い方には向かない
void setRenderScale(float scale)
{
	const float q = quantizeRenderScale(scale);
	if (q == m_renderScale && !m_upscaleNativeAA) { return; }
	m_renderScale = q;
	m_upscaleNativeAA = false;
	resetTemporalHistory();
	applyRenderSize();
}

/// @brief FSR と同じ画質の段で内部解像度を決める (Quality 1.5 倍、Balanced 1.7 倍、Performance 2 倍)。
///        NativeAA は倍率 1 のまま upscaler を AA として通し、Off は倍率 1 で通さない
void setUpscaleQuality(UpscaleQuality quality)
{
	const bool native = (quality == UpscaleQuality::NativeAA);
	const float q = quantizeRenderScale(renderScaleFor(quality));
	if (q == m_renderScale && native == m_upscaleNativeAA) { return; }
	m_renderScale = q;
	m_upscaleNativeAA = native;
	resetTemporalHistory();
	applyRenderSize();
}

/// @brief 内部解像度を戻す方法。Fsr3 がビルドに無いか作れない時は TAAU で戻す (activeUpscaler で分かる)
void setUpscaler(Upscaler3D upscaler)
{
	if (upscaler == m_upscaler) { return; }
	m_upscaler = upscaler;
	resetTemporalHistory();
	applyRenderSize();
}

[[nodiscard]] Upscaler3D upscaler() const noexcept { return m_upscaler; }

/// @brief 今戻すのに使っている方法 (upscale していなければ選んだ方法をそのまま返す)
[[nodiscard]] Upscaler3D activeUpscaler() const noexcept
{
	if (!upscaleActive()) { return m_upscaler; }
	return fsrActive() ? Upscaler3D::Fsr3 : Upscaler3D::Taau;
}

[[nodiscard]] float renderScale() const noexcept { return m_renderScale; }

/// @brief 鮮鋭化の強さ 0..1 (TAAU の間だけ効く)。0 で掛けない
void setUpscaleSharpness(float sharpness) noexcept { m_upscaleSharpness = std::clamp(sharpness, 0.0f, 1.0f); }

/// @brief 3D を描いている内部解像度
[[nodiscard]] int internalWidth() const noexcept { return static_cast<int>(m_config.viewportWidth); }
[[nodiscard]] int internalHeight() const noexcept { return static_cast<int>(m_config.viewportHeight); }

/// @brief 内部解像度の絵を TAAU か FSR で出力へ戻しているか (倍率が 1 未満か NativeAA で、資源とパイプラインが揃っている)
[[nodiscard]] bool upscaleActive() const noexcept
{
	return upscaleWanted() && m_sceneLdr && m_taauPSO && m_upscaleSrvHeap;
}

private:

float m_renderScale = 1.0f;
bool  m_upscaleNativeAA = false;
Upscaler3D m_upscaler = Upscaler3D::Taau;
float m_outputWidth = 0.0f;    ///< バックバッファの大きさ (resize の値)
float m_outputHeight = 0.0f;
float m_upscaleSharpness = 0.5f;
gfx::GpuResource             m_sceneLdr;            ///< 内部解像度の LDR (RGBA8)。倍率 1 未満の間だけある
gfx::Dx12RenderTarget        m_sceneLdrTarget;
ComPtr<ID3D12DescriptorHeap> m_sceneLdrRtvHeap;
gfx::GpuResource             m_upscaleHistory[2];   ///< 出力解像度の RGBA16F (TAAU の間だけある)
ComPtr<ID3D12DescriptorHeap> m_upscaleRtvHeap;
ComPtr<ID3D12DescriptorHeap> m_upscaleSrvHeap;      ///< 表 4 枚 × kUpscaleTableCount (番号は kUpscaleTable*)
ComPtr<ID3D12PipelineState>  m_taauPSO;
ComPtr<ID3D12PipelineState>  m_sharpenPSO;
ComPtr<ID3D12PipelineState>  m_upscaleBlitPSO;
int  m_upscaleWrite = 0;
bool m_upscaleHistoryValid = false;
bool m_upscaleTried = false;

static constexpr UINT kUpscaleTableTaau = 0;      ///< 0, 1
static constexpr UINT kUpscaleTableSharpen = 2;   ///< 2, 3
static constexpr UINT kUpscaleTableFsr = 4;          ///< FSR の出力を鮮鋭化の PSO で写す
static constexpr UINT kUpscaleTableFsrInputs = 5;    ///< FSR に渡す深度と反応マスクを作る
static constexpr UINT kUpscaleTableCount = 6;

[[nodiscard]] bool upscaleWanted() const noexcept { return m_renderScale < 1.0f || m_upscaleNativeAA; }

/// @brief 3D の後処理 (tonemap〜動きのぼけ) の描き先。TAAU の間は内部解像度の LDR、それ以外はバックバッファ
[[nodiscard]] gfx::Dx12RenderTarget* sceneColorTarget() noexcept
{
	if (upscaleActive()) { return &m_sceneLdrTarget; }
	return (m_device != nullptr) ? m_device->currentBackBuffer() : nullptr;
}

/// @brief 出力の大きさと倍率から内部解像度を決め、変わったら内部の資源を作り替える
void applyRenderSize()
{
	const auto iw = static_cast<float>(internalRenderExtent(static_cast<int>(m_outputWidth), m_renderScale));
	const auto ih = static_cast<float>(internalRenderExtent(static_cast<int>(m_outputHeight), m_renderScale));
	const bool sizeChanged = (iw != m_config.viewportWidth || ih != m_config.viewportHeight);
	m_config.viewportWidth = iw;
	m_config.viewportHeight = ih;
	if (!m_initialized) { return; }
	if (m_device != nullptr) { m_device->waitForGpu(); }
	if (sizeChanged) { rebuildViewportResources(); }
	createUpscaleResources();
}

[[nodiscard]] bool ensureUpscalePipelines()
{
	if (m_taauPSO && m_sharpenPSO && m_upscaleBlitPSO) { return true; }
	if (m_upscaleTried || !m_temporalRootSig || !m_outlinePostVS) { return false; }
	m_upscaleTried = true;
	const auto taau = gfx::Dx12Shader::createPixelShader(DX12_TAAU_PS, "PSMain");
	const auto sharpen = gfx::Dx12Shader::createPixelShader(DX12_UPSCALE_SHARPEN_PS, "PSMain");
	const auto blit = gfx::Dx12Shader::createPixelShader(DX12_UPSCALE_BLIT_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_temporalRootSig.Get(), taau);
	pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const bool ok = SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_taauPSO.ReleaseAndGetAddressOf())));
	pd = fullscreenPsoDesc(m_temporalRootSig.Get(), sharpen);
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	const bool ok2 = SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_sharpenPSO.ReleaseAndGetAddressOf())));
	pd = fullscreenPsoDesc(m_temporalRootSig.Get(), blit);
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	const bool ok3 = SUCCEEDED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_upscaleBlitPSO.ReleaseAndGetAddressOf())));
	if (!(ok && ok2 && ok3))
	{
		m_taauPSO.Reset();
		debug::warnOnce("dx12.upscale.pipeline", "TAAU のシェーダーを作れない — 内部解像度の絵を引き伸ばして出す");
		return false;
	}
	return true;
}

void releaseUpscaleResources()
{
	m_sceneLdr.Reset();
	m_sceneLdrTarget = gfx::Dx12RenderTarget{};
	m_upscaleHistory[0].Reset();
	m_upscaleHistory[1].Reset();
	m_upscaleHistoryValid = false;
	releaseFsrResources();
}

/// @brief upscale するなら内部解像度の LDR と、FSR の文脈か TAAU の履歴を作る。しないなら捨てる (GPU は待った後で呼ぶ)
void createUpscaleResources()
{
	releaseUpscaleResources();
	if (!upscaleWanted() || !ensureUpscalePipelines()) { return; }
	const UINT iw = static_cast<UINT>(m_config.viewportWidth);
	const UINT ih = static_cast<UINT>(m_config.viewportHeight);
	const UINT ow = static_cast<UINT>(m_outputWidth);
	const UINT oh = static_cast<UINT>(m_outputHeight);
	constexpr auto kRt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const bool fsr = createFsrResources(iw, ih, ow, oh);
	const bool ok = createUpscaleHeaps() && createSceneLdr(iw, ih) &&
		(fsr || (createTemporalTexture(ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D TAAU history 0",
		                               m_upscaleHistory[0]) &&
		         createTemporalTexture(ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D TAAU history 1",
		                               m_upscaleHistory[1])));
	if (!ok)
	{
		releaseUpscaleResources();
		return;
	}
	writeUpscaleViews();
}

[[nodiscard]] bool createUpscaleHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv = {};
	rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv.NumDescriptors = 1;
	D3D12_DESCRIPTOR_HEAP_DESC hist = rtv;
	hist.NumDescriptors = 2;
	D3D12_DESCRIPTOR_HEAP_DESC srv = {};
	srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv.NumDescriptors = kUpscaleTableCount * kTemporalTableSize;
	srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	return SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(m_sceneLdrRtvHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&hist, IID_PPV_ARGS(m_upscaleRtvHeap.ReleaseAndGetAddressOf()))) &&
	       SUCCEEDED(m_d3dDevice->CreateDescriptorHeap(&srv, IID_PPV_ARGS(m_upscaleSrvHeap.ReleaseAndGetAddressOf())));
}

/// @brief 内部解像度の LDR。3D の後処理はバックバッファと同じく RENDER_TARGET の状態で受け取るので、その状態で作る
[[nodiscard]] bool createSceneLdr(UINT w, UINT h)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = desc.Format;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
	                                  &clear, m_sceneLdr)) || !m_sceneLdr)
	{
		return false;
	}
	m_sceneLdr->SetName(L"Renderer3D scene LDR (internal)");
	m_sceneLdrTarget = gfx::Dx12RenderTarget::createFromBackBuffer(
		m_d3dDevice, m_sceneLdr.Get(), m_sceneLdrRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<int>(w),
		static_cast<int>(h));
	return true;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE upscaleSrvCpu(UINT table, UINT slot) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_upscaleSrvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(table * kTemporalTableSize + slot) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return h;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE upscaleRtv(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_upscaleRtvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return h;
}

void writeUpscaleSrv(UINT table, UINT slot, ID3D12Resource* res, DXGI_FORMAT format, bool msaa = false)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = format;
	sd.ViewDimension = msaa ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (!msaa) { sd.Texture2D.MipLevels = 1; }
	m_d3dDevice->CreateShaderResourceView(res, &sd, upscaleSrvCpu(table, slot));
}

void writeUpscaleViews()
{
	constexpr auto kRgba16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
	D3D12_RENDER_TARGET_VIEW_DESC rv = {};
	rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rv.Format = kRgba16;
	for (UINT i = 0; i < 2; ++i)
	{
		m_d3dDevice->CreateRenderTargetView(m_upscaleHistory[i].Get(), &rv, upscaleRtv(i));
		const UINT taau = kUpscaleTableTaau + i;
		writeUpscaleSrv(taau, 0, m_sceneLdr.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
		writeUpscaleSrv(taau, 1, m_upscaleHistory[1 - i].Get(), kRgba16);
		writeUpscaleSrv(taau, 2, m_velocityTex.Get(), DXGI_FORMAT_R16G16_FLOAT);
		writeUpscaleSrv(taau, 3, m_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, true);
		const UINT sharpen = kUpscaleTableSharpen + i;
		writeUpscaleSrv(sharpen, 0, m_upscaleHistory[i].Get(), kRgba16);
		writeUpscaleSrv(sharpen, 1, m_sceneLdr.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
		for (UINT slot = 2; slot < kTemporalTableSize; ++slot) { writeUpscaleSrv(sharpen, slot, nullptr, kRgba16); }
	}
	writeFsrTables();
}

void drawUpscaleFullscreen(ID3D12PipelineState* pso, UINT table, const void* cb, std::size_t cbBytes, float w, float h)
{
	auto* cl = m_graphicsCmdList.Get();
	const D3D12_VIEWPORT vp{0.0f, 0.0f, w, h, 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_temporalRootSig.Get());
	cl->SetPipelineState(pso);
	ID3D12DescriptorHeap* heaps[] = {m_upscaleSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	D3D12_GPU_DESCRIPTOR_HANDLE t = m_upscaleSrvHeap->GetGPUDescriptorHandleForHeapStart();
	t.ptr += static_cast<UINT64>(table * kTemporalTableSize) *
	         m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cl->SetGraphicsRootDescriptorTable(0, t);
	const auto a = m_uploadRing.upload(cb, cbBytes, 256);
	if (a.valid()) { cl->SetGraphicsRootConstantBufferView(1, a.gpuAddr); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}

/// @brief TAAU で履歴へ重ねる (出力解像度)。主パスの深度は DEPTH_WRITE で受け取って返す
void recordTaau()
{
	const int write = m_upscaleWrite;
	temporalBarrier(m_upscaleHistory[write].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = upscaleRtv(static_cast<UINT>(write));
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	struct alignas(256) CbTaau
	{
		float inputSize[2];
		float outputSize[2];
		float outputTexel[2];
		float jitterPx[2];
		float blendStill;
		float blendMoving;
		float movingPixels;
		float historyValid;
	};
	CbTaau cb{};
	cb.inputSize[0] = m_config.viewportWidth;
	cb.inputSize[1] = m_config.viewportHeight;
	cb.outputSize[0] = m_outputWidth;
	cb.outputSize[1] = m_outputHeight;
	cb.outputTexel[0] = 1.0f / m_outputWidth;
	cb.outputTexel[1] = 1.0f / m_outputHeight;
	cb.jitterPx[0] = m_jitterNdc.x * m_config.viewportWidth * 0.5f;
	cb.jitterPx[1] = -m_jitterNdc.y * m_config.viewportHeight * 0.5f;
	cb.blendStill = 0.1f;
	cb.blendMoving = 0.25f;
	cb.movingPixels = 8.0f;
	cb.historyValid = m_upscaleHistoryValid ? 1.0f : 0.0f;
	drawUpscaleFullscreen(m_taauPSO.Get(), kUpscaleTableTaau + static_cast<UINT>(write), &cb, sizeof(cb), m_outputWidth,
	                      m_outputHeight);
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	temporalBarrier(m_upscaleHistory[write].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/// @brief 動きのぼけの後に呼ぶ。TAAU か FSR → 鮮鋭化でバックバッファへ。出力の大きさが合わない間 (lo-fi) は引き伸ばすだけ。
///        FSR は自分で RCAS を掛けるので、鮮鋭化の PSO は強さ 0 の写しとして使う
void drawUpscalePass()
{
	if (!upscaleActive() || m_device == nullptr) { return; }
	auto* bb = m_device->currentBackBuffer();
	if (bb == nullptr || bb->nativeResource() == nullptr) { return; }
	const auto d = bb->nativeResource()->GetDesc();
	const bool matches = static_cast<float>(d.Width) == m_outputWidth && static_cast<float>(d.Height) == m_outputHeight;
	temporalBarrier(m_sceneLdr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const int write = m_upscaleWrite;
	const bool temporal = matches && velocityReady();
	const bool fsr = temporal && fsrActive() && recordFsr();
	const bool taau = temporal && !fsrActive();
	if (taau) { recordTaau(); }
	const auto rtv = bb->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const float sharpenCb[4] = {m_outputWidth, m_outputHeight, fsr ? 0.0f : m_upscaleSharpness, 0.0f};
	const UINT table = fsr ? kUpscaleTableFsr : kUpscaleTableSharpen + static_cast<UINT>(write);
	drawUpscaleFullscreen((fsr || taau) ? m_sharpenPSO.Get() : m_upscaleBlitPSO.Get(), table, sharpenCb, sizeof(sharpenCb),
	                      static_cast<float>(d.Width), static_cast<float>(d.Height));
	temporalBarrier(m_sceneLdr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_upscaleHistoryValid = fsr || taau;
	if (taau) { m_upscaleWrite = 1 - write; }
	++m_taaFrameIndex;
}
