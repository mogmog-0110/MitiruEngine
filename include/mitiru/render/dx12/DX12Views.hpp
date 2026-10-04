// Renderer3D_DX12 の副ビューの宣言。分割画面、小窓、材質に使う描画結果を扱う。
// 副ビューの描画は beginView から endView まで、そのビューにだけ積む。
// メッシュ、インスタンス、glTF、スキンモデル、屋外はその場で描き、局所光、デカール、半透明、水面、粒、剣筋、Effekseer は endFrame で仕上げる。
// 局所光とデカールの froxel は副ビューのカメラで作る。
// 仕上げは空、水面、半透明、Effekseer、剣筋、粒の順に描き、SSAO、resolve、bloom、tonemap、TAA か FXAA を通して LDR テクスチャに残す。
// 影は副ビューごとに自分のカメラへ合わせた影マップを持つ (DX12ViewShadows.hpp)。caster は全てのビューで描いた物の和を使う。
// TAA と SSAO も副ビューごとの履歴と遮蔽で掛ける (DX12ViewTemporal.hpp)。空を張るビューは空気遠近と体積フォグの froxel も
// 自分で持つ (DX12ViewAtmosphere.hpp)。輪郭線、動きのぼけ、FSR は主ビューだけ。
// clod の世界、CSG、スプラットは主ビューだけに描く。

public:

/// @brief 副ビューの設定。大きさは描画する画素数で、出力先は compositeView で決める
struct View3DDesc
{
	int width = 0;
	int height = 0;
	bool shadows = true;   ///< このビューのカメラに合わせた影マップで影を受け、描いた物を影の caster に入れるか
	int shadowMapSize = 0; ///< 影マップの一辺。0 なら主ビューと同じ。256〜4096 の 2 の冪へ丸める
	bool skybox = true;    ///< setSkybox と sceneLook の空を張るか
	sgc::Colorf clearColor{0.0f, 0.0f, 0.0f, 1.0f};   ///< 書いた sRGB
	bool bloom = true;     ///< 主ビューで bloom が有効なら同じ設定で掛ける
	bool antiAlias = true; ///< 主ビューが TAA ならこのビューの履歴で TAA を、FXAA なら FXAA を掛ける
	bool temporal = true;  ///< false なら主ビューが TAA でもこのビューは FXAA にする (antiAlias が true の時)
	bool ambientOcclusion = true;   ///< 主ビューで環境遮蔽が有効なら SSAO を掛ける (主ビューが GTAO でも SSAO)
};

/// @brief 副ビューを作る。フレームの内外で呼べるが、beginView から endView までは呼べない
/// @return 1 以上の番号。作れなければ 0。手放した番号は使い回さない
int createView(const View3DDesc& desc)
{
	if (!m_initialized || m_activeView != nullptr || desc.width <= 0 || desc.height <= 0 ||
	    desc.width > kMaxViewExtent || desc.height > kMaxViewExtent)
	{
		return 0;
	}
	auto view = std::make_unique<View3D>();
	view->desc = desc;
	view->viewportWidth = static_cast<float>(desc.width);
	view->viewportHeight = static_cast<float>(desc.height);
	if (!createViewTargets(*view) || !createViewFrameBuffers(*view))
	{
		debug::verboseOnce("dx12.view.create", "副ビューの描画先を作れなかったので、この副ビューは使えません。");
		return 0;
	}
	ensureViewCompositePipeline();
	return placeView(std::move(view));
}

/// @brief 副ビューを手放す。描画先は GPU が読み終えてから消す。viewTexture で得たテクスチャも以後は使えない
void releaseView(int id)
{
	View3D* v = viewAt(id);
	if (v == nullptr || v == m_activeView) { return; }
	std::erase_if(m_viewComposites, [id](const ViewComposite& c) { return c.id == id; });
	std::erase(m_viewsThisFrame, v->pass);
	if (m_releasedPassFrame != m_frameCounter)
	{
		m_releasedPassFrame = m_frameCounter;
		m_releasedPasses = 0;
	}
	m_releasedPasses |= 1u << v->pass;
	const std::size_t slot = viewSlot(id);
	m_viewGraveyard.push_back({std::move(m_views[slot]), m_frameCounter});
	m_viewGenerations[slot] = (m_viewGenerations[slot] + 1) & kViewGenerationMask;
}

