#pragma once

/// @file DX12SkinnedModel.hpp
/// @brief Renderer3D_DX12 のスキンアニメ付き glTF モデル描画実装 (部分ヘッダ、.inl)。
/// @details Renderer3D_DX12 のクラス内部から include される (DX12Splat.hpp と同じ流儀)。
///          責務: glTF/glb (FBX は asset/FbxImport.hpp で glb にしてから) の forward 用 registry (path → model、負キャッシュ) /
///          姿勢の評価 (animation/AnimPose.hpp) → palette → compute スキニング → 既存 drawMesh への合流。
///          clod (drawModel) とは別 registry。clod=静的世界、こちら=動的キャラ。
///          姿勢は AnimPoseParams の純関数で、時間はゲーム側 (GameMemory) が所有する。
///          姿勢は 3 通りで受ける: クリップ名と時刻 (drawSkinnedModel)、AnimPoseParams (host が評価する)、
///          ノードごとのモデル行列 (ゲームが IK まで済ませた姿勢をそのまま描く)。
///          変形した頂点は GPU から出ない。スキン prim の頂点と IB は読み込みの時に GPU へ上げ、CPU には残さない。
///          描画の pool の Mesh は数だけを持ち (Mesh::setGpuOnlyCounts)、VB / IB のキャッシュをその GPU のバッファへ向ける。
///          スキン prim は LOD の段を持つ (render/SkinnedLod.hpp)。段と姿勢の間引きは DX12SkinnedDraw.hpp。

/// @brief 同時ロードできる glTF モデル数の上限。静的な glb (drawModel の 3 軸回転版) も
///        この registry を通るので、小物ごとに 1 glb を持つゲームは数百個になる。
///        deque でアドレスは安定しており上限はロード暴走の歯止めとしてだけ残す。
static constexpr int kMaxSkinnedModels = 1024;
/// @brief 1 フレームに描けるスキン prim 数のハード上限 (pool の物理サイズ)。
///        敵の大群は「200 体 × 4 prim (マテリアル数)」を 1 フレームに積み、副ビューがあればさらに描く。
///        実効上限は `Config::maxSkinnedDrawsPerFrame`（B8、これでクランプ済み）
static constexpr uint32_t kMaxSkinnedDrawsPerFrame = 2048;

/// @brief スキン prim の 1 段ぶんの compute の入力と IB (段 0 は元のまま)
struct SkinnedLod
{
	gfx::GpuResource gpuBase;      ///< compute の入力: Vertex3D の並び
	gfx::GpuResource gpuBinding;   ///< compute の入力: joints/weights
	gfx::GpuResource gpuIndices;   ///< 描画の IB (32 bit)
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
};

/// @brief ロード済みモデルの 1 プリミティブ
struct SkinnedPrim
{
	SkinBoundsSource bounds;                 ///< カリング用の箱の元 (スキン prim のみ。段 0 から作り、どの段にも使う)
	std::array<SkinnedLod, kSkinnedLodLevels> lods;   ///< スキン prim のみ
	int lodCount = 0;                        ///< 使える段の数 (段 0 を含む)
	int skinIndex = -1;                      ///< skins への index (-1 = 剛体)
	int nodeIndex = -1;                      ///< 剛体 prim の姿勢に使うノード (-1 = instanceWorld のみ)
	Mesh mesh;                               ///< 剛体 prim の静的メッシュ (アドレス安定)
	Material material;                       ///< 描画の指定 (抜き・両面・最近傍) と Toon / Phong の色
	MaterialMaps maps;                       ///< GPU のテクスチャと PBR の係数
};

/// @brief 同じモデルをフレームの中で描いた順 (ordinal) ごとの、前のフレームの段と、間引いて使い回す姿勢
/// @details 描画の API に物の ID は無いので、動きベクトルと同じく「描いた順」で前のフレームと対にする。
///          途中の 1 体が消えると、後ろの物は 1 フレームだけ隣の段と姿勢を引き継ぐ (描画だけの崩れで済む)。
struct SkinnedDrawHistory
{
	uint64_t frame = 0;
	uint32_t ordinal = 0;
	std::vector<std::uint8_t> level;              ///< 前のフレームの段 (0xFF = 無し)
	std::vector<std::vector<sgc::Mat4f>> pose;    ///< 間引く段で最後に評価したノードの行列
	std::vector<uint64_t> poseFrame;              ///< pose を作ったフレーム
};

