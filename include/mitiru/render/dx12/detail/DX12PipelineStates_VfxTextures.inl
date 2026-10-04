// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される
//
// デカールと粒が共有するテクスチャの配列 (一辺 kVfxLayerSize、kVfxMaxLayers 層)。登録は層の画像を CPU で
// 作って溜めるだけで、GPU へ上げるのは次のフレームの頭 (描画の前) にまとめて行う。資源は TYPELESS で、
// 色として読む SRV (sRGB) とデータとして読む SRV (法線) の 2 本を場面の表に置く。

public:

/// @brief 画像 (RGBA8、w x h) を VFX テクスチャの層に登録する。返す番号を DecalDesc / ParticleEmitterDesc の層に書く
/// @return 層の番号。層が尽きたか画像が空なら -1
int registerVfxTexture(const std::uint8_t* rgba, int width, int height, VfxTextureKind kind = VfxTextureKind::Color)
{
	if (m_vfxLayerCount >= kVfxMaxLayers)
	{
		debug::warnOnce("dx12.vfx.layers", "VFX テクスチャは " + std::to_string(kVfxMaxLayers) +
		                                       " 枚までなので、これ以上は登録しません。vfxTexture3D で登録する画像を減らしてください。");
		return -1;
	}
	auto mips = buildVfxLayer(rgba, width, height, kind);
	if (mips.empty()) { return -1; }
	const int layer = m_vfxLayerCount++;
	m_vfxPending.push_back({layer, std::move(mips)});
	return layer;
}

/// @brief 画像ファイル (PNG / JPEG) を層に登録する。同じ path と種類なら前と同じ番号を返す。読めなければ -1 (理由は 1 度だけ出る)
int registerVfxTextureFile(const char* path, VfxTextureKind kind = VfxTextureKind::Color) override
{
	if (path == nullptr) { return -1; }
	const std::string key = std::string(kind == VfxTextureKind::Normal ? "n:" : "c:") + path;
	if (const auto it = m_vfxFileLayers.find(key); it != m_vfxFileLayers.end()) { return it->second; }
	const auto tex = Texture::fromFile(path);
	if (!tex || !tex->valid())
	{
		debug::warnOnce(std::string("dx12.vfx.file.") + path,
		                std::string("VFX テクスチャ ") + path + " を読めません。パスと、PNG か JPEG かを確かめてください。");
		return -1;
	}
	const int layer = registerVfxTexture(tex->pixels().data(), tex->width(), tex->height(), kind);
	if (layer >= 0) { m_vfxFileLayers.emplace(key, layer); }
	return layer;
}

private:

struct VfxPendingLayer
{
	int layer = 0;
	std::vector<std::vector<std::uint8_t>> mips;
};

gfx::GpuResource             m_vfxTexture;
std::vector<VfxPendingLayer> m_vfxPending;
std::unordered_map<std::string, int> m_vfxFileLayers;   ///< ファイルから登録した層 (種類つきの path → 番号)
int                          m_vfxLayerCount = 0;
static constexpr UINT        kVfxMipCount = 9;   // 256 から 1 まで
static_assert((1 << (kVfxMipCount - 1)) == kVfxLayerSize);

/// @brief beginFrame (記録の頭) で呼ぶ。溜まった層を配列へ写す。配列はここで初めて作る
void flushVfxUploads()
{
	if (m_vfxPending.empty() || !m_graphicsCmdList) { return; }
	const bool created = !m_vfxTexture;
	if (created && !createVfxTexture())
	{
		debug::verboseOnce("dx12.vfx.create", "VFX テクスチャの配列を作れなかったので、VFX テクスチャは描きません。");
		m_vfxPending.clear();
		return;
	}
	if (!created) { vfxBarrier(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST); }
	for (const VfxPendingLayer& p : m_vfxPending) { copyVfxLayer(p); }
	m_vfxPending.clear();
	vfxBarrier(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

[[nodiscard]] bool createVfxTexture()
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = kVfxLayerSize;
	desc.Height = kVfxLayerSize;
	desc.DepthOrArraySize = static_cast<UINT16>(kVfxMaxLayers);
	desc.MipLevels = static_cast<UINT16>(kVfxMipCount);
	desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	desc.SampleDesc.Count = 1;
	if (FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
	                                  m_vfxTexture)) || !m_vfxTexture)
	{
		return false;
	}
	m_vfxTexture->SetName(L"Renderer3D VFX textures");
	return true;
}

void copyVfxLayer(const VfxPendingLayer& p)
{
	D3D12_RESOURCE_DESC desc = m_vfxTexture->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[kVfxMipCount] = {};
	UINT rows[kVfxMipCount] = {};
	UINT64 rowBytes[kVfxMipCount] = {};
	UINT64 total = 0;
	const UINT first = static_cast<UINT>(p.layer) * kVfxMipCount;
	m_d3dDevice->GetCopyableFootprints(&desc, first, kVfxMipCount, 0, layouts, rows, rowBytes, &total);
	auto staging = m_uploadRing.allocate(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	if (!staging.valid() || p.mips.size() < kVfxMipCount) { return; }
	auto* dst = static_cast<std::uint8_t*>(staging.cpuPtr);
	for (UINT lv = 0; lv < kVfxMipCount; ++lv)
	{
		for (UINT r = 0; r < rows[lv]; ++r)
		{
			std::memcpy(dst + layouts[lv].Offset + static_cast<UINT64>(r) * layouts[lv].Footprint.RowPitch,
			            p.mips[lv].data() + static_cast<std::size_t>(r) * rowBytes[lv], static_cast<std::size_t>(rowBytes[lv]));
		}
		D3D12_TEXTURE_COPY_LOCATION to = {};
		to.pResource = m_vfxTexture.Get();
		to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		to.SubresourceIndex = first + lv;
		D3D12_TEXTURE_COPY_LOCATION from = {};
		from.pResource = staging.resource;
		from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		from.PlacedFootprint = layouts[lv];
		from.PlacedFootprint.Offset += staging.offset;
		m_graphicsCmdList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
	}
}

void vfxBarrier(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = m_vfxTexture.Get();
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

/// @brief 色 (sRGB) とデータの SRV を cpu と次の 1 枚に書く。配列が無ければ null の配列
void writeVfxSrvs(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Texture2DArray.MipLevels = kVfxMipCount;
	sv.Texture2DArray.ArraySize = static_cast<UINT>(kVfxMaxLayers);
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	m_d3dDevice->CreateShaderResourceView(m_vfxTexture.Get(), &sv, cpu);
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_d3dDevice->CreateShaderResourceView(m_vfxTexture.Get(), &sv, nextDescriptor(cpu, 1));
}