/// @brief 以後の描画を副ビュー id へ向ける。主ビューの beginFrame の後、endFrame の前に呼ぶ。同じフレームで再び始めると、消さずに続きを描き、最後のカメラを使う
/// @return 描けるなら true。false の間の描画は主ビューへ入る
bool beginView(int id, const Camera3D& camera)
{
	View3D* v = viewAt(id);
	if (v == nullptr || !m_frameActive || !m_graphicsCmdList || m_activeView != nullptr) { return false; }
	const bool first = v->frame.frame != m_frameCounter;
	v->camera = camera;
	if (first && !prepareViewFrame(*v)) { return false; }
	if (first) { prepareViewEffects(*v); }
	saveMainViewState();
	enterView(*v);
	if (first && v->frame.shadows) { renderViewShadowPass(*v); }
	bindViewTargets(*v, first);
	m_skyboxDrawnThisFrame = !v->desc.skybox || v->frame.skyDrawn;
	if (!v->frame.shadows) { m_shadowEnabled = false; }
	restoreMainState();
	return true;
}

/// @brief 描画先を主ビューへ戻す。副ビューの resolve と tonemap は endFrame で行う
void endView()
{
	View3D* v = m_activeView;
	if (v == nullptr) { return; }
	v->frame.skyDrawn = m_skyboxDrawnThisFrame;
	leaveView(*v);
	restoreMainViewState();
	bindMainTargets();
	restoreMainState();
}

/// @brief 副ビューの出力。sRGB の色で、MaterialMaps::albedo に渡せる。endFrame より前は前のフレームの絵になる
[[nodiscard]] const dx12::Dx12Texture2D* viewTexture(int id) const noexcept
{
	const View3D* v = viewAt(id);
	return (v != nullptr && v->texture.isReady()) ? &v->texture : nullptr;
}

/// @brief 後処理の後、HUD の前に、副ビューの出力を画面上の矩形へ貼る。座標と大きさは画素単位
void compositeView(int id, float x, float y, float width, float height)
{
	if (viewAt(id) == nullptr || !m_frameActive || !(width > 0.0f) || !(height > 0.0f)) { return; }
	m_viewComposites.push_back({id, x, y, width, height});
}

/// @brief 副ビュー viewId の出力を基本色に使ってメッシュを描く。材質の色はテクスチャに掛ける。出力には前のフレームの絵を使う。描画先が同じ副ビューか、まだ出力がなければ描かない
void drawMeshWithView(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, int viewId)
{
	const View3D* v = viewAt(viewId);
	if (v == nullptr || v == m_activeView || !v->hasOutput || !v->texture.isReady()) { return; }
	MaterialMaps maps;
	maps.albedo = &v->texture;
	drawMeshEx(mesh, world, material, &maps, DrawTint{});
}

[[nodiscard]] bool viewActive() const noexcept { return m_activeView != nullptr; }

/// @brief 副ビューの出力を RGBA8 の sRGB 値で読み戻す。診断とテスト用で、GPU の完了を待つためフレームの外で呼ぶ
[[nodiscard]] std::vector<std::uint8_t> readViewPixels(int id)
{
	const View3D* v = viewAt(id);
	if (v == nullptr || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	return gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), v->ldr.Get(),
	                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 4);
}

/// @brief 直前のフレームで副ビュー id に割り当てた局所光の数。テストと計測に使う
[[nodiscard]] int viewVisibleLocalLightCount(int id) const noexcept
{
	const View3D* v = viewAt(id);
	return v != nullptr ? v->frame.visibleLights : 0;
}

private:

static constexpr int kMaxViewExtent = 8192;
/// pass は主ビューが 0、副ビューが 1 から kMaxViews。粒と Effekseer は描画先を pass の bit 集合で持つ
static constexpr int kMaxViews = 15;
static constexpr int kViewPassCount = kMaxViews + 1;
/// SRV の表は、0 から 2 が tonemap 用の HDR、遮蔽、bloom、3 から 5 が貼り付け用の LDR と空き 2 個、6 が FXAA と TAA の読む tonemap 結果
static constexpr UINT kViewSrvCount = 7;
static constexpr UINT kViewSrvFxaaSource = 6;

