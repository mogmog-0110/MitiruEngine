// Renderer3D_DX12 のワールド描画の実装。
// 地形、草、撒いた物、水面を描く。
// 地形は CDLOD の区画、草は段ごとに instanced draw する。
// 撒いた物は drawModelInstancesImpl で描く。
// 水面は不透明と空の後に、画面の色の写しと深度を読む。

/// @brief 区画の箱が見えるかを視錐台と、有効なら遮蔽の読み戻しで判定する。撒いた物の instanced draw も同じ判定を使う
[[nodiscard]] bool worldBoxVisible(const terrain::LodBox& b)
{
	const AABB box{b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ};
	if (m_frustumCullingEnabled && !m_frustum.isBoxVisible(box))
	{
		++m_culledCount;
		return false;
	}
	if (m_occlusionCullingEnabled && m_occlusionCuller.hasDepth() &&
	    m_occlusionCuller.isOccluded(CullAABB{b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ}, occlusionViewProj().data()))
	{
		++m_occludedCount;
		return false;
	}
	return true;
}

void drawOutdoorScatter(OutdoorWorldGpu& g)
{
	for (std::size_t i = 0; i < g.scatter.size(); ++i)
	{
		const auto& inst = g.scatter[i];
		if (inst.empty()) { continue; }
		drawModelInstancesImpl(g.world->scatter[i].model.c_str(), inst.data(), inst.size());
		m_outdoorStats.scatterInstances += static_cast<int>(inst.size());
	}
}

/// @brief 区画の並びを GPU へ送る。地形は 2 つの float4、草は 1 つ
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadPatches()
{
	if (m_patchGpuScratch.empty()) { return 0; }
	const auto a = m_uploadRing.upload(m_patchGpuScratch.data(), m_patchGpuScratch.size() * sizeof(float), 256);
	return a.valid() ? a.gpuAddr : 0;
}

[[nodiscard]] bool bindWorldMesh(const Mesh& mesh)
{
	const auto& v = mesh.vertices();
	const auto& ix = mesh.indices();
	const UINT vbBytes = static_cast<UINT>(v.size() * sizeof(Vertex3D));
	const UINT ibBytes = static_cast<UINT>(ix.size() * sizeof(std::uint32_t));
	auto* vb = acquireMeshBuffer(m_meshVBCache, mesh, v.data(), vbBytes);
	auto* ib = acquireMeshBuffer(m_meshIBCache, mesh, ix.data(), ibBytes);
	if (vb == nullptr || ib == nullptr) { return false; }
	const D3D12_VERTEX_BUFFER_VIEW vbv{vb->GetGPUVirtualAddress(), vbBytes, sizeof(Vertex3D)};
	const D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), ibBytes, DXGI_FORMAT_R32_UINT};
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);
	m_graphicsCmdList->IASetIndexBuffer(&ibv);
	return true;
}

void drawOutdoorTerrain(OutdoorWorldGpu& g, const OutdoorDrawPod& pod)
{
	const terrain::OutdoorWorld& w = *g.world;
	w.lodTree.select(m_cameraPosition, [this](const terrain::LodBox& b) { return worldBoxVisible(b); }, m_lodScratch);
	m_outdoorStats.terrainPatches += static_cast<int>(m_lodScratch.size());
	if (m_lodScratch.empty()) { return; }
	m_patchGpuScratch.clear();
	for (const auto& p : m_lodScratch)
	{
		m_patchGpuScratch.insert(m_patchGpuScratch.end(),
		                         {p.x0, p.z0, p.size, p.sizeZ, p.morphStart, p.morphEnd, static_cast<float>(p.lod),
		                          static_cast<float>(p.preSnap)});
	}
	const auto patches = uploadPatches();
	CbWorldDx12 cb = makeWorldCb(w, pod);
	cb.terrainPatch[0] = static_cast<float>(w.lod.patchCells);
	if (patches == 0 || !bindWorldDraw(g, cb, false, m_terrainPso[worldShadeIndex()].Get()) ||
	    !bindWorldMesh(terrainMeshFor(w.lod.patchCells)))
	{
		return;
	}
	m_worldTimer.begin(m_graphicsCmdList.Get(), kWorldTimerTerrain);
	m_graphicsCmdList->SetGraphicsRootShaderResourceView(kWorldPatches, patches);
	m_graphicsCmdList->DrawIndexedInstanced(static_cast<UINT>(terrainMeshFor(w.lod.patchCells).indices().size()),
	                                        static_cast<UINT>(m_lodScratch.size()), 0, 0, 0);
	m_worldTimer.end(m_graphicsCmdList.Get(), kWorldTimerTerrain);
	++m_drawCallCount;
}

