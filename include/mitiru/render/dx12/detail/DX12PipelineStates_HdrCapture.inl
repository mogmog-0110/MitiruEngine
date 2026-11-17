// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 映像の HDR 撮影 (ADR 0148)。頼まれたときだけ、tonemap の直前に「肩の前の線形のシーン」(DX12_TONEMAP_SCENE_PS) を
// RGBA16F の別のターゲットへ書き、フレームの外で読み戻す。SDR の tonemap と LDR のターゲットには触れないので、頼まない間は
// 今までと同じ画素になる。パスと PSO は頼まれて最初のフレームで作る (使わない映像・ゲームに起動時の費用を持ち込まない)。

private:

gfx::GpuResource             m_hdrCapture;      ///< 線形のシーン (RGBA16F、tonemap のターゲットと同じ大きさ)。休んでいる間は PIXEL_SHADER_RESOURCE
UINT                         m_hdrCaptureW = 0;
UINT                         m_hdrCaptureH = 0;
ComPtr<ID3D12DescriptorHeap> m_hdrCaptureRtvHeap;
ComPtr<ID3D12PipelineState>  m_hdrCapturePSO;
bool                         m_hdrCaptureOn = false;
std::uint32_t                m_hdrCaptureKind = kHdrCaptureScene;   ///< Enable で頼まれた kind

/// @brief 速度の撮影を頼まれている間は、TAA も動きのぼけも無くても動きベクトルを作る (motionVectorsActive が読む)
[[nodiscard]] bool hdrVelocityRequested() const noexcept { return m_hdrCaptureOn && m_hdrCaptureKind == kHdrCaptureVelocity; }
bool                         m_hdrCaptureFailed = false;   ///< 作れなかった。頼み直されるまで再び試さない

[[nodiscard]] bool prepareHdrCapturePipeline()
{
	if (m_hdrCapturePSO) { return true; }
	if (m_hdrCaptureFailed || !m_tonemapVS || !m_tonemapRootSig) { return false; }
	try
	{
		const auto ps = gfx::Dx12Shader::createPixelShader(DX12_TONEMAP_SCENE_PS, "PSMain");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_tonemapRootSig.Get();
		pd.VS = m_tonemapVS->shaderBytecode();
		pd.PS = ps.shaderBytecode();
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.RasterizerState.DepthClipEnable = FALSE;
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = UINT_MAX;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		pd.SampleDesc.Count = 1;
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		hd.NumDescriptors = 1;
		const HRESULT psoResult = m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_hdrCapturePSO.ReleaseAndGetAddressOf()));
		const HRESULT heapResult = m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_hdrCaptureRtvHeap.ReleaseAndGetAddressOf()));
		if (SUCCEEDED(psoResult) && SUCCEEDED(heapResult)) { return true; }
		console::verbosef("HDR 撮影のパスを作れませんでした (PSO 0x%08lX、ヒープ 0x%08lX)", static_cast<unsigned long>(psoResult), static_cast<unsigned long>(heapResult));
	}
	catch (const std::exception& e)
	{
		console::verbosef("HDR 撮影のシェーダーのコンパイルに失敗しました (%s)", e.what());
	}
	m_hdrCapturePSO.Reset();
	m_hdrCaptureFailed = true;
	return false;
}

[[nodiscard]] bool ensureHdrCaptureTarget(UINT width, UINT height)
{
	if (m_hdrCapture && m_hdrCaptureW == width && m_hdrCaptureH == height) { return true; }
	if (m_hdrCapture) { m_frameTempResources.push_back(std::move(m_hdrCapture)); }
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
	                                  m_hdrCapture)) || !m_hdrCapture)
	{
		m_hdrCapture.Reset();
		return false;
	}
	m_hdrCapture->SetName(L"Renderer3D HDR capture");
	m_d3dDevice->CreateRenderTargetView(m_hdrCapture.Get(), nullptr, m_hdrCaptureRtvHeap->GetCPUDescriptorHandleForHeapStart());
	m_hdrCaptureW = width;
	m_hdrCaptureH = height;
	return true;
}

