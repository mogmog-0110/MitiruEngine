#pragma once

/// @file DX12SkinnedModel.hpp
/// @brief Renderer3D_DX12 のスキンアニメ付き glTF モデル描画実装 (部分ヘッダ、.inl)。
/// @details Renderer3D_DX12 のクラス内部から include される (DX12Splat.hpp と同じ流儀)。
///          責務: glTF/glb の forward 用 registry (path → model、負キャッシュ) /
///          クリップサンプル → palette → compute スキニング → 既存 drawMesh への合流。
///          clod (drawModel) とは別 registry。clod=静的世界、こちら=動的キャラ。
///          ポーズは (clip, time) の純関数で、時間はゲーム側 (GameMemory) が所有する。
///          変形した頂点は GPU から出ない。pool の Mesh は CPU 側にバインドポーズを持ち
///          (頂点数と IB のため)、VB キャッシュの中身だけを compute の出力へ差し替える。

/// @brief 同時ロードできる glTF モデル数の上限。静的な glb (drawModel の 3 軸回転版) も
///        この registry を通るので、小物ごとに 1 glb を持つゲームは数十個を超える。
///        deque でアドレスは安定しており上限はロード暴走の歯止めとしてだけ残す。
static constexpr int kMaxSkinnedModels = 256;
/// @brief 1 フレームに描けるスキン prim 数のハード上限 (pool の物理サイズ)。
///        群れ物は「数十体 × 数 prim (マテリアル数)」を消費するのでこの規模が要る。
///        実効上限は `Config::maxSkinnedDrawsPerFrame`（B8、これでクランプ済み）
static constexpr uint32_t kMaxSkinnedDrawsPerFrame = 256;

/// @brief ロード済みモデルの 1 プリミティブ
struct SkinnedPrim
{
	std::vector<Vertex3D> base;              ///< バインドポーズ頂点 (スキン prim のみ)
	std::vector<uint32_t> indices;           ///< インデックス (スキン prim のみ)
	SkinBoundsSource bounds;                 ///< カリング用の箱の元 (スキン prim のみ)
	gfx::GpuResource gpuBase;                ///< compute の入力: base (スキン prim のみ)
	gfx::GpuResource gpuBinding;             ///< compute の入力: joints/weights (スキン prim のみ)
	int skinIndex = -1;                      ///< skins への index (-1 = 剛体)
	int nodeIndex = -1;                      ///< 剛体 prim の姿勢に使うノード (-1 = instanceWorld のみ)
	Mesh mesh;                               ///< 剛体 prim の静的メッシュ (アドレス安定)
	Material material;                       ///< albedoTexture 配線済みマテリアル
};