/// @brief 区画のマス数に合う格子を返す。world.json が既定の 32 以外を指定したときは作り直す
[[nodiscard]] const Mesh& terrainMeshFor(std::uint32_t cells)
{
	if (m_terrainPatchMesh.vertexCount() != static_cast<std::size_t>(cells + 1) * (cells + 1))
	{
		buildTerrainPatchMesh(m_terrainPatchMesh, cells);
	}
	return m_terrainPatchMesh;
}

void drawOutdoorGrass(OutdoorWorldGpu& g, const OutdoorDrawPod& pod)
{
	const terrain::OutdoorWorld& w = *g.world;
	if (!w.hasGrass) { return; }
	w.grassField.select(m_cameraPosition, [this](const terrain::LodBox& b) { return worldBoxVisible(b); }, m_grassScratch);
	if (m_grassScratch.empty()) { return; }
	// 段が同じ区画は本数も同じため、段ごとにまとめて 1 回の draw で描く
	std::stable_sort(m_grassScratch.begin(), m_grassScratch.end(),
	                 [](const terrain::GrassPatch& a, const terrain::GrassPatch& b) { return a.lod < b.lod; });
	m_patchGpuScratch.clear();
	for (const auto& p : m_grassScratch)
	{
		m_patchGpuScratch.insert(m_patchGpuScratch.end(), {p.x0, p.z0, std::bit_cast<float>(p.index), 0.0f});
	}
	const auto patches = uploadPatches();
	const CbWorldDx12 cb = makeWorldCb(w, pod);
	const int shade = worldShadeIndex();
	ID3D12PipelineState* pso = m_grassPso[shade] ? m_grassPso[shade].Get() : m_grassPso[0].Get();
	const bool outline = m_outlineCasterEnabled;
	m_outlineCasterEnabled = false;   // 草の 1 本 1 本に輪郭線を引かない
	const bool bound = patches != 0 && bindWorldDraw(g, cb, false, pso) && bindWorldMesh(m_grassBladeMesh);
	m_outlineCasterEnabled = outline;
	if (!bound) { return; }
	m_worldTimer.begin(m_graphicsCmdList.Get(), kWorldTimerGrass);
	m_graphicsCmdList->SetGraphicsRootShaderResourceView(kWorldPatches, patches);
	const auto blade = static_cast<UINT>(m_grassBladeMesh.indices().size());
	for (std::size_t first = 0; first < m_grassScratch.size();)
	{
		std::size_t last = first;
		while (last < m_grassScratch.size() && m_grassScratch[last].lod == m_grassScratch[first].lod) { ++last; }
		const std::uint32_t perPatch = m_grassScratch[first].bladeCount;
		const std::uint32_t draw[4] = {static_cast<std::uint32_t>(first), perPatch, 0u, 0u};
		m_graphicsCmdList->SetGraphicsRoot32BitConstants(kWorldDraw, 4, draw, 0);
		m_graphicsCmdList->DrawIndexedInstanced(blade, perPatch * static_cast<UINT>(last - first), 0, 0, 0);
		m_outdoorStats.grassBlades += static_cast<std::uint64_t>(perPatch) * (last - first);
		++m_drawCallCount;
		first = last;
	}
	m_worldTimer.end(m_graphicsCmdList.Get(), kWorldTimerGrass);
	m_outdoorStats.grassPatches += static_cast<int>(m_grassScratch.size());
}

