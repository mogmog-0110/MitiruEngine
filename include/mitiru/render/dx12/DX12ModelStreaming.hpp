// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12ModelRelease.hpp の後に include)。資産の読み込みの流れ
//
// 重い前半 (ファイル、解析、BC 圧縮、DEFAULT heap への詰め込み、COPY キューでの写し) は AssetStreamer のワーカーで、
// registry への登録と COMMON からの遷移はこのスレッドで進める。読み終わるまで、そのモデルは描かない。
// 既定の「待つ」読み込みでは、描く前にその資産の前半を待つので、絵は読み込みの速さに左右されない (headless の
// 撮影・リプレイの照合・テスト)。窓のある実行は host が setAsyncLoads(true) にし、読み終えたフレームから描く。
// どちらの形でも、読み込みの結果は描画にだけ効き、GameMemory には届かない。

dx12::Dx12CopyQueue     m_copyQueue;       ///< m_streamer より先に作り、後に壊す (前半が提出するため)
bool                    m_copyQueueTried = false;
resource::AssetStreamer m_streamer;
bool                    m_asyncLoads = false;
double                  m_streamFinishBudgetMs = 2.0;
bool                    m_cmdListOpen = false;   ///< beginFrame の Reset から finalizeFrame の Close まで
std::vector<std::function<void(ID3D12GraphicsCommandList*)>> m_pendingListOps;
std::string             m_streamKey;
std::map<std::string, std::uint64_t, std::less<>> m_assetBytes;   ///< 読み込んだ資産の GPU の大きさ ("model:" / "world:" + path)
std::uint64_t           m_streamBudgetBytes = 0;
std::uint64_t           m_assetLoadFailures = 0;   ///< bytes から作れなかったモデル・world.json・region.json の延べ数

public:

/// @brief true にすると、読み終わっていない資産は描かずに先へ進む。false (既定) は描く前に読み終えるのを待つ
void setAsyncLoads(bool on) noexcept { m_asyncLoads = on; }
[[nodiscard]] bool asyncLoads() const noexcept { return m_asyncLoads; }

/// @brief 後半の 1 フレームの予算 (ms)。0 でも 1 件は進める
void setStreamFinishBudgetMs(double ms) noexcept { m_streamFinishBudgetMs = ms; }

/// @brief 頼んでまだ登録まで終えていない読み込みの数
[[nodiscard]] std::size_t pendingLoads() const noexcept { return m_streamer.pendingCount(); }

/// @brief 読み込みの集計。failed は前半が投げた数に、後半で作れなかった資産 (読めないモデルや world.json) の数を足したもの
[[nodiscard]] resource::AssetStreamerStats streamingStats() const noexcept
{
	resource::AssetStreamerStats s = m_streamer.stats();
	s.failed += m_assetLoadFailures;
	return s;
}

/// @brief 頼んだ読み込みを全部終えて登録する。headless の撮影はフレームの頭でこれを呼び、読み込みの速さを絵に出さない
/// @return 登録まで終えた数
std::size_t settleLoads() { return m_streamer.settleAll(); }

/// @brief 読み込んだ資産 (モデルと world.json) の GPU の大きさの合計がこれを越えている間、region の新しい区画を読まない。0 は無制限
void setStreamingBudgetBytes(std::uint64_t bytes) noexcept { m_streamBudgetBytes = bytes; }
[[nodiscard]] std::uint64_t streamingBudgetBytes() const noexcept { return m_streamBudgetBytes; }

/// @brief 読み込んだ資産の画像とバッファの大きさの合計 (頂点の cache と描画先は入らない)
[[nodiscard]] std::uint64_t residentAssetBytes() const noexcept
{
	std::uint64_t sum = 0;
	for (const auto& [key, bytes] : m_assetBytes) { sum += bytes; }
	return sum;
}

/// @brief 先に読む資産の種類。Auto は拡張子で決める (.glb / .gltf / .fbx / .vrm はスキンのモデル、.clod / .obj は世界のモデル)
enum class PreloadKind : std::uint8_t { Auto, Model, Clod };

