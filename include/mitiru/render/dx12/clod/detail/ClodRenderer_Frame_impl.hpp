#pragma once

/// @file ClodRenderer_Frame_impl.hpp
/// @brief ClodRenderer のフレーム記録 (cull → raster → HZB → resolve)

#include <cmath>

namespace mitiru::render::clod
{

namespace detail
{

/// @brief 右手系 lookAt (行優先 4x4)。列優先 CB へは転置して書く
inline void clodLookAt(float* m, const float* eye, const float* at)
{
	float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
	const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	for (float& v : f) { v /= fl > 1e-20f ? fl : 1.0f; }
	float s[3] = { -f[2], 0.0f, f[0] };
	float sl = std::sqrt(s[0] * s[0] + s[2] * s[2]);
	if (sl < 1e-6f)
	{
		s[0] = 1.0f;
		s[2] = 0.0f;
		sl = 1.0f;
	}
	s[0] /= sl;
	s[2] /= sl;
	const float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
	                     s[0] * f[1] - s[1] * f[0] };
	const float t[16] = {
		s[0], s[1], s[2], -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]),
		u[0], u[1], u[2], -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
		-f[0], -f[1], -f[2], (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]),
		0, 0, 0, 1
	};
	std::memcpy(m, t, sizeof(t));
}

/// @brief 右手系 perspective (NDC z ∈ [0,1]、行優先)
inline void clodPerspective(float* m, float fovY, float aspect, float zn, float zf)
{
	const float ys = 1.0f / std::tan(fovY * 0.5f);
	const float t[16] = {
		ys / aspect, 0, 0, 0,
		0, ys, 0, 0,
		0, 0, zf / (zn - zf), zn * zf / (zn - zf),
		0, 0, -1, 0
	};
	std::memcpy(m, t, sizeof(t));
}

inline void clodMatMul(float* o, const float* a, const float* b)
{
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			float s = 0;
			for (int k = 0; k < 4; ++k) { s += a[r * 4 + k] * b[k * 4 + c]; }
			o[r * 4 + c] = s;
		}
	}
}

/// @brief 行優先 viewProj から world 錐台平面を抽出 (Gribb-Hartmann, z [0,1])
inline void clodExtractFrustum(float out[6][4], const float* m)
{
	const float* r0 = m;
	const float* r1 = m + 4;
	const float* r2 = m + 8;
	const float* r3 = m + 12;
	for (int c = 0; c < 4; ++c)
	{
		out[0][c] = r3[c] + r0[c];
		out[1][c] = r3[c] - r0[c];
		out[2][c] = r3[c] + r1[c];
		out[3][c] = r3[c] - r1[c];
		out[4][c] = r2[c];
		out[5][c] = r3[c] - r2[c];
	}
	for (int i = 0; i < 6; ++i)
	{
		float* plane = out[i];
		const float l = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
		if (l > 1e-9f)
		{
			for (int c = 0; c < 4; ++c) { plane[c] /= l; }
		}
	}
}

} // namespace detail

inline void ClodRenderer::fillDrawCB(ClodDrawCB& cb, const Camera3D& camera,
                                     const float lightDir[3], const float lightColor[3],
                                     float ambient) const
{
	const auto eyeV = camera.position();
	const auto atV = camera.target();
	const float eye[3] = { eyeV.x, eyeV.y, eyeV.z };
	const float at[3] = { atV.x, atV.y, atV.z };
	float view[16], proj[16], vp[16];
	detail::clodLookAt(view, eye, at);
	detail::clodPerspective(proj, camera.fov(), camera.aspectRatio(), camera.nearClip(),
	                        camera.farClip());
	detail::clodMatMul(vp, proj, view);

	cb = {};
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c) { cb.viewProj[c * 4 + r] = vp[r * 4 + c]; }
	}
	cb.camPosTau[0] = eye[0];
	cb.camPosTau[1] = eye[1];
	cb.camPosTau[2] = eye[2];
	cb.camPosTau[3] = 1.0f / static_cast<float>(m_height);   // τ = 1px
	cb.misc[0] = 1.0f / std::tan(camera.fov() * 0.5f) * 0.5f;
	cb.misc[1] = camera.nearClip();
	std::memcpy(&cb.misc[2], &m_width, 4);
	detail::clodExtractFrustum(cb.frustum, vp);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			cb.viewRow[r][c] = view[r * 4 + c];
			cb.prevViewRow[r][c] = m_prevViewValid ? m_prevView[r * 4 + c] : view[r * 4 + c];
		}
	}
	cb.projParams[0] = proj[0];
	cb.projParams[1] = proj[5];
	cb.projParams[2] = proj[10];
	cb.projParams[3] = proj[11];
	cb.hzbParams[0] = static_cast<float>(m_hzbW);
	cb.hzbParams[1] = static_cast<float>(m_hzbH);
	cb.hzbParams[2] = static_cast<float>(m_hzbMips);
	cb.hzbParams[3] = m_prevViewValid ? 1.0f : 0.0f;
	cb.swParams[0] = 64.0f;   // 投影直径がこれ未満のクラスタは SW ラスタへ
	cb.swParams[1] = static_cast<float>(m_scene.maxLodDepth());
	std::memcpy(&cb.swParams[2], &m_frameInstanceCount, 4);
	std::memcpy(&cb.swParams[3], &m_height, 4);
	const uint32_t meshCount = m_frameMeshCount;
	const uint32_t itemCount = m_frameItemCount;
	std::memcpy(&cb.counts[0], &meshCount, 4);
	std::memcpy(&cb.counts[1], &itemCount, 4);
	const uint32_t dispatchX = 32768;
	std::memcpy(&cb.counts[2], &dispatchX, 4);
	cb.engineLightDir[0] = lightDir[0];
	cb.engineLightDir[1] = lightDir[1];
	cb.engineLightDir[2] = lightDir[2];
	cb.engineLightColor[0] = lightColor[0];
	cb.engineLightColor[1] = lightColor[1];
	cb.engineLightColor[2] = lightColor[2];
	cb.engineLightColor[3] = ambient;
}

/// @brief pending を mesh-major に並べ、instance / mesh 表を ring へ積む
inline void ClodRenderer::buildFrameTables(D3D12_GPU_VIRTUAL_ADDRESS& instances,
                                           D3D12_GPU_VIRTUAL_ADDRESS& meshTable)
{
	m_frameInstances.clear();
	m_frameMeshTable.clear();
	m_frameItemCount = 0;
	const auto& models = m_scene.models();
	for (size_t m = 0; m < models.size() && m_frameMeshTable.size() < kClodMaxMeshes; ++m)
	{
		GpuMeshRec rec = {};
		rec.itemBase = m_frameItemCount;
		rec.instBase = static_cast<uint32_t>(m_frameInstances.size());
		rec.clusterCount = models[m].clusterCount;
		rec.clusterBase = models[m].clusterBase;
		rec.bvhRoot = models[m].bvhRoot;
		const float radius = models[m].radius;
		std::memcpy(&rec.radiusBits, &radius, 4);
		bool any = false;
		for (const PendingInstance& p : m_pending)
		{
			if (p.model != static_cast<int>(m)) { continue; }
			GpuInstance gi = {};
			gi.ofs[0] = p.pos[0];
			gi.ofs[1] = p.pos[1];
			gi.ofs[2] = p.pos[2];
			gi.rotY = p.rotYDeg * 3.14159265f / 180.0f;
			gi.scale = p.scale;
			gi.clusterBase = models[m].clusterBase;
			gi.clusterCount = models[m].clusterCount;
			m_frameInstances.push_back(gi);
			m_frameItemCount += models[m].clusterCount;
			any = true;
		}
		if (any) { m_frameMeshTable.push_back(rec); }
	}
	m_frameMeshCount = static_cast<uint32_t>(m_frameMeshTable.size());
	m_frameInstanceCount = static_cast<uint32_t>(m_frameInstances.size());

	auto ai = m_ring.allocate(sizeof(GpuInstance) * m_frameInstances.size() + 16, 256);
	std::memcpy(ai.cpuPtr, m_frameInstances.data(), sizeof(GpuInstance) * m_frameInstances.size());
	instances = ai.gpuAddr;
	auto am = m_ring.allocate(sizeof(GpuMeshRec) * kClodMaxMeshes, 256);
	std::memcpy(am.cpuPtr, m_frameMeshTable.data(), sizeof(GpuMeshRec) * m_frameMeshTable.size());
	meshTable = am.gpuAddr;
}