/// 最初の beginView でこのフレームの ring から確保し、endFrame で書き込む。描画はこの場所を参照して積む
struct ViewFrame
{
	std::uint64_t frame = 0;   ///< 取ったフレーム (m_frameCounter)。違えばまだこのフレームに描いていない
	dx12::UploadAllocation cluster;
	dx12::UploadAllocation lights;
	dx12::UploadAllocation decals;
	dx12::UploadAllocation decalMask;
	D3D12_GPU_DESCRIPTOR_HANDLE sceneTable{};
	D3D12_GPU_VIRTUAL_ADDRESS shadowCb = 0;   ///< 影のパスで決めた CbShadow。ビューの描画はすべてこれを読む
	bool skyDrawn = false;
	bool shadows = false;   ///< このフレームに自分の影マップを焼いたか
	bool taa = false;       ///< このフレームに TAA を掛けるか (動きを記録し、履歴の資源を入れ替える)
	bool ao = false;        ///< このフレームに SSAO を掛けるか
	bool atmo = false;      ///< このフレームに空気遠近か体積フォグを、ビューの資源で描くか
	int visibleLights = 0;
};

/// 空気遠近と体積フォグの froxel、その記述子の組。並びは主ビューの m_atmoHeap と同じ。空の LUT は主ビューのものを読む
struct ViewAtmosphere
{
	ComPtr<ID3D12DescriptorHeap> heap;
	gfx::GpuResource aerial, fogInject[2], fogIntegrated;
	UINT fogDims[3] = {};
	int fogWrite = 0;
	bool fogHistoryValid = false;
	std::uint32_t fogFrameIndex = 0;
	glm::mat4 fogPrevViewProj{1.0f};
	sgc::Vec3f fogPrevCamera{};
	std::uint64_t lastFrame = 0;   ///< 最後に描いたフレーム。続いていなければフォグの履歴を捨てる
	std::array<const ID3D12Resource*, 7> written{};   ///< 記述子が指している資源。変わった時だけ書き直す
	bool used = false;             ///< 記述子を GPU が読んだことがあるか (書き直す前に GPU を待つ)
	D3D12_GPU_VIRTUAL_ADDRESS frameCb = 0, fogCb = 0;   ///< このフレームの空と霧の CB。resolve の後の合成が読む
};

/// TAA の履歴と動きベクトル。主ビューの同名の資源と同じ形式で、ビューの大きさに作る
struct ViewTemporal
{
	gfx::GpuResource velocityObject, velocityDepth, velocity, history[2];
	ComPtr<ID3D12DescriptorHeap> rtv, dsv, srv;
	FrameMotionHistory<MotionDraw> motion;
	JitterOffset jitter{};
	glm::mat4 viewProj{1.0f};       ///< このフレームのずらし抜きの viewProj
	glm::mat4 prevViewProj{1.0f};
	std::uint64_t lastFrame = 0;    ///< 最後に描いたフレーム。続いていなければ履歴を捨てる
	std::uint32_t frameIndex = 0;   ///< 履歴を作り直してからのフレーム数 = ずらしの相
	int historyWrite = 0;
	bool prevValid = false;
	bool historyValid = false;
};

/// SSAO の ping-pong。記述子の組の並びは主ビューの createSsaoResources と同じ
struct ViewSsao
{
	gfx::GpuResource tex[2];
	ComPtr<ID3D12DescriptorHeap> rtv, srv;
};

