// Renderer3D_DX12 のクラス本体の断片 (DX12DynamicGi.hpp から include)。DDGI のレイが当たる場面
//
// 前のフレームに描いた不透明の物 (drawMesh、インスタンス、clod のモデル、地形) から、Mesh ごとの BLAS と、毎フレーム作り直す
// TLAS を作る。描いた物は視錐台で落とす前に残すので、カメラの後ろの壁も光を遮り、跳ね返す (カメラを回しても GI が変わらない)。
// BLAS は Mesh の revision が変わらない限り作り直さない。同じ数のまま形の変わる Mesh (布など) は 2 回目から作り直さずに
// 更新 (refit) する。CPU に頂点を持たない Mesh (compute で変形するスキンのモデル) と、影を落とさない描画は入れない。

static constexpr std::uint32_t kDdgiMaxInstances = 8192;
static constexpr std::uint32_t kDdgiHeapSlots = 4096;
static constexpr std::uint64_t kDdgiEvictFrames = 120;

/// 描いた物 1 個。rows は world の行優先 3x4、albedo は線形の基本色
struct DdgiCaster
{
	const Mesh* mesh = nullptr;
	float rows[3][4] = {};
	float albedo[3] = {1.0f, 1.0f, 1.0f};
	std::uint32_t flags = 0;   ///< bit 0 = 両面
};

std::vector<DdgiCaster> m_ddgiCasters;       ///< このフレームに描いた物
std::vector<DdgiCaster> m_ddgiCastersPrev;   ///< 前のフレームの分。フレームの頭で場面にする
bool m_ddgiRecording = false;                ///< このフレームは描いた物を残すか (beginFrame で決める)
bool m_ddgiRecordedPrev = false;             ///< 前のフレームは残したか (残していなければ場面が空なので更新しない)

/// @brief 描いた物を次のフレームの場面に残す。半透明と影を落とさない描画は呼ばない
void recordDdgiCaster(const Mesh& mesh, const sgc::Mat4f& world, const sgc::Colorf& linearAlbedo, bool doubleSided)
{
	if (!m_ddgiRecording || !m_shadowCasterEnabled || mesh.isGpuOnly() || m_ddgiCasters.size() >= kDdgiMaxInstances) { return; }
	DdgiCaster c;
	c.mesh = &mesh;
	for (int r = 0; r < 3; ++r)
	{
		for (int k = 0; k < 4; ++k) { c.rows[r][k] = world.m[r][k]; }
	}
	c.albedo[0] = linearAlbedo.r;
	c.albedo[1] = linearAlbedo.g;
	c.albedo[2] = linearAlbedo.b;
	c.flags = doubleSided ? 1u : 0u;
	m_ddgiCasters.push_back(c);
}

/// @brief clod のモデルは太陽の影と同じ一番細かい段のメッシュで入れる (色は頂点の色に材質の平均を入れてある)
void recordDdgiClod(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale)
{
	if (!m_ddgiRecording) { return; }
	if (const Mesh* mesh = m_clod.shadowMesh(path); mesh != nullptr && mesh->indexCount() > 0)
	{
		recordDdgiCaster(*mesh, clod::clodInstanceWorld(position, rotYDeg, scale), {1.0f, 1.0f, 1.0f, 1.0f}, false);
	}
}

