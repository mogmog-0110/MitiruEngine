// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。焼いた光の GPU 側
//
// mitiru_lightbake が焼いた *.lighting.bin (render/gi/) を、放射照度のプローブの StructuredBuffer (t39)、プローブの距離の地図
// (t40)、反射のプローブの TextureCubeArray (t41) にして、前方の描画の場面の表へ載せる。格子と箱の数値は CbCluster の Gi* / Refl*。
// 読み込みは CPU で GPU の資源と転送元を作るだけで、転送の命令は次の beginFrame で最初の描画より前に積む。

public:

/// @brief 焼いた光をアセットのパスから読む。読めなければ false で、前に読んだものはそのまま残す
bool loadLightingBake(const char* path)
{
	const std::string p = (path != nullptr) ? path : "";
	std::string error;
	const auto bake = gi::readLightingBake(p, error);
	if (!bake)
	{
		debug::warnOnce("dx12.lightingBake.load." + p,
		                "焼いた光 " + p + " を読めないので、前の光のまま描きます (" + error + ")。パスを確かめるか、焼き直してください。");
		return false;
	}
	return setLightingBake(*bake);
}

/// @brief 焼いた光を差し替える。GPU へ写すのは次の beginFrame。資源を作れなければ false で、光は外れる
bool setLightingBake(const gi::LightingBake& bake)
{
	clearLightingBake();
	LightingBakeGpu next;
	const bool volumeOk = !bake.hasVolume || stageProbeVolume(bake.volume, next);
	const bool reflectionsOk = bake.reflections.empty() || stageReflectionProbes(bake.reflections, next);
	if (!volumeOk || !reflectionsOk)
	{
		debug::verboseOnce("dx12.lightingBake.stage", "焼いた光を置く GPU の資源を作れなかったので、間接光は使いません。");
		return false;
	}
	m_lightingBake = std::move(next);
	return true;
}

/// @brief 焼いた光を外す。GPU が読み終えてから資源を捨てる
void clearLightingBake()
{
	LightingBakeGpu& b = m_lightingBake;
	for (gfx::GpuResource* r : {&b.probes.buffer, &b.probes.staging, &b.visibility.texture, &b.visibility.staging,
	                            &b.reflections.texture, &b.reflections.staging})
	{
		if (*r) { m_frameTempResources.push_back(std::move(*r)); }
	}
	m_lightingBake = LightingBakeGpu{};
}

[[nodiscard]] bool hasLightingBake() const noexcept { return m_lightingBake.hasVolume || m_lightingBake.reflectionCount > 0; }
/// @brief 今載せている放射照度のプローブの数 (診断とテスト用)
[[nodiscard]] std::uint32_t lightingBakeProbeCount() const noexcept { return m_lightingBake.probeCount; }

/// @brief 焼いた光の使い方 (host の既定)。ゲームが SceneLook の indirect を頼んでいる間は、そちらが上に乗る。
///        焼いた光を読み込んでいなければ何も変わらない
void setGlobalIllumination(const GlobalIlluminationSettings& s) noexcept
{
	m_hostGi = s;
	refreshIndirectLighting();
}

[[nodiscard]] const GlobalIlluminationSettings& globalIllumination() const noexcept { return m_giSettings; }

/// @brief ゲーム DLL の SceneLook::indirect (ABI v51)。flags が 0 なら host の既定に戻る
void setIndirectLightLook(const IndirectLightLook& look) noexcept override
{
	m_indirectLook = look;
	refreshIndirectLighting();
}

/// @brief ゲーム DLL の lightingBake3D (ABI v51)。読み込みはワーカーで、読み終わるまで前の光のまま描く。
///        待つ読み込み (headless と撮影) では、ここで読み終えてから戻る
void requestLightingBake(const char* path) override
{
	const std::string_view p = (path != nullptr) ? path : "";
	if (p == m_bakeWanted) { return; }
	m_bakeWanted.assign(p);
	if (p.empty())
	{
		clearLightingBake();
		return;
	}
	if (applyWantedBake()) { return; }
	streamLightingBake(m_bakeWanted);
	if (!m_asyncLoads) { (void)m_streamer.settle(streamKey("lighting:", m_bakeWanted)); }
}

