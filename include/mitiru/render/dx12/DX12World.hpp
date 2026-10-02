// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// 屋外の 1 枚 (world.json: 地形・草・撒いた物・水面)。drawOutdoor に world.json を渡すと、地形・草・撒いた物は
// 呼ばれたその場で不透明パスへ描き、水面は不透明と空が出そろった後 (renderWaterPass) に描く。
// ルートシグネチャはメインの 0〜8 をそのまま持つ別物 (DX12WorldShaders.hpp の説明)。メインの bindForwardDraw が
// 張る引数の番号がそのまま通るので、影・局所光・IBL・フォグの束縛はメインと同じ関数で済む。
// 当たり判定と同じ OutdoorWorld を読むので、見た目の地形とゲームが歩く地形は同じファイルから来る。

struct OutdoorWorldGpu
{
	std::unique_ptr<terrain::OutdoorWorld> world;
	bool failed = false;
	dx12::Dx12Texture2D height;
	dx12::Dx12Texture2D splat[2];
	dx12::Dx12Texture2D density;
	dx12::Dx12Texture2D albedo[terrain::kMaxTerrainLayers];
	dx12::Dx12Texture2D normal[terrain::kMaxTerrainLayers];
	std::vector<std::unique_ptr<Mesh>> shadowChunks;   ///< 影を落とすための地形の粗い三角形 (チャンクごと)
	std::vector<sgc::Mat4f> shadowChunkWorld;
	std::vector<std::vector<MeshInstance>> scatter;    ///< ScatterSet ごとの行列
};

/// @brief 直前に描いた屋外の数。テストと計測に使う
struct OutdoorDrawStats
{
	int           terrainPatches = 0;
	int           grassPatches = 0;
	std::uint64_t grassBlades = 0;
	int           scatterInstances = 0;   ///< 渡した数 (カリングで落とした物も含む)
	int           waterBodies = 0;
};

/// @brief 屋外パスの GPU 時間 (ms)。FRAME_COUNT フレーム遅れで計測する
struct OutdoorGpuTimes
{
	double terrainMs = 0.0;
	double grassMs = 0.0;
	double waterMs = 0.0;
};

/// @brief ルート引数の番号。0〜8 はメインと同じ
enum WorldRootParam : UINT { kWorldTable = 9, kWorldPatches = 10, kWorldDraw = 11, kWorldCb = 12, kWorldRootCount = 13 };
/// @brief 表 t12〜t33 の並び
enum WorldSlot : UINT
{
	kSlotHeight = 0, kSlotSplat0 = 1, kSlotSplat1 = 2, kSlotAlbedo = 3, kSlotNormal = 11, kSlotDensity = 19,
	kSlotSceneColor = 20, kSlotSceneDepth = 21, kWorldTableSize = 22,
};
enum WorldTimerPass : std::uint32_t { kWorldTimerTerrain = 0, kWorldTimerGrass = 1, kWorldTimerWater = 2 };

struct alignas(256) CbWorldDx12
{
	float terrainOrigin[4]{};
	float terrainSize[4]{};
	float terrainGrid[4]{};
	float terrainPatch[4]{};
	float layerTile[8]{};
	float layerColor[8][4]{};
	float layerPbr[8][4]{};
	float grassShape[4]{};
	float grassBase[4]{};
	float grassTip[4]{};
	float grassWind[4]{};
	float waterRect[4]{};
	float waterShallow[4]{};
	float waterDeep[4]{};
	float waterAbsorb[4]{};
	float waterFoam[4]{};
	float waterWave[4]{};
	float screenSize[4]{};
	float invViewProj[4][4]{};
	float grassBenders[kMaxOutdoorBenders][4]{};
	float grassBenderCount[4]{};
};

std::unordered_map<std::string, std::unique_ptr<OutdoorWorldGpu>> m_outdoorWorlds;
struct QueuedWater
{
	const OutdoorWorldGpu* gpu = nullptr;
	OutdoorDrawPod pod;
};
std::vector<QueuedWater> m_waterQueue;