/// TLAS の 1 個ぶんの、当たった面で読む値 (ddgi.hlsl の InstanceInfo、80 byte)
struct DdgiInstanceGpu
{
	std::uint32_t attrib = 0;
	std::uint32_t flags = 0;
	float pad[2] = {0.0f, 0.0f};
	float albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float n0[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	float n1[4] = {0.0f, 1.0f, 0.0f, 0.0f};
	float n2[4] = {0.0f, 0.0f, 1.0f, 0.0f};
};
static_assert(sizeof(DdgiInstanceGpu) == 80);

/// Mesh 1 個の BLAS と、三角形ごとの値 (法線と色) の StructuredBuffer
struct DdgiMeshAs
{
	gfx::GpuResource blas;
	gfx::GpuResource attribs;
	gfx::GpuResource updateScratch;    ///< refit に使う。形の変わる Mesh だけが持つ
	std::uint32_t slot = 0;            ///< attribs の SRV を置いた m_ddgiHeap の番号
	std::uint64_t revision = 0;
	std::uint64_t lastUsed = 0;
	std::uint32_t vertices = 0;
	std::uint32_t triangles = 0;
	bool updatable = false;            ///< ALLOW_UPDATE で作った (次からは refit で済む)
};

std::unordered_map<const Mesh*, DdgiMeshAs> m_ddgiMeshes;
gi::RayGeometry m_ddgiGeometry;                                   ///< BLAS を作る時の CPU の作業領域 (使い回す)
std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_ddgiInstanceDescs;
std::vector<DdgiInstanceGpu> m_ddgiInstanceInfos;
gfx::GpuResource m_ddgiTlas;
gfx::GpuResource m_ddgiTlasScratch;
std::uint32_t m_ddgiTlasCapacity = 0;
std::uint32_t m_ddgiBlasBuiltThisFrame = 0;
ComPtr<ID3D12DescriptorHeap> m_ddgiHeap;
std::vector<std::uint32_t> m_ddgiFreeSlots;
std::vector<std::pair<std::uint32_t, std::uint64_t>> m_ddgiRetiredSlots;   ///< (番号, 手放したフレーム)

/// @brief m_ddgiHeap の空き番号。前のフレームの GPU がまだ読んでいる番号は FRAME_COUNT + 1 フレーム後に戻す
[[nodiscard]] bool takeDdgiSlot(std::uint32_t& slot)
{
	std::erase_if(m_ddgiRetiredSlots, [this](const auto& r) {
		if (m_frameCounter < r.second + FRAME_COUNT + 1) { return false; }
		m_ddgiFreeSlots.push_back(r.first);
		return true;
	});
	if (m_ddgiFreeSlots.empty()) { return false; }
	slot = m_ddgiFreeSlots.back();
	m_ddgiFreeSlots.pop_back();
	return true;
}

void retireDdgiSlot(std::uint32_t slot) { m_ddgiRetiredSlots.emplace_back(slot, m_frameCounter); }

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE ddgiCpuSlot(std::uint32_t slot) const
{
	auto h = m_ddgiHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(slot) * m_albedoSrvIncrement;
	return h;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE ddgiGpuSlot(std::uint32_t slot) const
{
	auto h = m_ddgiHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<UINT64>(slot) * m_albedoSrvIncrement;
	return h;
}

/// @brief このフレームの間だけ GPU が読む UPLOAD の区画
struct DdgiUpload
{
	ID3D12Resource* resource = nullptr;
	UINT64 offset = 0;
	D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
};

/// @brief data を UPLOAD に書く。小さいものはフレームの ring から取り、大きいもの (初めて見る広い地形など) は一時のバッファにする
[[nodiscard]] DdgiUpload ddgiUploadTemp(const void* data, std::size_t bytes)
{
	if (bytes <= 512 * 1024)
	{
		const auto a = m_uploadRing.upload(data, bytes, 256);
		if (a.valid()) { return {a.resource, a.offset, a.gpuAddr}; }
	}
	gfx::GpuResource buf;
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, buf))) { return {}; }
	void* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(buf->Map(0, &noRead, &mapped))) { return {}; }
	std::memcpy(mapped, data, bytes);
	buf->Unmap(0, nullptr);
	const DdgiUpload out{buf.Get(), 0, buf->GetGPUVirtualAddress()};
	m_frameTempResources.push_back(std::move(buf));
	return out;
}

[[nodiscard]] static D3D12_RAYTRACING_GEOMETRY_DESC ddgiGeometryDesc(D3D12_GPU_VIRTUAL_ADDRESS positions, std::uint32_t vertices,
                                                                     D3D12_GPU_VIRTUAL_ADDRESS indices, std::uint32_t indexCount)
{
	D3D12_RAYTRACING_GEOMETRY_DESC g = {};
	g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	g.Triangles.VertexBuffer.StartAddress = positions;
	g.Triangles.VertexBuffer.StrideInBytes = 12;
	g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	g.Triangles.VertexCount = vertices;
	g.Triangles.IndexBuffer = indices;
	g.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	g.Triangles.IndexCount = indexCount;
	return g;
}

