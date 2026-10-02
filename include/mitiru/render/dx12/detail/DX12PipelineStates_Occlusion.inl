// Renderer3D_DX12 のクラス本体の一部。DX12PipelineStates.hpp から include される

// ─────────────────────────────────────────────────────────────
//  オクルージョンカリング: min-depth resolve + readback
// ─────────────────────────────────────────────────────────────

/// @brief オクルージョン resolve 用の PSO・resolve RT・readback バッファを生成する
/// @details PS は新規、VS は `m_outlinePostVS`（フルスクリーン三角形）を流用する。
///          深度 SRV は `m_depthSRVHeap` のスロット 0（既存の t0=深度）をそのまま使う
///          ため、専用の SRV ヒープは作らない。`createOutlinePostProcess` の後で
///          呼ぶ必要がある（両方の前提を満たすため）。
void createOcclusionResolveResources()
{
	if (!m_outlinePostVS) { return; }

	m_occlusionResolvePS = gfx::Dx12Shader::createPixelShader(
		DX12_OCCLUSION_MIN_DEPTH_RESOLVE_PS, "PSMain");
	if (!m_occlusionResolvePS) { return; }

	/// ルートシグネチャ: SRV(t0) のみ（深度 SRV ヒープのスロット 0 を指す）
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors     = 1;
	srvRange.BaseShaderRegister = 0;

	D3D12_ROOT_PARAMETER param = {};
	param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param.DescriptorTable.NumDescriptorRanges = 1;
	param.DescriptorTable.pDescriptorRanges   = &srvRange;
	param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = 1;
	rootDesc.pParameters   = &param;
	rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> serialized, errorBlob;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serialized.GetAddressOf(), errorBlob.GetAddressOf())))
	{
		return;
	}
	if (FAILED(m_d3dDevice->CreateRootSignature(0, serialized->GetBufferPointer(),
			serialized->GetBufferSize(),
			IID_PPV_ARGS(m_occlusionResolveRootSig.GetAddressOf()))))
	{
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature                  = m_occlusionResolveRootSig.Get();
	psoDesc.VS                              = m_outlinePostVS->shaderBytecode();
	psoDesc.PS                              = m_occlusionResolvePS->shaderBytecode();
	psoDesc.InputLayout.NumElements         = 0;
	psoDesc.InputLayout.pInputElementDescs  = nullptr;
	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;  // フルスクリーン三角形は winding 不問
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask
		= D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable   = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 1;
	psoDesc.RTVFormats[0]         = DXGI_FORMAT_R32_FLOAT;
	psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count      = 1;

	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_occlusionResolvePSO.GetAddressOf()))))
	{
		return;
	}

	/// resolve RT（単一サンプル R32_FLOAT、深度と同サイズ）
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension       = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width           = static_cast<UINT64>(m_config.viewportWidth);
	rtDesc.Height          = static_cast<UINT>(m_config.viewportHeight);
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels       = 1;
	rtDesc.Format          = DXGI_FORMAT_R32_FLOAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Layout          = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rtDesc.Flags           = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format   = DXGI_FORMAT_R32_FLOAT;
	clearValue.Color[0] = 1.0f;

	m_occlusionResolveTex.Reset();
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, rtDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, m_occlusionResolveTex)))
	{
		return;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_occlusionResolveRtvHeap.Reset();
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(m_occlusionResolveRtvHeap.GetAddressOf()))))
	{
		return;
	}
	m_d3dDevice->CreateRenderTargetView(
		m_occlusionResolveTex.Get(), nullptr,
		m_occlusionResolveRtvHeap->GetCPUDescriptorHandleForHeapStart());

	/// readback（buffer リソース、行ピッチを 256 バイト境界に揃える）
	const UINT width  = static_cast<UINT>(m_config.viewportWidth);
	const UINT height = static_cast<UINT>(m_config.viewportHeight);
	constexpr UINT kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	m_occlusionReadbackRowPitch = (width * 4 + (kPitchAlign - 1)) & ~(kPitchAlign - 1);
	const UINT64 bufferSize = static_cast<UINT64>(m_occlusionReadbackRowPitch) * height;

	for (uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		(void)gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, bufferSize,
			D3D12_RESOURCE_STATE_COPY_DEST, m_occlusionReadback[i]);
		m_occlusionReadbackPending[i] = false;
	}
}