ComPtr<ID3D12RootSignature>    m_worldRootSig;
std::optional<gfx::Dx12Shader> m_terrainVS;
std::optional<gfx::Dx12Shader> m_grassVS;
std::optional<gfx::Dx12Shader> m_waterVS;
std::optional<gfx::Dx12Shader> m_waterPS;
std::optional<gfx::Dx12Shader> m_terrainPS[3];
ComPtr<ID3D12PipelineState>    m_terrainPso[3];
ComPtr<ID3D12PipelineState>    m_grassPso[3];
ComPtr<ID3D12PipelineState>    m_waterPso;
ComPtr<ID3D12PipelineState>    m_waterDepthPso;
bool                           m_worldPipelineTried = false;
Mesh                           m_terrainPatchMesh;
Mesh                           m_grassBladeMesh;
gfx::GpuResource               m_waterColorCopy;
UINT                           m_waterCopyWidth = 0;
UINT                           m_waterCopyHeight = 0;
ComPtr<ID3D12DescriptorHeap>   m_waterDsvHeap;
dx12::Dx12GpuTimer             m_worldTimer;
bool                           m_worldTimerTried = false;
std::uint64_t                  m_worldTimerFrame = 0;
OutdoorDrawStats               m_outdoorStats;
std::uint64_t                  m_outdoorStatsFrame = 0;
std::vector<terrain::LodPatch>   m_lodScratch;
std::vector<terrain::GrassPatch> m_grassScratch;
std::vector<float>               m_patchGpuScratch;

public:

/// @brief world.json を描く。配置は world.json と Blender のレベルで決まり、当たり判定と同じ座標になる
/// pod は風・水位・草を押し倒す球など、描画だけに効く毎フレームの指定
void drawOutdoor(const char* path, const OutdoorDrawPod& pod) override
{
	if (path == nullptr || !m_initialized || !m_graphicsCmdList || rejectInView("drawOutdoor")) { return; }
	OutdoorWorldGpu* gpu = ensureOutdoorWorld(path);
	if (gpu == nullptr || !ensureWorldPipeline()) { return; }
	drawSkyboxBeforeFirstDraw();
	beginOutdoorFrame();
	if ((pod.flags & kOutdoorNoScatter) == 0) { drawOutdoorScatter(*gpu); }
	drawOutdoorTerrain(*gpu, pod);
	if ((pod.flags & kOutdoorNoGrass) == 0) { drawOutdoorGrass(*gpu, pod); }
	restoreMainState();
	recordTerrainShadowCasters(*gpu);
	if (!gpu->world->water.empty() && (pod.flags & kOutdoorNoWater) == 0) { m_waterQueue.push_back({gpu, pod}); }
}

/// @brief 読み込み済みの world.json。読み込めていなければ nullptr
[[nodiscard]] const terrain::OutdoorWorld* outdoorWorld(const char* path) const
{
	const auto it = m_outdoorWorlds.find(path != nullptr ? path : "");
	return (it != m_outdoorWorlds.end() && it->second->world) ? it->second->world.get() : nullptr;
}

[[nodiscard]] const OutdoorDrawStats& outdoorDrawStats() const noexcept { return m_outdoorStats; }

[[nodiscard]] OutdoorGpuTimes outdoorGpuTimes() const noexcept
{
	return {m_worldTimer.milliseconds(kWorldTimerTerrain), m_worldTimer.milliseconds(kWorldTimerGrass),
	        m_worldTimer.milliseconds(kWorldTimerWater)};
}

private:

/// @brief world.json を drawModel に渡されたら 1 度だけ知らせて描かない (モデルとして読むと読み込みの失敗が続く)
[[nodiscard]] static bool rejectOutdoorPath(const char* path)
{
	if (path == nullptr || !terrain::isOutdoorWorldPath(path)) { return false; }
	debug::warnOnce(std::string("dx12.outdoor.drawModel.") + path,
	                std::string("world.json は drawOutdoor で描く (drawModel には渡さない): ") + path);
	return true;
}

/// @brief そのフレームで最初の屋外を描くときに、数と GPU 時間のスロットを新しくする
void beginOutdoorFrame()
{
	if (m_outdoorStatsFrame == m_frameCounter) { return; }
	m_outdoorStatsFrame = m_frameCounter;
	m_outdoorStats = {};
	if (!m_worldTimerTried && m_device != nullptr)
	{
		m_worldTimerTried = true;
		(void)m_worldTimer.init(m_d3dDevice, m_device->commandQueue(), FRAME_COUNT);
	}
	if (m_worldTimerFrame != m_frameCounter)
	{
		m_worldTimerFrame = m_frameCounter;
		m_worldTimer.beginFrame(m_frameCursor);
	}
}