/// @brief 三角形ごとの値を attribs へ写す (attribs は次の状態から COPY_DEST を経て戻す)
[[nodiscard]] bool copyDdgiAttribs(DdgiMeshAs& as, D3D12_RESOURCE_STATES before)
{
	const std::size_t bytes = m_ddgiGeometry.attribs.size() * sizeof(gi::RayTriangleAttrib);
	const DdgiUpload src = ddgiUploadTemp(m_ddgiGeometry.attribs.data(), bytes);
	if (src.resource == nullptr) { return false; }
	if (before != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		dx12::detail::recordTransition(m_graphicsCmdList.Get(), as.attribs.Get(), before, D3D12_RESOURCE_STATE_COPY_DEST);
	}
	m_graphicsCmdList->CopyBufferRegion(as.attribs.Get(), 0, src.resource, src.offset, bytes);
	dx12::detail::recordTransition(m_graphicsCmdList.Get(), as.attribs.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
	                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	return true;
}

/// @brief BLAS を (作り直すか更新して) 積む。update は同じ数の頂点と三角形で、as が ALLOW_UPDATE で作られている時だけ
[[nodiscard]] bool recordDdgiBlas(DdgiMeshAs& as, bool update, bool allowUpdate)
{
	const gi::RayGeometry& g = m_ddgiGeometry;
	const auto pos = ddgiUploadTemp(g.positions.data(), g.positions.size() * sizeof(float)).gpu;
	const auto idx = ddgiUploadTemp(g.indices.data(), g.indices.size() * sizeof(std::uint32_t)).gpu;
	if (pos == 0 || idx == 0) { return false; }
	const D3D12_RAYTRACING_GEOMETRY_DESC geom = ddgiGeometryDesc(pos, g.vertexCount(), idx, static_cast<std::uint32_t>(g.indices.size()));
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
	in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	in.NumDescs = 1;
	in.pGeometryDescs = &geom;
	in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (allowUpdate) { in.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE; }
	if (update) { in.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; }
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre = {};
	m_ddgiDevice->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
	constexpr auto kUav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (!update)
	{
		gfx::GpuResource blas;
		if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, pre.ResultDataMaxSizeInBytes,
		                                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, blas, kUav)))
		{
			return false;
		}
		if (as.blas) { m_frameTempResources.push_back(std::move(as.blas)); }
		as.blas = std::move(blas);
	}
	gfx::GpuResource* scratch = &as.updateScratch;
	gfx::GpuResource oneShot;
	const std::uint64_t scratchBytes = update ? pre.UpdateScratchDataSizeInBytes : pre.ScratchDataSizeInBytes;
	if (!allowUpdate) { scratch = &oneShot; }
	if (!*scratch || (*scratch)->GetDesc().Width < scratchBytes)
	{
		const std::uint64_t bytes = allowUpdate ? std::max(pre.ScratchDataSizeInBytes, pre.UpdateScratchDataSizeInBytes) : scratchBytes;
		if (*scratch) { m_frameTempResources.push_back(std::move(*scratch)); }
		if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, std::max<std::uint64_t>(bytes, 256),
		                                D3D12_RESOURCE_STATE_COMMON, *scratch, kUav)))
		{
			return false;
		}
	}
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs = in;
	desc.DestAccelerationStructureData = as.blas->GetGPUVirtualAddress();
	desc.SourceAccelerationStructureData = update ? as.blas->GetGPUVirtualAddress() : 0;
	desc.ScratchAccelerationStructureData = (*scratch)->GetGPUVirtualAddress();
	m_ddgiList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
	if (oneShot) { m_frameTempResources.push_back(std::move(oneShot)); }
	++m_ddgiBlasBuiltThisFrame;
	return true;
}

/// @brief 三角形ごとの値のバッファと、その SRV を m_ddgiHeap に置く
[[nodiscard]] bool createDdgiAttribs(DdgiMeshAs& as)
{
	const std::uint32_t tris = m_ddgiGeometry.triangleCount();
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, std::uint64_t{tris} * sizeof(gi::RayTriangleAttrib),
	                                D3D12_RESOURCE_STATE_COMMON, as.attribs)) ||
	    !takeDdgiSlot(as.slot))
	{
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC v = {};
	v.Format = DXGI_FORMAT_UNKNOWN;
	v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	v.Buffer.NumElements = tris;
	v.Buffer.StructureByteStride = sizeof(gi::RayTriangleAttrib);
	m_d3dDevice->CreateShaderResourceView(as.attribs.Get(), &v, ddgiCpuSlot(as.slot));
	return copyDdgiAttribs(as, D3D12_RESOURCE_STATE_COPY_DEST);
}