/// @brief endFrame から呼ぶ。深度を resolve RT へ焼き、当フレームスロットの
///        readback バッファへコピーする
/// @details 呼び出し時点で `m_depthBuffer` は DEPTH_WRITE（この時点でメッシュ描画は
///          全て終わっている）。このフレームの実 GPU 実行は `finalizeFrame` まで
///          遅延されるため、ここで積んだコピーが実際に完了しているのは
///          このスロットを次に再利用する `beginFrame`（デバイス側が既にフェンス
///          待機済み）の時点であり、`consumeOcclusionReadback` はそこで読む。
void recordOcclusionResolvePass()
{
	if (!m_occlusionResolvePSO || !m_occlusionResolveRootSig || !m_depthSRVHeap ||
	    !m_occlusionResolveTex || !m_depthBuffer)
	{
		return;
	}

	auto* cmd = m_graphicsCmdList.Get();

	D3D12_RESOURCE_BARRIER depthToSrv = {};
	depthToSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	depthToSrv.Transition.pResource   = m_depthBuffer.Get();
	depthToSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	depthToSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	depthToSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &depthToSrv);

	auto rtv = m_occlusionResolveRtvHeap->GetCPUDescriptorHandleForHeapStart();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width    = m_config.viewportWidth;
	vp.Height   = m_config.viewportHeight;
	vp.MaxDepth = 1.0f;
	cmd->RSSetViewports(1, &vp);
	const D3D12_RECT sc = {0, 0, static_cast<LONG>(m_config.viewportWidth),
	                              static_cast<LONG>(m_config.viewportHeight)};
	cmd->RSSetScissorRects(1, &sc);

	cmd->SetPipelineState(m_occlusionResolvePSO.Get());
	cmd->SetGraphicsRootSignature(m_occlusionResolveRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { m_depthSRVHeap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, m_depthSRVHeap->GetGPUDescriptorHandleForHeapStart());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER toCopySrc = {};
	toCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopySrc.Transition.pResource   = m_occlusionResolveTex.Get();
	toCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toCopySrc.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
	toCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &toCopySrc);

	const uint32_t frameIndex = m_frameCursor;
	if (m_occlusionReadback[frameIndex])
	{
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource        = m_occlusionResolveTex.Get();
		src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource                          = m_occlusionReadback[frameIndex].Get();
		dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.PlacedFootprint.Offset             = 0;
		dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R32_FLOAT;
		dst.PlacedFootprint.Footprint.Width    = static_cast<UINT>(m_config.viewportWidth);
		dst.PlacedFootprint.Footprint.Height   = static_cast<UINT>(m_config.viewportHeight);
		dst.PlacedFootprint.Footprint.Depth    = 1;
		dst.PlacedFootprint.Footprint.RowPitch = m_occlusionReadbackRowPitch;

		cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		m_occlusionReadbackPending[frameIndex] = true;
	}

	D3D12_RESOURCE_BARRIER post[2] = {};
	post[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	post[0].Transition.pResource   = m_occlusionResolveTex.Get();
	post[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	post[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	post[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	post[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	post[1].Transition.pResource   = m_depthBuffer.Get();
	post[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	post[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	post[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(2, post);
}

/// @brief beginFrame 先頭で呼ぶ。このスロットの前回 resolve 結果を読んで Hi-Z を更新する
void consumeOcclusionReadback(uint32_t frameIndex)
{
	if (frameIndex >= FRAME_COUNT ||
	    !m_occlusionReadbackPending[frameIndex] || !m_occlusionReadback[frameIndex])
	{
		return;
	}
	m_occlusionReadbackPending[frameIndex] = false;

	void* mapped = nullptr;
	const D3D12_RANGE readRange = {
		0, static_cast<SIZE_T>(m_occlusionReadbackRowPitch) *
		   static_cast<SIZE_T>(m_config.viewportHeight)
	};
	if (FAILED(m_occlusionReadback[frameIndex]->Map(0, &readRange, &mapped)) || !mapped)
	{
		return;
	}

	const auto width  = static_cast<int>(m_config.viewportWidth);
	const auto height = static_cast<int>(m_config.viewportHeight);
	const int stride = kOcclusionDownsampleStride;
	const int dsW = std::max(1, width / stride);
	const int dsH = std::max(1, height / stride);
	m_occlusionDepthScratch.resize(static_cast<std::size_t>(dsW) * static_cast<std::size_t>(dsH));

	const auto* base = static_cast<const std::uint8_t*>(mapped);
	for (int y = 0; y < dsH; ++y)
	{
		const int srcY = std::min(y * stride, height - 1);
		const auto* row = reinterpret_cast<const float*>(
			base + static_cast<std::size_t>(srcY) * m_occlusionReadbackRowPitch);
		for (int x = 0; x < dsW; ++x)
		{
			const int srcX = std::min(x * stride, width - 1);
			m_occlusionDepthScratch[
				static_cast<std::size_t>(y) * static_cast<std::size_t>(dsW) +
				static_cast<std::size_t>(x)] = row[srcX];
		}
	}

	const D3D12_RANGE writtenRange = {0, 0};
	m_occlusionReadback[frameIndex]->Unmap(0, &writtenRange);
	m_occlusionCuller.updateDepth(m_occlusionDepthScratch.data(), dsW, dsH);
}
