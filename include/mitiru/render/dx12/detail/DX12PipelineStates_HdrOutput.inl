// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// HDR 出力 (scRGB、ADR 0106)。スワップチェーンが HDR のとき (Dx12SwapChain::hdrActive) だけ、tonemap の直前に
// 「SDR の絵に載らなかった highlight excess」(HdrMath.hpp の hdrExcess) を RGBA16F に別に書き、スワップチェーンへ渡す。
// SDR の経路 (DX12_TONEMAP_PS と LDR のレンダーターゲット) には触れないので、HDR を使わない間は今までと同じ画素になる。
// excess のパスと PSO は、HDR になって最初のフレームで作る (SDR のゲームに起動時のスパイクを持ち込まない)。

private:

gfx::GpuResource             m_hdrExcess;      ///< highlight excess (RGBA16F、tonemap のレンダーターゲットと同じ大きさ)。休んでいる間は PIXEL_SHADER_RESOURCE
UINT                         m_hdrExcessW = 0;
UINT                         m_hdrExcessH = 0;
ComPtr<ID3D12DescriptorHeap> m_hdrExcessRtvHeap;
ComPtr<ID3D12PipelineState>  m_hdrExcessPSO;
bool                         m_hdrExcessFailed = false;   ///< 作れなかった。選び直されるまで再び試さない

[[nodiscard]] gfx::Dx12SwapChain* hdrSwapChain() const noexcept
{
	if (m_device == nullptr) { return nullptr; }
	gfx::Dx12SwapChain* swap = m_device->getSwapChain();
	return (swap != nullptr && swap->hdrActive()) ? swap : nullptr;
}

[[nodiscard]] bool prepareHdrExcessPipeline()
{
	if (m_hdrExcessPSO) { return true; }
	if (m_hdrExcessFailed || !m_tonemapVS || !m_tonemapRootSig) { return false; }
	try
	{
		const auto ps = gfx::Dx12Shader::createPixelShader(DX12_TONEMAP_EXCESS_PS, "PSMain");
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
		const HRESULT psoResult = m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_hdrExcessPSO.ReleaseAndGetAddressOf()));
		const HRESULT heapResult = m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_hdrExcessRtvHeap.ReleaseAndGetAddressOf()));
		if (SUCCEEDED(psoResult) && SUCCEEDED(heapResult)) { return true; }
		console::verbosef("HDR 出力のパスを作れませんでした (PSO 0x%08lX、ヒープ 0x%08lX)", static_cast<unsigned long>(psoResult), static_cast<unsigned long>(heapResult));
	}
	catch (const std::exception& e)
	{
		console::verbosef("HDR 出力のシェーダーのコンパイルに失敗しました (%s)", e.what());
	}
	m_hdrExcessPSO.Reset();
	m_hdrExcessFailed = true;
	return false;
}

/// @brief highlight excess のレンダーターゲットを tonemap のレンダーターゲットの大きさに合わせる。古い物は GPU が読み終わるまで一時リソースとして持つ
[[nodiscard]] bool ensureHdrExcessTarget(UINT width, UINT height)
{
	if (m_hdrExcess && m_hdrExcessW == width && m_hdrExcessH == height) { return true; }
	if (m_hdrExcess) { m_frameTempResources.push_back(std::move(m_hdrExcess)); }
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
	                                  m_hdrExcess)) || !m_hdrExcess)
	{
		m_hdrExcess.Reset();
		return false;
	}
	m_hdrExcess->SetName(L"Renderer3D HDR highlight excess");
	m_d3dDevice->CreateRenderTargetView(m_hdrExcess.Get(), nullptr, m_hdrExcessRtvHeap->GetCPUDescriptorHandleForHeapStart());
	m_hdrExcessW = width;
	m_hdrExcessH = height;
	return true;
}

/// @brief applyTonemap の中、HDR の resolve を PIXEL_SHADER_RESOURCE にした直後に呼ぶ。SDR の tonemap はこの後に今までどおり描く
void drawHdrExcess(gfx::Dx12SwapChain* swap, UINT width, UINT height)
{
	if (!prepareHdrExcessPipeline()) { return; }
	if (!ensureHdrExcessTarget(width, height))
	{
		debug::verboseOnce("dx12.hdr.target", "HDR 出力の highlight excess のレンダーターゲットを作れませんでした");
		return;
	}
	const render::HdrParams params = swap->hdrParams();
	struct alignas(256) Padded { TonemapExcessCB cb; };
	Padded padded{};
	padded.cb.exposure      = m_tonemapExposure;
	padded.cb.gamma         = m_tonemapGamma;
	padded.cb.aoOn          = m_aoAppliedThisFrame ? 1.0f : 0.0f;
	padded.cb.bloomOn       = m_bloomAppliedThisFrame ? 1.0f : 0.0f;
	padded.cb.bloomStrength = m_bloomStrength;
	padded.cb.saturation    = m_gradeSaturation;
	padded.cb.contrast      = m_gradeContrast;
	padded.cb.headroom      = render::hdrHeadroom(params);
	const auto c = m_uploadRing.upload(&padded, sizeof(padded), 256);
	if (!c.valid())
	{
		debug::verboseOnce("dx12.hdr.upload", "HDR 出力の定数を上りバッファへ書けませんでした");
		return;
	}

	temporalBarrier(m_hdrExcess.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = m_hdrExcessRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	cl->SetPipelineState(m_hdrExcessPSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_hdrIntermediateSrvHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, m_hdrIntermediateSrvHeap->GetGPUDescriptorHandleForHeapStart());
	cl->SetGraphicsRootConstantBufferView(1, c.gpuAddr);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	temporalBarrier(m_hdrExcess.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	swap->setHdrExtra(m_hdrExcess.Get());
}

public:

/// @brief 試験用。直前のフレームが書いた highlight excess (RGBA16F)。HDR でなかった・まだ書いていなければ null
[[nodiscard]] ID3D12Resource* hdrExcessResource() const noexcept { return m_hdrExcess.Get(); }

/// @brief 試験用。tonemap が読む MSAA を解いた HDR のシーン (RGBA16F、休んでいる間は RESOLVE_DEST)。excess の式の入力を実物から取る
[[nodiscard]] ID3D12Resource* hdrSceneForDiagnostics() const noexcept { return m_hdrIntermediateBuffer.Get(); }

private:
