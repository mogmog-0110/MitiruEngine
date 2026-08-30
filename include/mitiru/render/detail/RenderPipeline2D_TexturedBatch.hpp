#pragma once
// このヘッダは RenderPipeline2D.hpp から include される。直接 include しないこと。
//
// テクスチャ付きスプライトのバッチ描画 (DX12)。
//   • ensureSpriteTexture。render::Texture を GPU テクスチャ+SRV にキャッシュ
//   • submitTexturedBatch。texHandle のテクスチャをバインドして頂点バッチを描画
// base 2D root signature は既に SRV table(t0)+sampler(s0) を持ち、shader は
// uUseTexture!=0 で t0 をサンプルする。よって shader/PSO/root sig は無改造で、
// SRV を実テクスチャに差し替え uUseTexture=1 にするだけで textured 描画になる。

#ifdef _WIN32

namespace mitiru::render
{

inline std::uint32_t RenderPipeline2D::ensureSpriteTexture(
	const void* key, int w, int h, const std::uint8_t* rgba, bool contentMayChange)
{
	if (!m_valid || !m_useDx12Path || !m_dx12NativeDevice ||
	    w <= 0 || h <= 0 || rgba == nullptr)
	{
		return 0;
	}

	auto* device = m_dx12NativeDevice.Get();

	// ── 静的テクスチャ (contentMayChange=false): key(ポインタ) + 寸法 + pixel 先頭ポインタ で判定 ──
	// (drawSprite の render::Texture 等)。cache hit は即返し、毎フレームの全画素ハッシュを避ける。
	// 巨大スプライトシートを毎フレーム描く一般ケースでの CPU 浪費 (regression) を防ぐ。
	// srcPtr も照合する理由: sprite hot-reload は同じ Texture スロットに別画像を読み直すため key は
	// 不変だが pixels().data() が変わる。これを見ないと古い GPU テクスチャを返し続ける (実バグだった)。
	if (!contentMayChange)
	{
		// 直前と同じ texture (key,w,h,srcPtr) なら map find を省く。
		if (m_lastSpriteTexHandle != 0 && key == m_lastSpriteTexKey &&
		    w == m_lastSpriteTexW && h == m_lastSpriteTexH && rgba == m_lastSpriteTexSrc)
		{
			return m_lastSpriteTexHandle;
		}
		auto sit = m_dx12SpriteTexLookup.find(key);
		if (sit != m_dx12SpriteTexLookup.end())
		{
			const auto& cached = m_dx12SpriteTextures[sit->second];
			if (cached.tex && cached.w == w && cached.h == h && cached.srcPtr == rgba)
			{
				const std::uint32_t handle = sit->second + 1;
				m_lastSpriteTexKey = key; m_lastSpriteTexW = w; m_lastSpriteTexH = h;
				m_lastSpriteTexSrc = rgba; m_lastSpriteTexHandle = handle;
				return handle;
			}
			// srcPtr が変わった = 内容差し替え (hot-reload) → 下のアップロードで作り直す
		}
	}

	// ── 動的テクスチャ (contentMayChange=true): pixel 内容の指紋 (FNV-1a 64bit) で変化検出 (#19b) ──
	// 同じアドレスに毎フレーム作り直す動的テクスチャ (drawPixelGrid 等) で古い GPU 内容を防ぐ。
	// 静的パスではここを通らない (上で即返し済み)。
	std::uint64_t contentHash = 1469598103934665603ull;
	if (contentMayChange)
	{
		const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
		for (std::size_t i = 0; i < bytes; ++i)
		{
			contentHash ^= static_cast<std::uint64_t>(rgba[i]);
			contentHash *= 1099511628211ull;
		}

		// キャッシュ判定: key+(w,h)+内容ハッシュ が一致すれば再アップロードしない。
		auto it = m_dx12SpriteTexLookup.find(key);
		if (it != m_dx12SpriteTexLookup.end())
		{
			const auto& cached = m_dx12SpriteTextures[it->second];
			if (cached.tex && cached.w == w && cached.h == h && cached.contentHash == contentHash)
			{
				return it->second + 1;
			}
			// 寸法 or 内容が変わった: 同スロットに作り直す (下のアップロードへ落ちる)。
		}
	}

	m_lastSpriteTexHandle = 0;   // upload で slot が変わるので inline cache を無効化

	// ── default-heap texture (COPY_DEST) を作る ──
	D3D12_HEAP_PROPERTIES texHp = {};
	texHp.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width            = static_cast<UINT64>(w);
	texDesc.Height           = static_cast<UINT>(h);
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels        = 1;
	texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	Microsoft::WRL::ComPtr<ID3D12Resource> newTex;
	if (FAILED(device->CreateCommittedResource(
			&texHp, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&newTex))))
	{
		return 0;
	}