/// @brief 静的シーン GPU バッファを revision に同期させる (copy は cmd に記録)
inline void ClodRenderer::ensureSceneResources(ID3D12GraphicsCommandList* cmd)
{
	if (m_gpuSceneRevision == m_scene.revision()) { return; }
	if (waitIdle) { waitIdle(); }
	m_pendingUploads.clear();

	constexpr auto kSrvState = D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
	                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto upload = [&](ComPtr<ID3D12Resource>& dst, const void* src, uint64_t bytes)
	{
		const uint64_t padded = (bytes + 7) & ~7ull;   // 4B 読みの端を 8B 境界まで確保
		dst = makeBuffer(padded, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
		                 D3D12_RESOURCE_FLAG_NONE);
		auto staging = makeBuffer(padded, D3D12_HEAP_TYPE_UPLOAD,
		                          D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
		void* p = nullptr;
		const D3D12_RANGE rr = { 0, 0 };
		staging->Map(0, &rr, &p);
		std::memset(p, 0, padded);
		std::memcpy(p, src, bytes);
		staging->Unmap(0, nullptr);
		cmd->CopyBufferRegion(dst.Get(), 0, staging.Get(), 0, padded);
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst.Get();
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = kSrvState;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &b);
		m_pendingUploads.push_back(staging);
	};

	upload(m_bGroups, m_scene.groups().data(), m_scene.groups().size() * sizeof(ClodGroup));
	upload(m_bClusters, m_scene.clusters().data(), m_scene.clusters().size() * sizeof(ClodCluster));
	upload(m_bPos, m_scene.positions().data(), m_scene.positions().size() * 4);
	upload(m_bNorm, m_scene.normals().data(), m_scene.normals().size() * 4);
	upload(m_bUv, m_scene.uvs().data(), m_scene.uvs().size() * 4);
	upload(m_bVerts, m_scene.clusterVerts().data(), m_scene.clusterVerts().size() * 4);
	upload(m_bTris, m_scene.clusterTris().data(), m_scene.clusterTris().size());
	upload(m_bMats, m_scene.materials().data(), m_scene.materials().size() * sizeof(GpuMaterial));
	upload(m_bMeshRanges, m_scene.groupRanges().data(), m_scene.groupRanges().size() * 4);
	upload(m_bBvh, m_scene.bvhNodes().data(), m_scene.bvhNodes().size() * sizeof(GpuBvhNode));
	uploadTextures(cmd);
	rebuildDescriptorHeap();
	m_gpuSceneRevision = m_scene.revision();
}

inline void ClodRenderer::uploadTextures(ID3D12GraphicsCommandList* cmd)
{
	m_textures.clear();
	for (const CpuTexture& t : m_scene.textures())
	{
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC td = {};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = t.width;
		td.Height = t.height;
		td.DepthOrArraySize = 1;
		td.MipLevels = static_cast<UINT16>(t.mips.size());
		td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		td.SampleDesc.Count = 1;
		ComPtr<ID3D12Resource> tex;
		m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
		                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		                                  IID_PPV_ARGS(&tex));
		uint64_t total = 0;
		uint32_t mw = t.width, mh = t.height;
		for (size_t m = 0; m < t.mips.size(); ++m)
		{
			total = (total + static_cast<uint64_t>((mw * 4 + 255u) & ~255u) * mh + 511) & ~511ull;
			mw = mw > 1 ? mw / 2 : 1;
			mh = mh > 1 ? mh / 2 : 1;
		}
		auto staging = makeBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
		                          D3D12_RESOURCE_FLAG_NONE);
		uint8_t* p = nullptr;
		const D3D12_RANGE rr = { 0, 0 };
		staging->Map(0, &rr, reinterpret_cast<void**>(&p));
		uint64_t ofs = 0;
		mw = t.width;
		mh = t.height;
		for (size_t m = 0; m < t.mips.size(); ++m)
		{
			const uint32_t pitch = (mw * 4 + 255u) & ~255u;
			for (uint32_t y = 0; y < mh; ++y)
			{
				std::memcpy(p + ofs + static_cast<uint64_t>(y) * pitch,
				            t.mips[m].data() + static_cast<size_t>(y) * mw * 4, mw * 4);
			}
			D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
			src.pResource = staging.Get();
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint.Offset = ofs;
			src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			src.PlacedFootprint.Footprint.Width = mw;
			src.PlacedFootprint.Footprint.Height = mh;
			src.PlacedFootprint.Footprint.Depth = 1;
			src.PlacedFootprint.Footprint.RowPitch = pitch;
			dst.pResource = tex.Get();
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = static_cast<UINT>(m);
			cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			ofs = (ofs + static_cast<uint64_t>(pitch) * mh + 511) & ~511ull;
			mw = mw > 1 ? mw / 2 : 1;
			mh = mh > 1 ? mh / 2 : 1;
		}
		staging->Unmap(0, nullptr);
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = tex.Get();
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = D3D12_RESOURCE_STATES(
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmd->ResourceBarrier(1, &b);
		m_pendingUploads.push_back(staging);
		m_textures.push_back(tex);
	}
}