struct View3D
{
	View3DDesc desc;
	int pass = 0;   ///< 1..kMaxViews (m_views の添字 + 1)
	gfx::GpuResource color, normal, depth, hdr, ldr, fxaaSource;
	ComPtr<ID3D12DescriptorHeap> colorRtv, normalRtv, dsv, ldrRtv, fxaaRtv, srv;
	dx12::Dx12Texture2D texture;   ///< ldr を _UNORM_SRGB で読む (材質の色)
	bool hasOutput = false;        ///< 1 度でも仕上げたか
	// 以下は enterView と leaveView で主ビューのものと入れ替える。副ビューの間は主ビューのものを保持する
	gfx::GpuResource clusterMasks, decalMasks, waterCopy;
	UINT waterCopyWidth = 0, waterCopyHeight = 0;
	float viewportWidth = 0.0f, viewportHeight = 0.0f;
	ViewFrame frame;
	Camera3D camera;
	std::vector<LocalLight> lights;
	std::vector<DecalDesc> decals;
	std::vector<LocalLightGpu> visible;   ///< 割り当てに使った光 (選別・並べ替え後)
	std::unique_ptr<dx12::WeightedBlendedOIT> oit;   ///< 半透明と粒を初めて重ねる時に作る
	BloomChain bloom;                                ///< bloom を初めて掛ける時に作る
	// 影マップと行列の計算は、影を初めて焼く時に作る。enterView で主ビューのものと入れ替える
	dx12::Dx12ShadowMap shadowNear, shadowFar;
	DirectionalShadow shadow;
	std::unique_ptr<ViewTemporal> temporal;   ///< TAA を初めて掛ける時に作る
	std::unique_ptr<ViewSsao> ssao;           ///< SSAO を初めて掛ける時に作る
	std::unique_ptr<ViewAtmosphere> atmo;     ///< 空か体積フォグを初めて描く時に作る
	ComPtr<ID3D12DescriptorHeap> hdrRtv;      ///< resolve 後の HDR へ空気遠近とフォグを合成する
};

struct ViewComposite
{
	int id = 0;
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	bool normalized = false;   ///< x, y, w, h が出力の幅と高さに対する割合 (ゲーム DLL の slot から)
};

struct RetiredView
{
	std::unique_ptr<View3D> view;
	std::uint64_t frame = 0;
};

struct SavedMainView
{
	glm::mat4 view{1.0f};
	glm::mat4 proj{1.0f};
	glm::mat4 projNoJitter{1.0f};
	glm::mat4 viewProjNoJitter{1.0f};
	sgc::Vec3f eye{};
	Frustum frustum;
	Camera3D camera;
	bool skyDrawn = false;
	bool shadowEnabled = false;
};

std::vector<std::unique_ptr<View3D>> m_views;   ///< 空きは nullptr
std::vector<std::uint32_t> m_viewGenerations;   ///< 添字ごとに、手放すたびに 1 進める
static constexpr std::uint32_t kViewGenerationMask = 0x7FFF;
std::vector<RetiredView> m_viewGraveyard;
std::vector<ViewComposite> m_viewComposites;
std::vector<int> m_viewsThisFrame;   ///< このフレームに begin した副ビューの pass (最初に begin した順)
View3D* m_activeView = nullptr;
SavedMainView m_savedMainView;
std::optional<gfx::Dx12Shader> m_viewCompositePS;
ComPtr<ID3D12PipelineState> m_viewCompositePSO;

[[nodiscard]] int currentPass() const noexcept { return m_activeView != nullptr ? m_activeView->pass : 0; }

/// 番号は下位 16 bit が添字 + 1、上位が世代。手放した番号から新しい副ビューを操作できないようにする
[[nodiscard]] int viewId(std::size_t slot) const noexcept
{
	return static_cast<int>((m_viewGenerations[slot] << 16) | static_cast<std::uint32_t>(slot + 1));
}

[[nodiscard]] static std::size_t viewSlot(int id) noexcept
{
	return static_cast<std::size_t>((static_cast<std::uint32_t>(id) & 0xFFFFu) - 1u);
}

[[nodiscard]] View3D* viewAt(int id) const noexcept
{
	if (id <= 0 || (id & 0xFFFF) == 0) { return nullptr; }
	const std::size_t slot = viewSlot(id);
	if (slot >= m_views.size() || (static_cast<std::uint32_t>(id) >> 16) != m_viewGenerations[slot]) { return nullptr; }
	return m_views[slot].get();
}

[[nodiscard]] View3D* viewOfPass(int pass) const noexcept
{
	const auto i = static_cast<std::size_t>(pass - 1);
	return (pass > 0 && i < m_views.size()) ? m_views[i].get() : nullptr;
}

/// 半透明・水面・剣筋・粒・Effekseer の列は pass で行き先を持つ。同じフレームに手放した pass を新しい副ビューへ渡すと、
/// 手放した副ビューが積んだものが新しい副ビューに出るので、そのフレームの間は使わない
std::uint32_t m_releasedPasses = 0;
std::uint64_t m_releasedPassFrame = 0;

