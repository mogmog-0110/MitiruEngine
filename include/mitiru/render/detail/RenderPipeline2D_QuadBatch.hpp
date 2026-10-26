#pragma once

/// @file RenderPipeline2D_QuadBatch.hpp
/// @brief 四角形のレコードを GPU が 4 隅に広げて描く経路 (v61)
// RenderPipeline2D.hpp からだけ include する。四角形のレコード (Quad2D) を GPU が 4 隅に広げて描く経路 (v61)。
// 1 枚 48 バイトのレコードを upload heap に写し、DrawInstanced(6, 枚数) で描く。円・線・グラデーションなど
// 従来の頂点 + 添字の列は、同じ提出の中で描いた順に DrawIndexedInstanced で続ける。

#ifdef _WIN32

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/Log.hpp>

namespace mitiru::render
{

inline bool RenderPipeline2D::quadPipelineReady() const noexcept
{
	// 8x MSAA の変種が作れない環境では、通常の変種も MSAA の変種も無い組み合わせにしない
	return m_dx12QuadPso && ((m_dx12PipelineMsaa == nullptr) == (m_dx12QuadPsoMsaa == nullptr));
}

inline void RenderPipeline2D::buildDx12QuadResources(ID3D12Device* device)
{
	// 切り分け用: MITIRU_QUAD_BATCH=0 で従来の頂点バッチだけにする (四角形のレコードの絵との比べにも使う)
	if (env::value("MITIRU_QUAD_BATCH") == "0") { return; }
	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> compileErr;
	(void)gfx::compileDx12Shader(DEFAULT_QUAD_VS_2D, "VSMain", "vs_5_0", 0, &vsBlob, &compileErr);
	if (!vsBlob || !m_dx12PsBlob || !m_dx12RootSig)
	{
		MITIRU_LOG_WARN("RenderPipeline2D", "quad batch shader compile failed; falling back to vertex batches");
		return;
	}

	// 1 インスタンス = 1 レコード (Quad2D)。位置の四隅と UV の四隅は頂点シェーダーが SV_VertexID で選ぶ
	const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "QUADRECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "QUADUV",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
	};
	const UINT count = static_cast<UINT>(std::size(layout));

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	try
	{
		pso = buildDx12Pso(device, m_dx12RootSig.Get(), vsBlob.Get(), m_dx12PsBlob.Get(), layout, count);
	}
	catch (...)
	{
		pso.Reset();
	}
	if (!pso)
	{
		MITIRU_LOG_WARN("RenderPipeline2D", "quad batch PSO build failed; falling back to vertex batches");
		return;
	}
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoMsaa =
		tryBuildDx12PsoMsaa(device, m_dx12RootSig.Get(), vsBlob.Get(), m_dx12PsBlob.Get(), layout, count);

	// pixel-art の輪郭を保つ point filter の変種。作れなければ linear だけで描く (従来の画像の描き方と同じ選び方)
	if (m_dx12PointRootSig && m_dx12PointPipeline)
	{
		Microsoft::WRL::ComPtr<ID3D12PipelineState> point;
		try
		{
			point = buildDx12Pso(device, m_dx12PointRootSig.Get(), vsBlob.Get(), m_dx12PsBlob.Get(), layout, count);
		}
		catch (...)
		{
			point.Reset();
		}
		if (point)
		{
			auto pointMsaa = tryBuildDx12PsoMsaa(
				device, m_dx12PointRootSig.Get(), vsBlob.Get(), m_dx12PsBlob.Get(), layout, count);
			// MSAA の変種が片方だけ作れなかった組は使わない (linear の組で描く)。MSAA が使えない環境は、どちらも null で揃う
			if ((pointMsaa == nullptr) == (m_dx12PointPipelineMsaa == nullptr))
			{
				m_dx12QuadPointPsoMsaa = std::move(pointMsaa);
				m_dx12QuadPointPso = std::move(point);
			}
		}
	}

	constexpr std::uint32_t kInitialBytes = 65536;
	for (int i = 0; i < kDx12Ring; ++i)
	{
		m_dx12QuadBuffer[i] = createUploadBufferDx12(device, kInitialBytes);
		m_dx12QuadCapacity[i] = kInitialBytes;
	}
	m_dx12QuadPsoMsaa = std::move(psoMsaa);
	m_dx12QuadPso = std::move(pso);
}