/// @brief ロード済みスキンモデル (registry の値)
struct SkinnedModel
{
	std::vector<SkinnedPrim> prims;
	animation::AnimAsset anim;               ///< 骨格とクリップ。ゲーム DLL が同じファイルから組むものと同じ
	std::deque<dx12::Dx12Texture2D> textures;   ///< MaterialMaps が指す GPU テクスチャ (アドレス安定)
	std::vector<sgc::Mat4f> restModel;          ///< レストポーズの節点のモデル行列 (インスタンス描画用)
	sgc::Vec3f boundsCenter{0, 0, 0};           ///< レスト姿勢の外接球 (モデル空間)。段を選ぶ画面の大きさに使う
	float boundsRadius = 0.0f;
	mutable SkinnedDrawHistory history;
};

/// @brief pool slot ごとの compute 出力。偶奇フレームで交互に書く
/// @details frame N の出力は N+1 の頭の shadow pass が読む。同じバッファを N+1 で書くと
///          「読んでから書く」になり、COMMON からの暗黙昇格で UAV にできなくなる。
struct SkinnedPoolTarget
{
	gfx::GpuResource buffers[2];
	uint64_t bytes = 0;
};

std::map<std::string, int, std::less<>> m_skinnedRegistry;  ///< path → index (-1 = 負キャッシュ)
std::deque<SkinnedModel> m_skinnedModels;                   ///< 要素アドレス安定 (deque)
std::array<Mesh, kMaxSkinnedDrawsPerFrame> m_skinnedPool;   ///< スキン描画の Mesh (数だけ。中身は GPU)
std::array<const void*, kMaxSkinnedDrawsPerFrame> m_skinnedPoolPrim{};  ///< slot が最後に持った段 (SkinnedLod)
std::array<SkinnedPoolTarget, kMaxSkinnedDrawsPerFrame> m_skinnedTargets;  ///< slot の変形済み頂点
uint32_t m_skinnedPoolCursor = 0;                           ///< beginFrame で 0 リセット
uint64_t m_meshBufferCreates = 0;                           ///< VB/IB committed resource 生成回数 (計測用)
dx12::Dx12SkinningCompute m_skinningCompute;
bool m_skinningComputeFailed = false;                       ///< 生成失敗を毎フレーム試さない
std::vector<sgc::Mat4f> m_skinPaletteScratch;               ///< palette 組み立て用 (再確保しない)
std::vector<sgc::Mat4f> m_jointModelScratch;                ///< joint 順のモデル行列 (再確保しない)
animation::AnimPose m_skinnedPose;                          ///< 姿勢の評価の置き場 (再確保しない)

/// @brief path のモデルの番号。読み込みは DX12ModelStreaming.hpp の前半と後半で進め、終わるまでは描かない
/// @return モデル index。読み込み中と失敗は -1 (失敗は負キャッシュ + warnOnce)
[[nodiscard]] int ensureSkinnedModel(const char* path)
{
	if (const auto it = m_skinnedRegistry.find(path); it != m_skinnedRegistry.end())
	{
		return it->second;
	}
	return streamSkinnedModel(path);
}

/// @brief 変更されたファイルから読んだモデルを手放し、次の描画で読み直させる (ホットリロード)
/// @return 忘れた登録の数
int forgetSkinnedModel(const std::filesystem::path& changed)
{
	int forgotten = 0;
	for (auto it = m_skinnedRegistry.begin(); it != m_skinnedRegistry.end();)
	{
		const bool hit = asset::sameAssetFile(it->first, changed);
		if (hit && it->second >= 0) { retireSkinnedSlot(it->second); }
		it = hit ? m_skinnedRegistry.erase(it) : std::next(it);
		forgotten += hit ? 1 : 0;
	}
	return forgotten;
}

/// @brief glTF の材質 1 個ぶんの描画の指定とマップ
struct SkinnedMaterial
{
	Material material;
	MaterialMaps maps;
};