private:

/// @brief host の既定とゲームの頼みから、今使う GI と SSR を決め直す。SSR は設定画面の上限で切る
void refreshIndirectLighting() noexcept
{
	GlobalIlluminationSettings gi;
	ScreenSpaceReflectionSettings ssr;
	resolveIndirectLook(m_indirectLook, m_hostGi, m_hostSsr, gi, ssr);
	m_giSettings = gi;
	m_giSettings.intensity = std::max(gi.intensity, 0.0f);
	m_giSettings.toonBands = std::clamp(gi.toonBands, 0, 8);
	m_giSettings.normalBias = std::max(gi.normalBias, 0.0f);
	m_giSettings.reflectionIntensity = std::max(gi.reflectionIntensity, 0.0f);
	ssr.enabled = ssr.enabled && m_qualityCaps.screenSpaceReflections;
	applySsrSettings(ssr);
}

/// @brief 焼いた光の先読み (.lighting.bin)。読み込みを頼むか、読み込み済みなら true
bool preloadLightingBake(std::string_view path)
{
	if (m_bakeCache.find(path) == m_bakeCache.end()) { streamLightingBake(std::string(path)); }
	return true;
}

[[nodiscard]] bool lightingBakeReady(std::string_view path) const
{
	const auto it = m_bakeCache.find(path);
	return it != m_bakeCache.end() && it->second != nullptr;
}

/// @brief 先読みした光を手放す。今描いている光はそのまま (外すのは lightingBake3D(nullptr))
bool forgetLightingBake(std::string_view path)
{
	const auto it = m_bakeCache.find(path);
	if (it == m_bakeCache.end()) { return false; }
	m_bakeCache.erase(it);
	return true;
}

/// @brief 読み終えた光のうち、ゲームが今選んでいるものを GPU へ載せる。読めなかった時は前の光のまま
/// @return 読み込みを済ませていた (載せたか、読めなかったと分かっている) なら true
bool applyWantedBake()
{
	const auto it = m_bakeCache.find(m_bakeWanted);
	if (it == m_bakeCache.end()) { return false; }
	// フレームの途中で差し替えた時も、このフレームの残りの描画から新しい光を使う (前の資源はフレームの後に捨てる)
	if (it->second != nullptr && setLightingBake(*it->second) && m_cmdListOpen) { uploadLightingBakeIfPending(); }
	return true;
}

void streamLightingBake(const std::string& path)
{
	const std::string& key = streamKey("lighting:", path);
	if (m_streamer.isPending(key)) { return; }
	(void)m_streamer.request(key, [this, p = path]() -> resource::AssetFinish {
		auto error = std::make_shared<std::string>();
		auto bake = std::make_shared<std::optional<gi::LightingBake>>(gi::readLightingBake(p, *error));
		return [this, p, bake, error] { finishLightingBake(p, *bake, *error); };
	});
}

void finishLightingBake(const std::string& path, std::optional<gi::LightingBake>& bake, const std::string& error)
{
	if (!bake)
	{
		debug::warnOnce("dx12.lightingBake.load." + path,
		                "焼いた光 " + path + " を読めないので、前の光のまま描きます (" + error + ")。パスを確かめるか、焼き直してください。");
		++m_assetLoadFailures;
		m_bakeCache[path] = nullptr;
		return;
	}
	m_bakeCache[path] = std::make_shared<const gi::LightingBake>(std::move(*bake));
	if (path == m_bakeWanted) { (void)applyWantedBake(); }
}

static constexpr std::uint32_t kMaxReflectionProbes = 8;

/// @brief 全段 (subresource) を詰めた転送元と、DEFAULT heap の本体
struct PendingTexture
{
	gfx::GpuResource texture;
	gfx::GpuResource staging;
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
};