/// @brief applyTonemap 内で HDR resolve を PIXEL_SHADER_RESOURCE にした直後、SDR tonemap の前に呼ぶ
void drawHdrCapture(UINT width, UINT height)
{
	if (!prepareHdrCapturePipeline()) { return; }
	if (!ensureHdrCaptureTarget(width, height))
	{
		debug::verboseOnce("dx12.hdrcapture.target", "HDR 撮影のレンダーターゲットを作れませんでした");
		return;
	}
	struct alignas(256) Padded { TonemapCB cb; };
	Padded padded{};
	padded.cb.exposure      = m_tonemapExposure * autoExposureScale();
	padded.cb.gamma         = m_tonemapGamma;
	padded.cb.aoOn          = m_aoAppliedThisFrame ? 1.0f : 0.0f;
	padded.cb.bloomOn       = m_bloomAppliedThisFrame ? 1.0f : 0.0f;
	padded.cb.bloomStrength = m_bloomStrength;
	padded.cb.saturation    = m_gradeSaturation;
	padded.cb.contrast      = m_gradeContrast;
	const auto c = m_uploadRing.upload(&padded, sizeof(padded), 256);
	if (!c.valid())
	{
		debug::verboseOnce("dx12.hdrcapture.upload", "HDR 撮影の定数を上りバッファへ書けませんでした");
		return;
	}
	temporalBarrier(m_hdrCapture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = m_hdrCaptureRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	cl->SetPipelineState(m_hdrCapturePSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_hdrIntermediateSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, m_hdrIntermediateSrvHeap->GetGPUDescriptorHandleForHeapStart());
	cl->SetGraphicsRootConstantBufferView(1, c.gpuAddr);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_hdrCapture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/// @brief 動きベクトル (UV、前 − 今) を、画素の (今 − 前) の RGB (b = 0) にして rgb へ書く。x は右、y は下が正。書いた float の数を返す
std::int32_t readHdrCaptureVelocity(HdrCaptureRequest* req)
{
	if (!m_velocityTex || m_device == nullptr) { return -1; }
	const D3D12_RESOURCE_DESC desc = m_velocityTex->GetDesc();
	req->width = static_cast<std::uint32_t>(desc.Width);
	req->height = desc.Height;
	const std::size_t pixels = static_cast<std::size_t>(req->width) * req->height;
	if (req->rgb == nullptr || req->capacityFloats < pixels * 3u) { return -2; }
	const std::vector<float> uv = readVelocityForDiagnostics();
	if (uv.size() != pixels * 2u) { return -1; }
	const float w = static_cast<float>(req->width);
	const float h = static_cast<float>(req->height);
	for (std::size_t i = 0; i < pixels; ++i)
	{
		req->rgb[i * 3] = -uv[i * 2] * w;
		req->rgb[i * 3 + 1] = -uv[i * 2 + 1] * h;
		req->rgb[i * 3 + 2] = 0.0f;
	}
	return static_cast<std::int32_t>(pixels * 3u);
}

public:

/// @brief HdrCapture.hpp の kExtHdrCaptureEnable と kExtHdrCaptureRead を処理する
std::int32_t hdrCaptureExtension(std::uint32_t id, void* data, std::uint32_t size)
{
	if (data == nullptr || size < sizeof(HdrCaptureRequest)) { return -1; }
	auto* req = static_cast<HdrCaptureRequest*>(data);
	if (req->kind != kHdrCaptureScene && req->kind != kHdrCaptureVelocity) { return -1; }
	if (id == kExtHdrCaptureEnable)
	{
		const bool on = req->on != 0;
		if (on && !m_hdrCaptureOn) { m_hdrCaptureFailed = false; }
		m_hdrCaptureOn = on;
		m_hdrCaptureKind = req->kind;
		return 0;
	}
	if (!m_hdrCaptureOn || req->kind != m_hdrCaptureKind) { return -1; }
	if (req->kind == kHdrCaptureVelocity) { return readHdrCaptureVelocity(req); }
	if (!m_hdrCapture || m_device == nullptr) { return -1; }
	req->width = m_hdrCaptureW;
	req->height = m_hdrCaptureH;
	const std::size_t pixels = static_cast<std::size_t>(m_hdrCaptureW) * m_hdrCaptureH;
	if (req->rgb == nullptr || req->capacityFloats < pixels * 3u) { return -2; }
	m_device->waitForGpu();
	const std::vector<std::uint8_t> raw = gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), m_hdrCapture.Get(),
	                                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 8);
	if (raw.size() != pixels * 8u) { return -1; }
	// RGBA の half から RGB の float へ、中間の配列を作らずに表を引いて直接書く (Debug ビルドで 1 コマ 100 ms の変換を 10 ms にする。ADR 0153)
	const float* lut = halfToFloatTable();
	const std::uint8_t* src = raw.data();
	float* dst = req->rgb;
	for (std::size_t i = 0; i < pixels; ++i)
	{
		std::uint16_t h[3];
		std::memcpy(h, src + i * 8, sizeof(h));
		dst[i * 3] = lut[h[0]];
		dst[i * 3 + 1] = lut[h[1]];
		dst[i * 3 + 2] = lut[h[2]];
	}
	return static_cast<std::int32_t>(pixels * 3u);
}

private:
