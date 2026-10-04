// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。描画ごとの材質の SRV 表と定数
//
// SRV は 2 つの表に分ける。材質の表 (root 4: t0 基本色 / t3 法線 / t4 金属・粗さ / t5 自発光) は
// 同じフレームで同じテクスチャの組なら同じ表を使い、場面の表 (root 5: t1/t2 影 / t8-t10 IBL / t11 スポットの影 /
// t35-t38 デカールと VFX テクスチャ / t39-t43 間接光。並びは DX12SceneTableLayout.hpp) は
// フレームに 1 枚。
// どちらも m_albedoSrvHeap のこのフレームの区画に書く。

/// @brief glTF 由来の材質の GPU 側。null のマップは使わない (シェーダーは MapFlags で見分ける)
struct MaterialMaps
{
	const dx12::Dx12Texture2D* albedo = nullptr;             ///< 非 null なら Material::albedoTexture より優先
	const dx12::Dx12Texture2D* normal = nullptr;
	const dx12::Dx12Texture2D* metallicRoughness = nullptr;  ///< G = 粗さ、B = 金属 (glTF)
	const dx12::Dx12Texture2D* emissive = nullptr;
	float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};           ///< PBR の基本色 (glTF の baseColorFactor)
	float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
	float normalScale = 1.0f;
	float occlusionStrength = 0.0f;                          ///< 金属・粗さマップの R を遮蔽に使う強さ
	bool  hasBaseColor = false;                              ///< false なら Material::diffuse を PBR の基本色にする
};

/// @brief 材質の表の 1 件 (同じフレームで同じ組なら同じ表を引く)
struct MaterialTableEntry
{
	const dx12::Dx12Texture2D* albedo = nullptr;   ///< nullptr = 空き
	const dx12::Dx12Texture2D* normal = nullptr;
	const dx12::Dx12Texture2D* metalRough = nullptr;
	const dx12::Dx12Texture2D* emissive = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
};

static constexpr UINT kMaterialTableSize = 4;
/// 1 フレームの区画。材質の表は (区画 - 場面の表) / 4 = 508 枚まで
static constexpr UINT kAlbedoSrvPerFrame = 2048;
/// 開番地法のハッシュ表。表の最大数の 1.5 倍以上の 2 冪にして埋まり切らないようにする
static constexpr int kMaterialTableCacheSize = 1024;
static_assert(kMaterialTableCacheSize >= static_cast<int>(kAlbedoSrvPerFrame / kMaterialTableSize) * 3 / 2);

ComPtr<ID3D12DescriptorHeap> m_albedoSrvHeap;
UINT m_albedoSrvCapacity  = 0;  ///< 1 frame 分の実効 capacity
UINT m_albedoSrvBase      = 0;  ///< 現 frame partition の先頭 slot
UINT m_albedoSrvCursor    = 0;
UINT m_albedoSrvIncrement = 0;
MaterialTableEntry m_materialTableCache[kMaterialTableCacheSize]{};
int m_materialTableCacheCount = 0;
D3D12_GPU_DESCRIPTOR_HANDLE m_sceneTableGpu{};   ///< このフレームの場面の表 (ptr 0 = まだ書いていない)
bool m_sceneTableHasIbl = false;
dx12::Dx12Texture2D m_defaultWhiteTexture;
bool m_defaultWhiteReady = false;
std::unordered_map<const Texture*, std::unique_ptr<dx12::Dx12Texture2D>> m_textureCache;

/// @brief beginFrame で呼ぶ。自分のフレームの区画の先頭へ戻し、表の再利用を忘れる
void beginFrameMaterialTables(uint32_t frameIndex)
{
	m_albedoSrvBase = frameIndex * m_albedoSrvCapacity;
	m_albedoSrvCursor = 0;
	m_sceneTableGpu = {};
	if (m_materialTableCacheCount > 0)
	{
		for (auto& e : m_materialTableCache) { e = MaterialTableEntry{}; }
		m_materialTableCacheCount = 0;
	}
}

/// @brief このフレームの区画から n 枚ぶんの連続した場所を取る。足りなければ false
[[nodiscard]] bool reserveSrvSlots(UINT n, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
{
	if (m_albedoSrvCursor + n > m_albedoSrvCapacity) { return false; }
	const UINT slot = m_albedoSrvBase + m_albedoSrvCursor;
	cpu = m_albedoSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(slot) * m_albedoSrvIncrement;
	gpu = m_albedoSrvHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot) * m_albedoSrvIncrement;
	m_albedoSrvCursor += n;
	return true;
}

void writeTextureOrNull(const dx12::Dx12Texture2D* tex, D3D12_CPU_DESCRIPTOR_HANDLE cpu) const
{
	if (tex != nullptr && tex->nativeResource() != nullptr)
	{
		tex->createSRV(m_d3dDevice, cpu);
		return;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(nullptr, &srv, cpu);
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE nextDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE h, UINT n = 1) const
{
	h.ptr += static_cast<SIZE_T>(n) * m_albedoSrvIncrement;
	return h;
}

[[nodiscard]] static UINT64 materialTableHash(const MaterialTableEntry& k) noexcept
{
	const auto mix = [](const void* p) { return static_cast<UINT64>(reinterpret_cast<std::uintptr_t>(p) >> 4); };
	return ((mix(k.albedo) ^ (mix(k.normal) * 31) ^ (mix(k.metalRough) * 131) ^ (mix(k.emissive) * 1031)) *
	        0x9E3779B97F4A7C15ull) >> 40;
}

/// @brief 材質の表 { t0, t3, t4, t5 } を引く (同じ組は再利用)。区画が尽きたら ptr 0
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE writeMaterialTable(const dx12::Dx12Texture2D* albedo, const MaterialMaps* maps)
{
	const MaterialTableEntry key{albedo, maps ? maps->normal : nullptr, maps ? maps->metallicRoughness : nullptr,
	                             maps ? maps->emissive : nullptr, {}};
	constexpr UINT64 kMask = static_cast<UINT64>(kMaterialTableCacheSize - 1);
	UINT64 probe = materialTableHash(key);
	MaterialTableEntry* freeSlot = nullptr;
	for (int n = 0; n < kMaterialTableCacheSize; ++n, ++probe)
	{
		MaterialTableEntry& e = m_materialTableCache[probe & kMask];
		if (e.albedo == nullptr) { freeSlot = &e; break; }
		if (e.albedo == key.albedo && e.normal == key.normal && e.metalRough == key.metalRough && e.emissive == key.emissive)
		{
			return e.gpu;
		}
	}
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	if (!reserveSrvSlots(kMaterialTableSize, cpu, gpu))
	{
		// 超えた draw は表を張り替えないので、直前の draw のテクスチャのまま描かれる
		mitiru::debug::warnOnce("dx12.materialTable.full",
			"1 フレームに描ける材質のテクスチャの組は " + std::to_string(m_albedoSrvCapacity / kMaterialTableSize) +
			" 種類までなので、超えた分は直前のテクスチャのまま描きます。1 フレームに描く材質の種類を減らしてください。");
		return {};
	}
	writeTextureOrNull(key.albedo, cpu);
	writeTextureOrNull(key.normal, nextDescriptor(cpu, 1));
	writeTextureOrNull(key.metalRough, nextDescriptor(cpu, 2));
	writeTextureOrNull(key.emissive, nextDescriptor(cpu, 3));
	if (freeSlot != nullptr)
	{
		MaterialTableEntry& dst = freeSlot[0];
		dst = key;
		dst.gpu = gpu;
		++m_materialTableCacheCount;
	}
	return gpu;
}

/// @brief 影マップの SRV。初期化に失敗していれば白テクスチャで埋める (未初期化の SRV は読ませない)
void writeShadowSrv(dx12::Dx12ShadowMap& map, D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	if (map.isInitialized() && map.nativeResource())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R32_FLOAT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		m_d3dDevice->CreateShaderResourceView(map.nativeResource(), &srv, cpu);
		return;
	}
	ensureDefaultWhiteTexture();
	writeTextureOrNull(m_defaultWhiteReady ? &m_defaultWhiteTexture : nullptr, cpu);
}