/// @brief ロード済みスキンモデル (registry の値)
struct SkinnedModel
{
	std::vector<SkinnedPrim> prims;
	std::vector<GltfNode> nodes;
	std::vector<GltfSkinData> skins;
	std::vector<GltfAnimationClip> clips;
	std::deque<Texture> textures;            ///< Material.albedoTexture の安定アドレス所有
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
std::array<Mesh, kMaxSkinnedDrawsPerFrame> m_skinnedPool;   ///< スキン描画の Mesh (CPU 頂点はバインドポーズ)
std::array<const void*, kMaxSkinnedDrawsPerFrame> m_skinnedPoolPrim{};  ///< slot が最後に持った prim
std::array<SkinnedPoolTarget, kMaxSkinnedDrawsPerFrame> m_skinnedTargets;  ///< slot の変形済み頂点
uint32_t m_skinnedPoolCursor = 0;                           ///< beginFrame で 0 リセット
uint64_t m_meshBufferCreates = 0;                           ///< VB/IB committed resource 生成回数 (計測用)
dx12::Dx12SkinningCompute m_skinningCompute;
bool m_skinningComputeFailed = false;                       ///< 生成失敗を毎フレーム試さない
std::vector<sgc::Mat4f> m_skinPaletteScratch;               ///< palette 組み立て用 (再確保しない)

/// @brief glTF/glb をロードして registry へ入れる (失敗は負キャッシュ + warnOnce)
/// @return モデル index、失敗時 -1
[[nodiscard]] int ensureSkinnedModel(const char* path)
{
	if (const auto it = m_skinnedRegistry.find(path); it != m_skinnedRegistry.end())
	{
		return it->second;
	}
	const auto fail = [&](const char* why) {
		debug::warnOnce(std::string("dx12.skinned.load.") + path,
		                std::string("skinned model のロードに失敗: ") + path + " (" + why + ")");
		m_skinnedRegistry.emplace(path, -1);
		return -1;
	};
	if (m_skinnedModels.size() >= static_cast<std::size_t>(kMaxSkinnedModels))
	{
		return fail("モデル数上限");
	}
	const auto bytes = vfs::readGlobal(path);
	if (!bytes) { return fail("読めない"); }
	auto scene = loadGltfFromMemory(bytes->data(), bytes->size());
	if (!scene) { return fail("glTF parse 失敗"); }

	SkinnedModel model;
	model.nodes = std::move(scene->nodes);
	model.skins = std::move(scene->skins);
	model.clips = std::move(scene->animations);

	const std::string pathStr(path);
	const auto materials = loadSkinnedMaterials(*scene, pathStr, model.textures);
	for (std::size_t n = 0; n < model.nodes.size(); ++n)
	{
		appendSkinnedPrims(model, *scene, materials, n, pathStr);
	}
	if (model.prims.empty()) { return fail("描ける prim が無い"); }

	m_skinnedModels.push_back(std::move(model));
	const int idx = static_cast<int>(m_skinnedModels.size()) - 1;
	m_skinnedRegistry.emplace(path, idx);
	return idx;
}

/// @brief マテリアル → Material (albedo は埋め込み優先、外部 URI はモデルディレクトリ相対)
[[nodiscard]] std::vector<Material> loadSkinnedMaterials(const GltfSceneData& scene, const std::string& pathStr,
                                                         std::deque<Texture>& textures)
{
	const auto dirEnd = pathStr.find_last_of("/\\");
	const std::string dir = (dirEnd == std::string::npos) ? "" : pathStr.substr(0, dirEnd + 1);
	std::vector<Material> materials(scene.materials.size());
	for (std::size_t i = 0; i < scene.materials.size(); ++i)
	{
		const auto& gmat = scene.materials[i];
		materials[i] = convertGltfMaterial(gmat);
		if (gmat.baseColorTexture.valid())
		{
			textures.emplace_back(Texture(gmat.baseColorTexture.width, gmat.baseColorTexture.height,
			                              gmat.baseColorTexture.rgba));
			materials[i].albedoTexture = &textures.back();
		}
		else if (!gmat.baseColorTexturePath.empty())
		{
			if (auto tex = Texture::fromFile(dir + gmat.baseColorTexturePath))
			{
				textures.emplace_back(std::move(*tex));
				materials[i].albedoTexture = &textures.back();
			}
			else
			{
				debug::warnOnce("dx12.skinned.tex." + pathStr,
				                "skinned model のテクスチャが読めない: " + dir + gmat.baseColorTexturePath);
			}
		}
	}
	return materials;
}

/// @brief mesh を持つノード n の primitive を prim へ展開する
void appendSkinnedPrims(SkinnedModel& model, const GltfSceneData& scene, const std::vector<Material>& materials,
                        std::size_t n, const std::string& pathStr)
{
	const auto& node = model.nodes[n];
	if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= scene.meshes.size()) { return; }
	const GltfSkinData* skin = (node.skin >= 0 && static_cast<std::size_t>(node.skin) < model.skins.size())
		? &model.skins[static_cast<std::size_t>(node.skin)] : nullptr;
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
		sp.material = (prim.materialIndex >= 0 && static_cast<std::size_t>(prim.materialIndex) < materials.size())
			? materials[static_cast<std::size_t>(prim.materialIndex)] : Material::defaultMaterial();
		// copy (move しない): 複数ノードが同じ mesh を参照する glTF が有効なため
		const bool skinned = skin != nullptr && !prim.skin.empty();
		const bool paletteValid = skinned && prim.skin.size() == prim.vertices.size() &&
		                          !skin->inverseBindMatrices.empty() &&
		                          skin->inverseBindMatrices.size() == skin->joints.size();
		if (paletteValid && uploadSkinInputs(prim, sp))
		{
			sp.skinIndex = node.skin;
			sp.base = prim.vertices;
			sp.indices = prim.indices;
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

/// @brief compute の入力 (base 頂点と束縛) を DEFAULT heap へ上げる (読み込み時に 1 回)
[[nodiscard]] bool uploadSkinInputs(const GltfMeshPrimitive& prim, SkinnedPrim& sp)
{
	if (!ensureSkinningCompute()) { return false; }
	return uploadStaticBuffer(prim.vertices.data(), prim.vertices.size() * sizeof(Vertex3D), sp.gpuBase) &&
	       uploadStaticBuffer(prim.skin.data(), prim.skin.size() * sizeof(SkinVertexBinding), sp.gpuBinding);
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

/// @brief 読み込み時の一度きりの転送。copy 元は frame 完了まで m_frameTempResources が持つ
[[nodiscard]] bool uploadStaticBuffer(const void* data, std::size_t bytes, gfx::GpuResource& out)
{
	if (bytes == 0) { return false; }
	auto staging = createUploadBuffer(bytes);
	if (!staging ||
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COPY_DEST, out)))
	{
		return false;
	}
	uploadToBuffer(staging.Get(), data, static_cast<UINT>(bytes));
	m_graphicsCmdList->CopyBufferRegion(out.Get(), 0, staging.Get(), 0, bytes);
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = out.Get();
	b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
	m_frameTempResources.push_back(std::move(staging));
	++m_meshBufferCreates;
	return true;
}

/// @brief クリップ名を引く。空/不在は nullptr (= レストポーズ)。不在は warnOnce。
[[nodiscard]] const GltfAnimationClip* findSkinnedClip(
	const SkinnedModel& model, const char* path, const char* clipName) const
{
	if (clipName == nullptr || clipName[0] == '\0') { return nullptr; }
	for (const auto& clip : model.clips)
	{
		if (clip.name == clipName) { return &clip; }
	}
	debug::warnOnce(std::string("dx12.skinned.clip.") + path + "." + clipName,
	                std::string("clip が見つからない (レストポーズで継続): ") + clipName +
	                    " in " + path);
	return nullptr;
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

/// @brief instanceWorld 指定の描画本体
void drawSkinnedModelWorldImpl(const char* path, const sgc::Mat4f& instanceWorld,
                               const char* clipA, float timeA,
                               const char* clipB, float timeB, float blend01)
{
	if (path == nullptr) { return; }
	const int idx = ensureSkinnedModel(path);
	if (idx < 0) { return; }
	const auto& model = m_skinnedModels[static_cast<std::size_t>(idx)];

	// ポーズ合成: クリップ不在/未指定はレストポーズ (空クリップのサンプル)
	static const GltfAnimationClip kRestPose{};
	const auto* a = findSkinnedClip(model, path, clipA);
	auto pose = samplePose(model.nodes, a ? *a : kRestPose,
	                       a ? wrapTime(timeA, a->durationSec) : 0.0f);
	if (clipB != nullptr)
	{
		const auto* b = findSkinnedClip(model, path, clipB);
		pose = blendPoses(pose,
		                  samplePose(model.nodes, b ? *b : kRestPose,
		                             b ? wrapTime(timeB, b->durationSec) : 0.0f),
		                  blend01);
	}
	const auto world = computeWorldPose(model.nodes, pose);

	for (const auto& prim : model.prims)
	{
		if (prim.skinIndex < 0)
		{
			// 剛体 prim: ノード姿勢で描く (ボーンに付いた小物もアニメに追従する)
			const auto nodeWorld =
				(prim.nodeIndex >= 0 && static_cast<std::size_t>(prim.nodeIndex) < world.size())
					? world[static_cast<std::size_t>(prim.nodeIndex)]
					: sgc::Mat4f::identity();
			drawMesh(prim.mesh, instanceWorld * nodeWorld, prim.material);
			continue;
		}
		// Config の上限がプール物理サイズを超えて指定されても array の外へは出さない
		const uint32_t effectiveLimit = std::min(m_config.maxSkinnedDrawsPerFrame, kMaxSkinnedDrawsPerFrame);
		if (m_skinnedPoolCursor >= effectiveLimit)
		{
			debug::warnOnce("dx12.skinned.pool.full",
			                "スキン描画がフレーム上限 (" + std::to_string(effectiveLimit) +
			                    ") に達した — 以降は skip");
			continue;
		}
		// スキン prim はノード変換を無視する (glTF 仕様)。配置は instanceWorld のみ
		const auto& skin = model.skins[static_cast<std::size_t>(prim.skinIndex)];
		drawSkinnedPrim(prim, gatherJointWorld(world, skin), skin.inverseBindMatrices, instanceWorld);
	}
}

/// @brief スキン prim 1 個を pool slot へ変形して描く
void drawSkinnedPrim(const SkinnedPrim& prim, const std::vector<sgc::Mat4f>& jointWorld,
                     const std::vector<sgc::Mat4f>& inverseBind, const sgc::Mat4f& instanceWorld)
{
	const uint32_t slot = m_skinnedPoolCursor++;
	auto& pool = m_skinnedPool[slot];
	if (m_skinnedPoolPrim[slot] != static_cast<const void*>(&prim))
	{
		pool.setVertices(prim.base);
		pool.setIndices(prim.indices);
		m_skinnedPoolPrim[slot] = static_cast<const void*>(&prim);
	}
	const gfx::GpuResource* skinned = skinOnGpu(prim, jointWorld, inverseBind, slot);
	if (skinned == nullptr) { return; }
	pool.setLocalAABB(skinnedBounds(prim.bounds, jointWorld));
	bindGpuVertexBuffer(pool, *skinned);
	drawMesh(pool, instanceWorld, prim.material);
}

/// @brief palette を ring へ置き、slot の今フレーム側の出力バッファへ変形を記録する
/// @return 変形済み頂点のバッファ。確保失敗は nullptr
[[nodiscard]] const gfx::GpuResource* skinOnGpu(const SkinnedPrim& prim, const std::vector<sgc::Mat4f>& jointWorld,
                                                const std::vector<sgc::Mat4f>& inverseBind, uint32_t slot)
{
	m_skinPaletteScratch.resize(jointWorld.size());
	for (std::size_t j = 0; j < jointWorld.size(); ++j) { m_skinPaletteScratch[j] = jointWorld[j] * inverseBind[j]; }
	const auto palette = m_uploadRing.upload(m_skinPaletteScratch.data(),
	                                         m_skinPaletteScratch.size() * sizeof(sgc::Mat4f), 256);
	const gfx::GpuResource* out = skinTargetFor(slot, prim.base.size() * sizeof(Vertex3D));
	if (!palette.valid() || out == nullptr) { return nullptr; }

	dx12::SkinningDispatch d;
	d.palette = palette.gpuAddr;
	d.baseVertices = prim.gpuBase->GetGPUVirtualAddress();
	d.bindings = prim.gpuBinding->GetGPUVirtualAddress();
	d.out = out->Get();
	d.vertexCount = static_cast<uint32_t>(prim.base.size());
	d.jointCount = static_cast<uint32_t>(m_skinPaletteScratch.size());
	m_skinningCompute.encode(m_graphicsCmdList.Get(), d);
	return out;
}

/// @brief slot の今フレーム側の出力 (足りなければ作り直す)。古い方は in-flight が読み終えてから捨てる
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
/// @details revision を mesh に合わせるので acquireMeshBuffer は CPU 頂点を上げずにこれを返し、
///          次フレーム頭の shadow pass も同じ entry からこれを読む。
void bindGpuVertexBuffer(const Mesh& mesh, const gfx::GpuResource& buffer)
{
	auto& entry = m_meshVBCache[static_cast<const void*>(&mesh)];
	entry.resource = buffer;
	entry.size = static_cast<UINT>(mesh.vertexCount() * sizeof(Vertex3D));
	entry.revision = mesh.revision();
	entry.lastUsedFrame = m_frameCounter;
}