struct LightingBakeGpu
{
	bool hasVolume = false;
	gi::ProbeGridDesc grid;
	std::uint32_t probeCount = 0;
	std::uint32_t tilesPerRow = 1;
	std::uint32_t atlasWidth = 1;
	std::uint32_t atlasHeight = 1;
	dx12::StagedBuffer probes;      ///< float4 x 7 / プローブ
	PendingTexture visibility;      ///< R32G32_FLOAT の距離の地図
	PendingTexture reflections;     ///< R16G16B16A16_FLOAT の TextureCubeArray
	std::uint32_t reflectionCount = 0;
	std::uint32_t reflectionMips = 0;
	float boxMin[kMaxReflectionProbes][4]{};
	float boxMax[kMaxReflectionProbes][4]{};
	float position[kMaxReflectionProbes][4]{};
	bool uploaded = false;
};

GlobalIlluminationSettings m_giSettings;      ///< 今使う設定 (host の既定 + ゲームの頼み)
GlobalIlluminationSettings m_hostGi;          ///< host の既定 (setGlobalIllumination)
IndirectLightLook          m_indirectLook{};  ///< ゲームの頼み (flags 0 = 頼んでいない)
LightingBakeGpu            m_lightingBake;
std::string                m_bakeWanted;      ///< ゲームが lightingBake3D で選んだ path (空 = 選んでいない)
/// 読み終えた焼いた光 (CPU 側)。null は読めなかったもの。先読みと lightingBake3D の両方が使う
std::map<std::string, std::shared_ptr<const gi::LightingBake>, std::less<>> m_bakeCache;

/// @brief data (subresource の順に、行の隙間なく並べた画素) から転送元と本体を作る
[[nodiscard]] bool stageTexture(const D3D12_RESOURCE_DESC& desc, const std::uint8_t* data, std::size_t bytes, PendingTexture& out)
{
	const UINT subs = desc.MipLevels * desc.DepthOrArraySize;
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subs);
	std::vector<UINT> rows(subs);
	std::vector<UINT64> rowBytes(subs);
	UINT64 total = 0;
	m_d3dDevice->GetCopyableFootprints(&desc, 0, subs, 0, layouts.data(), rows.data(), rowBytes.data(), &total);
	std::size_t need = 0;
	for (UINT i = 0; i < subs; ++i) { need += static_cast<std::size_t>(rowBytes[i]) * rows[i]; }
	if (need != bytes ||
	    FAILED(gfx::createGpuResource(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, out.texture)) ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ, out.staging)))
	{
		return false;
	}
	std::uint8_t* mapped = nullptr;
	const D3D12_RANGE noRead = {0, 0};
	if (FAILED(out.staging->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))) { return false; }
	std::size_t cursor = 0;
	for (UINT i = 0; i < subs; ++i)
	{
		for (UINT r = 0; r < rows[i]; ++r)
		{
			std::memcpy(mapped + layouts[i].Offset + static_cast<UINT64>(r) * layouts[i].Footprint.RowPitch, data + cursor,
			            static_cast<std::size_t>(rowBytes[i]));
			cursor += static_cast<std::size_t>(rowBytes[i]);
		}
	}
	out.staging->Unmap(0, nullptr);
	out.layouts = std::move(layouts);
	return true;
}

[[nodiscard]] bool stageProbeVolume(const gi::ProbeVolume& vol, LightingBakeGpu& out)
{
	const std::uint32_t n = vol.grid.count();
	std::vector<float> packed(static_cast<std::size_t>(n) * gi::kProbeFloat4s * 4, 0.0f);
	for (std::uint32_t i = 0; i < n; ++i)
	{
		float* dst = packed.data() + static_cast<std::size_t>(i) * gi::kProbeFloat4s * 4;
		for (int k = 0; k < gi::kShCoefficients; ++k)
		{
			dst[k * 3 + 0] = vol.irradiance[i].c[k].x;
			dst[k * 3 + 1] = vol.irradiance[i].c[k].y;
			dst[k * 3 + 2] = vol.irradiance[i].c[k].z;
		}
		dst[27] = vol.valid[i] != 0 ? 1.0f : 0.0f;
	}
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = vol.atlasWidth();
	d.Height = vol.atlasHeight();
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = DXGI_FORMAT_R32G32_FLOAT;
	d.SampleDesc.Count = 1;
	if (!dx12::stageBuffer(m_d3dDevice, packed.data(), packed.size() * sizeof(float), out.probes) ||
	    !stageTexture(d, reinterpret_cast<const std::uint8_t*>(vol.visibility.data()), vol.visibility.size() * sizeof(float),
	                  out.visibility))
	{
		return false;
	}
	out.hasVolume = true;
	out.grid = vol.grid;
	out.probeCount = n;
	out.tilesPerRow = vol.tilesPerRow;
	out.atlasWidth = vol.atlasWidth();
	out.atlasHeight = vol.atlasHeight();
	return true;
}

/// @brief 反射のプローブを 1 枚の TextureCubeArray にする。大きさと段数が最初のものと違うプローブは使わない
[[nodiscard]] bool stageReflectionProbes(const std::vector<gi::ReflectionProbeData>& probes, LightingBakeGpu& out)
{
	const gi::ReflectionProbeData& first = probes.front();
	std::vector<std::uint8_t> texels;
	std::uint32_t count = 0;
	for (const gi::ReflectionProbeData& p : probes)
	{
		if (count == kMaxReflectionProbes || p.size != first.size || p.mips != first.mips)
		{
			debug::warnOnce("dx12.lightingBake.reflections",
			                "反射のプローブは大きさと段数が同じものを 8 個までしか使えないので、残りは使いません。焼くときにプローブの数と大きさをそろえてください。");
			continue;
		}
		const auto* raw = reinterpret_cast<const std::uint8_t*>(p.texels.data());
		texels.insert(texels.end(), raw, raw + p.texels.size() * sizeof(std::uint16_t));
		for (int a = 0; a < 3; ++a)
		{
			out.boxMin[count][a] = (&p.boxMin.x)[a];
			out.boxMax[count][a] = (&p.boxMax.x)[a];
			out.position[count][a] = (&p.position.x)[a];
		}
		out.boxMin[count][3] = p.blend;
		out.position[count][3] = static_cast<float>(count);
		++count;
	}
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = first.size;
	d.Height = first.size;
	d.DepthOrArraySize = static_cast<UINT16>(count * 6);
	d.MipLevels = static_cast<UINT16>(first.mips);
	d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	d.SampleDesc.Count = 1;
	if (count == 0 || !stageTexture(d, texels.data(), texels.size(), out.reflections)) { return false; }
	out.reflectionCount = count;
	out.reflectionMips = first.mips;
	return true;
}

