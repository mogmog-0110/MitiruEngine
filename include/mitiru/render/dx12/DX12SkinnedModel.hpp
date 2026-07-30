#pragma once

/// @file DX12SkinnedModel.hpp
/// @brief Renderer3D_DX12 のスキンアニメ付き glTF モデル描画実装 (部分ヘッダ、.inl)。
/// @details Renderer3D_DX12 のクラス内部から include される (DX12Splat.hpp と同じ流儀)。
///          責務: glTF/glb の forward 用 registry (path → model、負キャッシュ) /
///          クリップサンプル → CPU スキニング → 既存 drawMesh への合流 (ADR 0028)。
///          clod (drawModel) とは別 registry — clod=静的世界、こちら=動的キャラ。
///          ポーズは (clip, time) の純関数で、時間はゲーム側 (GameMemory) が所有する。
///          将来 compute スキニングへ差し替える時は skinVertices+setVertices の区間を
///          palette アップロード + dispatch に置き換えるだけでよい (境界 API 不変)。

/// @brief 同時ロードできるスキンモデル数の上限
static constexpr int kMaxSkinnedModels = 32;
/// @brief 1 フレームに描けるスキン prim 数の上限 (pool サイズ)
static constexpr uint32_t kMaxSkinnedDrawsPerFrame = 64;
/// @brief 1 スキンの joint 数上限 (超過は剛体 fallback)
static constexpr uint32_t kMaxSkinJoints = 256;

/// @brief ロード済みモデルの 1 プリミティブ
struct SkinnedPrim
{
	std::vector<Vertex3D> base;              ///< バインドポーズ頂点 (スキン prim のみ)
	std::vector<uint32_t> indices;           ///< インデックス (スキン prim のみ)
	std::vector<SkinVertexBinding> binding;  ///< 頂点束縛 (空 = 剛体 prim)
	int skinIndex = -1;                      ///< skins への index (-1 = 剛体)
	int nodeIndex = -1;                      ///< 剛体 prim の姿勢に使うノード
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

std::map<std::string, int, std::less<>> m_skinnedRegistry;  ///< path → index (-1 = 負キャッシュ)
std::deque<SkinnedModel> m_skinnedModels;                   ///< 要素アドレス安定 (deque)
std::array<Mesh, kMaxSkinnedDrawsPerFrame> m_skinnedPool;   ///< スキン結果の書き込み先 (アドレス安定)
std::array<const void*, kMaxSkinnedDrawsPerFrame> m_skinnedPoolPrim{};  ///< slot が最後に持った prim
uint32_t m_skinnedPoolCursor = 0;                           ///< beginFrame で 0 リセット
uint64_t m_meshBufferCreates = 0;                           ///< VB/IB committed resource 生成回数 (計測用)

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

	// マテリアル → Material (albedo は埋め込み優先、外部 URI はモデルディレクトリ相対)
	const std::string pathStr(path);
	const auto dirEnd = pathStr.find_last_of("/\\");
	const std::string dir = (dirEnd == std::string::npos) ? "" : pathStr.substr(0, dirEnd + 1);
	std::vector<Material> materials(scene->materials.size());
	for (std::size_t i = 0; i < scene->materials.size(); ++i)
	{
		const auto& gmat = scene->materials[i];
		materials[i] = convertGltfMaterial(gmat);
		if (gmat.baseColorTexture.valid())
		{
			model.textures.emplace_back(
				Texture(gmat.baseColorTexture.width, gmat.baseColorTexture.height,
			            gmat.baseColorTexture.rgba));
			materials[i].albedoTexture = &model.textures.back();
		}
		else if (!gmat.baseColorTexturePath.empty())
		{
			if (auto tex = Texture::fromFile(dir + gmat.baseColorTexturePath))
			{
				model.textures.emplace_back(std::move(*tex));
				materials[i].albedoTexture = &model.textures.back();
			}
			else
			{
				debug::warnOnce("dx12.skinned.tex." + pathStr,
				                "skinned model のテクスチャが読めない: " + dir + gmat.baseColorTexturePath);
			}
		}
	}

	// mesh を持つノードを prim へ展開する
	for (std::size_t n = 0; n < model.nodes.size(); ++n)
	{
		const auto& node = model.nodes[n];
		if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= scene->meshes.size())
		{
			continue;
		}
		const bool skinValid =
			node.skin >= 0 && static_cast<std::size_t>(node.skin) < model.skins.size();
		for (auto& prim : scene->meshes[static_cast<std::size_t>(node.mesh)].primitives)
		{
			SkinnedPrim sp;
			sp.nodeIndex = static_cast<int>(n);
			sp.material =
				(prim.materialIndex >= 0 &&
			     static_cast<std::size_t>(prim.materialIndex) < materials.size())
					? materials[static_cast<std::size_t>(prim.materialIndex)]
					: Material::defaultMaterial();

			bool skinned = skinValid && !prim.skin.empty();
			if (skinned &&
			    model.skins[static_cast<std::size_t>(node.skin)].joints.size() > kMaxSkinJoints)
			{
				debug::warnOnce("dx12.skinned.joints." + pathStr,
				                "joint 数が上限を超過 — 剛体で描画: " + pathStr);
				skinned = false;
			}
			// copy (move しない): 複数ノードが同じ mesh を参照する glTF が有効なため
			if (skinned)
			{
				sp.skinIndex = node.skin;
				sp.base = prim.vertices;
				sp.binding = prim.skin;
				sp.indices = prim.indices;
			}
			else
			{
				sp.mesh.setVertices(prim.vertices);
				sp.mesh.setIndices(prim.indices);
			}
			model.prims.push_back(std::move(sp));
		}
	}
	if (model.prims.empty()) { return fail("描ける prim が無い"); }

	m_skinnedModels.push_back(std::move(model));
	const int idx = static_cast<int>(m_skinnedModels.size()) - 1;
	m_skinnedRegistry.emplace(path, idx);
	return idx;
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

	constexpr float kDeg2Rad = 0.017453292519943295f;
	const auto instanceWorld = sgc::Mat4f::translation(position) *
	                           sgc::Mat4f::rotationY(rotYDeg * kDeg2Rad) *
	                           sgc::Mat4f::scaling(scale);

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
		if (m_skinnedPoolCursor >= kMaxSkinnedDrawsPerFrame)
		{
			debug::warnOnce("dx12.skinned.pool.full",
			                "スキン描画がフレーム上限に達した — 以降は skip");
			continue;
		}
		// スキン prim はノード変換を無視する (glTF 仕様) — 配置は instanceWorld のみ
		const auto& skin = model.skins[static_cast<std::size_t>(prim.skinIndex)];
		const auto jointWorld = gatherJointWorld(world, skin);
		auto& pool = m_skinnedPool[m_skinnedPoolCursor];
		pool.setVertices(skinVertices(prim.base, prim.binding, skin.inverseBindMatrices,
		                              jointWorld));
		if (m_skinnedPoolPrim[m_skinnedPoolCursor] != static_cast<const void*>(&prim))
		{
			pool.setIndices(prim.indices);
			m_skinnedPoolPrim[m_skinnedPoolCursor] = static_cast<const void*>(&prim);
		}
		++m_skinnedPoolCursor;
		drawMesh(pool, instanceWorld, prim.material);
	}
}
