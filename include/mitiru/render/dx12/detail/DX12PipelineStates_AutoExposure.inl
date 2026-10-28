// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される。
// 自動露出 (ADR 0109)。
// resolve 済みの HDR から log2 輝度の平均を 1 画素の R32F に測定し、FRAME_COUNT 枚の読み戻しリングを通して CPU へ渡す。
// 読み戻しは常に FRAME_COUNT フレーム遅れる。
// CPU はゲームのフレーム番号の差で EV を追従させるため、同じ台本では露出の履歴も同じになる (AutoExposureMath.hpp)。
// 倍率は画面への提示にだけ使い、ゲームの状態には渡さない。
// SceneLook::autoExposureSpeed が 0 のフレームは記録しない。

public:

/// @brief SceneLook::autoExposureSpeed、RangeEv、BiasEv を適用する (ADR 0109)。speed が 0 なら無効にし、倍率を 1 に戻して履歴を破棄する
void applyAutoExposure(float speed, float rangeEv, float biasEv)
{
	const AutoExposureParams next{speed, rangeEv, biasEv};
	if (!next.enabled())
	{
		m_autoExposureParams = AutoExposureParams{};
		m_autoExposureState = AutoExposureState{};
		m_autoExposureHasLast = false;
		for (bool& pending : m_autoExposurePending) { pending = false; }   // 切っている間に届く古い測定を、入れ直した後に使わない
		return;
	}
	features::use(features::Feature::AutoExposure);
	if (!m_needs.wants(features::Feature::AutoExposure))
	{
		features::note(features::Feature::AutoExposure);
		m_autoExposureParams = AutoExposureParams{};
		return;
	}
	// 作れなかったときに毎フレーム、シェーダーのコンパイルを繰り返さない
	if (!m_autoExposurePSO && !m_autoExposureCreateTried)
	{
		m_autoExposureCreateTried = true;
		createAutoExposureResources();
	}
	m_autoExposureParams = m_autoExposurePSO ? next : AutoExposureParams{};
}

/// @brief 測定結果が届いていれば tonemap の露出に掛ける倍率。無効なときと最初の測定結果が届くまでは 1
[[nodiscard]] float autoExposureScale() const noexcept
{
	return m_autoExposureParams.enabled() ? render::autoExposureScale(m_autoExposureState) : 1.0f;
}

private:

AutoExposureParams                m_autoExposureParams;
AutoExposureState                 m_autoExposureState;
std::optional<gfx::Dx12Shader>    m_autoExposurePS;
ComPtr<ID3D12PipelineState>       m_autoExposurePSO;
gfx::GpuResource                  m_autoExposureTex;        ///< 1x1 R32F。常に RENDER_TARGET で待つ
ComPtr<ID3D12DescriptorHeap>      m_autoExposureRtvHeap;
gfx::GpuResource                  m_autoExposureReadback[FRAME_COUNT];
bool                              m_autoExposurePending[FRAME_COUNT] = {};
std::uint32_t                     m_autoExposureSlotFrame[FRAME_COUNT] = {};
std::uint32_t                     m_autoExposureLastFrame = 0;
bool                              m_autoExposureHasLast = false;
bool                              m_autoExposureCreateTried = false;

[[nodiscard]] bool autoExposureWanted() const noexcept
{
	return m_autoExposureParams.enabled() && m_autoExposurePSO && m_autoExposureTex && m_autoExposureRtvHeap &&
	       m_hdrIntermediateBuffer && m_hdrIntermediateSrvHeap && m_tonemapRootSig;
}

void createAutoExposureResources()
{
	if (m_autoExposurePSO || !m_tonemapRootSig || !m_outlinePostVS || !m_d3dDevice) { return; }
	try
	{
		m_autoExposurePS = gfx::Dx12Shader::createPixelShader(DX12_AUTO_EXPOSURE_PS, "PSMain");
	}
	catch (const std::exception& e)
	{
		console::verbosef("自動露出のシェーダーのコンパイルに失敗しました (%s)", e.what());
		m_autoExposurePS.reset();
		return;
	}

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 1;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_R32_FLOAT;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
	                                  m_autoExposureTex)) || !m_autoExposureTex)
	{
		return;
	}
	m_autoExposureTex->SetName(L"Renderer3D AutoExposure");

	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.NumDescriptors = 1;
	hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_autoExposureRtvHeap.ReleaseAndGetAddressOf()))))
	{
		m_autoExposureTex.Reset();
		return;
	}
	m_d3dDevice->CreateRenderTargetView(m_autoExposureTex.Get(), nullptr, m_autoExposureRtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (std::uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
		                                D3D12_RESOURCE_STATE_COPY_DEST, m_autoExposureReadback[i])))
		{
			m_autoExposureTex.Reset();
			return;
		}
		m_autoExposurePending[i] = false;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = fullscreenPsoDesc(m_tonemapRootSig.Get(), *m_autoExposurePS);
	pd.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_autoExposurePSO.ReleaseAndGetAddressOf()))))
	{
		m_autoExposurePSO.Reset();
		m_autoExposureTex.Reset();
	}
}