/// @brief 描く前に読み込みを頼む。待つ読み込みでも、ここでは待たない (描くときか settleLoads で待つ)
/// @return 種類が分かり、頼んだか読み込み済みなら true
bool preloadAsset(const char* path, PreloadKind kind = PreloadKind::Auto)
{
	if (path == nullptr || path[0] == '\0') { return false; }
	const std::string_view p(path);
	if (kind == PreloadKind::Clod || (kind == PreloadKind::Auto && (hasExtension(p, ".clod") || hasExtension(p, ".obj"))))
	{
		requestClodModel(path);
		return m_clod.supported();
	}
	if (terrain::isOutdoorWorldPath(p)) { requestOutdoorWorld(path); return true; }
	if (terrain::isOutdoorRegionPath(p)) { return ensureOutdoorRegion(path) != nullptr; }
	if (hasExtension(p, ".efk") || hasExtension(p, ".efkefc")) { return preloadEffect(path); }
	const bool model = kind == PreloadKind::Model || hasExtension(p, ".glb") || hasExtension(p, ".gltf") ||
	                   hasExtension(p, ".vrm") || asset::isFbxPath(p);
	if (!model) { return false; }
	if (m_skinnedRegistry.find(p) != m_skinnedRegistry.end()) { return true; }
	const std::string& key = streamKey("model:", p);
	if (!m_streamer.isPending(key) && loadedModelCount() < kMaxSkinnedModels) { requestSkinnedModel(key, path); }
	return true;
}

/// @brief 読み込みを終えて描ける資産か。region.json は区画の表を読めたか
[[nodiscard]] bool assetReady(const char* path, PreloadKind kind = PreloadKind::Auto)
{
	if (path == nullptr) { return false; }
	const std::string_view p(path);
	if (kind == PreloadKind::Clod || (kind == PreloadKind::Auto && (hasExtension(p, ".clod") || hasExtension(p, ".obj"))))
	{
		return m_clod.modelIndex(p) >= 0;
	}
	if (terrain::isOutdoorWorldPath(p)) { return findOutdoorWorld(p) != nullptr; }
	if (hasExtension(p, ".efk") || hasExtension(p, ".efkefc")) { return preloadEffect(path); }
	if (terrain::isOutdoorRegionPath(p))
	{
		const auto it = m_outdoorRegions.find(p);
		return it != m_outdoorRegions.end() && it->second && !it->second->failed;
	}
	const auto it = m_skinnedRegistry.find(p);
	return it != m_skinnedRegistry.end() && it->second >= 0;
}

/// @brief 初めて使うフレームで作るパイプラインと資源 (空、体積フォグ、空気遠近、GTAO、内部解像度の拡大、屋外、スキニング)
///        を今作る。ロード画面の間に呼ぶと、ゲームの途中で初めて使うフレームの 10〜30 ms の山が消える
/// @return 作れた数
int prewarmPipelines()
{
	if (!m_initialized) { return 0; }
	int made = 0;
	made += ensureAtmosphere() ? 1 : 0;
	made += ensureAerialComposite() ? 1 : 0;
	made += ensureVolumetricFog() ? 1 : 0;
	made += ensureGtao() ? 1 : 0;
	made += ensureUpscalePipelines() ? 1 : 0;
	made += ensureWorldPipeline() ? 1 : 0;
	made += ensureSkinningCompute() ? 1 : 0;
	(void)streamingCopyQueue();
	return made;
}

/// @brief VRAM の使用量と OS の予算
[[nodiscard]] gfx::GpuMemoryBudget vramBudget() const { return gfx::gpuMemoryBudget(m_d3dDevice); }

/// @brief COPY キューへの提出の回数 (テストと計測用)
[[nodiscard]] std::uint64_t streamingCopySubmits() const { return m_copyQueue.submittedCount(); }

private:

/// @brief Effekseer は自分の装置で読むので、ワーカーへ出さずにこのスレッドで読む (ロード画面の間に呼ぶ)
[[nodiscard]] bool preloadEffect(const char* path)
{
#if defined(MITIRU_HAS_EFFEKSEER)
	return m_effekseer && m_effekseer->preload(path);
#else
	(void)path;
	return false;
#endif
}

[[nodiscard]] static bool hasExtension(std::string_view path, std::string_view ext) noexcept
{
	if (path.size() < ext.size()) { return false; }
	const std::string_view tail = path.substr(path.size() - ext.size());
	return std::equal(tail.begin(), tail.end(), ext.begin(), ext.end(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) == b;
	});
}

/// @brief 前半を頼む鍵。描くたびに引くので、確保を繰り返さない置き場で作る
[[nodiscard]] const std::string& streamKey(std::string_view kind, std::string_view path)
{
	m_streamKey.assign(kind);
	m_streamKey.append(path);
	return m_streamKey;
}

[[nodiscard]] dx12::Dx12CopyQueue* streamingCopyQueue()
{
	if (!m_copyQueueTried && m_d3dDevice != nullptr)
	{
		m_copyQueueTried = true;
		(void)vfs::hasGlobalMount();   // pack の mount をこのスレッドで済ませ、ワーカーが同時に開かないようにする
		if (!m_copyQueue.init(m_d3dDevice))
		{
			debug::warnOnce("dx12.stream.copyqueue", "COPY キューを作れない — 読み込みの転送は描画のリストで写す");
		}
	}
	return m_copyQueue.ready() ? &m_copyQueue : nullptr;
}

