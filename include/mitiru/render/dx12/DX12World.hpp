// Renderer3D_DX12 の class body 内 include チャンク (Renderer3D_DX12.hpp private セクションから include)
//
// 屋外の 1 枚 (world.json: 地形・草・撒いた物・水面)。drawOutdoor に world.json を渡すと、地形・草・撒いた物は
// 呼ばれたその場で不透明パスへ描き、水面は不透明と空が出そろった後 (renderWaterPass) に描く。
// 副ビューの間に渡せば、そのビューのカメラで区画を選んで描き、水面もそのビューの仕上げで描く。
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
	std::uint64_t gpuBytes = 0;                        ///< 画像の大きさ (予算の集計)
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

std::unordered_map<std::string, std::unique_ptr<OutdoorWorldGpu>, util::TransparentStringHash, std::equal_to<>> m_outdoorWorlds;
struct QueuedWater
{
	const OutdoorWorldGpu* gpu = nullptr;
	OutdoorDrawPod pod;
	int pass = 0;   ///< 渡したビュー (0 = 主ビュー)
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
/// region.json を渡すと、目に近い区画の world.json だけを読んで描き、遠くなった区画は手放す (DX12WorldRegion.hpp)
void drawOutdoor(const char* path, const OutdoorDrawPod& pod) override
{
	if (path == nullptr || !m_initialized || !m_graphicsCmdList) { return; }
	if (terrain::isOutdoorRegionPath(path)) { drawOutdoorRegion(path, pod); return; }
	if (OutdoorWorldGpu* gpu = ensureOutdoorWorld(path)) { drawOutdoorWorld(*gpu, pod); }
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

void drawOutdoorWorld(OutdoorWorldGpu& gpu, const OutdoorDrawPod& pod)
{
	if (!ensureWorldPipeline()) { return; }
	drawSkyboxBeforeFirstDraw();
	beginOutdoorFrame();
	if ((pod.flags & kOutdoorNoScatter) == 0) { drawOutdoorScatter(gpu); }
	drawOutdoorTerrain(gpu, pod);
	if ((pod.flags & kOutdoorNoGrass) == 0) { drawOutdoorGrass(gpu, pod); }
	restoreMainState();
	recordTerrainShadowCasters(gpu);
	if (!gpu.world->water.empty() && (pod.flags & kOutdoorNoWater) == 0) { m_waterQueue.push_back({&gpu, pod, currentPass()}); }
}

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

/// @brief 読み込み済みの world.json。初めてなら AssetStreamer に頼み、読み終わるまで (と読めなかったとき) は nullptr
[[nodiscard]] OutdoorWorldGpu* ensureOutdoorWorld(const char* path)
{
	if (const auto it = m_outdoorWorlds.find(std::string_view(path)); it != m_outdoorWorlds.end() && it->second)
	{
		return it->second->failed ? nullptr : it->second.get();
	}
	requestOutdoorWorld(path);
	if (!m_asyncLoads) { (void)m_streamer.settle(streamKey("world:", path)); }
	return findOutdoorWorld(path);
}

[[nodiscard]] OutdoorWorldGpu* findOutdoorWorld(std::string_view path)
{
	const auto it = m_outdoorWorlds.find(path);
	return (it != m_outdoorWorlds.end() && it->second && !it->second->failed) ? it->second.get() : nullptr;
}

/// @brief 読み込みを頼むだけで待たない。読み込み済みか読み込み中なら何もしない
void requestOutdoorWorld(const char* path)
{
	if (m_outdoorWorlds.find(std::string_view(path)) != m_outdoorWorlds.end()) { return; }
	const std::string& key = streamKey("world:", path);
	if (!m_streamer.isPending(key))
	{
		ID3D12Device* device = m_d3dDevice;
		dx12::Dx12CopyQueue* copy = streamingCopyQueue();
		(void)m_streamer.request(key, [this, device, copy, p = std::string(path)]() -> resource::AssetFinish {
			auto prepared = std::make_shared<dx12::PreparedOutdoorWorld>(dx12::prepareOutdoorWorld(p, device, copy));
			return [this, p, prepared] { finishOutdoorWorld(p, *prepared); };
		});
	}
}

/// @brief 読み込みの後半。画像を読む状態へ移して登録する。読めなければ 1 度だけ知らせ、以後は何もしない
void finishOutdoorWorld(const std::string& path, dx12::PreparedOutdoorWorld& p)
{
	auto& slot = m_outdoorWorlds[path];
	slot = std::make_unique<OutdoorWorldGpu>();
	if (!p.world)
	{
		slot->failed = true;
		++m_assetLoadFailures;
		debug::warnOnce("dx12.world.load." + path, "world.json を読めない: " + p.error);
		return;
	}
	slot->world = std::move(p.world);
	adoptWorldTexture(p.copied, p.height, slot->height);
	for (std::size_t k = 0; k < 2; ++k) { adoptWorldTexture(p.copied, p.splat[k], slot->splat[k]); }
	adoptWorldTexture(p.copied, p.density, slot->density);
	for (std::size_t k = 0; k < terrain::kMaxTerrainLayers; ++k)
	{
		adoptWorldTexture(p.copied, p.albedo[k], slot->albedo[k]);
		adoptWorldTexture(p.copied, p.normal[k], slot->normal[k]);
	}
	slot->shadowChunks = std::move(p.shadowChunks);
	slot->shadowChunkWorld = std::move(p.shadowChunkWorld);
	slot->scatter = std::move(p.scatter);
	slot->gpuBytes = p.gpuBytes;
	noteAssetBytes("world:", path, p.gpuBytes);
}

/// @brief 地形の画像は頂点シェーダー (高さ) とピクセルシェーダーの両方が読む
void adoptWorldTexture(bool copied, dx12::StagedTexture& staged, dx12::Dx12Texture2D& out)
{
	if (!staged.valid()) { return; }
	auto holder = std::make_shared<dx12::StagedTexture>(std::move(staged));
	onGraphicsList([this, copied, holder](ID3D12GraphicsCommandList* list) {
		adoptStagedResource(list, copied, *holder, holder->texture.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	});
	dx12::StagedTexture view;
	view.texture = holder->texture;
	view.layouts = holder->layouts;
	view.width = holder->width;
	view.height = holder->height;
	view.format = holder->format;
	out.adoptStaged(std::move(view));
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
		(void)addShadowCaster({&mesh, g.shadowChunkWorld[i], 0, 0, worldOcclusionAABB(mesh.localAABB(), g.shadowChunkWorld[i])});
	}
}

// NOLINTNEXTLINE(google-build-namespaces) intentional in-class .inl include
#include <mitiru/render/dx12/DX12WorldSetup.hpp> // NOLINT(build/include)
// NOLINTNEXTLINE(google-build-namespaces)
#include <mitiru/render/dx12/DX12WorldDraw.hpp> // NOLINT(build/include)
// NOLINTNEXTLINE(google-build-namespaces)
#include <mitiru/render/dx12/DX12WorldRegion.hpp> // NOLINT(build/include)