/// @brief 水面の屈折用に、不透明と空を描き終えた画面の色を内部解像度、1 標本で写す
[[nodiscard]] bool ensureWaterCopy()
{
	const auto w = static_cast<UINT>(m_config.viewportWidth);
	const auto h = static_cast<UINT>(m_config.viewportHeight);
	if (m_waterColorCopy && m_waterCopyWidth == w && m_waterCopyHeight == h) { return true; }
	if (m_waterColorCopy) { m_frameTempResources.push_back(std::move(m_waterColorCopy)); }   // 読み終わるまで残す
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                  nullptr, m_waterColorCopy)))
	{
		return false;
	}
	m_waterColorCopy->SetName(L"Renderer3D water scene copy");
	m_waterCopyWidth = w;
	m_waterCopyHeight = h;
	if (!m_waterDsvHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.NumDescriptors = 1;
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		if (FAILED(m_d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_waterDsvHeap.GetAddressOf())))) { return false; }
	}
	return true;
}

/// @brief MSAA の色を写しへ resolve する。前後では MSAA の色を RENDER_TARGET に戻す
void copySceneForWater()
{
	auto* cl = m_graphicsCmdList.Get();
	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
	temporalBarrier(m_waterColorCopy.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	cl->ResolveSubresource(m_waterColorCopy.Get(), 0, m_msaaColorBuffer.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
	temporalBarrier(m_waterColorCopy.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	temporalBarrier(m_msaaColorBuffer.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

/// @brief 不透明、clod、空の後、半透明の前に呼ぶ。このフレームに drawOutdoor された world.json の水面を描く
void renderWaterPass()
{
	if (m_waterQueue.empty()) { return; }
	if (!m_waterPso || !m_waterDepthPso || !m_msaaColorRtvHeap || !m_normalRTVHeap || !ensureWaterCopy())
	{
		m_waterQueue.clear();
		return;
	}
	MITIRU_ZONE_NAMED("Render::Dx12::WaterPass");
	auto* cl = m_graphicsCmdList.Get();
	m_worldTimer.begin(cl, kWorldTimerWater);
	copySceneForWater();
	// 深度は読み取り専用の DSV で読みながらテストする。書き込みは色の後の深度だけのパスで行う
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
	                D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
	dv.Format = DXGI_FORMAT_D32_FLOAT;
	dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
	dv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
	const auto roDsv = m_waterDsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_d3dDevice->CreateDepthStencilView(m_depthBuffer.Get(), &dv, roDsv);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart(),
	                                             m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart()};
	cl->OMSetRenderTargets(2, rtvs, FALSE, &roDsv);
	setFullViewport();
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	drawWaterBodies(m_waterPso.Get());
	temporalBarrier(m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
	drawWaterBodies(m_waterDepthPso.Get());
	cl->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
	m_worldTimer.end(cl, kWorldTimerWater);
	m_waterQueue.clear();
	restoreMainState();
}

void drawWaterBodies(ID3D12PipelineState* pso)
{
	for (const QueuedWater& q : m_waterQueue)
	{
		CbWorldDx12 cb = makeWorldCb(*q.gpu->world, q.pod);
		for (const terrain::WaterBody& body : q.gpu->world->water)
		{
			fillWaterCb(cb, body, q.pod);
			const bool colorPass = pso == m_waterPso.Get();
			if (!bindWorldDraw(*q.gpu, cb, colorPass, pso)) { continue; }
			m_graphicsCmdList->DrawInstanced(6, 1, 0, 0);
			++m_drawCallCount;
			if (colorPass) { ++m_outdoorStats.waterBodies; }
		}
	}
}
