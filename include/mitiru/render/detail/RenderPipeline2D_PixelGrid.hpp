#pragma once
// This header is included by RenderPipeline2D.hpp — do not include directly.

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
	(void)screenW; (void)screenH; // viewport size is tracked separately
	if (!m_valid) return;

	if (m_useDx12Path)
	{
		submitPixelGridDx12(dest, pixels, pw, ph, filter);
		return;
	}

	// NOTE: DX11 path uses a baked-in D3D11_FILTER_MIN_MAG_MIP_POINT sampler
	// (already correct for pixel-art content), so `filter` is intentionally
	// ignored here. Plumbing it through DX11 is out of scope per task spec.
	(void)filter;

	// Non-DX11 backends: no-op.
	if (!m_dx11Device || !m_dx11Context)
	{
		return;
	}

	// ── 1. Texture cache: reallocate only when dimensions change ──────────
	if (!m_pgTexture || pw != m_pgTexW || ph != m_pgTexH)
	{
		// Build a span<const uint8_t> from the uint32_t* pixel buffer.
		// Byte order: RGBA — byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A.
		// On little-endian hardware a uint32_t reads as 0xAABBGGRR, but
		// DXGI_FORMAT_R8G8B8A8_UNORM interprets bytes in memory order,
		// so byte[0] maps to R. The consumer must fill pixels accordingly.
		const auto byteCount = static_cast<std::size_t>(pw) *
		                       static_cast<std::size_t>(ph) * 4u;
		const auto* bytePtr = reinterpret_cast<const std::uint8_t*>(pixels);

		// Update m_pgTexW/H only on success (exception safety: if
		// createFromData throws, the old texture remains valid).
		auto newTex = gfx::Dx11Texture::createFromData(
			m_dx11Device, pw, ph,
			std::span<const std::uint8_t>(bytePtr, byteCount));

		m_pgTexture = std::make_unique<gfx::Dx11Texture>(std::move(newTex));
		m_pgTexW = pw;
		m_pgTexH = ph;
	}
	else
	{
		// ── 2. Same dimensions: stream new pixel data via UpdateSubresource ──
		m_dx11Context->UpdateSubresource(
			m_pgTexture->getTexture(),
			0,        // subresource index
			nullptr,  // full texture
			pixels,
			static_cast<UINT>(pw) * sizeof(std::uint32_t),
			0);
	}

	// ── 3. Lazily create the point-filter sampler ─────────────────────────
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
			return; // Cannot draw without sampler.
		}
	}

	// ── 4. Build 4-vertex / 6-index textured quad ─────────────────────────
	// Vertex layout: float2 position, float2 texCoord, float4 color (white)
	// Matches the existing Vertex2D struct and DEFAULT_VS_2D / DEFAULT_PS_2D.
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

	// Upload quad geometry via the existing DX11 vertex/index buffers,
	// using the same submitBatchDx11 infrastructure path.
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

	// ── 5. Bind SRV + sampler, set uUseTexture = 1.0f ────────────────────
	ID3D11ShaderResourceView* srv = m_pgTexture->getSRV();
	m_dx11Context->PSSetShaderResources(0, 1, &srv);

	ID3D11SamplerState* sampler = m_pgSampler.Get();
	m_dx11Context->PSSetSamplers(0, 1, &sampler);

	// Write uUseTexture = 1.0f into the PS constant buffer (b0: float uUseTexture, float3 _pad).
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst, sizeof(psConst));
	}

	// ── 6. Issue the draw call ────────────────────────────────────────────
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

	// ── 7. Clear SRV binding (prevent texture leaking into later passes) ──
	// These raw immediate-context calls are issued before end() so they are
	// ordered after the draw but still within the same logical frame
	// submission. Dx11CommandList is a thin immediate-context facade
	// (no D3D11 deferred contexts), so sequencing is guaranteed.
	ID3D11ShaderResourceView* nullSrv = nullptr;
	m_dx11Context->PSSetShaderResources(0, 1, &nullSrv);

	// Restore uUseTexture = 0.0f for subsequent non-textured draws.
	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst0, sizeof(psConst0));
	}

	m_commandList->end();
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12 pixel-grid implementation
// ─────────────────────────────────────────────────────────────────────────────
//
// Strategy:
//   • Default-heap TEXTURE2D in COPY_DEST / PIXEL_SHADER_RESOURCE states.
//   • Upload-heap buffer used as CopyTextureRegion source each frame.
//   • Both resources reallocated only when (pw, ph) changes.
//   • Dedicated 1-slot shader-visible SRV heap (m_dx12PgSrvHeap).
//   • Filter selection:
//       - PixelArtFilter::Linear → m_dx12RootSig + m_dx12Pipeline (shared with
//         the base 2D path, sampler s0 = D3D12_FILTER_MIN_MAG_MIP_LINEAR).
//       - PixelArtFilter::Point  → m_dx12PointRootSig + m_dx12PointPipeline
//         (eager-built alongside the base PSO in createFromDx12; same shaders
//         / blend / input layout as the base PSO, only the root signature's
//         static sampler differs — D3D12_FILTER_MIN_MAG_MIP_POINT).
//   • Fallback contract: if the point variant failed to build at
//     createFromDx12 time (m_dx12PointPipeline == nullptr), the Point request
//     transparently falls back to the linear PSO. Visual quality degrades
//     (bilinear blurring on pixel-art) but the draw still completes — the
//     point variant is a quality optimisation, not a functional requirement.
//   • No lazy initialisation in this draw path (engine rule:
//     `.claude/rules/mitiru-engine.md` — "Lazy initialisation / null-skip in
//     `draw()` is forbidden").
//
inline void RenderPipeline2D::submitPixelGridDx12(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pw, int ph,
	PixelArtFilter filter)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue)
		return;

	// Choose root signature / PSO based on filter. The point variant is
	// eager-built in createFromDx12; if that build failed (member is null) we
	// transparently fall back to the linear PSO so a Point request still
	// draws — only the sharpness of pixel-art is lost, never the draw itself.
	ID3D12RootSignature* activeRootSig = m_dx12RootSig.Get();
	ID3D12PipelineState* activePso     = m_dx12Pipeline.Get();
	if (filter == PixelArtFilter::Point &&
		m_dx12PointPipeline && m_dx12PointRootSig)
	{
		activeRootSig = m_dx12PointRootSig.Get();
		activePso     = m_dx12PointPipeline.Get();
	}

	auto* device = m_dx12NativeDevice.Get();

	// ── 1. Reallocate GPU resources when dimensions change ────────────────
	if (!m_dx12PgTexture || pw != m_dx12PgTexW || ph != m_dx12PgTexH)
	{
		// Default-heap texture (COPY_DEST initial state).
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

		// Upload heap buffer: size = aligned row pitch * height.
		// DX12 requires row pitch aligned to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256).
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

		// SRV heap (1 descriptor, shader-visible).
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

		// Create SRV for the new texture.
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
		m_dx12PgTexReady = false; // texture starts in COPY_DEST state
	}

	// ── 2. Copy pixel data into upload heap (row-pitch aligned) ──────────
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

	// ── 3. Build quad geometry ────────────────────────────────────────────
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
	updateDx12Buffer(m_dx12VertexBuffer, m_dx12VbCapacity, verts, vbSize);
	updateDx12Buffer(m_dx12IndexBuffer,  m_dx12IbCapacity, quadIdx, ibSize);

	// ── 4. Update PS constant buffer: uUseTexture = 1.0f ─────────────────
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb.Get(), psConst, sizeof(psConst));

	// ── 5. Wait for previous frame then record commands ───────────────────
	waitDx12Fence();

	auto* swapChain = m_dx12Device->getSwapChain();
	if (!swapChain) return;
	auto* rt = dynamic_cast<gfx::Dx12RenderTarget*>(swapChain->backBuffer());
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	m_dx12Alloc->Reset();
	m_dx12Cl->Reset(m_dx12Alloc.Get(), activePso);

	// Transition texture: PIXEL_SHADER_RESOURCE → COPY_DEST
	// Skip on first use: texture was created in COPY_DEST state.
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

	// CopyTextureRegion: upload buffer → default texture
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

	// Transition texture: COPY_DEST → PIXEL_SHADER_RESOURCE
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

	// Draw quad — bind chosen root signature + PSO (linear or point variant).
	m_dx12Cl->SetGraphicsRootSignature(activeRootSig);
	m_dx12Cl->SetPipelineState(activePso);

	m_dx12Cl->SetGraphicsRootConstantBufferView(
		0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(
		1, m_dx12PsCb->GetGPUVirtualAddress());

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
	vbv.BufferLocation = m_dx12VertexBuffer->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbSize;
	vbv.StrideInBytes  = sizeof(Vertex2D);
	m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = m_dx12IndexBuffer->GetGPUVirtualAddress();
	ibv.SizeInBytes    = ibSize;
	ibv.Format         = DXGI_FORMAT_R32_UINT;
	m_dx12Cl->IASetIndexBuffer(&ibv);

	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_dx12Cl->DrawIndexedInstanced(6, 1, 0, 0, 0);

	// ── 6. Restore uUseTexture = 0.0f ────────────────────────────────────
	// We patch the CB *after* Close/Execute so the restored value is visible
	// to the *next* batch draw. The CB write happens CPU-side (upload heap
	// map), so it must not race with the GPU read we just submitted.
	// Solution: close + execute first, then signal fence, then restore CB
	// (the fence ensures GPU has finished reading the old value before we
	// overwrite — but we signal *after* execute, so the restore happens
	// before the next waitDx12Fence). In practice the CB restore is safe
	// because the next submitBatch* call will overwrite it anyway; we still
	// do it for state cleanliness.

	m_dx12Cl->Close();

	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);

	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);

	// Restore PS constant buffer after fence signal (safe: next call to any
	// submit* will waitDx12Fence before touching the CB again).
	waitDx12Fence();
	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb.Get(), psConst0, sizeof(psConst0));
}

} // namespace mitiru::render

#endif // _WIN32