void releaseDdgiMesh(DdgiMeshAs& as)
{
	for (gfx::GpuResource* r : {&as.blas, &as.attribs, &as.updateScratch})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	if (as.slot != 0) { retireDdgiSlot(as.slot); }
	as.slot = 0;
}

/// @brief mesh の BLAS。初めてか形が変わったら積み直す。作れなければ nullptr
[[nodiscard]] DdgiMeshAs* ensureDdgiMesh(const Mesh& mesh)
{
	auto [it, added] = m_ddgiMeshes.try_emplace(&mesh);
	DdgiMeshAs& as = it->second;
	as.lastUsed = m_frameCounter;
	if (!added && as.revision == mesh.revision() && as.blas) { return &as; }
	gi::buildRayGeometry(mesh, m_ddgiGeometry);
	if (m_ddgiGeometry.triangleCount() == 0)
	{
		releaseDdgiMesh(as);
		m_ddgiMeshes.erase(it);
		return nullptr;
	}
	const bool sameShape = !added && as.blas && as.vertices == m_ddgiGeometry.vertexCount() &&
	                       as.triangles == m_ddgiGeometry.triangleCount();
	bool ok = false;
	if (sameShape)
	{
		// 同じ数のまま変わった形 (布など) は、2 回目からは更新で済むよう ALLOW_UPDATE で作り直す
		ok = recordDdgiBlas(as, as.updatable, true) && copyDdgiAttribs(as, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		as.updatable = ok;
	}
	else
	{
		releaseDdgiMesh(as);
		as.updatable = false;
		as.vertices = m_ddgiGeometry.vertexCount();
		as.triangles = m_ddgiGeometry.triangleCount();
		ok = createDdgiAttribs(as) && recordDdgiBlas(as, false, false);
	}
	if (!ok)
	{
		releaseDdgiMesh(as);
		m_ddgiMeshes.erase(it);
		return nullptr;
	}
	as.revision = mesh.revision();
	return &as;
}

/// @brief しばらく描かれていない Mesh の BLAS を手放す
void evictDdgiMeshes()
{
	std::erase_if(m_ddgiMeshes, [this](auto& kv) {
		if (kv.second.lastUsed + kDdgiEvictFrames >= m_frameCounter) { return false; }
		releaseDdgiMesh(kv.second);
		return true;
	});
}

/// @brief TLAS へ 1 個積む。rows は world の行優先 3x4、albedo は線形
void appendDdgiInstance(const DdgiMeshAs& as, const float (&rows)[3][4], const float (&albedo)[3], std::uint32_t flags)
{
	if (m_ddgiInstanceDescs.size() >= kDdgiMaxInstances) { return; }
	D3D12_RAYTRACING_INSTANCE_DESC d = {};
	std::memcpy(d.Transform, rows, sizeof(d.Transform));
	d.InstanceID = static_cast<UINT>(m_ddgiInstanceInfos.size());
	d.InstanceMask = 0xFF;
	d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
	d.AccelerationStructure = as.blas->GetGPUVirtualAddress();
	m_ddgiInstanceDescs.push_back(d);
	DdgiInstanceGpu info;
	info.attrib = as.slot;
	info.flags = flags;
	for (int i = 0; i < 3; ++i) { info.albedo[i] = albedo[i]; }
	const auto n = gi::normalMatrix(rows);
	for (int c = 0; c < 3; ++c)
	{
		info.n0[c] = n[c];
		info.n1[c] = n[3 + c];
		info.n2[c] = n[6 + c];
	}
	m_ddgiInstanceInfos.push_back(info);
}

/// @brief 主ビューと副ビューが同じ物を描いたフレームは同じ行が並ぶので、並べ替えて 1 つにする (確保しない)
void dedupeDdgiCasters()
{
	const auto less = [](const DdgiCaster& a, const DdgiCaster& b) {
		if (a.mesh != b.mesh) { return std::less<const Mesh*>{}(a.mesh, b.mesh); }
		return std::memcmp(a.rows, b.rows, sizeof(a.rows)) < 0;
	};
	const auto same = [](const DdgiCaster& a, const DdgiCaster& b) {
		return a.mesh == b.mesh && std::memcmp(a.rows, b.rows, sizeof(a.rows)) == 0;
	};
	std::sort(m_ddgiCastersPrev.begin(), m_ddgiCastersPrev.end(), less);
	m_ddgiCastersPrev.erase(std::unique(m_ddgiCastersPrev.begin(), m_ddgiCastersPrev.end(), same), m_ddgiCastersPrev.end());
}

/// @brief 前のフレームに描いた物から BLAS と TLAS を積む
[[nodiscard]] bool buildDdgiScene()
{
	m_ddgiInstanceDescs.clear();
	m_ddgiInstanceInfos.clear();
	m_ddgiBlasBuiltThisFrame = 0;
	dedupeDdgiCasters();
	for (const DdgiCaster& c : m_ddgiCastersPrev)
	{
		if (c.mesh->vertexCount() < 3) { continue; }
		if (const DdgiMeshAs* as = ensureDdgiMesh(*c.mesh)) { appendDdgiInstance(*as, c.rows, c.albedo, c.flags); }
	}
	if ((m_frameCounter & 31u) == 0) { evictDdgiMeshes(); }
	if (m_ddgiBlasBuiltThisFrame > 0)
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		m_graphicsCmdList->ResourceBarrier(1, &b);
	}
	return recordDdgiTlas();
}

