// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 当たった瞬間の画面の演出 (HitFeel.hpp)。3D の後処理の最後 (アップスケールの後) に、出力の大きさのバック
// バッファを写しへ取って全画面で書き戻す。TAA と FSR の履歴の後なので残像にならず、HUD はこの後に描かれる。
// 値はそのフレームだけ効き、パスの終わりで 0 に戻す (ゲームが毎フレーム渡す)。

public:

/// @brief このフレームの演出を渡す。渡さなかったフレームは何もしない
void setHitFeel(const HitFeel& feel) noexcept override { m_hitFeel = clampHitFeel(feel); }

private:

HitFeel                      m_hitFeel{};
gfx::GpuResource             m_hitFeelCopy;   ///< バックバッファの写し (同じ形式・大きさ)
UINT                         m_hitFeelCopyW = 0;
UINT                         m_hitFeelCopyH = 0;
ComPtr<ID3D12RootSignature>  m_hitFeelRS;
ComPtr<ID3D12PipelineState>  m_hitFeelPSO;
ComPtr<ID3D12DescriptorHeap> m_hitFeelHeap;   ///< フレームごとに 1 枚

/// @brief createTemporalPipelines の後に呼ぶ (outline post の VS を使う)。作れなければ演出は描かない
void createHitFeelPipeline()
{
	if (!m_outlinePostVS) { return; }
	D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
	D3D12_ROOT_PARAMETER p[2] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	p[1].DescriptorTable = {1, &range};
	p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	const D3D12_STATIC_SAMPLER_DESC samp = clampSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0);
	D3D12_ROOT_SIGNATURE_DESC rd = {};
	rd.NumParameters = 2;
	rd.pParameters = p;
	rd.NumStaticSamplers = 1;
	rd.pStaticSamplers = &samp;
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = FRAME_COUNT;
	hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!serializeRootSig(rd, m_hitFeelRS) ||
	    FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_hitFeelHeap.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	const auto ps = gfx::Dx12Shader::createPixelShader(DX12_HIT_FEEL_PS, "PSMain");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_hitFeelRS.Get(), ps);
	pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_hitFeelPSO.ReleaseAndGetAddressOf()))))
	{
		m_hitFeelPSO.Reset();
	}
}

/// @brief 写しを出力の大きさに合わせる。古い写しは GPU が読み終わるまで一時資源として持つ
[[nodiscard]] bool ensureHitFeelCopy(const D3D12_RESOURCE_DESC& bb)
{
	if (m_hitFeelCopy && m_hitFeelCopyW == bb.Width && m_hitFeelCopyH == bb.Height) { return true; }
	if (m_hitFeelCopy) { m_frameTempResources.push_back(std::move(m_hitFeelCopy)); }
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = bb.Width;
	desc.Height = bb.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                  nullptr, m_hitFeelCopy)) || !m_hitFeelCopy)
	{
		m_hitFeelCopy.Reset();
		return false;
	}
	m_hitFeelCopy->SetName(L"Renderer3D hit feel copy");
	m_hitFeelCopyW = static_cast<UINT>(bb.Width);
	m_hitFeelCopyH = bb.Height;
	return true;
}

/// @brief アップスケールの後に呼ぶ。バックバッファ (RENDER_TARGET) を写して演出を掛け、書き戻す
void drawHitFeelPass()
{
	const HitFeel feel = m_hitFeel;
	m_hitFeel = HitFeel{};
	auto* bb = (m_device != nullptr) ? m_device->currentBackBuffer() : nullptr;
	if (!hitFeelActive(feel) || !m_hitFeelPSO || bb == nullptr || bb->nativeResource() == nullptr) { return; }
	const D3D12_RESOURCE_DESC bbDesc = bb->nativeResource()->GetDesc();
	if (bbDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		debug::verboseOnce("dx12.hitFeel.format", "バックバッファが RGBA8 ではないので、当たりの演出を掛けません。");
		return;
	}
	if (!ensureHitFeelCopy(bbDesc)) { return; }
	copyBackBufferForHitFeel(bb->nativeResource());

	struct alignas(256) CbHitFeel
	{
		float flash[4];
		float params[4];
	};
	CbHitFeel cb{};
	for (int i = 0; i < 3; ++i) { cb.flash[i] = feel.flashColor[i]; }
	cb.flash[3] = feel.flash;
	cb.params[0] = feel.chromatic;
	cb.params[1] = feel.radialBlur;
	cb.params[2] = feel.center[0];
	cb.params[3] = feel.center[1];
	const auto c = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!c.valid()) { return; }

	const UINT inc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT slot = m_frameCursor % FRAME_COUNT;
	auto cpu = m_hitFeelHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * inc;
	m_d3dDevice->CreateShaderResourceView(m_hitFeelCopy.Get(), nullptr, cpu);
	auto gpu = m_hitFeelHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot) * inc;

	auto* cl = m_graphicsCmdList.Get();
	const auto rtv = bb->rtvHandle();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(bbDesc.Width), static_cast<float>(bbDesc.Height), 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, static_cast<LONG>(bbDesc.Width), static_cast<LONG>(bbDesc.Height)};
	cl->RSSetViewports(1, &vp);
	cl->RSSetScissorRects(1, &sr);
	cl->SetGraphicsRootSignature(m_hitFeelRS.Get());
	cl->SetPipelineState(m_hitFeelPSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_hitFeelHeap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootConstantBufferView(0, c.gpuAddr);
	cl->SetGraphicsRootDescriptorTable(1, gpu);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	cl->SetGraphicsRootSignature(m_rootSignature.Get());
}

void copyBackBufferForHitFeel(ID3D12Resource* bb)
{
	temporalBarrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	temporalBarrier(m_hitFeelCopy.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	m_graphicsCmdList->CopyResource(m_hitFeelCopy.Get(), bb);
	temporalBarrier(m_hitFeelCopy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}