[[nodiscard]] bool passReleasedThisFrame(int pass) const noexcept
{
	return m_releasedPassFrame == m_frameCounter && (m_releasedPasses & (1u << pass)) != 0;
}

/// @brief 空いた添字へ置く。添字が pass になるため、kMaxViews 個までに限る
[[nodiscard]] int placeView(std::unique_ptr<View3D> view)
{
	for (std::size_t i = 0; i < static_cast<std::size_t>(kMaxViews); ++i)
	{
		if (i == m_views.size())
		{
			m_views.push_back(nullptr);
			m_viewGenerations.push_back(0);
		}
		if (!m_views[i] && !passReleasedThisFrame(static_cast<int>(i) + 1))
		{
			view->pass = static_cast<int>(i) + 1;
			m_views[i] = std::move(view);
			return viewId(i);
		}
	}
	debug::verboseOnce("dx12.view.cap", "副ビューは同時に " + std::to_string(kMaxViews) + " 枚までなので、新しい副ビューは作りません。");
	return 0;
}

/// @brief 副ビューでは描けない clod、CSG、スプラットを捨てる
[[nodiscard]] bool rejectInView(const char* what)
{
	if (m_activeView == nullptr) { return false; }
	debug::warnOnce(std::string("dx12.view.unsupported.") + what,
	                std::string(what) + " は副ビューでは描けないので、描きません。beginView3D と endView3D の外で描いてください。");
	return true;
}

void saveMainViewState()
{
	m_savedMainView.view = m_viewMatrix;
	m_savedMainView.proj = m_projMatrix;
	m_savedMainView.projNoJitter = m_projNoJitter;
	m_savedMainView.viewProjNoJitter = m_viewProjNoJitter;
	m_savedMainView.eye = m_cameraPosition;
	m_savedMainView.frustum = m_frustum;
	m_savedMainView.camera = m_clodCamera;
	m_savedMainView.skyDrawn = m_skyboxDrawnThisFrame;
	m_savedMainView.shadowEnabled = m_shadowEnabled;
}

void restoreMainViewState()
{
	m_viewMatrix = m_savedMainView.view;
	m_projMatrix = m_savedMainView.proj;
	m_projNoJitter = m_savedMainView.projNoJitter;
	m_viewProjNoJitter = m_savedMainView.viewProjNoJitter;
	m_cameraPosition = m_savedMainView.eye;
	m_frustum = m_savedMainView.frustum;
	m_clodCamera = m_savedMainView.camera;
	m_skyboxDrawnThisFrame = m_savedMainView.skyDrawn;
	m_shadowEnabled = m_savedMainView.shadowEnabled;
}

/// @brief 描画処理が主ビュー用の名前から描画先、froxel、ring を参照するため、副ビューの間は中身を入れ替える。入るときと出るときに同じ入れ替えを行う
void swapViewResources(View3D& v)
{
	std::swap(m_msaaColorRtvHeap, v.colorRtv);
	std::swap(m_normalRTVHeap, v.normalRtv);
	std::swap(m_dsvHeap, v.dsv);
	std::swap(m_msaaColorBuffer, v.color);
	std::swap(m_normalBuffer, v.normal);
	std::swap(m_depthBuffer, v.depth);
	std::swap(m_clusterMasks, v.clusterMasks);
	std::swap(m_decalMasks, v.decalMasks);
	std::swap(m_waterColorCopy, v.waterCopy);
	std::swap(m_waterCopyWidth, v.waterCopyWidth);
	std::swap(m_waterCopyHeight, v.waterCopyHeight);
	std::swap(m_config.viewportWidth, v.viewportWidth);
	std::swap(m_config.viewportHeight, v.viewportHeight);
	std::swap(m_clusterFrameAlloc, v.frame.cluster);
	std::swap(m_lightBufferAlloc, v.frame.lights);
	std::swap(m_decalBufferAlloc, v.frame.decals);
	std::swap(m_decalMaskAlloc, v.frame.decalMask);
	std::swap(m_sceneTableGpu, v.frame.sceneTable);
	if (v.frame.shadows) { swapViewShadows(v); }
	if (v.frame.taa) { swapViewTemporal(*v.temporal); }
	if (v.frame.ao) { swapViewSsao(*v.ssao); }
	if (v.frame.atmo) { swapViewAtmosphere(*v.atmo); }
}