[[nodiscard]] bool ensureDdgiTlasCapacity(std::uint32_t count)
{
	if (m_ddgiTlas && count <= m_ddgiTlasCapacity) { return true; }
	const std::uint32_t capacity = std::max<std::uint32_t>(64, std::bit_ceil(std::max(count, 1u)));
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
	in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	in.NumDescs = capacity;
	in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre = {};
	m_ddgiDevice->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
	for (gfx::GpuResource* r : {&m_ddgiTlas, &m_ddgiTlasScratch})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	constexpr auto kUav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, pre.ResultDataMaxSizeInBytes,
	                                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, m_ddgiTlas, kUav)) ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, std::max<std::uint64_t>(pre.ScratchDataSizeInBytes, 256),
	                                D3D12_RESOURCE_STATE_COMMON, m_ddgiTlasScratch, kUav)))
	{
		m_ddgiTlasCapacity = 0;
		return false;
	}
	m_ddgiTlasCapacity = capacity;
	return true;
}

/// @brief このフレームの TLAS を作り直す (Mesh が動いても TLAS は毎フレーム新しいので、動く剛体もそのまま入る)
[[nodiscard]] bool recordDdgiTlas()
{
	const auto count = static_cast<std::uint32_t>(m_ddgiInstanceDescs.size());
	if (!ensureDdgiTlasCapacity(count)) { return false; }
	D3D12_GPU_VIRTUAL_ADDRESS descs = 0;
	if (count > 0)
	{
		descs = ddgiUploadTemp(m_ddgiInstanceDescs.data(), m_ddgiInstanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)).gpu;
		if (descs == 0) { return false; }
	}
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	desc.Inputs.NumDescs = count;
	desc.Inputs.InstanceDescs = descs;
	desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	desc.DestAccelerationStructureData = m_ddgiTlas->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = m_ddgiTlasScratch->GetGPUVirtualAddress();
	m_ddgiList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	b.UAV.pResource = m_ddgiTlas.Get();
	m_graphicsCmdList->ResourceBarrier(1, &b);
	return true;
}

/// @brief BLAS と TLAS と記述子を全部手放す (GPU が読み終えてから)
void releaseDdgiScene()
{
	for (auto& kv : m_ddgiMeshes) { releaseDdgiMesh(kv.second); }
	m_ddgiMeshes.clear();
	for (gfx::GpuResource* r : {&m_ddgiTlas, &m_ddgiTlasScratch})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	m_ddgiTlasCapacity = 0;
}