	// ── upload heap (row-pitch を 256 align) ──
	const UINT rowPitch =
		(static_cast<UINT>(w) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
		& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
	const UINT uploadSize = rowPitch * static_cast<UINT>(h);

	Microsoft::WRL::ComPtr<ID3D12Resource> newUpload =
		createUploadBufferDx12(device, uploadSize);
	if (!newUpload) { return 0; }

	// pixel data を upload heap へ (行ごとに aligned コピー)。
	{
		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		if (FAILED(newUpload->Map(0, &readRange, &mapped))) { return 0; }
		auto* dst = static_cast<std::uint8_t*>(mapped);
		const UINT srcRowBytes = static_cast<UINT>(w) * 4u;
		for (int row = 0; row < h; ++row)
		{
			std::memcpy(dst + static_cast<std::size_t>(row) * rowPitch,
			            rgba + static_cast<std::size_t>(row) * srcRowBytes,
			            srcRowBytes);
		}
		const D3D12_RANGE writeRange = {0, uploadSize};
		newUpload->Unmap(0, &writeRange);
	}

	// ── 1-slot shader-visible SRV heap + SRV ──
	D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
	dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 1;
	dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newSrvHeap;
	if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&newSrvHeap))))
	{
		return 0;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels     = 1;
	device->CreateShaderResourceView(
		newTex.Get(), &srvDesc,
		newSrvHeap->GetCPUDescriptorHandleForHeapStart());

	// ── copy + barrier(COPY_DEST→PSR) を記録・実行・待機 (cache miss 時の同期 upload) ──
	// cold path: 全 in-flight を drain してから slot 0 の allocator を使う。
	waitDx12Fence();
	m_dx12Alloc[0]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[0].Get(), nullptr); // copy のみ; PSO 不要

	D3D12_TEXTURE_COPY_LOCATION copyDst = {};
	copyDst.pResource        = newTex.Get();
	copyDst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	copyDst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION copySrc = {};
	copySrc.pResource                          = newUpload.Get();
	copySrc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	copySrc.PlacedFootprint.Offset             = 0;
	copySrc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
	copySrc.PlacedFootprint.Footprint.Width    = static_cast<UINT>(w);
	copySrc.PlacedFootprint.Footprint.Height   = static_cast<UINT>(h);
	copySrc.PlacedFootprint.Footprint.Depth    = 1;
	copySrc.PlacedFootprint.Footprint.RowPitch = rowPitch;
	m_dx12Cl->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource   = newTex.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_dx12Cl->ResourceBarrier(1, &barrier);

	m_dx12Cl->Close();
	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);
	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[0] = m_dx12FenceValue;
	waitDx12Fence();

	// ── キャッシュ格納 (寸法変化なら同スロット上書き) ──
	Dx12SpriteTexture entry;
	entry.tex     = std::move(newTex);
	entry.upload  = std::move(newUpload);
	entry.srvHeap = std::move(newSrvHeap);
	entry.w       = w;
	entry.h       = h;
	entry.key     = key;
	entry.srcPtr  = rgba;
	entry.contentHash = contentHash;

	// 既存 key なら同スロット上書き (寸法/内容変化)、無ければ新規追加。
	// (static/dynamic どちらの経路からも到達するため、ここで lookup し直す)
	std::uint32_t index;
	auto storeIt = m_dx12SpriteTexLookup.find(key);
	if (storeIt != m_dx12SpriteTexLookup.end())
	{
		index = storeIt->second;
		m_dx12SpriteTextures[index] = std::move(entry);
	}
	else
	{
		index = static_cast<std::uint32_t>(m_dx12SpriteTextures.size());
		m_dx12SpriteTextures.push_back(std::move(entry));
		m_dx12SpriteTexLookup.emplace(key, index);
	}
	return index + 1;
}