inline void RenderPipeline2D::submitQuadBatch(const SpriteBatch& batch, std::uint32_t texHandle)
{
	if (!m_dx12Pipeline || !m_dx12RootSig || !m_dx12Cl || !m_dx12Queue || !m_dx12QuadPso) { return; }
	const QuadStore& quads = batch.quads();
	const std::vector<BatchSegment>& segments = batch.segments();
	const std::vector<Vertex2D>& vertices = batch.vertices();
	const std::vector<std::uint32_t>& indices = batch.indices();
	if (segments.empty()) { return; }

	ID3D12RootSignature* rootSig = m_dx12RootSig.Get();
	ID3D12PipelineState* classicPso = m_dx12Pipeline.Get();
	ID3D12PipelineState* quadPso = m_dx12QuadPso.Get();
	ID3D12PipelineState* classicMsaa = m_dx12PipelineMsaa.Get();
	ID3D12PipelineState* quadMsaa = m_dx12QuadPsoMsaa.Get();
	ID3D12DescriptorHeap* srvHeap = m_dx12SrvHeap.Get();
	float psConst[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	if (texHandle != 0)
	{
		if (texHandle > m_dx12SpriteTextures.size()) { return; }
		const Dx12SpriteTexture& entry = m_dx12SpriteTextures[texHandle - 1];
		if (!entry.srvHeap) { return; }
		// 距離場は補間した距離から輪郭を求めるので linear、それ以外は point を優先する (submitTexturedBatch と同じ)
		const bool distanceField = entry.distanceRange > 0.0f;
		if (!distanceField && m_dx12PointRootSig && m_dx12PointPipeline && m_dx12QuadPointPso)
		{
			rootSig = m_dx12PointRootSig.Get();
			classicPso = m_dx12PointPipeline.Get();
			quadPso = m_dx12QuadPointPso.Get();
			classicMsaa = m_dx12PointPipelineMsaa.Get();
			quadMsaa = m_dx12QuadPointPsoMsaa.Get();
		}
		srvHeap = entry.srvHeap.Get();
		psConst[0] = distanceField ? 2.0f : 1.0f;
		psConst[1] = entry.distanceRange;
	}

	auto* rt = dx12RenderTarget();
	if (!rt) { return; }
	const bool msaa = rt->sampleCount() == static_cast<int>(gfx::Dx12MsaaTarget::kSampleCount);
	if (msaa)
	{
		classicPso = classicMsaa;
		quadPso = quadMsaa;
	}
	const bool hasQuads = !quads.empty();
	const bool hasTriangles = !indices.empty();
	// MSAA の変種が無いときは、他の経路と同じく飛ばす
	if ((hasQuads && quadPso == nullptr) || (hasTriangles && classicPso == nullptr)) { return; }

	const int s = acquireDx12Slot();

	// 光が有効なフレームは lit の PSO と root signature に差し替える。光を使わない描画は bool 1 つの確認だけが増える
	LitBind lit;
	const bool usePoint = rootSig == m_dx12PointRootSig.Get() && rootSig != nullptr;
	const bool useLit = m_lit2DActive && bindLit(lit, s, texHandle, msaa, usePoint);
	D3D12_GPU_VIRTUAL_ADDRESS psCb = 0;
	if (useLit)
	{
		rootSig = lit.rootSig;
		classicPso = lit.classic;
		quadPso = lit.quad;
		srvHeap = lit.heap;
		psCb = lit.cb;
	}
	else
	{
		updateCbDx12(m_dx12PsCb[s].Get(), psConst, sizeof(psConst));
		psCb = m_dx12PsCb[s]->GetGPUVirtualAddress();
	}

	const auto quadBytes = static_cast<std::uint32_t>(quads.size() * sizeof(Quad2D));
	const auto vertexBytes = static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex2D));
	const auto indexBytes = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t));
	if (hasQuads) { updateDx12Buffer(m_dx12QuadBuffer[s], m_dx12QuadCapacity[s], quads.data(), quadBytes); }
	if (hasTriangles)
	{
		updateDx12Buffer(m_dx12VertexBuffer[s], m_dx12VbCapacity[s], vertices.data(), vertexBytes);
		updateDx12Buffer(m_dx12IndexBuffer[s], m_dx12IbCapacity[s], indices.data(), indexBytes);
	}

	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rt->rtvHandle();
	ID3D12PipelineState* firstPso = segments.front().kind == BatchSegment::Kind::Quads ? quadPso : classicPso;

	m_dx12Alloc[s]->Reset();
	m_dx12Cl->Reset(m_dx12Alloc[s].Get(), firstPso);
	m_dx12Cl->SetGraphicsRootSignature(rootSig);
	m_dx12Cl->SetGraphicsRootConstantBufferView(0, m_dx12VsCb->GetGPUVirtualAddress());
	m_dx12Cl->SetGraphicsRootConstantBufferView(1, psCb);
	ID3D12DescriptorHeap* heaps[] = { srvHeap };
	m_dx12Cl->SetDescriptorHeaps(1, heaps);
	m_dx12Cl->SetGraphicsRootDescriptorTable(2, srvHeap->GetGPUDescriptorHandleForHeapStart());
	m_dx12Cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	D3D12_VIEWPORT vp = {};
	vp.TopLeftX = viewportOffsetX();
	vp.TopLeftY = viewportOffsetY();
	vp.Width = viewportWidth();
	vp.Height = viewportHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_dx12Cl->RSSetViewports(1, &vp);
	D3D12_RECT sci = {
		static_cast<LONG>(viewportOffsetX()),
		static_cast<LONG>(viewportOffsetY()),
		static_cast<LONG>(viewportOffsetX() + viewportWidth()),
		static_cast<LONG>(viewportOffsetY() + viewportHeight()) };
	m_dx12Cl->RSSetScissorRects(1, &sci);
	m_dx12Cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const std::size_t segmentCount = segments.size();
	ID3D12PipelineState* bound = firstPso;
	for (std::size_t i = 0; i < segmentCount; ++i)
	{
		const BatchSegment& seg = segments[i];
		const bool last = i + 1 == segmentCount;
		if (seg.kind == BatchSegment::Kind::Quads)
		{
			const std::uint32_t end = last ? static_cast<std::uint32_t>(quads.size()) : segments[i + 1].quadFirst;
			if (end <= seg.quadFirst) { continue; }
			if (bound != quadPso) { m_dx12Cl->SetPipelineState(quadPso); bound = quadPso; }
			D3D12_VERTEX_BUFFER_VIEW vbv = {};
			vbv.BufferLocation = m_dx12QuadBuffer[s]->GetGPUVirtualAddress();
			vbv.SizeInBytes = quadBytes;
			vbv.StrideInBytes = sizeof(Quad2D);
			m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);
			// StartInstanceLocation が、インスタンスごとの頂点バッファの読み始めになる
			m_dx12Cl->DrawInstanced(6, end - seg.quadFirst, 0, seg.quadFirst);
		}
		else
		{
			const std::uint32_t end = last ? static_cast<std::uint32_t>(indices.size()) : segments[i + 1].indexFirst;
			if (end <= seg.indexFirst) { continue; }
			if (bound != classicPso) { m_dx12Cl->SetPipelineState(classicPso); bound = classicPso; }
			D3D12_VERTEX_BUFFER_VIEW vbv = {};
			vbv.BufferLocation = m_dx12VertexBuffer[s]->GetGPUVirtualAddress();
			vbv.SizeInBytes = vertexBytes;
			vbv.StrideInBytes = sizeof(Vertex2D);
			m_dx12Cl->IASetVertexBuffers(0, 1, &vbv);
			D3D12_INDEX_BUFFER_VIEW ibv = {};
			ibv.BufferLocation = m_dx12IndexBuffer[s]->GetGPUVirtualAddress();
			ibv.SizeInBytes = indexBytes;
			ibv.Format = DXGI_FORMAT_R32_UINT;
			m_dx12Cl->IASetIndexBuffer(&ibv);
			m_dx12Cl->DrawIndexedInstanced(end - seg.indexFirst, 1, seg.indexFirst, 0, 0);
		}
	}

	m_dx12Cl->Close();
	ID3D12CommandList* lists[] = { m_dx12Cl.Get() };
	m_dx12Queue->ExecuteCommandLists(1, lists);
	++m_dx12FenceValue;
	m_dx12Queue->Signal(m_dx12Fence.Get(), m_dx12FenceValue);
	m_dx12SlotSignal[s] = m_dx12FenceValue;
}

} // namespace mitiru::render

#else // !_WIN32

namespace mitiru::render
{

inline bool RenderPipeline2D::quadPipelineReady() const noexcept { return false; }
inline void RenderPipeline2D::submitQuadBatch(const SpriteBatch&, std::uint32_t) {}

} // namespace mitiru::render

#endif // _WIN32