/// @brief 場面の表 { t1, t2, t8..t11, t35..t43 } をフレームに 1 回書く。環境マップがあれば IBL を載せる
/// @details 陰影の種類に関わらず載せる (フレームの途中で PBR へ切り替えても、表は最初の描画で決まるため)
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE ensureSceneTable()
{
	if (m_sceneTableGpu.ptr != 0) { return m_sceneTableGpu; }
	m_sceneTableHasIbl = false;
	if (m_pbrEnvironmentCubemap.valid())
	{
		ensurePBREnvironmentTexturesDx12();
		if (m_pbrEnvironmentTextureReady && m_pbrEnvironmentNeedsUpload) { uploadPBREnvironmentTexturesDx12(); }
		m_sceneTableHasIbl = m_pbrEnvironmentTextureReady && m_pbrEnvironmentTextureInPSR;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	if (!reserveSrvSlots(dx12::kSceneTableSize, cpu, gpu)) { return {}; }
	writeShadowSrv(m_shadowMap, cpu);
	writeShadowSrv(m_shadowMapFar, nextDescriptor(cpu, 1));
	writeEnvironmentSrvs(nextDescriptor(cpu, 2));
	writeSpotShadowSrv(nextDescriptor(cpu, 5));
	writeDecalSrvs(nextDescriptor(cpu, 6));
	writeIndirectSrvs(nextDescriptor(cpu, dx12::kSceneTableIndirect));
	m_sceneTableGpu = gpu;
	return gpu;
}

/// @brief t8 irradiance / t9 prefiltered / t10 BRDF 表。IBL が無ければ null SRV
void writeEnvironmentSrvs(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC cube = {};
	cube.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	cube.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	cube.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	cube.TextureCube.MipLevels = 1;
	D3D12_SHADER_RESOURCE_VIEW_DESC lut = {};
	lut.Format = DXGI_FORMAT_R32G32_FLOAT;
	lut.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	lut.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	lut.Texture2D.MipLevels = 1;
	const bool ibl = m_sceneTableHasIbl;
	m_d3dDevice->CreateShaderResourceView(ibl ? m_pbrIrradianceTexture.Get() : nullptr, &cube, cpu);
	cube.TextureCube.MipLevels = ibl ? static_cast<UINT>(kPbrPrefilterMipCount) : 1u;
	m_d3dDevice->CreateShaderResourceView(ibl ? m_pbrPrefilteredTexture.Get() : nullptr, &cube, nextDescriptor(cpu, 1));
	m_d3dDevice->CreateShaderResourceView(ibl ? m_pbrBrdfLutTexture.Get() : nullptr, &lut, nextDescriptor(cpu, 2));
}

/// @brief 1x1 白テクスチャを遅延アップロードする (recording 中の command list が必要)
void ensureDefaultWhiteTexture()
{
	if (m_defaultWhiteReady || !m_graphicsCmdList) { return; }
	const auto white = Texture::solid(1, 1, 255, 255, 255, 255);
	m_defaultWhiteReady = m_defaultWhiteTexture.uploadFrom(m_d3dDevice, m_graphicsCmdList.Get(), white, m_frameTempResources);
}

/// @brief CPU の Texture を (初回だけ) GPU へ上げて返す。失敗時 nullptr
[[nodiscard]] const dx12::Dx12Texture2D* getOrUploadAlbedo(const Texture* tex)
{
	if (!tex) { return nullptr; }
	if (const auto it = m_textureCache.find(tex); it != m_textureCache.end()) { return it->second.get(); }
	auto t = std::make_unique<dx12::Dx12Texture2D>();
	if (!t->uploadFrom(m_d3dDevice, m_graphicsCmdList.Get(), *tex, m_frameTempResources)) { return nullptr; }
	const auto* raw = t.get();
	m_textureCache.emplace(tex, std::move(t));
	return raw;
}

/// @brief 基本色のテクスチャ: maps の GPU テクスチャ > Material::albedoTexture > 白
[[nodiscard]] const dx12::Dx12Texture2D* resolveAlbedo(const Material& material, const MaterialMaps* maps)
{
	if (maps != nullptr && maps->albedo != nullptr) { return maps->albedo; }
	if (const auto* t = getOrUploadAlbedo(material.albedoTexture)) { return t; }
	ensureDefaultWhiteTexture();
	return m_defaultWhiteReady ? &m_defaultWhiteTexture : nullptr;
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadDrawExCB(const Material& material, const MaterialMaps* maps,
                                                       const DrawTint& tint)
{
	DX12CbDrawEx cb;
	// glTF の baseColorFactor は線形のまま、Material::diffuse は書いた sRGB なので線形にする
	const sgc::Colorf base = (maps != nullptr && maps->hasBaseColor)
		? sgc::Colorf{maps->baseColor[0], maps->baseColor[1], maps->baseColor[2], maps->baseColor[3]}
		: linearColor(material.diffuse);
	const sgc::Colorf b = linearTinted(base, tint);
	cb.baseColor[0] = b.r;
	cb.baseColor[1] = b.g;
	cb.baseColor[2] = b.b;
	cb.baseColor[3] = b.a;
	cb.pbr[0] = std::clamp(material.metallic, 0.0f, 1.0f);
	cb.pbr[1] = std::clamp(material.roughness, 0.0f, 1.0f);
	const auto add = linearRgb(tint.add[0], tint.add[1], tint.add[2]);
	for (int i = 0; i < 3; ++i) { cb.tintAdd[i] = add[i]; }
	if (maps != nullptr)
	{
		for (int i = 0; i < 3; ++i) { cb.emissive[i] = maps->emissiveFactor[i]; }
		cb.mapFlags[0] = maps->normal ? 1.0f : 0.0f;
		cb.mapFlags[1] = maps->normalScale;
		cb.mapFlags[2] = maps->metallicRoughness ? 1.0f : 0.0f;
		cb.mapFlags[3] = maps->emissive ? 1.0f : 0.0f;
		cb.pbr[2] = maps->metallicRoughness ? maps->occlusionStrength : 0.0f;
	}
	auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief forward の描画 1 回ぶんの root 引数 (0〜8) を張る。PSO と VB/IB は呼び出し側
[[nodiscard]] bool bindForwardDraw(const sgc::Mat4f& world, const Material& material, const MaterialMaps* maps,
                                   const DrawTint& tint)
{
	const auto cbTransform = uploadTransformCB(world);
	const auto cbLighting = uploadLightingCB(material, tint);
	const auto cbDrawEx = uploadDrawExCB(material, maps, tint);
	const auto cbShadow = uploadShadowCB();
	if (cbTransform == 0 || cbLighting == 0 || cbDrawEx == 0) { return false; }
	ID3D12DescriptorHeap* heaps[] = {m_albedoSrvHeap.Get()};
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	const auto scene = ensureSceneTable();
	const auto table = writeMaterialTable(resolveAlbedo(material, maps), maps);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbTransform);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbLighting);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(2, cbDrawEx);
	if (cbShadow != 0) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(3, cbShadow); }
	if (table.ptr != 0) { m_graphicsCmdList->SetGraphicsRootDescriptorTable(4, table); }
	if (scene.ptr != 0) { m_graphicsCmdList->SetGraphicsRootDescriptorTable(5, scene); }
	bindFrameLighting();
	return true;
}
