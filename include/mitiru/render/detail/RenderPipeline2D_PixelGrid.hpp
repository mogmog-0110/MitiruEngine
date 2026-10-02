#pragma once
// RenderPipeline2D.hpp からだけインクルードする。

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

	// DX11 では pixel art 向けに固定した D3D11_FILTER_MIN_MAG_MIP_POINT sampler を使うため、filter を無視する。
	(void)filter;

	if (!m_dx11Device || !m_dx11Context)
	{
		return;
	}

	// ── 1. Texture cache ──
	if (!m_pgTexture || pw != m_pgTexW || ph != m_pgTexH)
	{
		// Pixel buffer は RGBA の byte 順で格納する。
		// Little endian では uint32_t の値は 0xAABBGGRR になる。
		const auto byteCount = static_cast<std::size_t>(pw) *
		                       static_cast<std::size_t>(ph) * 4u;
		const auto* bytePtr = reinterpret_cast<const std::uint8_t*>(pixels);

		// Texture と寸法は createFromData の成功後に更新し、例外時は以前の texture を残す。
		auto newTex = gfx::Dx11Texture::createFromData(
			m_dx11Device, pw, ph,
			std::span<const std::uint8_t>(bytePtr, byteCount));

		m_pgTexture = std::make_unique<gfx::Dx11Texture>(std::move(newTex));
		m_pgTexW = pw;
		m_pgTexH = ph;
	}
	else
	{
		// ── 2. Pixel data の更新 ──
		m_dx11Context->UpdateSubresource(
			m_pgTexture->getTexture(),
			0,        // subresource index
			nullptr,  // texture 全体
			pixels,
			static_cast<UINT>(pw) * sizeof(std::uint32_t),
			0);
	}

	// ── 3. Point-filter sampler の遅延生成 ──
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

	// ── 4. Textured quad の構築 ──
	// Vertex layout は float2 position、float2 texCoord、float4 color で、Vertex2D と既定の 2D shader に合わせる。
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

	// ── 5. SRV と sampler の設定 ──
	ID3D11ShaderResourceView* srv = m_pgTexture->getSRV();
	m_dx11Context->PSSetShaderResources(0, 1, &srv);

	ID3D11SamplerState* sampler = m_pgSampler.Get();
	m_dx11Context->PSSetSamplers(0, 1, &sampler);

	// PS constant buffer の b0 は float uUseTexture、float3 padding の並び。
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst, sizeof(psConst));
	}

	// ── 6. Draw call ──
	m_commandList->begin();
	// ICommandList::setViewport は TopLeftX/Y を持たないため、letterbox と pillarbox の offset は D3D11 に直接設定する。
	D3D11_VIEWPORT pgVp = {};
	pgVp.TopLeftX = viewportOffsetX();
	pgVp.TopLeftY = viewportOffsetY();
	pgVp.Width = viewportWidth();
	pgVp.Height = viewportHeight();
	pgVp.MinDepth = 0.0f;
	pgVp.MaxDepth = 1.0f;
	m_dx11Context->RSSetViewports(1, &pgVp);
	m_commandList->setPipeline(m_pipeline.get());
	m_commandList->setVSConstantBuffer(0, m_constantBuffer.get());
	if (m_psConstantBuffer)
	{
		m_commandList->setPSConstantBuffer(0, m_psConstantBuffer.get());
	}
	m_commandList->setVertexBuffer(m_vertexBuffer.get());
	m_commandList->setIndexBuffer(m_indexBuffer.get());
	m_commandList->drawIndexed(6, 0, 0);

	// ── 7. SRV binding の解除 ──
	// Immediate context への呼び出しは end() より前に行い、draw と同じ frame submission 内で順序を保つ。
	ID3D11ShaderResourceView* nullSrv = nullptr;
	m_dx11Context->PSSetShaderResources(0, 1, &nullSrv);

	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	if (m_psConstantBuffer)
	{
		m_psConstantBuffer->update(m_dx11Context, psConst0, sizeof(psConst0));
	}

	m_commandList->end();
}

