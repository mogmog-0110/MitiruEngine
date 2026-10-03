// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12SkinnedModel.hpp の後に include)。スキン prim を描く
//
// 変形 (compute) はメインとは別のコマンドリストに積み、finalizeFrame で「変形 → メイン」の順に同じ提出で流す。
// 変形どうしは出力のバッファが別なので間に barrier を挟まず続けて流し、頂点として読む遷移は最後にまとめて 1 回張る。
// palette はキャラ (skin) ごとに 1 回だけ作り、同じ skin の prim はそれを共有する。
// LOD の段はキャラの外接球が画面の高さに占める割合で選び (render/SkinnedLod.hpp)、遠い段では姿勢の評価も間引く。
// 段と間引きは絵を変えるだけで、ゲームが同じ AnimPoseParams から当たり判定に出す骨の位置とは関係しない。

ComPtr<ID3D12CommandAllocator>      m_skinCmdAllocators[FRAME_COUNT];
ComPtr<ID3D12GraphicsCommandList>   m_skinCmdList;
bool                                m_skinListOpen = false;      ///< このフレームの変形を積んでいる
uint64_t                            m_skinListFrame = 0;
std::vector<D3D12_RESOURCE_BARRIER> m_skinBarriers;              ///< 頂点として読む遷移 (最後にまとめて張る)
SkinnedLodThresholds                m_skinnedLodThresholds;
float                               m_skinnedLodScale = 1.0f;    ///< 画面の大きさに掛ける (大きいほど細かい段を長く使う)
int                                 m_skinnedLodForced = -1;     ///< 0 以上なら全キャラをその段で描く (確かめる用)
bool                                m_skinnedPoseThinning = true;
uint64_t                            m_skinnedStatsFrame = 0;
std::array<uint32_t, kSkinnedLodLevels> m_skinnedLodDraws{};     ///< 段ごとに描いたスキン prim の数 (そのフレーム)
uint32_t                            m_skinnedPoseReuses = 0;     ///< 姿勢を評価せずに使い回したキャラの数 (そのフレーム)

public:
/// @brief LOD の段を選ぶ境目と、画面の大きさに掛ける倍率 (2 なら同じ距離で 1 つ細かい段を使う)
void setSkinnedLod(const SkinnedLodThresholds& thresholds, float scale = 1.0f) noexcept
{
	m_skinnedLodThresholds = thresholds;
	m_skinnedLodScale = std::max(scale, 0.0f);
}

/// @brief 全キャラをこの段で描く (-1 で画面の大きさに戻す)。段の見た目を並べて比べるときに使う
void forceSkinnedLod(int level) noexcept { m_skinnedLodForced = std::clamp(level, -1, kSkinnedLodLevels - 1); }

/// @brief 遠い段の姿勢の評価を間引くか (既定は間引く)
void setSkinnedPoseThinning(bool enabled) noexcept { m_skinnedPoseThinning = enabled; }

/// @brief ゲーム DLL の設定 (ABI v50)。0 の欄は既定に戻す
void setSkinnedLodLook(const SkinnedLodLook& look) override
{
	SkinnedLodThresholds t;
	for (int k = 0; k < kSkinnedLodLevels - 1; ++k)
	{
		if (look.screen[k] > 0.0f) { t.screen[k] = look.screen[k]; }
	}
	setSkinnedLod(t, look.scale > 0.0f ? look.scale : 1.0f);
	forceSkinnedLod(look.forcedLevel);
	setSkinnedPoseThinning((look.flags & kSkinnedLodNoPoseThinning) == 0);
}

/// @brief そのフレームに段 level で描いたスキン prim の数
[[nodiscard]] uint32_t skinnedDrawsAtLod(int level) const noexcept
{
	return (level >= 0 && level < kSkinnedLodLevels) ? m_skinnedLodDraws[static_cast<std::size_t>(level)] : 0u;
}

/// @brief そのフレームに姿勢を評価せずに前の姿勢で描いたキャラの数
[[nodiscard]] uint32_t skinnedPoseReuses() const noexcept { return m_skinnedPoseReuses; }

private:
/// @brief 1 体を描き始めたときの、フレームの中での描いた順と使う段
struct SkinnedDrawSlot
{
	uint32_t ordinal = 0;
	int level = 0;
};