/// @brief bloom の直後、HDR が RESOLVE_DEST のときに呼ぶ。HDR を測定し、このフレームの読み戻しスロットへコピーする
void drawAutoExposurePass()
{
	if (!autoExposureWanted()) { return; }
	const std::uint32_t slot = m_frameCursor;
	if (slot >= FRAME_COUNT || !m_autoExposureReadback[slot]) { return; }

	D3D12_RESOURCE_BARRIER hdr = {};
	hdr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	hdr.Transition.pResource = m_hdrIntermediateBuffer.Get();
	hdr.Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	hdr.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	hdr.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &hdr);

	const D3D12_VIEWPORT vp{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
	const D3D12_RECT sr{0, 0, 1, 1};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sr);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_autoExposureRtvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	m_graphicsCmdList->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	m_graphicsCmdList->SetPipelineState(m_autoExposurePSO.Get());
	ID3D12DescriptorHeap* heaps[] = {m_hdrIntermediateSrvHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(0, m_hdrIntermediateSrvHeap->GetGPUDescriptorHandleForHeapStart());
	struct alignas(256) Unused { float v[4]; };
	const Unused unused{};
	const auto cb = m_uploadRing.upload(&unused, sizeof(Unused), 256);
	if (cb.valid()) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cb.gpuAddr); }
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->IASetVertexBuffers(0, 0, nullptr);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	hdr.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	hdr.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	m_graphicsCmdList->ResourceBarrier(1, &hdr);

	D3D12_RESOURCE_BARRIER toCopy = {};
	toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopy.Transition.pResource = m_autoExposureTex.Get();
	toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &toCopy);

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = m_autoExposureTex.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = m_autoExposureReadback[slot].Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
	dst.PlacedFootprint.Footprint.Width = 1;
	dst.PlacedFootprint.Footprint.Height = 1;
	dst.PlacedFootprint.Footprint.Depth = 1;
	dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	m_autoExposurePending[slot] = true;
	m_autoExposureSlotFrame[slot] = m_colorFinishSeed;

	std::swap(toCopy.Transition.StateBefore, toCopy.Transition.StateAfter);
	m_graphicsCmdList->ResourceBarrier(1, &toCopy);
}

/// @brief beginFrame の最初に呼ぶ。スロットに記録された測定結果を読み、露出の状態を 1 歩進める。Dx12Device::beginFrame がこのスロットの GPU 処理の完了を待つため、追加の同期なしで Map できる
void consumeAutoExposureReadback(std::uint32_t slot)
{
	if (slot >= FRAME_COUNT || !m_autoExposurePending[slot] || !m_autoExposureReadback[slot]) { return; }
	m_autoExposurePending[slot] = false;
	float mean = 0.0f;
	void* mapped = nullptr;
	const D3D12_RANGE read{0, sizeof(float)};
	if (FAILED(m_autoExposureReadback[slot]->Map(0, &read, &mapped)) || mapped == nullptr) { return; }
	std::memcpy(&mean, mapped, sizeof(float));
	const D3D12_RANGE none{0, 0};
	m_autoExposureReadback[slot]->Unmap(0, &none);
	if (!m_autoExposureParams.enabled() || !(mean == mean)) { return; }

	// フレーム番号が進まない場面でも 1 歩進める
	const std::uint32_t frame = m_autoExposureSlotFrame[slot];
	const int frames = (m_autoExposureHasLast && frame > m_autoExposureLastFrame)
		? static_cast<int>(std::min<std::uint32_t>(frame - m_autoExposureLastFrame, 600u)) : 1;
	m_autoExposureLastFrame = frame;
	m_autoExposureHasLast = true;
	m_autoExposureState = stepAutoExposure(m_autoExposureState, mean, m_autoExposureParams, frames);
}