/// @brief mesh を持つノード n の primitive を prim へ展開する
void appendSkinnedPrims(SkinnedModel& model, const GltfSceneData& scene,
                        const std::vector<SkinnedMaterial>& materials, std::size_t n, const std::string& pathStr,
                        const std::vector<dx12::PreparedSkinPrim>& skinPrims)
{
	const auto& node = model.anim.nodes[n];
	if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= scene.meshes.size()) { return; }
	const auto& skins = model.anim.skins;
	const GltfSkinData* skin = (node.skin >= 0 && static_cast<std::size_t>(node.skin) < skins.size())
		? &skins[static_cast<std::size_t>(node.skin)] : nullptr;
	if (skin != nullptr && skin->joints.size() > m_config.maxSkinJoints)
	{
		debug::warnOnce("dx12.skinned.joints." + pathStr,
		                "joint 数が上限 (" + std::to_string(m_config.maxSkinJoints) +
		                    ") を超過 — 剛体で描画: " + pathStr);
		skin = nullptr;
	}
	for (const auto& prim : scene.meshes[static_cast<std::size_t>(node.mesh)].primitives)
	{
		SkinnedPrim sp;
		sp.nodeIndex = static_cast<int>(n);
		if (prim.materialIndex >= 0 && static_cast<std::size_t>(prim.materialIndex) < materials.size())
		{
			sp.material = materials[static_cast<std::size_t>(prim.materialIndex)].material;
			sp.maps = materials[static_cast<std::size_t>(prim.materialIndex)].maps;
		}
		// copy (move しない): 複数ノードが同じ mesh を参照する glTF が有効なため
		const bool skinned = skin != nullptr && !prim.skin.empty();
		const bool paletteValid = skinned && prim.skin.size() == prim.vertices.size() &&
		                          !skin->inverseBindMatrices.empty() &&
		                          skin->inverseBindMatrices.size() == skin->joints.size();
		// 前半が prim の通し番号 (読む順) ごとに段を詰めてある。段の sidecar の鍵も同じ番号
		const std::size_t ordinal = model.prims.size();
		if (paletteValid && ordinal < skinPrims.size() && adoptSkinLods(skinPrims[ordinal], sp))
		{
			sp.skinIndex = node.skin;
			sp.bounds = skinBoundsSource(prim.vertices, prim.skin, skin->inverseBindMatrices);
		}
		else
		{
			// 束縛がおかしくなっているスキン prim は変形せずバインドポーズのまま (ノード変換も掛けない)
			if (skinned) { sp.nodeIndex = -1; }
			sp.mesh.setVertices(prim.vertices);
			sp.mesh.setIndices(prim.indices);
		}
		model.prims.push_back(std::move(sp));
	}
}

/// @brief 前半が詰めた段ごとの compute の入力と IB を prim に持たせる。COPY キューで写した入力は COMMON のまま
///        root SRV で読まれ、暗黙の昇格で読める
[[nodiscard]] bool adoptSkinLods(const dx12::PreparedSkinPrim& prepared, SkinnedPrim& sp)
{
	if (prepared.lods.empty() || !ensureSkinningCompute()) { return false; }
	sp.lodCount = 0;
	for (const auto& level : prepared.lods)
	{
		if (sp.lodCount >= kSkinnedLodLevels) { break; }
		SkinnedLod& out = sp.lods[static_cast<std::size_t>(sp.lodCount)];
		out.gpuBase = level.base.buffer;
		out.gpuBinding = level.binding.buffer;
		out.gpuIndices = level.indices;
		out.vertexCount = level.vertexCount;
		out.indexCount = level.indexCount;
		++sp.lodCount;
		m_meshBufferCreates += 3;
	}
	return true;
}