void enterView(View3D& v)
{
	swapViewResources(v);
	m_activeView = &v;
	setViewCamera(v.camera);
}

void leaveView(View3D& v)
{
	m_activeView = nullptr;
	swapViewResources(v);
}

/// @brief 副ビューのカメラを設定する。ずらしは TAA を掛けるフレームだけ、ビューの相で掛ける (入れ替え済みの m_jitterNdc)
void setViewCamera(const Camera3D& camera)
{
	m_viewMatrix = lookAt(camera.position(), camera.target(), camera.up());
	m_projNoJitter = perspective(camera.fov(), camera.aspectRatio(), camera.nearClip(), camera.farClip());
	const bool taa = m_activeView != nullptr && m_activeView->frame.taa;
	m_projMatrix = taa ? jitteredProjection(m_projNoJitter) : m_projNoJitter;
	m_viewProjNoJitter = m_projNoJitter * m_viewMatrix;
	if (taa) { m_activeView->temporal->viewProj = m_viewProjNoJitter; }
	m_cameraPosition = camera.position();
	m_clodCamera = camera;
	m_frustum.extractFromCamera(camera);
}

void setTargetViewport(float width, float height)
{
	const D3D12_VIEWPORT vp{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	const D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sc);
}

void bindViewTargets(const View3D& v, bool clear)
{
	const auto color = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto normal = m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {color, normal};
	m_graphicsCmdList->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
	if (clear)
	{
		const sgc::Colorf c = linearColor(v.desc.clearColor);
		const float cc[4] = {c.r, c.g, c.b, c.a};
		const float nc[4] = {0.5f, 0.5f, 0.5f, 0.0f};
		m_graphicsCmdList->ClearRenderTargetView(color, cc, 0, nullptr);
		m_graphicsCmdList->ClearRenderTargetView(normal, nc, 0, nullptr);
		m_graphicsCmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}
	setTargetViewport(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
}

void bindMainTargets()
{
	const auto color = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto normal = m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {color, normal};
	m_graphicsCmdList->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
	setTargetViewport(m_config.viewportWidth, m_config.viewportHeight);
}

void viewBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = res;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &b);
}

