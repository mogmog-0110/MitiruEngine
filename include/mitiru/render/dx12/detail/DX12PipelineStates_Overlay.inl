// Renderer3D_DX12 のクラス本体の一部。DX12PipelineStates.hpp から include される


// ─────────────────────────────────────────────────────────────
//  ポストプロセスアウトライン描画
// ─────────────────────────────────────────────────────────────

/// @brief ポストプロセスアウトラインを描画する
/// @details 現在の m_outlineMode に応じて PSO と SRV ヒープを切り替える。
void drawPostProcessOutline()
{
	/// Fresnel モードではポストプロセスを実行しない
	if (m_outlineMode == OutlineMode::Fresnel) return;

	/// 使用する PSO を選択する
	const int modeIdx = static_cast<int>(m_outlineMode);
	ID3D12PipelineState* pso = nullptr;
	if (modeIdx == 0)
	{
		pso = m_outlinePostPSO.Get();
	}
	else if (modeIdx >= 1 && modeIdx <= 4)
	{
		pso = m_outlinePostPSOs[modeIdx].Get();
	}
	if (!pso) return;

	/// 色バッファが必要なモード(3,4)はバックバッファをコピーする
	const bool needsColorCopy =
		(m_outlineMode == OutlineMode::ColorEdge ||
		 m_outlineMode == OutlineMode::DepthColorCombo);

	auto* bbPost = m_device->currentBackBuffer();
	if (!bbPost) return;

	if (needsColorCopy && m_colorCopyBuffer)
	{
		/// バックバッファをコピーソースに遷移する
		D3D12_RESOURCE_BARRIER preCopy = {};
		preCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		preCopy.Transition.pResource = bbPost->nativeResource();
		preCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		preCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		preCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_graphicsCmdList->ResourceBarrier(1, &preCopy);

		m_graphicsCmdList->CopyResource(m_colorCopyBuffer.Get(), bbPost->nativeResource());

		/// コピーバッファを SRV に、バックバッファを RT に戻す
		D3D12_RESOURCE_BARRIER postCopy[2] = {};
		postCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		postCopy[0].Transition.pResource = bbPost->nativeResource();
		postCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		postCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		postCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		postCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		postCopy[1].Transition.pResource = m_colorCopyBuffer.Get();
		postCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		postCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		postCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_graphicsCmdList->ResourceBarrier(2, postCopy);
	}

	/// 深度バッファと法線バッファを SRV に遷移する
	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = m_depthBuffer.Get();
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = m_normalBuffer.Get();
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(2, barriers);

	/// MRT を解除しバックバッファのみにする（法線を SRV で読むため）
	auto rtvOnly = bbPost->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtvOnly, FALSE, nullptr);

	/// PSO とルートシグネチャをバインドする
	m_graphicsCmdList->SetPipelineState(pso);
	m_graphicsCmdList->SetGraphicsRootSignature(m_outlinePostRootSig.Get());

	/// SRV ヒープを選択する（色バッファ使用モードは拡張ヒープ）
	ID3D12DescriptorHeap* srvHeap = nullptr;
	if (m_outlineMode == OutlineMode::ColorEdge && m_colorEdgeSRVHeap)
	{
		srvHeap = m_colorEdgeSRVHeap.Get();
	}
	else if (m_outlineMode == OutlineMode::DepthColorCombo && m_depthColorSRVHeap)
	{
		srvHeap = m_depthColorSRVHeap.Get();
	}
	else
	{
		srvHeap = m_depthSRVHeap.Get();
	}

	ID3D12DescriptorHeap* heaps[] = {srvHeap};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	m_graphicsCmdList->SetGraphicsRootDescriptorTable(
		0, srvHeap->GetGPUDescriptorHandleForHeapStart());

	/// アウトラインパラメータ CB をアップロードする
	struct alignas(256) CbOutline {
		float texelSizeX, texelSizeY;
		float outlineWidth;
		float threshold;
		// シェーダの決め打ち (0.1 / 100) を廃止。setCamera の実値と食い違うと
		// 線形化が歪み、同じ閾値でも距離によって効き方が変わってしまう。
		float nearZ, farZ;
		float fadeNear, fadeFar;
		float fadeMin;
		float darken;
		float _pad1, _pad2;
	};
	CbOutline cb;
	cb.texelSizeX = 1.0f / m_config.viewportWidth;
	cb.texelSizeY = 1.0f / m_config.viewportHeight;
	cb.outlineWidth = m_outlineWidthPx;
	cb.threshold = m_outlineThresh;
	cb.nearZ = m_clodCamera.nearClip();
	cb.farZ  = m_clodCamera.farClip();
	cb.fadeNear = m_outlineFadeNear;
	cb.fadeFar  = m_outlineFadeFar;
	cb.fadeMin  = m_outlineFadeMin;
	cb.darken   = m_outlineDarken;

	const auto outlineCb = m_uploadRing.upload(&cb, sizeof(CbOutline), 256);
	if (outlineCb.valid())
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, outlineCb.gpuAddr);
	}

	/// フルスクリーン三角形を描画する（頂点バッファ不要、SV_VertexID 使用）
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_graphicsCmdList->DrawInstanced(3, 1, 0, 0);

	/// 深度バッファと法線バッファを元に戻す
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_graphicsCmdList->ResourceBarrier(2, barriers);

	/// 色コピーバッファを元のステートに戻す
	if (needsColorCopy && m_colorCopyBuffer)
	{
		D3D12_RESOURCE_BARRIER colorRestore = {};
		colorRestore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		colorRestore.Transition.pResource = m_colorCopyBuffer.Get();
		colorRestore.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		colorRestore.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		colorRestore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_graphicsCmdList->ResourceBarrier(1, &colorRestore);
	}

	/// メインのルートシグネチャに戻す
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

// ─────────────────────────────────────────────────────────────
//  D3D12 runtime validation poll (ENG-105 v2 debug)
// ─────────────────────────────────────────────────────────────

/// @brief 前フレームで溜まった D3D12 検証メッセージをログファイルに書き出す
/// @details Debug build かつ debug layer 有効時のみ意味がある。
///          beginFrame() の頭で呼ぶこと。message queue は読み終わったら
///          clear する。ERROR / CORRUPTION 級だけ記録。
void pollD3D12Validation()
{
	if (!m_infoQueue) return;
	const UINT64 n = m_infoQueue->GetNumStoredMessages();
	if (n == 0) return;

	std::ofstream log("mitiru_d3d12_runtime.log",
		std::ios::out | std::ios::app);
	for (UINT64 i = 0; i < n; ++i)
	{
		SIZE_T sz = 0;
		m_infoQueue->GetMessage(i, nullptr, &sz);
		if (sz == 0) continue;
		std::vector<char> buf(sz);
		auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
		if (FAILED(m_infoQueue->GetMessage(i, msg, &sz))) continue;
		// CORRUPTION (0) / ERROR (1) / WARNING (2) を記録、INFO (3) / MESSAGE (4) はスキップ
		if (msg->Severity > D3D12_MESSAGE_SEVERITY_WARNING) continue;
		if (log)
		{
			log << "[frame " << m_frameCounter
			    << " sev=" << static_cast<int>(msg->Severity)
			    << " id="  << static_cast<int>(msg->ID)
			    << "] "    << (msg->pDescription ? msg->pDescription : "")
			    << std::endl;
		}
	}
	m_infoQueue->ClearStoredMessages();
}
