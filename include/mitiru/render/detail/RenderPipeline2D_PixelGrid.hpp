#pragma once
// このヘッダは RenderPipeline2D.hpp からインクルードされる — 直接インクルード禁止。

#ifdef _WIN32

#include <mitiru/gfx/dx11/Dx11Texture.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>

namespace mitiru::render
{

inline void RenderPipeline2D::submitPixelGrid(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pw, int ph,
	float screenW, float screenH,
	PixelArtFilter filter)
{
	(void)screenW; (void)screenH; // viewport サイズは別途管理している
	if (!m_valid) return;

	if (m_useDx12Path)
	{
		submitPixelGridDx12(dest, pixels, pw, ph, filter);
		return;
	}

	// NOTE: DX11 path は baked-in の D3D11_FILTER_MIN_MAG_MIP_POINT sampler を
	// 使う (pixel-art content には既に正しい) ため、ここでは `filter` を意図的に
	// 無視する。DX11 path の filter 切替は未対応。
	(void)filter;

	// DX11 以外の backend: no-op。
	if (!m_dx11Device || !m_dx11Context)
	{
		return;
	}

	// ── 1. Texture cache: 寸法が変わった時のみ再確保する ──────────
	if (!m_pgTexture || pw != m_pgTexW || ph != m_pgTexH)
	{
		// uint32_t* の pixel buffer から span<const uint8_t> を構築する。
		// byte 順: RGBA — byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A。
		// little-endian hardware では uint32_t は 0xAABBGGRR と読めるが、
		// DXGI_FORMAT_R8G8B8A8_UNORM は byte を memory order で解釈するため、
		// byte[0] が R に対応する。consumer はそれに従って pixel を埋めること。
		const auto byteCount = static_cast<std::size_t>(pw) *
		                       static_cast<std::size_t>(ph) * 4u;
		const auto* bytePtr = reinterpret_cast<const std::uint8_t*>(pixels);

		// m_pgTexW/H は成功時のみ更新する (例外安全性: createFromData が
		// throw しても旧 texture は valid なまま残る)。
		auto newTex = gfx::Dx11Texture::createFromData(
			m_dx11Device, pw, ph,
			std::span<const std::uint8_t>(bytePtr, byteCount));

		m_pgTexture = std::make_unique<gfx::Dx11Texture>(std::move(newTex));
		m_pgTexW = pw;
		m_pgTexH = ph;
	}
	else
	{
		// ── 2. 同寸法: UpdateSubresource で新しい pixel data を流し込む ──
		m_dx11Context->UpdateSubresource(
			m_pgTexture->getTexture(),
			0,        // subresource index
			nullptr,  // texture 全体
			pixels,
			static_cast<UINT>(pw) * sizeof(std::uint32_t),
			0);
	}

	// ── 3. point-filter sampler を遅延生成する ─────────────────────────
	if (!m_pgSampler)
	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD         = 0.0f;
		sd.MaxLOD         = D3D11_FLOAT32_MAX;

		HRESULT hr = m_dx11Device->CreateSamplerState(
			&sd, m_pgSampler.GetAddressOf());
		if (FAILED(hr))
		{
			return; // sampler 無しでは描画できない。
		}
	}

	// ── 4. 4-vertex / 6-index の textured quad を構築する ─────────────────────────
	// Vertex layout: float2 position, float2 texCoord, float4 color (白)
	// 既存の Vertex2D struct および DEFAULT_VS_2D / DEFAULT_PS_2D に一致する。
	const float x0 = dest.x();
	const float y0 = dest.y();
	const float x1 = dest.x() + dest.width();
	const float y1 = dest.y() + dest.height();