inline void ClodRenderer::bindCompute(ID3D12GraphicsCommandList* cmd,
                                      D3D12_GPU_VIRTUAL_ADDRESS cb) const
{
	cmd->SetComputeRootSignature(m_rootSig.Get());
	cmd->SetComputeRootConstantBufferView(0, cb);
	cmd->SetComputeRootShaderResourceView(1, m_bGroups->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(2, m_bClusters->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(3, m_bPos->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(4, m_bVerts->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(5, m_bTris->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(6, m_frameInstancesVA);
	cmd->SetComputeRootUnorderedAccessView(7, m_bStats->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(8, m_bMarked->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(9, m_visBuf->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(10, m_bVisListHw->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(11, m_bCounters->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(12, m_bIndArgs->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(13, m_bVisListSw->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(15, m_overdraw->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(16, m_bInstVis->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(17, m_frameMeshTableVA);
	cmd->SetComputeRootShaderResourceView(18, m_bMeshRanges->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(19, m_bBvh->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(20, m_bQueueA->GetGPUVirtualAddress());
	cmd->SetComputeRootUnorderedAccessView(21, m_bQueueB->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(22, m_bNorm->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(23, m_bUv->GetGPUVirtualAddress());
	cmd->SetComputeRootShaderResourceView(24, m_bMats->GetGPUVirtualAddress());
}

inline void ClodRenderer::bindGraphics(ID3D12GraphicsCommandList* cmd,
                                       D3D12_GPU_VIRTUAL_ADDRESS cb) const
{
	cmd->SetGraphicsRootSignature(m_rootSig.Get());
	cmd->SetPipelineState(m_meshPso.Get());
	cmd->SetGraphicsRootConstantBufferView(0, cb);
	cmd->SetGraphicsRootShaderResourceView(1, m_bGroups->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(2, m_bClusters->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(3, m_bPos->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(4, m_bVerts->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(5, m_bTris->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(6, m_frameInstancesVA);
	cmd->SetGraphicsRootUnorderedAccessView(7, m_bStats->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(8, m_bMarked->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(9, m_visBuf->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(10, m_bVisListHw->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(11, m_bCounters->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(12, m_bIndArgs->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(13, m_bVisListSw->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(15, m_overdraw->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(16, m_bInstVis->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(17, m_frameMeshTableVA);
	cmd->SetGraphicsRootShaderResourceView(18, m_bMeshRanges->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(19, m_bBvh->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(20, m_bQueueA->GetGPUVirtualAddress());
	cmd->SetGraphicsRootUnorderedAccessView(21, m_bQueueB->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(22, m_bNorm->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(23, m_bUv->GetGPUVirtualAddress());
	cmd->SetGraphicsRootShaderResourceView(24, m_bMats->GetGPUVirtualAddress());

	const D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(m_width), static_cast<float>(m_height),
	                            0, 1 };
	const D3D12_RECT sc = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
}

inline void ClodRenderer::uavBarrierAll(ID3D12GraphicsCommandList* cmd) const
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	cmd->ResourceBarrier(1, &b);
}

inline void ClodRenderer::recordClears(ID3D12GraphicsCommandList* cmd,
                                       D3D12_GPU_VIRTUAL_ADDRESS cb0) const
{
	bindCompute(cmd, cb0);
	cmd->SetPipelineState(m_clearPso.Get());
	const uint32_t instVisWords = (kClodMaxInstances + 31) / 32;
	const uint32_t cc[4] = { 0, instVisWords, 0, 0 };
	cmd->SetComputeRoot32BitConstants(14, 4, cc, 0);
	cmd->Dispatch((m_width * m_height + 255u) / 256u, 1, 1);
	uavBarrierAll(cmd);

	cmd->SetPipelineState(m_icullPso.Get());
	cmd->Dispatch((m_frameInstanceCount + 63u) / 64u, 1, 1);
	uavBarrierAll(cmd);
}

/// @brief BVH 走査: Seed → レベル毎に prepQ + 間接 Traverse (可視クラスタを list へ)
inline void ClodRenderer::recordBvhCull(ID3D12GraphicsCommandList* cmd,
                                        D3D12_GPU_VIRTUAL_ADDRESS cb0) const
{
	cmd->SetPipelineState(m_seedPso.Get());
	cmd->Dispatch((m_frameInstanceCount + 63u) / 64u, 1, 1);
	uavBarrierAll(cmd);

	D3D12_RESOURCE_BARRIER tb = {};
	tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	tb.Transition.pResource = m_bIndArgs.Get();
	tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	for (uint32_t lvl = 0; lvl <= m_scene.bvhMaxDepth(); ++lvl)
	{
		cmd->SetPipelineState(m_prepQPso.Get());
		const uint32_t qc[4] = { 5 + (lvl & 1), 5 + ((lvl + 1) & 1), 0, 0 };
		cmd->SetComputeRoot32BitConstants(14, 4, qc, 0);
		cmd->Dispatch(1, 1, 1);
		uavBarrierAll(cmd);

		tb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		tb.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		cmd->ResourceBarrier(1, &tb);
		cmd->SetPipelineState(m_travPso.Get());
		const uint32_t lc[4] = { lvl & 1, 0, 0, 0 };
		cmd->SetComputeRoot32BitConstants(14, 4, lc, 0);
		cmd->ExecuteIndirect(m_dispatchSig.Get(), 1, m_bIndArgs.Get(), 60, nullptr, 0);
		tb.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		tb.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		cmd->ResourceBarrier(1, &tb);
		uavBarrierAll(cmd);
	}
}

/// @brief prep → HW (DispatchMesh) + SW (Dispatch) の間接描画 1 パス分
inline void ClodRenderer::recordDrawPass(ID3D12GraphicsCommandList* cmd, uint32_t pass,
                                         D3D12_GPU_VIRTUAL_ADDRESS cb) const
{
	bindCompute(cmd, cb);
	cmd->SetPipelineState(m_prepPso.Get());
	const uint32_t pc[4] = { pass, 0, 0, 0 };
	cmd->SetComputeRoot32BitConstants(14, 4, pc, 0);
	cmd->Dispatch(1, 1, 1);
	uavBarrierAll(cmd);

	D3D12_RESOURCE_BARRIER tb = {};
	tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	tb.Transition.pResource = m_bIndArgs.Get();
	tb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	tb.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &tb);

	bindGraphics(cmd, cb);
	cmd->ExecuteIndirect(m_dispatchMeshSig.Get(), 1, m_bIndArgs.Get(),
	                     static_cast<uint64_t>(pass) * 12, nullptr, 0);
	bindCompute(cmd, cb);
	cmd->SetPipelineState(m_swPso.Get());
	cmd->ExecuteIndirect(m_dispatchSig.Get(), 1, m_bIndArgs.Get(),
	                     24 + static_cast<uint64_t>(pass) * 12, nullptr, 0);

	tb.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	tb.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	cmd->ResourceBarrier(1, &tb);
	uavBarrierAll(cmd);
}

inline void ClodRenderer::recordHzbBuild(ID3D12GraphicsCommandList* cmd,
                                         D3D12_GPU_VIRTUAL_ADDRESS cb0) const
{
	bindCompute(cmd, cb0);
	cmd->SetPipelineState(m_hzbPso.Get());
	D3D12_RESOURCE_BARRIER ub = {};
	ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	ub.UAV.pResource = m_hzb.Get();

	const uint32_t op[4] = { 0, 0, m_hzbW, m_hzbH };
	cmd->SetComputeRoot32BitConstants(14, 4, op, 0);
	cmd->Dispatch((m_hzbW + 7) / 8, (m_hzbH + 7) / 8, 1);
	cmd->ResourceBarrier(1, &ub);
	for (uint32_t m = 1; m < m_hzbMips; ++m)
	{
		const uint32_t w = (m_hzbW >> m) > 0 ? (m_hzbW >> m) : 1;
		const uint32_t h = (m_hzbH >> m) > 0 ? (m_hzbH >> m) : 1;
		const uint32_t opm[4] = { 1, m, w, h };
		cmd->SetComputeRoot32BitConstants(14, 4, opm, 0);
		cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
		cmd->ResourceBarrier(1, &ub);
	}
}

inline void ClodRenderer::recordResolve(ID3D12GraphicsCommandList* cmd,
                                        D3D12_GPU_VIRTUAL_ADDRESS cb0) const
{
	bindCompute(cmd, cb0);
	cmd->SetPipelineState(m_resolvePso.Get());
	cmd->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
	uavBarrierAll(cmd);
}

inline void ClodRenderer::record(ID3D12GraphicsCommandList* cmd, const Camera3D& camera,
                                 const float lightDir[3], const float lightColor[3],
                                 float ambient, uint32_t width, uint32_t height,
                                 UINT frameIndex)
{
	if (!m_supported || m_pending.empty()) { return; }
	ensureScreenResources(width, height);
	ensureSceneResources(cmd);
	if (!m_visBuf || !m_heap) { return; }

	m_ring.beginFrame(frameIndex);
	buildFrameTables(m_frameInstancesVA, m_frameMeshTableVA);
	if (m_frameInstanceCount == 0) { return; }

	ClodDrawCB cb;
	fillDrawCB(cb, camera, lightDir, lightColor, ambient);
	uint32_t pass = 0;
	std::memcpy(&cb.counts[3], &pass, 4);
	auto a0 = m_ring.allocate(sizeof(ClodDrawCB), 256);
	std::memcpy(a0.cpuPtr, &cb, sizeof(cb));
	pass = 1;
	std::memcpy(&cb.counts[3], &pass, 4);
	auto a1 = m_ring.allocate(sizeof(ClodDrawCB), 256);
	std::memcpy(a1.cpuPtr, &cb, sizeof(cb));

	ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);

	recordClears(cmd, a0.gpuAddr);
	recordBvhCull(cmd, a0.gpuAddr);
	recordDrawPass(cmd, 0, a0.gpuAddr);
	recordHzbBuild(cmd, a0.gpuAddr);
	if (cb.hzbParams[3] > 0.5f)
	{
		// pass1: pass0 で遮蔽マークした item を現フレーム HZB で再テストして開示
		uavBarrierAll(cmd);
		D3D12_RESOURCE_BARRIER tb = {};
		tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		tb.Transition.pResource = m_bIndArgs.Get();
		tb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		tb.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		bindCompute(cmd, a1.gpuAddr);
		cmd->SetPipelineState(m_cullPso.Get());
		cmd->ResourceBarrier(1, &tb);
		cmd->ExecuteIndirect(m_dispatchSig.Get(), 1, m_bIndArgs.Get(), 48, nullptr, 0);
		tb.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		tb.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		cmd->ResourceBarrier(1, &tb);
		uavBarrierAll(cmd);
		recordDrawPass(cmd, 1, a1.gpuAddr);
		recordHzbBuild(cmd, a0.gpuAddr);
	}
	recordResolve(cmd, a0.gpuAddr);

	const auto eyeV = camera.position();
	const auto atV = camera.target();
	const float eye[3] = { eyeV.x, eyeV.y, eyeV.z };
	const float at[3] = { atV.x, atV.y, atV.z };
	float view[16];
	detail::clodLookAt(view, eye, at);
	std::memcpy(m_prevView, view, sizeof(m_prevView));
	m_prevViewValid = true;
}

inline void ClodRenderer::transitionForInject(ID3D12GraphicsCommandList* cmd)
{
	if (!m_colorTex || !m_visBuf) { return; }
	D3D12_RESOURCE_BARRIER b[2] = {};
	for (auto& e : b)
	{
		e.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		e.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		e.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		e.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	}
	b[0].Transition.pResource = m_colorTex.Get();
	b[1].Transition.pResource = m_visBuf.Get();
	cmd->ResourceBarrier(2, b);
}

inline void ClodRenderer::transitionAfterInject(ID3D12GraphicsCommandList* cmd)
{
	if (!m_colorTex || !m_visBuf) { return; }
	D3D12_RESOURCE_BARRIER b[2] = {};
	for (auto& e : b)
	{
		e.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		e.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		e.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		e.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	}
	b[0].Transition.pResource = m_colorTex.Get();
	b[1].Transition.pResource = m_visBuf.Get();
	cmd->ResourceBarrier(2, b);
}

} // namespace mitiru::render::clod