/// @brief tonemap の root signature で全画面三角形を描く。table は副ビューの SRV ヒープ先頭からの番号
void drawViewFullscreen(const View3D& v, ID3D12PipelineState* pso, UINT table, D3D12_GPU_VIRTUAL_ADDRESS cb)
{
	auto* cl = m_graphicsCmdList.Get();
	cl->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	cl->SetPipelineState(pso);
	ID3D12DescriptorHeap* heaps[] = {v.srv.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, viewSrvGpu(v, table));
	if (cb != 0) { cl->SetGraphicsRootConstantBufferView(1, cb); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE viewSrvGpu(const View3D& v, UINT slot) const
{
	auto gpu = v.srv->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(slot) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return gpu;
}

/// @brief endFrame の後処理後、HUD 前に呼び、積まれた副ビューを画面上の矩形へ貼る
void drawViewComposites()
{
	if (m_viewComposites.empty() || !m_viewCompositePSO || !m_device) { m_viewComposites.clear(); return; }
	auto* bb = m_device->currentBackBuffer();
	if (bb == nullptr || bb->nativeResource() == nullptr) { m_viewComposites.clear(); return; }
	const auto rtv = bb->rtvHandle();
	m_graphicsCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const auto desc = bb->nativeResource()->GetDesc();
	const D3D12_RECT sc{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
	m_graphicsCmdList->RSSetScissorRects(1, &sc);
	for (const ViewComposite& c : m_viewComposites)
	{
		const View3D* v = viewAt(c.id);
		if (v == nullptr || !v->hasOutput) { continue; }
		const float sx = c.normalized ? static_cast<float>(desc.Width) : 1.0f;
		const float sy = c.normalized ? static_cast<float>(desc.Height) : 1.0f;
		const D3D12_VIEWPORT vp{c.x * sx, c.y * sy, c.w * sx, c.h * sy, 0.0f, 1.0f};
		m_graphicsCmdList->RSSetViewports(1, &vp);
		drawViewFullscreen(*v, m_viewCompositePSO.Get(), 3, 0);
	}
	m_viewComposites.clear();
	// 後続の HUD と Live2D が viewport を設定しなくても画面全体に描けるように戻す
	setTargetViewport(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

/// 影の caster の重複を開番地法で除く表。0 は空きを表し、副ビューを描いたフレームだけ使う
static constexpr std::size_t kCasterKeyTableSize = 16384;
std::vector<std::uint64_t> m_casterKeys;
std::size_t m_casterKeyCount = 0;
std::uint64_t m_casterDedupFrame = 0;   ///< 表を使い始めたフレーム (m_frameCounter)。違えば使っていない

/// @brief 影の caster を積む。副ビューを描くフレームでは、同じメッシュと姿勢を重複して積まない
/// @return 積んだら true。重複なら false
bool addShadowCaster(const ShadowCaster& c)
{
	if (m_activeView != nullptr && m_casterDedupFrame != m_frameCounter) { beginCasterDedup(); }
	if (m_casterDedupFrame == m_frameCounter && !insertCasterKey(casterKey(c))) { return false; }
	m_shadowCommands.push_back(c);
	return true;
}

/// @brief このフレームで副ビューが最初に caster を積むとき、それまでに主ビューが積んだ分を表へ入れる
void beginCasterDedup()
{
	m_casterKeys.assign(kCasterKeyTableSize, 0);
	m_casterKeyCount = 0;
	m_casterDedupFrame = m_frameCounter;
	for (const ShadowCaster& c : m_shadowCommands) { (void)insertCasterKey(casterKey(c)); }
}

/// @brief 表へ入れる。既にあれば false。表が 3/4 まで埋まったら重複の除外をやめる
[[nodiscard]] bool insertCasterKey(std::uint64_t key)
{
	if (m_casterKeyCount * 4 >= kCasterKeyTableSize * 3) { return true; }
	key = (key == 0) ? 1 : key;
	for (std::size_t i = static_cast<std::size_t>(key) & (kCasterKeyTableSize - 1);; i = (i + 1) & (kCasterKeyTableSize - 1))
	{
		if (m_casterKeys[i] == key) { return false; }
		if (m_casterKeys[i] == 0)
		{
			m_casterKeys[i] = key;
			++m_casterKeyCount;
			return true;
		}
	}
}

/// @brief メッシュと行列 (インスタンスは数と全ての行列) を 4 byte ずつ混ぜた FNV-1a
[[nodiscard]] std::uint64_t casterKey(const ShadowCaster& c) const noexcept
{
	std::uint64_t h = 14695981039346656037ull;
	const auto mix = [&h](const void* p, std::size_t bytes) {
		const auto* w = static_cast<const std::uint32_t*>(p);
		for (std::size_t i = 0; i < bytes / sizeof(std::uint32_t); ++i) { h = (h ^ w[i]) * 0x100000001B3ull; }
	};
	const auto mesh = reinterpret_cast<std::uintptr_t>(c.mesh);
	mix(&mesh, sizeof(mesh));
	mix(&c.instanceCount, sizeof(c.instanceCount));
	if (c.instanceCount == 0)
	{
		mix(&c.world, sizeof(c.world));
		return h;
	}
	mix(&m_shadowInstances[c.instanceFirst], sizeof(InstanceDataDx12) * c.instanceCount);
	return h;
}

/// @brief beginFrame から呼ぶ。手放した副ビューは、GPU が読み終える FRAME_COUNT + 1 フレーム後に消す
void collectRetiredViews()
{
	m_viewComposites.clear();
	m_viewsThisFrame.clear();
	if (m_activeView != nullptr)
	{
		// 前のフレームが endFrame まで進まなかった (装置の喪失など)。資源と主ビューの状態だけ戻す
		View3D* v = m_activeView;
		leaveView(*v);
		restoreMainViewState();
	}
	std::erase_if(m_viewGraveyard, [this](const RetiredView& r) { return m_frameCounter > r.frame + FRAME_COUNT; });
}