/// @brief 初回に読み込んで GPU へ送る。読み込めなければ 1 度だけ知らせ、以後は何もしない
[[nodiscard]] OutdoorWorldGpu* ensureOutdoorWorld(const char* path)
{
	auto& slot = m_outdoorWorlds[path];
	if (slot) { return slot->failed ? nullptr : slot.get(); }
	slot = std::make_unique<OutdoorWorldGpu>();
	auto result = terrain::loadOutdoorWorld(path);
	if (!result.world)
	{
		slot->failed = true;
		debug::warnOnce(std::string("dx12.world.load.") + path, "world.json を読めない: " + result.error);
		return nullptr;
	}
	for (const auto& w : result.world->warnings) { debug::warnOnce(std::string("dx12.world.warn.") + path + w, w); }
	slot->world = std::move(result.world);
	uploadOutdoorWorld(*slot);
	buildTerrainShadowChunks(*slot);
	for (const auto& set : slot->world->scatter)
	{
		std::vector<MeshInstance> inst(set.instances.size());
		for (std::size_t i = 0; i < inst.size(); ++i) { std::memcpy(inst[i].world, set.instances[i].world, sizeof(inst[i].world)); }
		slot->scatter.push_back(std::move(inst));
	}
	return slot.get();
}

/// @brief 1 段の生画像 (R16、R8、RGBA8)を GPU へ送る
bool uploadRawTexture(dx12::Dx12Texture2D& out, DXGI_FORMAT format, std::uint32_t w, std::uint32_t h,
                      const std::uint8_t* data, std::size_t bytes, bool mips)
{
	MipImage img;
	img.format = static_cast<TextureFormat>(format);
	img.width = w;
	img.height = h;
	if (mips) { img.mips = buildMipChain(data, static_cast<int>(w), static_cast<int>(h), MipFilter::Linear); }
	else { img.mips.emplace_back(data, data + bytes); }
	return out.uploadMip(m_d3dDevice, m_graphicsCmdList.Get(), img, m_frameTempResources);
}

/// @brief 層の画像を読み込む。辺が 4 の倍数なら BC 圧縮し、隣に DDS を置く
void uploadLayerTexture(dx12::Dx12Texture2D& out, const std::string& world, const std::string& image, std::size_t layer,
                        TextureKind kind)
{
	if (image.empty()) { return; }
	CpuTexture cpu;
	if (const auto bytes = vfs::readAsset(image))
	{
		int w = 0, h = 0, c = 0;
		if (stbi_uc* px = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &c, 4))
		{
			cpu.width = w;
			cpu.height = h;
			cpu.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
			stbi_image_free(px);
		}
	}
	if (!cpu.valid())
	{
		debug::warnOnce("dx12.world.layer." + image, "地形の層の画像を読めない: " + image);
		return;
	}
	const auto sidecar = materialTextureSidecar(world, layer, kind == TextureKind::Normal ? "normal" : "base", image);
	const MipImage img = prepareMaterialTexture(cpu, kind, &sidecar);
	(void)out.uploadMip(m_d3dDevice, m_graphicsCmdList.Get(), img, m_frameTempResources);
}

void uploadOutdoorWorld(OutdoorWorldGpu& g)
{
	const terrain::OutdoorWorld& w = *g.world;
	const auto& s = w.terrain.samples();
	(void)uploadRawTexture(g.height, DXGI_FORMAT_R16_UNORM, w.terrain.width(), w.terrain.depth(),
	                       reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 2, false);
	for (std::size_t k = 0; k < 2; ++k)
	{
		const auto& img = w.splat[k];
		if (img.valid()) { (void)uploadRawTexture(g.splat[k], DXGI_FORMAT_R8G8B8A8_UNORM, img.width, img.height, img.rgba.data(), img.rgba.size(), true); }
	}
	const std::uint8_t one = 255;
	const auto& d = w.grassDensity;
	if (d.valid()) { (void)uploadRawTexture(g.density, DXGI_FORMAT_R8_UNORM, d.width, d.height, d.values.data(), d.values.size(), false); }
	else { (void)uploadRawTexture(g.density, DXGI_FORMAT_R8_UNORM, 1, 1, &one, 1, false); }
	for (std::size_t k = 0; k < w.layers.size(); ++k)
	{
		uploadLayerTexture(g.albedo[k], w.path, w.layers[k].albedo, k, TextureKind::Color);
		uploadLayerTexture(g.normal[k], w.path, w.layers[k].normal, k, TextureKind::Normal);
	}
}

