// Renderer3D_DX12 の class body 内 include チャンク (DX12World.hpp の末尾から include)。区画に分けた屋外 (region.json)
//
// 区画は 1 つずつ普通の world.json で、置き場所はそれぞれのファイルが持つ。フレームで最初に region を描くときに主ビューの目の
// 位置で区画を選び直し (terrain::updateRegionResidency)、近づいた区画は AssetStreamer に頼み、離れた区画は手放す。
// 手放した区画は GPU が読み終えるまで墓場に置いてから消す (影のパスが前フレームの caster を次のフレームに描くため)。
// 資産の大きさの合計が予算 (setStreamingBudgetBytes) を越えている間は、新しい区画を読まない。

struct OutdoorRegionGpu
{
	terrain::OutdoorRegion    region;
	std::vector<std::uint8_t> resident;
	std::uint64_t             residencyFrame = ~0ull;
	bool                      failed = false;
};

struct RetiredOutdoorWorld
{
	std::unique_ptr<OutdoorWorldGpu> gpu;
	std::uint64_t                    frame = 0;
};

std::unordered_map<std::string, std::unique_ptr<OutdoorRegionGpu>, util::TransparentStringHash, std::equal_to<>> m_outdoorRegions;
std::vector<RetiredOutdoorWorld> m_outdoorGraveyard;
std::uint64_t                    m_regionBudgetSkips = 0;

public:

/// @brief 読み込んだ region の区画の数。テストと予算の表示に使う
struct OutdoorRegionStats
{
	int cells = 0;
	int resident = 0;   ///< 目に近く、読む (読んだ) 区画
	int loaded = 0;     ///< 描ける区画
};

[[nodiscard]] OutdoorRegionStats outdoorRegionStats() const
{
	OutdoorRegionStats s;
	for (const auto& [path, rg] : m_outdoorRegions)
	{
		if (!rg) { continue; }
		s.cells += static_cast<int>(rg->region.cells.size());
		for (std::size_t i = 0; i < rg->resident.size(); ++i)
		{
			s.resident += rg->resident[i];
			const auto it = m_outdoorWorlds.find(rg->region.cells[i].world);
			s.loaded += (it != m_outdoorWorlds.end() && it->second && !it->second->failed) ? 1 : 0;
		}
	}
	return s;
}

/// @brief 予算を越えて読まなかった区画の延べ数
[[nodiscard]] std::uint64_t regionBudgetSkips() const noexcept { return m_regionBudgetSkips; }

/// @brief GPU が読み終えるのを待っている、手放した world.json の数 (診断とテスト用)
[[nodiscard]] int retiringOutdoorWorldCount() const noexcept { return static_cast<int>(m_outdoorGraveyard.size()); }

private:

void drawOutdoorRegion(const char* path, const OutdoorDrawPod& pod)
{
	OutdoorRegionGpu* rg = ensureOutdoorRegion(path);
	if (rg == nullptr) { return; }
	if (rg->residencyFrame != m_frameCounter)
	{
		rg->residencyFrame = m_frameCounter;
		(void)terrain::updateRegionResidency(rg->region, m_cameraPosition.x, m_cameraPosition.z, rg->resident);
		applyRegionResidency(*rg);
	}
	for (std::size_t i = 0; i < rg->region.cells.size(); ++i)
	{
		if (rg->resident[i] == 0) { continue; }
		if (OutdoorWorldGpu* gpu = findOutdoorWorld(rg->region.cells[i].world)) { drawOutdoorWorld(*gpu, pod); }
	}
}

/// @brief region.json は小さな表なので、その場で読む。読めなければ 1 度だけ知らせ、以後は何もしない
[[nodiscard]] OutdoorRegionGpu* ensureOutdoorRegion(const char* path)
{
	if (const auto it = m_outdoorRegions.find(std::string_view(path)); it != m_outdoorRegions.end())
	{
		return it->second->failed ? nullptr : it->second.get();
	}
	auto& slot = m_outdoorRegions[path];
	slot = std::make_unique<OutdoorRegionGpu>();
	auto loaded = terrain::loadOutdoorRegion(path);
	if (!loaded.region)
	{
		slot->failed = true;
		++m_assetLoadFailures;
		debug::warnOnce(std::string("dx12.region.load.") + path,
		                std::string("区画の ") + path + " を読めません (" + loaded.error + ")。パスと書き方を確かめてください。");
		return nullptr;
	}
	slot->region = std::move(*loaded.region);
	return slot.get();
}

/// @brief 近づいた区画をまとめて頼んでから待ち、離れた区画を手放す。まとめて頼むので待つ読み込みでも並んで進む
void applyRegionResidency(OutdoorRegionGpu& rg)
{
	for (std::size_t i = 0; i < rg.region.cells.size(); ++i)
	{
		const std::string& world = rg.region.cells[i].world;
		if (rg.resident[i] == 0) { (void)releaseOutdoorWorld(world.c_str()); continue; }
		if (m_outdoorWorlds.find(world) != m_outdoorWorlds.end() || m_streamer.isPending(streamKey("world:", world))) { continue; }
		if (overStreamingBudget())
		{
			++m_regionBudgetSkips;
			debug::warnOnce("dx12.region.budget",
			                "読み込んだ資産が EngineConfig::streamingBudgetBytes を超えたので、近づいた区画を読みません。上限を上げるか、区画の資産を減らしてください。");
			rg.resident[i] = 0;
			continue;
		}
		requestOutdoorWorld(world.c_str());
	}
	if (m_asyncLoads) { return; }
	for (std::size_t i = 0; i < rg.region.cells.size(); ++i)
	{
		if (rg.resident[i] != 0) { (void)m_streamer.settle(streamKey("world:", rg.region.cells[i].world)); }
	}
}

/// @brief world.json を手放す。読み込み中なら取り消す
/// @return 手放したか取り消したら true
bool releaseOutdoorWorld(const char* path)
{
	forgetAssetBytes("world:", path);
	const auto it = m_outdoorWorlds.find(std::string_view(path));
	if (it == m_outdoorWorlds.end()) { return m_streamer.cancel(streamKey("world:", path)); }
	const bool loaded = it->second && !it->second->failed;
	if (it->second) { m_outdoorGraveyard.push_back({std::move(it->second), m_frameCounter}); }
	m_outdoorWorlds.erase(it);
	return loaded;
}

/// @brief region と、その読み込んだ区画を全部手放す
bool releaseOutdoorRegion(const char* path)
{
	const auto it = m_outdoorRegions.find(std::string_view(path));
	if (it == m_outdoorRegions.end()) { return false; }
	if (it->second)
	{
		for (const auto& cell : it->second->region.cells) { (void)releaseOutdoorWorld(cell.world.c_str()); }
	}
	m_outdoorRegions.erase(it);
	return true;
}

/// @brief beginFrame から呼ぶ。待ちを終えた区画の影の三角形を cache から外して消す
void collectRetiredOutdoorWorlds()
{
	std::erase_if(m_outdoorGraveyard, [this](RetiredOutdoorWorld& r)
	{
		if (m_frameCounter < r.frame + 2 * FRAME_COUNT) { return false; }
		for (const auto& mesh : r.gpu->shadowChunks) { evictMeshBuffers(mesh.get()); }
		return true;
	});
}