// DX12 pixel-grid 実装
// Texture は default heap、毎 frame の転送元は upload heap に置く。
// Texture と upload buffer は寸法が変わったときだけ再確保する。
// SRV には専用の 1-slot shader-visible heap を使う。
// Linear と Point は static sampler の異なる root signature と PSO を使い分ける。
// Point PSO を作れなかった場合は Linear PSO で描画する。
// Draw path では遅延初期化と null-skip を行わない。
inline void RenderPipeline2D::submitPixelGridDx12(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pw, int ph,
	PixelArtFilter filter)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue)
		return;

	// Point PSO を作れなかった場合は Linear PSO で描画する。
	ID3D12RootSignature* activeRootSig = m_dx12RootSig.Get();
	ID3D12PipelineState* activePso     = m_dx12Pipeline.Get();
	if (filter == PixelArtFilter::Point &&
		m_dx12PointPipeline && m_dx12PointRootSig)
	{
		activeRootSig = m_dx12PointRootSig.Get();
		activePso     = m_dx12PointPipeline.Get();
	}

	auto* device = m_dx12NativeDevice.Get();

	// ── 1. GPU resource の再確保 ──
	if (!m_dx12PgTexture || pw != m_dx12PgTexW || ph != m_dx12PgTexH)
	{
		// 下で差し替える SRV ヒープは前フレームの描画がまだ参照しているので、先に読み終わりを待つ
		waitDx12Fence();
		// Texture は COPY_DEST 状態の default heap に置く。
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

		gfx::GpuResource newTex;
		if (FAILED(gfx::createGpuResource(device, D3D12_HEAP_TYPE_DEFAULT, texDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, newTex)))
		{
			return;
		}

		// Row pitch は D3D12_TEXTURE_DATA_PITCH_ALIGNMENT の 256 byte 境界にそろえる。
		const UINT rowPitch =
			(static_cast<UINT>(pw) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
			& ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT uploadSize = rowPitch * static_cast<UINT>(ph);

		gfx::GpuResource newUpload;
		if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, uploadSize,
			D3D12_RESOURCE_STATE_GENERIC_READ, newUpload)))
		{
			return;
		}

		// SRV には descriptor 1 個の shader-visible heap を使う。
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

	// 共有 upload buffer を上書きする前に、前 frame の GPU による読み取り完了を待つ。
	waitDx12Fence();

	// ── 2. Pixel data の転送 ──
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

	// ── 3. Quad geometry の構築 ──
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
	// GPU の完了を待った後なので、slot 0 の buffer を再利用できる。
	updateDx12Buffer(m_dx12VertexBuffer[0], m_dx12VbCapacity[0], verts, vbSize);
	updateDx12Buffer(m_dx12IndexBuffer[0],  m_dx12IbCapacity[0], quadIdx, ibSize);

	// ── 4. PS constant buffer の更新 ──
	const float psConst[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb[0].Get(), psConst, sizeof(psConst));

	// ── 5. Command の記録 ──
	auto* rt = dx12RenderTarget();
	if (!rt) return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();

	// MSAA 中間 RT では、Linear と Point の選択を保ったまま MSAA 用 PSO に差し替える。
	if (rt->sampleCount() == static_cast<int>(gfx::Dx12MsaaTarget::kSampleCount))
	{
		const bool usingPoint = (activePso == m_dx12PointPipeline.Get());
		ID3D12PipelineState* msaa = usingPoint ? m_dx12PointPipelineMsaa.Get()
		                                       : m_dx12PipelineMsaa.Get();
		if (!msaa) return; // MSAA 変種が無い — 安全にスキップ
		activePso = msaa;
	}

	m_dx12Alloc[0]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[0].Get(), activePso);

	// 初回は COPY_DEST 状態で作成済みなので、PIXEL_SHADER_RESOURCE からの遷移を行わない。
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

	// Letterbox と pillarbox の offset は viewport の TopLeftX/Y に入れ、scissor にも反映する。
	D3D12_VIEWPORT vp = {};
	vp.TopLeftX = viewportOffsetX();
	vp.TopLeftY = viewportOffsetY();
	vp.Width    = viewportWidth();
	vp.Height   = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);

	D3D12_RECT sci = {
		static_cast<LONG>(viewportOffsetX()),
		static_cast<LONG>(viewportOffsetY()),
		static_cast<LONG>(viewportOffsetX() + viewportWidth()),
		static_cast<LONG>(viewportOffsetY() + viewportHeight())
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

	// ── 6. uUseTexture の復元 ──
	// GPU が以前の値を読み終えてから、slot 0 の constant buffer を 0.0f に戻す。
	waitDx12Fence();
	const float psConst0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	updateCbDx12(m_dx12PsCb[0].Get(), psConst0, sizeof(psConst0));
}

} // namespace mitiru::render

#endif // _WIN32