inline void RenderPipeline2D::submitTexturedBatch(
	const std::vector<Vertex2D>& vertices,
	const std::vector<std::uint32_t>& indices,
	std::uint32_t texHandle)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue) { return; }
	if (vertices.empty() || indices.empty()) { return; }
	if (texHandle == 0 || texHandle > m_dx12SpriteTextures.size()) { return; }
	const auto& entry = m_dx12SpriteTextures[texHandle - 1];
	if (!entry.srvHeap) { return; }

	// pixel-art の鮮鋭さのため point-filter variant を優先 (無ければ linear)。
	ID3D12RootSignature* rootSig = m_dx12RootSig.Get();
	ID3D12PipelineState* pso     = m_dx12Pipeline.Get();
	if (m_dx12PointRootSig && m_dx12PointPipeline)
	{
		rootSig = m_dx12PointRootSig.Get();
		pso     = m_dx12PointPipeline.Get();
	}

	// ring slot を確保し、その slot の前回 GPU 完了だけ待つ
	const int s = acquireDx12Slot();

	// uUseTexture = 1 (slot s 専用 CB なので前 GPU 読み取りと race しない)。
	{
		const float psOn[4] = {1.0f, 0.0f, 0.0f, 0.0f};
		updateCbDx12(m_dx12PsCb[s].Get(), psOn, sizeof(psOn));
	}

	const auto vbSize = static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t));
	updateDx12Buffer(m_dx12VertexBuffer[s], m_dx12VbCapacity[s], vertices.data(), vbSize);
	updateDx12Buffer(m_dx12IndexBuffer[s],  m_dx12IbCapacity[s], indices.data(),  ibSize);

	auto* swapChain = m_dx12Device->getSwapChain();
	if (!swapChain) { return; }
	auto* rt = dynamic_cast<gfx::Dx12RenderTarget*>(swapChain->backBuffer());
	if (!rt) { return; }
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	// MSAA 中間 RT へ描くときは MSAA 変種へ差し替える (linear/point の選択は維持)。
	if (rt->sampleCount() == static_cast<int>(gfx::Dx12MsaaTarget::kSampleCount))
	{
		const bool usingPoint = (pso == m_dx12PointPipeline.Get());
		ID3D12PipelineState* msaa = usingPoint ? m_dx12PointPipelineMsaa.Get()
		                                       : m_dx12PipelineMsaa.Get();
		if (!msaa) { return; } // MSAA 変種が無い — 安全にスキップ
		pso = msaa;
	}

	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), pso);

	m_dx12Cl->SetGraphicsRootSignature(rootSig);
	m_dx12Cl->SetPipelineState(pso);
	m_dx12Cl->SetGraphicsRootConstantBufferView(0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(1, m_dx12PsCb[s]->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { entry.srvHeap.Get() };
	m_dx12Cl->SetDescriptorHeaps(1, heaps);
	m_dx12Cl->SetGraphicsRootDescriptorTable(
		2, entry.srvHeap->GetGPUDescriptorHandleForHeapStart());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width    = viewportWidth();
	vp.Height   = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = { 0, 0,
		static_cast<LONG>(viewportWidth()),
		static_cast<LONG>(viewportHeight()) };
	m_dx12Cl->RSSetScissorRects(1, &sci);

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_dx12VertexBuffer[s]->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbSize;
	vbv.StrideInBytes  = sizeof(Vertex2D);
	m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_dx12IndexBuffer[s]->GetGPUVirtualAddress();
	ibv.SizeInBytes    = ibSize;
	ibv.Format         = DXGI_FORMAT_R32_UINT;
	m_dx12Cl->IASetIndexBuffer(&ibv);

	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_dx12Cl->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);

	m_dx12Cl->Close();
	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);
	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[s] = m_dx12FenceValue;
	// uUseTexture の 0 への復帰は次の submitBatchDx12 が冒頭で行う。
}

} // namespace mitiru::render

#else // !_WIN32

namespace mitiru::render
{
// 非 Windows backend は textured batch 未対応 (supportsTexturedBatch()==false)。
// Screen は per-pixel fallback を使うため、これらは呼ばれない安全スタブ。
inline std::uint32_t RenderPipeline2D::ensureSpriteTexture(
	const void*, int, int, const std::uint8_t*, bool)
{
	return 0;
}

inline void RenderPipeline2D::submitTexturedBatch(
	const std::vector<Vertex2D>&, const std::vector<std::uint32_t>&, std::uint32_t)
{
}
} // namespace mitiru::render

#endif // _WIN32