[[nodiscard]] SkinnedDrawSlot beginSkinnedDraw(const SkinnedModel& model, const sgc::Mat4f& instanceWorld)
{
	if (m_skinnedStatsFrame != m_frameCounter)
	{
		m_skinnedStatsFrame = m_frameCounter;
		m_skinnedLodDraws.fill(0);
		m_skinnedPoseReuses = 0;
	}
	auto& h = model.history;
	if (h.frame != m_frameCounter) { h.frame = m_frameCounter; h.ordinal = 0; }
	const uint32_t ordinal = h.ordinal++;
	if (h.level.size() <= ordinal) { h.level.resize(ordinal + 1, 0xFF); }
	const int levels = skinnedLodLevelsOf(model);
	const int previous = (h.level[ordinal] == 0xFF) ? -1 : h.level[ordinal];
	const int level = (m_skinnedLodForced >= 0)
		? std::min(m_skinnedLodForced, levels - 1)
		: selectSkinnedLod(screenFractionOf(model, instanceWorld), previous, levels, m_skinnedLodThresholds);
	h.level[ordinal] = static_cast<std::uint8_t>(level);
	return {ordinal, level};
}

/// @brief モデルの中で最も段の多い prim の段の数 (剛体だけのモデルは 1)
[[nodiscard]] static int skinnedLodLevelsOf(const SkinnedModel& model) noexcept
{
	int levels = 1;
	for (const auto& prim : model.prims) { levels = std::max(levels, prim.lodCount); }
	return levels;
}

/// @brief キャラの外接球の直径が、今のカメラの画面の高さに占める割合
[[nodiscard]] float screenFractionOf(const SkinnedModel& model, const sgc::Mat4f& instanceWorld) const
{
	const Camera3D& cam = (m_activeView != nullptr) ? m_activeView->camera : m_clodCamera;
	const sgc::Vec3f center = instanceWorld.transformPoint(model.boundsCenter);
	const float scale = std::max({sgc::Vec3f{instanceWorld.m[0][0], instanceWorld.m[1][0], instanceWorld.m[2][0]}.length(),
	                              sgc::Vec3f{instanceWorld.m[0][1], instanceWorld.m[1][1], instanceWorld.m[2][1]}.length(),
	                              sgc::Vec3f{instanceWorld.m[0][2], instanceWorld.m[1][2], instanceWorld.m[2][2]}.length()});
	const float distance = std::max((center - cam.position()).length(), 1e-3f);
	const float halfHeight = distance * std::tan(cam.fov() * 0.5f);
	return m_skinnedLodScale * model.boundsRadius * scale / std::max(halfHeight, 1e-6f);
}

/// @brief host が姿勢を評価して描く。遠い段では数フレームに 1 回だけ評価し、間は前の姿勢を使う
/// @details 評価するフレームはキャラごとにずらす (同じフレームに全員の評価が重ならない)
void drawSkinnedEvaluated(const SkinnedModel& model, const sgc::Mat4f& instanceWorld,
                          const animation::AnimPoseParams& params, const animation::AnimIkRequest* ik, int ikCount,
                          const DrawTint& tint)
{
	const SkinnedDrawSlot slot = beginSkinnedDraw(model, instanceWorld);
	const int interval = m_skinnedPoseThinning ? skinnedPoseInterval(slot.level) : 1;
	auto& h = model.history;
	if (h.pose.size() <= slot.ordinal)
	{
		h.pose.resize(slot.ordinal + 1);
		h.poseFrame.resize(slot.ordinal + 1, 0);
	}
	auto& cached = h.pose[slot.ordinal];
	const auto every = static_cast<uint64_t>(interval);
	const bool reuse = interval > 1 && cached.size() == model.anim.nodes.size() &&
	                   m_frameCounter - h.poseFrame[slot.ordinal] < every && (m_frameCounter + slot.ordinal) % every != 0;
	if (reuse)
	{
		++m_skinnedPoseReuses;
		drawSkinnedPosed(model, instanceWorld, cached, tint, slot.level);
		return;
	}
	animation::evaluatePose(model.anim, params, m_skinnedPose);
	animation::applyIkRequests(model.anim, m_skinnedPose, instanceWorld, ik, ikCount);
	if (interval > 1)
	{
		cached.assign(m_skinnedPose.model.begin(), m_skinnedPose.model.end());
		h.poseFrame[slot.ordinal] = m_frameCounter;
	}
	drawSkinnedPosed(model, instanceWorld, m_skinnedPose.model, tint, slot.level);
}

