// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// 投影デカール (Decals.hpp)。描画はフレームの途中で命令を積むので、デカールの一覧と froxel への割り当ては
// endFrame で決まる。そこでフレームの頭で upload ring の場所を取り、ビット集合を DEFAULT のバッファへ写す
// 命令を記録の先頭に置いておき、中身は endFrame で埋める (CPU が書くのは提出の前なので間に合う)。
// 割り当ては 1 枚あたり数十 froxel の CPU の判定で済むので、compute を足さない。

public:

/// @brief このフレームのデカールを積む。毎フレーム、生きている分を渡す (レンダラは前のフレームを覚えない)。
///        副ビューの間に積んだデカールはそのビューにだけ出る
void submitDecals(const DecalDesc* decals, int count) override
{
	if (decals == nullptr || count <= 0) { return; }
	std::vector<DecalDesc>& queue = (m_activeView != nullptr) ? m_activeView->decals : m_decals;
	const int room = kMaxDecals - static_cast<int>(queue.size());
	if (count > room)
	{
		debug::warnOnce("dx12.decals.cap", "デカールは 1 フレームに " + std::to_string(kMaxDecals) + " 枚まで — 超えた分は捨てる");
		count = std::max(room, 0);
	}
	queue.insert(queue.end(), decals, decals + count);
}

/// @brief 直前に描画へ使ったデカールの数 (見えるものを選んだ後)
[[nodiscard]] int visibleDecalCount() const noexcept { return m_visibleDecalCount; }

/// @brief 直前に描画へ使ったデカール (描く順。テストが箱と画素を突き合わせる)
[[nodiscard]] const std::vector<DecalGpu>& visibleDecals() const noexcept { return m_visibleDecals; }

private:

std::vector<DecalDesc>             m_decals;              ///< このフレームに積まれた分 (beginFrame で空)
std::vector<DecalGpu>              m_visibleDecals;
std::vector<DecalGpu>              m_viewDecalScratch;    ///< 副ビューの選別の作業場所
std::vector<LocalLightGpu>         m_decalBounds;
std::vector<std::pair<float, int>> m_decalSortScratch;
dx12::UploadAllocation             m_decalBufferAlloc;    ///< t35。kMaxVisibleDecals 枚ぶん
dx12::UploadAllocation             m_decalMaskAlloc;      ///< 写し元。kDecalMaskBufferWords 語
gfx::GpuResource                   m_decalMasks;          ///< t36
int                                m_visibleDecalCount = 0;

void createDecalResources()
{
	m_decals.reserve(64);
	m_visibleDecals.reserve(kMaxVisibleDecals);
	m_decalBounds.reserve(kMaxVisibleDecals);
	const UINT64 bytes = sizeof(std::uint32_t) * kDecalMaskBufferWords;
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COMMON, m_decalMasks)))
	{
		debug::warnOnce("dx12.decals.init", "デカールの froxel のバッファを作れない — デカールは描かない");
		m_decalMasks.Reset();
		return;
	}
	m_decalMasks->SetName(L"Renderer3D decal masks");
}