	const Vertex2D verts[4] = {
		{ {x0, y0}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f} }, // TL
		{ {x1, y0}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f} }, // TR
		{ {x1, y1}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} }, // BR
		{ {x0, y1}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} }, // BL
	};
	const std::uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

	// 既存の DX11 vertex/index buffer 経由で quad geometry を upload する。
	// submitBatchDx11 と同じ infrastructure path を使う。
	const auto vbSize = static_cast<std::uint32_t>(4 * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(6 * sizeof(std::uint32_t));

	if (vbSize > m_vbCapacity)
	{
		m_vertexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device, gfx::BufferType::Vertex, vbSize, true);
		m_vbCapacity = vbSize;
	}
	if (ibSize > m_ibCapacity)
	{
		m_indexBuffer = std::make_unique<gfx::Dx11Buffer>(
			m_dx11Device, gfx::BufferType::Index, ibSize, true);
		m_ibCapacity = ibSize;
	}

	m_vertexBuffer->update(m_dx11Context, verts, vbSize);
	m_indexBuffer->update(m_dx11Context, indices, ibSize);

	// ── 5. SRV + sampler を bind し、uUseTexture = 1.0f を設定する ────────────────────
	ID3D11ShaderResourceView* srv = m_pgTexture->getSRV();
	m_dx11Context->PSSetShaderResources(0, 1, &srv);

	ID3D11SamplerState* sampler = m_pgSampler.Get();
	m_dx11Context->PSSetSamplers(0, 1, &sampler);

	// PS constant buffer に uUseTexture = 1.0f を書き込む (b0: float uUseTexture, float3 _pad)。
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst, sizeof(psConst));
	}

	// ── 6. draw call を発行する ────────────────────────────────────────────
	m_commandList->begin();
	m_commandList->setViewport(viewportWidth(), viewportHeight());
	m_commandList->setPipeline(m_pipeline.get());
	m_commandList->setVSConstantBuffer(0, m_constantBuffer.get());
	if (m_psConstantBuffer)
	{
		m_commandList->setPSConstantBuffer(0, m_psConstantBuffer.get());
	}
	m_commandList->setVertexBuffer(m_vertexBuffer.get());
	m_commandList->setIndexBuffer(m_indexBuffer.get());
	m_commandList->drawIndexed(6, 0, 0);

	// ── 7. SRV binding をクリアする (texture が後続 pass に漏れるのを防ぐ) ──
	// これらの raw immediate-context call は end() の前に発行されるため、
	// draw の後・同じ論理 frame submission 内で順序付けされる。Dx11CommandList は
	// 薄い immediate-context facade (D3D11 deferred context は無し) なので
	// 順序が保証される。
	ID3D11ShaderResourceView* nullSrv = nullptr;
	m_dx11Context->PSSetShaderResources(0, 1, &nullSrv);

	// 後続の non-textured draw のため uUseTexture = 0.0f に戻す。
	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst0, sizeof(psConst0));
	}

	m_commandList->end();
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12 pixel-grid 実装
// ─────────────────────────────────────────────────────────────────────────────
//
// 方針:
//   • COPY_DEST / PIXEL_SHADER_RESOURCE 状態の default-heap TEXTURE2D。
//   • 毎フレーム CopyTextureRegion source として使う upload-heap buffer。
//   • 両 resource は (pw, ph) が変わった時のみ再確保する。
//   • 専用の 1-slot shader-visible SRV heap (m_dx12PgSrvHeap)。
//   • Filter 選択:
//       - PixelArtFilter::Linear → m_dx12RootSig + m_dx12Pipeline (base 2D path
//         と共有、sampler s0 = D3D12_FILTER_MIN_MAG_MIP_LINEAR)。
//       - PixelArtFilter::Point  → m_dx12PointRootSig + m_dx12PointPipeline
//         (createFromDx12 で base PSO と並べて eager 構築。base PSO と同じ shader
//         / blend / input layout で、root signature の static sampler だけが
//         異なる — D3D12_FILTER_MIN_MAG_MIP_POINT)。
//   • Fallback 規約: point variant が createFromDx12 時に構築失敗していた場合
//     (m_dx12PointPipeline == nullptr)、Point リクエストは透過的に linear PSO へ
//     フォールバックする。視覚品質は劣化する (pixel-art に bilinear blur) が
//     draw 自体は完了する — point variant は品質最適化であって機能要件ではない。
//   • この draw path では遅延初期化を行わない (エンジン規約: draw() 内の
//     遅延初期化 / null-skip は禁止)。
//
inline void RenderPipeline2D::submitPixelGridDx12(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pw, int ph,
	PixelArtFilter filter)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue)
		return;

	// filter に基づいて root signature / PSO を選ぶ。point variant は
	// createFromDx12 で eager 構築される; その構築が失敗していた場合 (member が null)
	// は透過的に linear PSO へフォールバックし、Point リクエストでも draw は成立する
	// — 失われるのは pixel-art の鮮鋭さだけで、draw 自体は失われない。
	ID3D12RootSignature* activeRootSig = m_dx12RootSig.Get();
	ID3D12PipelineState* activePso     = m_dx12Pipeline.Get();
	if (filter == PixelArtFilter::Point &&
		m_dx12PointPipeline && m_dx12PointRootSig)
	{
		activeRootSig = m_dx12PointRootSig.Get();
		activePso     = m_dx12PointPipeline.Get();
	}

	auto* device = m_dx12NativeDevice.Get();

	// ── 1. 寸法が変わった時に GPU resource を再確保する ────────────────
	if (!m_dx12PgTexture || pw != m_dx12PgTexW || ph != m_dx12PgTexH)
	{
		// Default-heap texture (初期状態 COPY_DEST)。
		D3D12_HEAP_PROPERTIES texHp = {};
		texHp.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width            = static_cast<UINT64>(pw);
		texDesc.Height           = static_cast<UINT>(ph);
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels        = 1;
		texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3D12Resource> newTex;
		if (FAILED(device->CreateCommittedResource(
				&texHp, D3D12_HEAP_FLAG_NONE, &texDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr, IID_PPV_ARGS(&newTex))))
		{
			return;
		}

		// Upload heap buffer: size = aligned row pitch * height。
		// DX12 は row pitch を D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256) に align する必要がある。
		const UINT rowPitch =
			(static_cast<UINT>(pw) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT uploadSize = rowPitch * static_cast<UINT>(ph);

		D3D12_HEAP_PROPERTIES upHp = {};
		upHp.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC upDesc = {};
		upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		upDesc.Width            = uploadSize;
		upDesc.Height           = 1;
		upDesc.DepthOrArraySize = 1;
		upDesc.MipLevels        = 1;
		upDesc.Format           = DXGI_FORMAT_UNKNOWN;
		upDesc.SampleDesc.Count = 1;
		upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		Microsoft::WRL::ComPtr<ID3D12Resource> newUpload;
		if (FAILED(device->CreateCommittedResource(
				&upHp, D3D12_HEAP_FLAG_NONE, &upDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr, IID_PPV_ARGS(&newUpload))))
		{
			return;
		}

		// SRV heap (descriptor 1 個、shader-visible)。
		D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
		dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		dhd.NumDescriptors = 1;
		dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newSrvHeap;
		if (FAILED(device->CreateDescriptorHeap(
				&dhd, IID_PPV_ARGS(&newSrvHeap))))
		{
			return;
		}

		// 新しい texture 用の SRV を作成する。
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels       = 1;
		device->CreateShaderResourceView(
			newTex.Get(), &srvDesc,
			newSrvHeap->GetCPUDescriptorHandleForHeapStart());

		m_dx12PgTexture  = std::move(newTex);
		m_dx12PgUpload   = std::move(newUpload);
		m_dx12PgSrvHeap  = std::move(newSrvHeap);
		m_dx12PgTexW     = pw;
		m_dx12PgTexH     = ph;
		m_dx12PgTexReady = false; // texture は COPY_DEST 状態で開始する
	}

	// cold path: 共有 upload (m_dx12PgUpload) / slot 0 buffer を書く前に全 in-flight を
	// drain する。前 frame の GPU が m_dx12PgUpload を copy source として読み終える前に
	// CPU が上書きする hazard を防ぐ。
	waitDx12Fence();

	// ── 2. pixel data を upload heap にコピーする (row-pitch aligned) ──────────
	{
		const UINT rowPitch =
			(static_cast<UINT>(pw) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		if (FAILED(m_dx12PgUpload->Map(0, &readRange, &mapped))) return;

		auto* dst = static_cast<std::uint8_t*>(mapped);
		const auto* src = reinterpret_cast<const std::uint8_t*>(pixels);
		const UINT srcRowBytes = static_cast<UINT>(pw) * 4u;
		for (int row = 0; row < ph; ++row)
		{
			std::memcpy(dst + row * rowPitch, src + row * srcRowBytes, srcRowBytes);
		}

		const D3D12_RANGE writeRange = {0, rowPitch * static_cast<UINT>(ph)};
		m_dx12PgUpload->Unmap(0, &writeRange);
	}

	// ── 3. quad geometry を構築する ────────────────────────────────────────────
	const float x0 = dest.x();
	const float y0 = dest.y();
	const float x1 = dest.x() + dest.width();
	const float y1 = dest.y() + dest.height();

	const Vertex2D verts[4] = {
		{ {x0, y0}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		{ {x1, y0}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		{ {x1, y1}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		{ {x0, y1}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
	};
	const std::uint32_t quadIdx[6] = { 0, 1, 2, 0, 2, 3 };

	const auto vbSize = static_cast<std::uint32_t>(4 * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(6 * sizeof(std::uint32_t));
	// drain 済みなので slot 0 の buffer を安全に使える。
	updateDx12Buffer(m_dx12VertexBuffer[0], m_dx12VbCapacity[0], verts, vbSize);
	updateDx12Buffer(m_dx12IndexBuffer[0],  m_dx12IbCapacity[0], quadIdx, ibSize);

	// ── 4. PS constant buffer を更新する: uUseTexture = 1.0f ─────────────────
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb[0].Get(), psConst, sizeof(psConst));

	// ── 5. command を記録する (冒頭で drain 済み) ───────────────────
	auto* swapChain = m_dx12Device->getSwapChain();
	if (!swapChain) return;
	auto* rt = dynamic_cast<gfx::Dx12RenderTarget*>(swapChain->backBuffer());
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	m_dx12Alloc[0]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[0].Get(), activePso);

	// texture を遷移: PIXEL_SHADER_RESOURCE → COPY_DEST
	// 初回使用時はスキップ: texture は COPY_DEST 状態で作成済み。
	if (m_dx12PgTexReady)
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = m_dx12PgTexture.Get();
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_dx12Cl->ResourceBarrier(1, &b);
	}

	// CopyTextureRegion: upload buffer → default texture へ
	{
		const UINT rowPitch =
			(static_cast<UINT>(pw) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource        = m_dx12PgTexture.Get();
		dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource                              = m_dx12PgUpload.Get();
		src.Type                                   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset                 = 0;
		src.PlacedFootprint.Footprint.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
		src.PlacedFootprint.Footprint.Width        = static_cast<UINT>(pw);
		src.PlacedFootprint.Footprint.Height       = static_cast<UINT>(ph);
		src.PlacedFootprint.Footprint.Depth        = 1;
		src.PlacedFootprint.Footprint.RowPitch     = rowPitch;

		m_dx12Cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}

	// texture を遷移: COPY_DEST → PIXEL_SHADER_RESOURCE
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = m_dx12PgTexture.Get();
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_dx12Cl->ResourceBarrier(1, &b);
	}
	m_dx12PgTexReady = true;

	// quad を描画 — 選択した root signature + PSO (linear または point variant) を bind する。
	m_dx12Cl->SetGraphicsRootSignature(activeRootSig);
	m_dx12Cl->SetPipelineState(activePso);

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12PsCb[0]->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { m_dx12PgSrvHeap.Get() };
	m_dx12Cl->SetDescriptorHeaps(1, heaps);
	m_dx12Cl->SetGraphicsRootDescriptorTable(
		2, m_dx12PgSrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.Width    = viewportWidth();
	vp.Height   = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = {
		0, 0,
		static_cast<LONG>(viewportWidth()),
		static_cast<LONG>(viewportHeight())
	};
	m_dx12Cl->RSSetScissorRects(1, &sci);

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = m_dx12VertexBuffer[0]->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbSize;
	vbv.StrideInBytes  = sizeof(Vertex2D);
	m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_dx12IndexBuffer[0]->GetGPUVirtualAddress();
	ibv.SizeInBytes    = ibSize;
	ibv.Format         = DXGI_FORMAT_R32_UINT;
	m_dx12Cl->IASetIndexBuffer(&ibv);

	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_dx12Cl->DrawIndexedInstanced(6, 1, 0, 0, 0);

	m_dx12Cl->Close();

	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);

	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[0] = m_dx12FenceValue;

	// ── 6. uUseTexture = 0.0f に戻す ────────────────────────────────────
	// drain して GPU の旧値読み取り完了を待ってから slot 0 CB を戻す
	// (次の submitBatch* がどのみち上書きするが state を clean に保つ)。
	waitDx12Fence();
	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb[0].Get(), psConst0, sizeof(psConst0));
}

} // namespace mitiru::render

#endif // _WIN32