void recordPendingCopies(const PendingTexture& t)
{
	for (UINT i = 0; i < static_cast<UINT>(t.layouts.size()); ++i)
	{
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = t.texture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = i;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = t.staging.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = t.layouts[i];
		m_graphicsCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}
	dx12::detail::recordTransition(m_graphicsCmdList.Get(), t.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
	                               D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

/// @brief beginFrame で最初の描画より前に呼ぶ。読み込んだ焼いた光を GPU へ写す (1 回だけ)
void uploadLightingBakeIfPending()
{
	LightingBakeGpu& b = m_lightingBake;
	if (b.uploaded || (!b.hasVolume && b.reflectionCount == 0)) { return; }
	if (b.hasVolume)
	{
		dx12::recordStagedCopies(m_graphicsCmdList.Get(), b.probes);
		dx12::detail::recordTransition(m_graphicsCmdList.Get(), b.probes.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		                               D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		recordPendingCopies(b.visibility);
		m_frameTempResources.push_back(std::move(b.probes.staging));
		m_frameTempResources.push_back(std::move(b.visibility.staging));
	}
	if (b.reflectionCount > 0)
	{
		recordPendingCopies(b.reflections);
		m_frameTempResources.push_back(std::move(b.reflections.staging));
	}
	b.uploaded = true;
}

/// @brief 場面の表の t39..t43 を書く。無いものは null
void writeIndirectSrvs(D3D12_CPU_DESCRIPTOR_HANDLE cpu) const
{
	const LightingBakeGpu& b = m_lightingBake;
	const bool volume = b.hasVolume && b.uploaded;
	D3D12_SHADER_RESOURCE_VIEW_DESC buf = {};
	buf.Format = DXGI_FORMAT_UNKNOWN;
	buf.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	buf.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	buf.Buffer.NumElements = volume ? b.probeCount * gi::kProbeFloat4s : 1;
	buf.Buffer.StructureByteStride = 16;
	m_d3dDevice->CreateShaderResourceView(volume ? b.probes.buffer.Get() : nullptr, &buf, cpu);
	cpu.ptr += m_albedoSrvIncrement;
	D3D12_SHADER_RESOURCE_VIEW_DESC vis = {};
	vis.Format = DXGI_FORMAT_R32G32_FLOAT;
	vis.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	vis.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	vis.Texture2D.MipLevels = 1;
	m_d3dDevice->CreateShaderResourceView(volume ? b.visibility.texture.Get() : nullptr, &vis, cpu);
	cpu.ptr += m_albedoSrvIncrement;
	const bool refl = b.reflectionCount > 0 && b.uploaded;
	D3D12_SHADER_RESOURCE_VIEW_DESC cube = {};
	cube.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	cube.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
	cube.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	cube.TextureCubeArray.MipLevels = refl ? b.reflectionMips : 1;
	cube.TextureCubeArray.NumCubes = refl ? b.reflectionCount : 1;
	m_d3dDevice->CreateShaderResourceView(refl ? b.reflections.texture.Get() : nullptr, &cube, cpu);
	cpu.ptr += m_albedoSrvIncrement;
	writeSsrSrvs(cpu);
}

/// @brief CbCluster の Gi* / Refl* を埋める
void fillIndirectLightingCB(DX12CbCluster& cb) const
{
	const LightingBakeGpu& b = m_lightingBake;
	const bool gi = b.hasVolume && b.uploaded && m_giSettings.enabled;
	const gi::ProbeGridDesc& g = b.grid;
	cb.giOrigin[0] = g.origin.x;
	cb.giOrigin[1] = g.origin.y;
	cb.giOrigin[2] = g.origin.z;
	cb.giOrigin[3] = gi ? 1.0f : 0.0f;
	cb.giSpacing[0] = g.spacing.x;
	cb.giSpacing[1] = g.spacing.y;
	cb.giSpacing[2] = g.spacing.z;
	cb.giSpacing[3] = m_giSettings.intensity;
	for (int a = 0; a < 3; ++a) { cb.giDims[a] = g.dims[a]; }
	cb.giDims[3] = b.tilesPerRow;
	cb.giParams[0] = m_giSettings.normalBias > 0.0f ? m_giSettings.normalBias : gi::defaultProbeBias(g);
	cb.giParams[1] = static_cast<float>(m_giSettings.toonBands);
	cb.giParams[2] = 1.0f / static_cast<float>(b.atlasWidth);
	cb.giParams[3] = 1.0f / static_cast<float>(b.atlasHeight);
	const bool refl = b.uploaded && m_giSettings.reflectionProbes;
	cb.reflParams[0] = refl ? static_cast<float>(b.reflectionCount) : 0.0f;
	cb.reflParams[1] = static_cast<float>(b.reflectionMips > 0 ? b.reflectionMips - 1 : 0);
	cb.reflParams[2] = m_giSettings.reflectionIntensity;
	std::memcpy(cb.reflBoxMin, b.boxMin, sizeof(cb.reflBoxMin));
	std::memcpy(cb.reflBoxMax, b.boxMax, sizeof(cb.reflBoxMax));
	std::memcpy(cb.reflPos, b.position, sizeof(cb.reflPos));
}