/// @brief ノードごとのモデル行列で全 prim を描く (材質のマップと tint つき)。スキン prim は段 level で描く
void drawSkinnedPosed(const SkinnedModel& model, const sgc::Mat4f& instanceWorld, std::span<const sgc::Mat4f> nodeModel,
                      const DrawTint& tint, int level)
{
	// Config の上限がプール物理サイズを超えて指定されても array の外へは出さない
	const uint32_t limit = std::min(m_config.maxSkinnedDrawsPerFrame, kMaxSkinnedDrawsPerFrame);
	int paletteSkin = -1;
	D3D12_GPU_VIRTUAL_ADDRESS palette = 0;
	for (const auto& prim : model.prims)
	{
		if (prim.skinIndex < 0)
		{
			// 剛体 prim: ノード姿勢で描く (ボーンに付いた小物もアニメに追従する)
			const bool hasNode = prim.nodeIndex >= 0 && static_cast<std::size_t>(prim.nodeIndex) < nodeModel.size();
			const auto nodeWorld = hasNode ? nodeModel[static_cast<std::size_t>(prim.nodeIndex)] : sgc::Mat4f::identity();
			drawMeshEx(prim.mesh, instanceWorld * nodeWorld, prim.material, &prim.maps, tint);
			continue;
		}
		if (m_skinnedPoolCursor >= limit)
		{
			debug::warnOnce("dx12.skinned.pool.full",
			                "スキン描画が 1 フレームの上限 (Config::maxSkinnedDrawsPerFrame) に達した — 以降は描かない");
			continue;
		}
		// スキン prim はノード変換を無視する (glTF 仕様)。配置は instanceWorld のみ
		if (prim.skinIndex != paletteSkin)
		{
			const auto& skin = model.anim.skins[static_cast<std::size_t>(prim.skinIndex)];
			gatherJointModelInto(skin, nodeModel);
			palette = uploadSkinPalette(skin);
			paletteSkin = prim.skinIndex;
		}
		if (palette == 0) { continue; }
		const int lod = std::clamp(level, 0, prim.lodCount - 1);
		drawSkinnedPrim(prim, prim.lods[static_cast<std::size_t>(lod)], palette, instanceWorld, tint);
		++m_skinnedLodDraws[static_cast<std::size_t>(lod)];
	}
}

/// @brief skin の joints 順にノードのモデル行列を m_jointModelScratch へ集める
void gatherJointModelInto(const GltfSkinData& skin, std::span<const sgc::Mat4f> nodeModel)
{
	m_jointModelScratch.resize(skin.joints.size());
	for (std::size_t j = 0; j < skin.joints.size(); ++j)
	{
		const int node = skin.joints[j];
		const bool valid = node >= 0 && static_cast<std::size_t>(node) < nodeModel.size();
		m_jointModelScratch[j] = valid ? nodeModel[static_cast<std::size_t>(node)] : sgc::Mat4f::identity();
	}
}