/// @brief beginFrame で、記録の頭 (最初の描画より前) に呼ぶ
void beginFrameDecals()
{
	m_decals.clear();
	m_visibleDecalCount = 0;
	flushVfxUploads();
	m_decalBufferAlloc = m_uploadRing.allocate(sizeof(DecalGpu) * kMaxVisibleDecals, 256);
	m_decalMaskAlloc = m_uploadRing.allocate(sizeof(std::uint32_t) * kDecalMaskBufferWords, 256);
	if (!m_decalMasks || !m_decalMaskAlloc.valid() || !m_decalBufferAlloc.valid()) { return; }
	// 埋めないまま終わったフレームでも、シェーダーが数 0 を読んで何もしないように
	std::memset(m_decalMaskAlloc.cpuPtr, 0, sizeof(std::uint32_t) * kDecalMaskHeaderWords);
	// バッファは提出の終わりで COMMON に戻るので、写す前は暗黙の昇格に任せ、読む側へだけ遷移を張る
	m_graphicsCmdList->CopyBufferRegion(m_decalMasks.Get(), 0, m_decalMaskAlloc.resource, m_decalMaskAlloc.offset,
	                                    m_decalMaskAlloc.size);
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = m_decalMasks.Get();
	b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

/// @brief endFrame で呼ぶ。主ビューと、このフレームに描いた副ビューごとに見えるデカールを選んで並べ、
///        froxel へ割り当てて頭で取った場所へ書く
void finalizeDecals()
{
	m_visibleDecalCount = 0;
	m_visibleDecals.clear();
	if (m_decalMasks && !m_decals.empty())
	{
		m_visibleDecalCount = writeDecalsFor(m_decals, m_clodCamera, m_decalBufferAlloc, m_decalMaskAlloc, m_visibleDecals);
	}
	for (const int pass : m_viewsThisFrame)
	{
		View3D* v = viewOfPass(pass);
		if (v == nullptr || v->frame.frame != m_frameCounter || v->decals.empty()) { continue; }
		(void)writeDecalsFor(v->decals, v->camera, v->frame.decals, v->frame.decalMask, m_viewDecalScratch);
	}
}

/// @brief camera から見えるデカールを visible へ選び、buffer と mask (頭で取った場所) へ書く。描く枚数を返す
int writeDecalsFor(const std::vector<DecalDesc>& decals, const Camera3D& camera, const dx12::UploadAllocation& buffer,
                   const dx12::UploadAllocation& mask, std::vector<DecalGpu>& visible)
{
	if (!mask.valid() || !buffer.valid()) { return 0; }
	const ClusterView view = ClusterView::fromCamera(camera);
	const int dropped = selectVisibleDecals(decals, view, m_decalSortScratch, visible, m_decalBounds);
	if (dropped > 0)
	{
		debug::warnOnce("dx12.decals.visibleCap",
		                "見えているデカールが " + std::to_string(kMaxVisibleDecals) + " を超えた — 遠いものから描かない");
	}
	std::memcpy(buffer.cpuPtr, visible.data(), sizeof(DecalGpu) * visible.size());
	assignDecalsToClusters(m_decalBounds, view,
	                       std::span<std::uint32_t>(static_cast<std::uint32_t*>(mask.cpuPtr), kDecalMaskBufferWords));
	return static_cast<int>(visible.size());
}

/// @brief 場面の表の t35 (デカール)・t36 (ビット集合)・t37/t38 (VFX テクスチャ) を cpu から 4 枚に書く
void writeDecalSrvs(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_UNKNOWN;
	const bool usable = m_decalMasks && m_decalBufferAlloc.valid() && m_decalMaskAlloc.valid();
	if (!usable)
	{
		// null の SRV は構造化にできないので型つきにする。読むと 0 で、数 0 として何もしない
		sv.Format = DXGI_FORMAT_R32_UINT;
		sv.Buffer.NumElements = 1;
		m_d3dDevice->CreateShaderResourceView(nullptr, &sv, cpu);
		m_d3dDevice->CreateShaderResourceView(nullptr, &sv, nextDescriptor(cpu, 1));
		writeVfxSrvs(nextDescriptor(cpu, 2));
		return;
	}
	sv.Buffer.StructureByteStride = sizeof(DecalGpu);
	sv.Buffer.FirstElement = m_decalBufferAlloc.offset / sizeof(DecalGpu);
	sv.Buffer.NumElements = kMaxVisibleDecals;
	m_d3dDevice->CreateShaderResourceView(m_decalBufferAlloc.resource, &sv, cpu);
	sv.Buffer.StructureByteStride = sizeof(std::uint32_t);
	sv.Buffer.FirstElement = 0;
	sv.Buffer.NumElements = static_cast<UINT>(kDecalMaskBufferWords);
	m_d3dDevice->CreateShaderResourceView(m_decalMasks.Get(), &sv, nextDescriptor(cpu, 1));
	writeVfxSrvs(nextDescriptor(cpu, 2));
}