/// @brief レスト姿勢の外接球。スキン prim は骨ごとの箱をレストの骨で動かし、剛体 prim は節点で動かす
void measureSkinnedBounds(SkinnedModel& model) const
{
	Mesh::AABB box;
	std::vector<sgc::Mat4f> joints;
	for (const auto& prim : model.prims)
	{
		if (prim.skinIndex < 0)
		{
			const auto node = (prim.nodeIndex >= 0) ? model.restModel[static_cast<std::size_t>(prim.nodeIndex)]
			                                         : sgc::Mat4f::identity();
			const auto& b = prim.mesh.localAABB();
			if (b.min.x > b.max.x) { continue; }
			for (int c = 0; c < 8; ++c)
			{
				detail::growBox(box, node.transformPoint({(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y,
				                                          (c & 4) ? b.max.z : b.min.z}));
			}
			continue;
		}
		const auto& skin = model.anim.skins[static_cast<std::size_t>(prim.skinIndex)];
		joints.assign(skin.joints.size(), sgc::Mat4f::identity());
		for (std::size_t j = 0; j < skin.joints.size(); ++j)
		{
			const int node = skin.joints[j];
			if (node >= 0 && static_cast<std::size_t>(node) < model.restModel.size()) { joints[j] = model.restModel[static_cast<std::size_t>(node)]; }
		}
		const auto b = skinnedBounds(prim.bounds, joints);
		if (b.min.x <= b.max.x) { detail::growBox(box, b.min); detail::growBox(box, b.max); }
	}
	if (box.min.x > box.max.x) { return; }
	model.boundsCenter = box.center();
	model.boundsRadius = box.extent().length();
}

[[nodiscard]] bool ensureSkinningCompute()
{
	if (m_skinningCompute.ready()) { return true; }
	if (m_skinningComputeFailed) { return false; }
	if (!m_skinningCompute.init(m_d3dDevice))
	{
		m_skinningComputeFailed = true;
		debug::warnOnce("dx12.skinned.compute", "スキニングの compute を作れない — スキン prim はバインドポーズで描く");
		return false;
	}
	return true;
}

/// @brief クリップ名を引く。空/不在は -1 (= レストポーズ)。不在は warnOnce。
[[nodiscard]] int findSkinnedClip(const SkinnedModel& model, const char* path, const char* clipName) const
{
	if (clipName == nullptr || clipName[0] == '\0') { return -1; }
	const int idx = model.anim.findClip(clipName);
	if (idx < 0)
	{
		debug::warnOnce(std::string("dx12.skinned.clip.") + path + "." + clipName,
		                std::string("clip が見つからない (レストポーズで継続): ") + clipName +
		                    " in " + path);
	}
	return idx;
}

/// @brief スキンモデル描画の実体 (IRenderer3D::drawSkinnedModel から呼ばれる)
/// @details clipB 非 null で A→B crossfade (blend01: 0=A, 1=B)。時間は絶対秒 (ループ)。
void drawSkinnedModelImpl(const char* path, const sgc::Vec3f& position, float rotYDeg,
                          float scale, const char* clipA, float timeA,
                          const char* clipB, float timeB, float blend01)
{
	constexpr float kDeg2Rad = 0.017453292519943295f;
	drawSkinnedModelWorldImpl(path,
	                          sgc::Mat4f::translation(position) *
	                              sgc::Mat4f::rotationY(rotYDeg * kDeg2Rad) *
	                              sgc::Mat4f::scaling(scale),
	                          clipA, timeA, clipB, timeB, blend01);
}

/// @brief 3 軸回転の rigid glb 描画 (IRenderer3D::drawModelRot)。rot は drawMesh と同じ
///        {pitch, yaw, roll} 度・Ry→Rx→Rz 順
void drawModelRotImpl(const char* path, const sgc::Vec3f& position,
                      const sgc::Vec3f& rotDeg, float scale)
{
	constexpr float kDeg2Rad = 0.017453292519943295f;
	drawSkinnedModelWorldImpl(path,
	                          sgc::Mat4f::translation(position) *
	                              sgc::Mat4f::rotationY(rotDeg.y * kDeg2Rad) *
	                              sgc::Mat4f::rotationX(rotDeg.x * kDeg2Rad) *
	                              sgc::Mat4f::rotationZ(rotDeg.z * kDeg2Rad) *
	                              sgc::Mat4f::scaling(scale),
	                          "", 0.0f, nullptr, 0.0f, 0.0f);
}

/// @brief クリップ名と時刻を AnimPoseParams にして描く (A を全身、B を blend01 で重ねる)。tint は全 prim に掛ける
void drawSkinnedModelWorldImpl(const char* path, const sgc::Mat4f& instanceWorld,
                               const char* clipA, float timeA,
                               const char* clipB, float timeB, float blend01, const DrawTint& tint = {})
{
	const SkinnedModel* model = skinnedModelFor(path);
	if (model == nullptr) { return; }
	animation::AnimPoseParams params;
	animation::AnimLayer a;
	a.clip = static_cast<std::int16_t>(findSkinnedClip(*model, path, clipA));
	a.timeSec = timeA;
	animation::pushLayer(params, a);
	if (clipB != nullptr)
	{
		animation::AnimLayer b;
		b.clip = static_cast<std::int16_t>(findSkinnedClip(*model, path, clipB));
		b.timeSec = timeB;
		b.weight = std::clamp(blend01, 0.0f, 1.0f);
		animation::pushLayer(params, b);
	}
	drawSkinnedEvaluated(*model, instanceWorld, params, nullptr, 0, tint);
}

public:
/// @brief AnimPoseParams から host が姿勢を評価して描く。ゲーム DLL が同じ params で評価した姿勢と一致する。
/// @details ik は評価のあとに順に掛ける (animation::applyIkRequests。DLL が当たり判定に使うのと同じ関数)。
void drawSkinnedModelParams(const char* path, const sgc::Mat4f& instanceWorld,
                            const animation::AnimPoseParams& params, const DrawTint& tint = {},
                            const animation::AnimIkRequest* ik = nullptr, int ikCount = 0)
{
	const SkinnedModel* model = skinnedModelFor(path);
	if (model == nullptr) { return; }
	drawSkinnedEvaluated(*model, instanceWorld, params, ik, ikCount, tint);
}

/// @brief ノードごとのモデル空間の行列 (AnimPose::model と同じ並び) で描く。IK を掛けた姿勢はこちら。
/// @details 個数がモデルのノード数と違えば warnOnce して描かない (別のモデルの姿勢を渡した誤りを見逃さない)。
void drawSkinnedModelPose(const char* path, const sgc::Mat4f& instanceWorld,
                          std::span<const sgc::Mat4f> nodeModel, const DrawTint& tint = {})
{
	const SkinnedModel* model = skinnedModelFor(path);
	if (model == nullptr) { return; }
	if (nodeModel.size() != model->anim.nodes.size())
	{
		debug::warnOnce(std::string("dx12.skinned.pose.count.") + path,
		                std::string("姿勢の行列の数がノード数と合わない (描かない): ") + path);
		return;
	}
	drawSkinnedPosed(*model, instanceWorld, nodeModel, tint, beginSkinnedDraw(*model, instanceWorld).level);
}

/// @brief 描画に使っている骨格とクリップ。モデルが読めなければ nullptr。
[[nodiscard]] const animation::AnimAsset* skinnedAnimAsset(const char* path)
{
	const SkinnedModel* model = skinnedModelFor(path);
	return model != nullptr ? &model->anim : nullptr;
}

private:
[[nodiscard]] const SkinnedModel* skinnedModelFor(const char* path)
{
	if (path == nullptr) { return nullptr; }
	const int idx = ensureSkinnedModel(path);
	return idx < 0 ? nullptr : &m_skinnedModels[static_cast<std::size_t>(idx)];
}

/// @brief 剛体の glTF モデルをインスタンス描画する。prim ごとに 1 回の instanced draw (スキン prim は描かない)
void drawModelInstancesImpl(const char* path, const MeshInstance* instances, std::size_t count)
{
	if (path == nullptr || instances == nullptr || count == 0) { return; }
	const int idx = ensureSkinnedModel(path);
	if (idx < 0) { return; }
	const auto& model = m_skinnedModels[static_cast<std::size_t>(idx)];
	for (const auto& prim : model.prims)
	{
		if (prim.skinIndex >= 0)
		{
			debug::warnOnce(std::string("dx12.skinned.instanced.") + path,
			                std::string("スキン付きの prim はインスタンス描画しない: ") + path);
			continue;
		}
		const sgc::Mat4f node = (prim.nodeIndex >= 0 && static_cast<std::size_t>(prim.nodeIndex) < model.restModel.size())
			? model.restModel[static_cast<std::size_t>(prim.nodeIndex)] : sgc::Mat4f::identity();
		m_instanceInputScratch.clear();
		for (std::size_t i = 0; i < count; ++i)
		{
			const MeshInstance& in = instances[i];
			m_instanceInputScratch.push_back(makeInstance(instanceWorld(in) * node, in.tint[0], in.tint[1], in.tint[2], in.tint[3]));
		}
		drawMeshInstancesDx12(prim.mesh, m_instanceInputScratch.data(), m_instanceInputScratch.size(), prim.material,
		                      &prim.maps);
	}
}