/// @brief m_jointModelScratch と逆バインド行列から palette を作って ring に置く。置けなければ 0
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadSkinPalette(const GltfSkinData& skin)
{
	const std::size_t n = std::min(m_jointModelScratch.size(), skin.inverseBindMatrices.size());
	m_skinPaletteScratch.resize(n);
	for (std::size_t j = 0; j < n; ++j) { m_skinPaletteScratch[j] = m_jointModelScratch[j] * skin.inverseBindMatrices[j]; }
	const auto a = m_uploadRing.upload(m_skinPaletteScratch.data(), n * sizeof(sgc::Mat4f), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief スキン prim の段 1 つを pool slot へ変形して描く
void drawSkinnedPrim(const SkinnedPrim& prim, const SkinnedLod& lod, D3D12_GPU_VIRTUAL_ADDRESS palette,
                     const sgc::Mat4f& instanceWorld, const DrawTint& tint)
{
	const uint32_t slot = m_skinnedPoolCursor++;
	Mesh& pool = m_skinnedPool[slot];
	const gfx::GpuResource* out = skinTargetFor(slot, uint64_t{lod.vertexCount} * sizeof(Vertex3D));
	ID3D12GraphicsCommandList* list = skinList();
	if (out == nullptr || list == nullptr) { return; }
	if (m_skinnedPoolPrim[slot] != static_cast<const void*>(&lod))
	{
		pool.setGpuOnlyCounts(lod.vertexCount, lod.indexCount);
		m_skinnedPoolPrim[slot] = static_cast<const void*>(&lod);
	}
	dx12::SkinningDispatch d;
	d.palette = palette;
	d.baseVertices = lod.gpuBase->GetGPUVirtualAddress();
	d.bindings = lod.gpuBinding->GetGPUVirtualAddress();
	d.out = out->Get();
	d.vertexCount = lod.vertexCount;
	d.jointCount = static_cast<uint32_t>(m_skinPaletteScratch.size());
	m_skinningCompute.dispatch(list, d);
	m_skinBarriers.push_back(dx12::Dx12SkinningCompute::vertexReadBarrier(out->Get()));

	pool.setLocalAABB(skinnedBounds(prim.bounds, m_jointModelScratch));
	bindGpuVertexBuffer(pool, *out);
	bindGpuIndexBuffer(pool, lod);
	recordSkinnedMotionDraw(&lod, slot, *out, instanceWorld);
	drawMeshEx(pool, instanceWorld, prim.material, &prim.maps, tint);
}

/// @brief slot の今フレーム側の出力 (足りなければ作り直す)。古い方は in-flight が読み終えてから捨てる
/// @details frame N の出力は N+1 の頭の shadow pass が読む。同じバッファを N+1 で書くと
///          「読んでから書く」になり、COMMON からの暗黙昇格で UAV にできなくなる。だから偶奇で 2 枚を交互に書く
[[nodiscard]] const gfx::GpuResource* skinTargetFor(uint32_t slot, uint64_t bytes)
{
	auto& target = m_skinnedTargets[slot];
	if (target.bytes < bytes)
	{
		target.bytes = 0;
		for (auto& buf : target.buffers)
		{
			buf.Reset();
			if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COMMON,
			                                buf, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
			{
				return nullptr;
			}
			++m_meshBufferCreates;
		}
		target.bytes = bytes;
	}
	return &target.buffers[m_frameCounter & 1];
}

/// @brief mesh の VB キャッシュを GPU が書いたバッファに向ける
/// @details 偶奇の 2 枚を entry.resource と entry.slots[0] の間で入れ替えて持つ。複製で差し替えると、
///          手放す参照ごとに解放の待ち行列へ積む確保と fence の照会が走る (prim ごと、毎フレーム)
void bindGpuVertexBuffer(const Mesh& mesh, const gfx::GpuResource& buffer)
{
	auto& entry = m_meshVBCache[static_cast<const void*>(&mesh)];
	if (entry.resource.Get() != buffer.Get())
	{
		if (entry.slots[0].Get() == buffer.Get()) { std::swap(entry.resource, entry.slots[0]); }
		else
		{
			entry.slots[0] = std::move(entry.resource);
			entry.resource = buffer;
		}
	}
	entry.size = static_cast<UINT>(mesh.vertexCount() * sizeof(Vertex3D));
	entry.revision = mesh.revision();
	entry.lastUsedFrame = m_frameCounter;
}

/// @brief mesh の IB キャッシュを段の IB に向ける (段が変わった slot だけ差し替わる)
void bindGpuIndexBuffer(const Mesh& mesh, const SkinnedLod& lod)
{
	auto& entry = m_meshIBCache[static_cast<const void*>(&mesh)];
	if (entry.resource.Get() != lod.gpuIndices.Get()) { entry.resource = lod.gpuIndices; }
	entry.size = lod.indexCount * static_cast<UINT>(sizeof(uint32_t));
	entry.revision = mesh.revision();
	entry.lastUsedFrame = m_frameCounter;
}

// ── 変形を積む補助リスト ─────────────────────────────────────────────

/// @brief このフレームの変形を積むリスト。初めて呼ばれたフレームで開き、PSO を設定する。作れなければ nullptr
[[nodiscard]] ID3D12GraphicsCommandList* skinList()
{
	if (m_skinListOpen && m_skinListFrame == m_frameCounter) { return m_skinCmdList.Get(); }
	if (m_skinListOpen) { (void)m_skinCmdList->Close(); }   // 提出されずに前のフレームから残ったものは捨てる
	m_skinListOpen = false;
	if (!m_skinCmdList && !createSkinCommandList()) { return nullptr; }
	auto* alloc = m_skinCmdAllocators[m_frameCursor % FRAME_COUNT].Get();
	if (FAILED(alloc->Reset()) || FAILED(m_skinCmdList->Reset(alloc, nullptr))) { return nullptr; }
	m_frameTimer.begin(m_skinCmdList.Get(), kFrameTimerSkin);
	m_skinningCompute.bind(m_skinCmdList.Get());
	m_skinBarriers.clear();
	m_skinListOpen = true;
	m_skinListFrame = m_frameCounter;
	return m_skinCmdList.Get();
}

[[nodiscard]] bool createSkinCommandList()
{
	for (auto& a : m_skinCmdAllocators)
	{
		if (FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		                                               IID_PPV_ARGS(a.ReleaseAndGetAddressOf()))))
		{
			return false;
		}
	}
	if (FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_skinCmdAllocators[0].Get(), nullptr,
	                                          IID_PPV_ARGS(m_skinCmdList.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	m_skinCmdList->SetName(L"Renderer3D skinning");
	m_skinBarriers.reserve(kMaxSkinnedDrawsPerFrame);
	return SUCCEEDED(m_skinCmdList->Close());
}

/// @brief finalizeFrame で呼ぶ。頂点として読む遷移をまとめて張って閉じる。提出するリストを返す (無ければ nullptr)
[[nodiscard]] ID3D12CommandList* closeSkinList()
{
	if (!m_skinListOpen || m_skinListFrame != m_frameCounter) { return nullptr; }
	m_skinListOpen = false;
	if (!m_skinBarriers.empty())
	{
		m_skinCmdList->ResourceBarrier(static_cast<UINT>(m_skinBarriers.size()), m_skinBarriers.data());
	}
	m_frameTimer.end(m_skinCmdList.Get(), kFrameTimerSkin);
	return SUCCEEDED(m_skinCmdList->Close()) ? m_skinCmdList.Get() : nullptr;
}