/// @brief 描画のリストに積む。リストが閉じていれば次の beginFrame の頭で積む
template <typename Fn>
void onGraphicsList(Fn&& fn)
{
	if (m_cmdListOpen && m_graphicsCmdList) { fn(m_graphicsCmdList.Get()); return; }
	m_pendingListOps.emplace_back(std::forward<Fn>(fn));
}

/// @brief beginFrame の Reset の直後に呼ぶ。待たせていた遷移を積み、終わった前半の後半を予算の分だけ進める
void beginFrameStreaming()
{
	m_cmdListOpen = true;
	collectRetiredOutdoorWorlds();
	for (auto& op : m_pendingListOps) { op(m_graphicsCmdList.Get()); }
	m_pendingListOps.clear();
	(void)m_streamer.pump(m_streamFinishBudgetMs);
}

/// @brief 壊す前に呼ぶ。走っている前半を待ち、後半は捨てる
void discardStreaming()
{
	m_streamer.cancelAll();
	(void)m_streamer.settleAll();
	m_pendingListOps.clear();
	m_copyQueue.shutdown();
	m_copyQueueTried = false;
	m_cmdListOpen = false;
}

/// @brief 写した資源を読む状態へ移す。COPY キューで写していなければ、ここで写してから移す。本体は COMMON で作ってある
template <typename Staged>
void adoptStagedResource(ID3D12GraphicsCommandList* list, bool copied, Staged& staged, ID3D12Resource* res,
                         D3D12_RESOURCE_STATES after)
{
	if (!copied)
	{
		dx12::detail::recordTransition(list, res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		dx12::recordStagedCopies(list, staged);
		dx12::detail::recordTransition(list, res, D3D12_RESOURCE_STATE_COPY_DEST, after);
		m_frameTempResources.push_back(std::move(staged.staging));
		return;
	}
	dx12::detail::recordTransition(list, res, D3D12_RESOURCE_STATE_COMMON, after);
	staged.staging.Reset();
}

void noteAssetBytes(std::string_view kind, std::string_view path, std::uint64_t bytes)
{
	m_assetBytes[std::string(kind).append(path)] = bytes;
}

void forgetAssetBytes(std::string_view kind, std::string_view path)
{
	if (const auto it = m_assetBytes.find(streamKey(kind, path)); it != m_assetBytes.end()) { m_assetBytes.erase(it); }
}

[[nodiscard]] bool overStreamingBudget() const noexcept
{
	return m_streamBudgetBytes > 0 && residentAssetBytes() >= m_streamBudgetBytes;
}

[[nodiscard]] int failSkinnedModel(const char* path, const std::string& why)
{
	debug::warnOnce(std::string("dx12.skinned.load.") + path,
	                std::string("skinned model のロードに失敗: ") + path + " (" + why + ")");
	m_skinnedRegistry.emplace(path, -1);
	++m_assetLoadFailures;
	return -1;
}

/// @brief まだ registry に無いモデルを頼む。待つ読み込みならその場で登録まで終える
/// @return モデル index。読み込み中と失敗は -1
[[nodiscard]] int streamSkinnedModel(const char* path)
{
	const std::string& key = streamKey("model:", path);
	if (!m_streamer.isPending(key))
	{
		if (loadedModelCount() >= kMaxSkinnedModels) { return failSkinnedModel(path, "モデル数上限"); }
		requestSkinnedModel(key, path);
	}
	if (!m_asyncLoads) { (void)m_streamer.settle(key); }
	const auto it = m_skinnedRegistry.find(path);
	return it != m_skinnedRegistry.end() ? it->second : -1;
}

void requestSkinnedModel(const std::string& key, const char* path)
{
	ID3D12Device* device = m_d3dDevice;
	dx12::Dx12CopyQueue* copy = streamingCopyQueue();
	const std::uint32_t joints = m_config.maxSkinJoints;
	(void)m_streamer.request(key, [this, device, copy, joints, p = std::string(path)]() -> resource::AssetFinish {
		auto prepared = std::make_shared<dx12::PreparedSkinnedModel>(dx12::prepareSkinnedModel(p, device, copy, joints));
		return [this, p, prepared] { finishSkinnedModel(p, *prepared); };
	});
}

/// @brief 後半。画像と compute の入力を読む状態へ移し、prim を組んで registry へ入れる
void finishSkinnedModel(const std::string& path, dx12::PreparedSkinnedModel& p)
{
	if (!p.error.empty()) { (void)failSkinnedModel(path.c_str(), p.error); return; }
	if (loadedModelCount() >= kMaxSkinnedModels) { (void)failSkinnedModel(path.c_str(), "モデル数上限"); return; }
	SkinnedModel model;
	model.anim = std::move(p.anim);
	const auto materials = adoptSkinnedMaterials(p, model.textures);
	for (std::size_t n = 0; n < model.anim.nodes.size(); ++n)
	{
		appendSkinnedPrims(model, p.scene, materials, n, path, p.skinPrims);
	}
	if (model.prims.empty()) { (void)failSkinnedModel(path.c_str(), "描ける prim が無い"); return; }
	animation::evaluatePose(model.anim, animation::AnimPoseParams{}, m_skinnedPose);
	model.restModel = m_skinnedPose.model;
	measureSkinnedBounds(model);
	m_skinnedRegistry.emplace(path, placeSkinnedModel(std::move(model)));
	noteAssetBytes("model:", path, p.gpuBytes);
}

/// @brief 材質の描画の指定とマップ。画像は COPY キューで写したものを受け取り、読む状態への遷移を積む
[[nodiscard]] std::vector<SkinnedMaterial> adoptSkinnedMaterials(dx12::PreparedSkinnedModel& p,
                                                                 std::deque<dx12::Dx12Texture2D>& textures)
{
	std::vector<SkinnedMaterial> materials(p.scene.materials.size());
	for (std::size_t i = 0; i < p.scene.materials.size(); ++i)
	{
		const auto& gmat = p.scene.materials[i];
		SkinnedMaterial& out = materials[i];
		out.material = convertGltfMaterial(gmat);
		MaterialMaps& maps = out.maps;
		auto& staged = p.textures[i].maps;
		maps.albedo = adoptModelTexture(p.copied, staged[0], textures);
		maps.normal = adoptModelTexture(p.copied, staged[1], textures);
		maps.metallicRoughness = adoptModelTexture(p.copied, staged[2], textures);
		maps.emissive = adoptModelTexture(p.copied, staged[3], textures);
		maps.baseColor[0] = gmat.baseColor.r;
		maps.baseColor[1] = gmat.baseColor.g;
		maps.baseColor[2] = gmat.baseColor.b;
		maps.baseColor[3] = gmat.baseColor.a;
		maps.hasBaseColor = true;
		for (int k = 0; k < 3; ++k) { maps.emissiveFactor[k] = gmat.emissiveFactor[k]; }
		maps.normalScale = gmat.normalScale;
		maps.occlusionStrength = gmat.occlusionStrength;
	}
	return materials;
}

[[nodiscard]] const dx12::Dx12Texture2D* adoptModelTexture(bool copied, dx12::StagedTexture& staged,
                                                           std::deque<dx12::Dx12Texture2D>& textures)
{
	if (!staged.valid()) { return nullptr; }
	auto holder = std::make_shared<dx12::StagedTexture>(std::move(staged));
	onGraphicsList([this, copied, holder](ID3D12GraphicsCommandList* list) {
		adoptStagedResource(list, copied, *holder, holder->texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	});
	dx12::StagedTexture view;
	view.texture = holder->texture;
	view.layouts = holder->layouts;
	view.width = holder->width;
	view.height = holder->height;
	view.format = holder->format;
	textures.emplace_back();
	textures.back().adoptStaged(std::move(view));
	return &textures.back();
}

/// @brief clod の世界のモデル (drawModel) を頼む。待つ読み込みならその場で連結シーンへ足す
/// @return 登録済みなら true (失敗の負キャッシュも true で、queueInstance が描かずに返す)
[[nodiscard]] bool streamClodModel(const char* path)
{
	if (path == nullptr || !m_clod.supported() || m_clod.knowsModel(path)) { return true; }
	requestClodModel(path);
	if (!m_asyncLoads) { (void)m_streamer.settle(streamKey("clod:", path)); }
	return m_clod.knowsModel(path);
}

/// @brief 変換と読み込みはワーカーで、連結シーンへの追加はこのスレッドでする
void requestClodModel(const char* path)
{
	if (!m_clod.supported() || m_clod.knowsModel(path)) { return; }
	const std::string& key = streamKey("clod:", path);
	if (m_streamer.isPending(key)) { return; }
	(void)m_streamer.request(key, [this, p = std::string(path)]() -> resource::AssetFinish {
		auto err = std::make_shared<std::string>();
		auto blob = std::make_shared<std::optional<std::vector<std::uint8_t>>>(clod::ClodRenderer::readModelBlob(p, *err));
		return [this, p, blob, err] { (void)m_clod.addModel(p, *blob, *err); };
	});
}
