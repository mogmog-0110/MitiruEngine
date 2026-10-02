// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12SkinnedModel.hpp の後に include)。glTF モデルを手放す
//
// 手放したモデルは墓場へ移し、2 × FRAME_COUNT フレーム後に CPU の Mesh ごと消す。GPU は FRAME_COUNT フレーム遅れて
// 読み、影のパスはさらに 1 フレーム後に前フレームの caster (Mesh*) を描くので、すぐに消すと解放後の読み出しになる。
// 空いた registry の番号は次に読むモデルが使う (読み込み数の上限は生きているモデルだけを数える)。

/// 墓場の 1 件。テクスチャの deque は複製できず、vector の伸長で動かせないので箱に入れる
struct RetiredSkinnedModel
{
	std::unique_ptr<SkinnedModel> model;
	std::uint64_t                 frame = 0;
};
std::vector<RetiredSkinnedModel> m_skinnedGraveyard;
std::vector<int>                 m_freeSkinnedSlots;

public:

/// @brief path で読んだ glTF / FBX モデル (drawSkinnedModel・drawModelPosed・drawModelInstances が読むもの) を手放す。
///        次に同じ path を描くと読み直す。clod の世界のモデル (drawModel の .clod) は 1 枚の場面に混ぜてあるので対象外
/// @return 読み込み済みのモデルを手放したら true。読み込みに失敗していた path は失敗の記録だけを消して false
bool releaseModel(const char* path) override
{
	if (path == nullptr) { return false; }
	const auto it = m_skinnedRegistry.find(path);
	if (it == m_skinnedRegistry.end()) { return false; }
	const int idx = it->second;
	m_skinnedRegistry.erase(it);
	if (idx < 0) { return false; }
	retireSkinnedSlot(idx);
	return true;
}

/// @brief 生きている glTF モデルの数 (手放して墓場にあるものは数えない)
[[nodiscard]] int loadedModelCount() const noexcept
{
	return static_cast<int>(m_skinnedModels.size() - m_freeSkinnedSlots.size());
}

/// @brief GPU が読み終えるのを待っている、手放したモデルの数 (診断とテスト用)
[[nodiscard]] int retiringModelCount() const noexcept { return static_cast<int>(m_skinnedGraveyard.size()); }

private:

/// @brief 番号 idx のモデルを墓場へ移し、番号を空きにする。pool の slot が覚えている prim も忘れさせる
///        (墓場から消えた後に同じアドレスへ別の prim が来ても、古い頂点を使い回さない)
void retireSkinnedSlot(int idx)
{
	SkinnedModel& slot = m_skinnedModels[static_cast<std::size_t>(idx)];
	for (const auto& prim : slot.prims)
	{
		std::replace(m_skinnedPoolPrim.begin(), m_skinnedPoolPrim.end(), static_cast<const void*>(&prim),
		             static_cast<const void*>(nullptr));
	}
	m_skinnedGraveyard.push_back({std::make_unique<SkinnedModel>(std::move(slot)), m_frameCounter});
	slot = SkinnedModel{};
	m_freeSkinnedSlots.push_back(idx);
}

/// @brief 新しいモデルを置く番号。空きがあれば使い回す
[[nodiscard]] int placeSkinnedModel(SkinnedModel&& model)
{
	if (!m_freeSkinnedSlots.empty())
	{
		const int idx = m_freeSkinnedSlots.back();
		m_freeSkinnedSlots.pop_back();
		m_skinnedModels[static_cast<std::size_t>(idx)] = std::move(model);
		return idx;
	}
	m_skinnedModels.push_back(std::move(model));
	return static_cast<int>(m_skinnedModels.size()) - 1;
}

/// @brief mesh の VB/IB を cache から外し、GPU 資源はこのフレームの一時資源として 1 周後に解放する
void evictMeshBuffers(const void* mesh)
{
	for (auto* cache : {&m_meshVBCache, &m_meshIBCache})
	{
		const auto it = cache->find(mesh);
		if (it == cache->end()) { continue; }
		m_frameTempResources.push_back(std::move(it->second.resource));
		for (auto& slot : it->second.slots) { if (slot) { m_frameTempResources.push_back(std::move(slot)); } }
		cache->erase(it);
	}
}

/// @brief beginFrame から呼ぶ。待ちを終えたモデルの VB/IB を cache から外し、テクスチャとスキンの入力ごと消す
void collectRetiredModels()
{
	std::erase_if(m_skinnedGraveyard, [this](RetiredSkinnedModel& r)
	{
		if (m_frameCounter < r.frame + 2 * FRAME_COUNT) { return false; }
		for (const auto& prim : r.model->prims) { evictMeshBuffers(&prim.mesh); }
		return true;
	});
}