/// @brief 影のパス用に、地形を 64 マス角のチャンクの三角形にする。512 標本を超える辺は間引く
void buildTerrainShadowChunks(OutdoorWorldGpu& g)
{
	const terrain::Heightfield& h = g.world->terrain;
	const std::uint32_t stride = std::max(1u, (std::max(h.width(), h.depth()) + 511u) / 512u);
	const std::uint32_t chunk = 64u * stride;
	for (std::uint32_t z0 = 0; z0 + 1 < h.depth(); z0 += chunk)
	{
		for (std::uint32_t x0 = 0; x0 + 1 < h.width(); x0 += chunk)
		{
			const std::uint32_t x1 = std::min(x0 + chunk, h.width() - 1);
			const std::uint32_t z1 = std::min(z0 + chunk, h.depth() - 1);
			const sgc::Vec3f base = h.vertex(x0, z0);
			std::vector<Vertex3D> verts;
			for (std::uint32_t iz = z0; iz <= z1; iz = (iz == z1) ? z1 + 1 : std::min(iz + stride, z1))
			{
				for (std::uint32_t ix = x0; ix <= x1; ix = (ix == x1) ? x1 + 1 : std::min(ix + stride, x1))
				{
					const sgc::Vec3f p = h.vertex(ix, iz);
					verts.emplace_back(sgc::Vec3f{p.x - base.x, p.y, p.z - base.z}, sgc::Vec3f{0, 1, 0});
				}
			}
			const std::uint32_t cols = (x1 - x0 + stride - 1) / stride + 1;
			const auto rows = static_cast<std::uint32_t>(verts.size() / cols);
			std::vector<std::uint32_t> idx;
			for (std::uint32_t r = 0; r + 1 < rows; ++r)
			{
				for (std::uint32_t c = 0; c + 1 < cols; ++c)
				{
					const std::uint32_t a = r * cols + c, b = a + 1, cc = a + cols, d = cc + 1;
					idx.insert(idx.end(), {a, cc, b, b, cc, d});
				}
			}
			auto mesh = std::make_unique<Mesh>();
			mesh->setVertices(std::move(verts));
			mesh->setIndices(std::move(idx));
			g.shadowChunks.push_back(std::move(mesh));
			g.shadowChunkWorld.push_back(sgc::Mat4f::translation(sgc::Vec3f{base.x, 0.0f, base.z}));
		}
	}
}

/// @brief 地形のチャンクを影の caster に積む。影のパスは次のフレームにキャッシュした VB で描くため、ここで GPU へ送る
void recordTerrainShadowCasters(OutdoorWorldGpu& g)
{
	if (!wantsShadowCasters()) { return; }
	for (std::size_t i = 0; i < g.shadowChunks.size(); ++i)
	{
		const Mesh& mesh = *g.shadowChunks[i];
		const auto& v = mesh.vertices();
		const auto& ix = mesh.indices();
		if (acquireMeshBuffer(m_meshVBCache, mesh, v.data(), static_cast<UINT>(v.size() * sizeof(Vertex3D))) == nullptr ||
		    acquireMeshBuffer(m_meshIBCache, mesh, ix.data(), static_cast<UINT>(ix.size() * sizeof(std::uint32_t))) == nullptr)
		{
			continue;
		}
		m_shadowCommands.push_back({&mesh, g.shadowChunkWorld[i], 0, 0});
	}
}

// NOLINTNEXTLINE(google-build-namespaces) intentional in-class .inl include
#include <mitiru/render/dx12/DX12WorldSetup.hpp> // NOLINT(build/include)
// NOLINTNEXTLINE(google-build-namespaces)
#include <mitiru/render/dx12/DX12WorldDraw.hpp> // NOLINT(build/include)
