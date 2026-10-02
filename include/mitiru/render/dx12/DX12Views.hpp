// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。副ビュー (分割画面・小窓・描いた絵を材質に使う)
//
// 副ビューは主ビューと同じフレームの中で、自分の大きさの MSAA の色・法線・深度へ forward の描画を積み、resolve と
// tonemap まで済ませて LDR のテクスチャに残す。beginView から endView の間に呼んだ drawMesh・インスタンス・
// glTF / スキンモデルが副ビューへ入る。後処理は tonemap だけで、SSAO・bloom・輪郭線・FXAA・TAA・動きのぼけは
// 主ビューにだけ掛かる (副ビューは動きを記録しないので、主ビューの TAA の履歴も崩さない)。
// 影は主ビューの影マップを共有する。局所光と デカールは froxel を主ビューのカメラで作るので、副ビューでは足さない。
// 1 フレームの終わりにまとめて描くもの (clod の世界、屋外の地形、CSG、スプラット、粒、デカール、剣筋、Effekseer、
// 半透明) は主ビューにだけ出る。副ビューの間に積むと 1 度だけ警告して捨てる。

public:

/// @brief 副ビューの作り。大きさは描く画素数 (出力のどこへ置くかは compositeView で決める)
struct View3DDesc
{
	int width = 0;
	int height = 0;
	bool shadows = true;   ///< 主ビューの影マップを読むか
	bool skybox = true;    ///< setSkybox の空を最初の描画の前に張るか
	sgc::Colorf clearColor{0.0f, 0.0f, 0.0f, 1.0f};   ///< 書いた sRGB
};

/// @brief 副ビューを作る。フレームの外でも中でもよい (beginView の間は不可)
/// @return 番号 (1 以上)。作れなければ 0。手放した番号は別の副ビューに使い回さない (世代を含む)
int createView(const View3DDesc& desc)
{
	if (!m_initialized || m_activeView != nullptr || desc.width <= 0 || desc.height <= 0 ||
	    desc.width > kMaxViewExtent || desc.height > kMaxViewExtent)
	{
		return 0;
	}
	auto view = std::make_unique<View3D>();
	view->desc = desc;
	if (!createViewTargets(*view))
	{
		debug::warnOnce("dx12.view.create", "副ビューの描画先を作れない");
		return 0;
	}
	ensureViewCompositePipeline();
	for (std::size_t i = 0; i < m_views.size(); ++i)
	{
		if (!m_views[i])
		{
			m_views[i] = std::move(view);
			return viewId(i);
		}
	}
	m_views.push_back(std::move(view));
	m_viewGenerations.push_back(0);
	return viewId(m_views.size() - 1);
}

/// @brief 副ビューを手放す。描画先は GPU が読み終えてから消す。viewTexture で得たテクスチャもこの後は使えない
void releaseView(int id)
{
	View3D* v = viewAt(id);
	if (v == nullptr || v == m_activeView) { return; }
	std::erase_if(m_viewComposites, [id](const ViewComposite& c) { return c.id == id; });
	const std::size_t slot = viewSlot(id);
	m_viewGraveyard.push_back({std::move(m_views[slot]), m_frameCounter});
	m_viewGenerations[slot] = (m_viewGenerations[slot] + 1) & kViewGenerationMask;
}

/// @brief 以後の描画を副ビュー id へ向ける。主ビューの beginFrame の後、endFrame の前に呼ぶ
/// @return 描けるなら true (false の間の描画は主ビューへ入る)
bool beginView(int id, const Camera3D& camera)
{
	View3D* v = viewAt(id);
	if (v == nullptr || !m_frameActive || !m_graphicsCmdList || m_activeView != nullptr) { return false; }
	const dx12::UploadAllocation cluster = viewClusterCB();
	if (!cluster.valid()) { return false; }
	saveMainViewState();
	m_activeView = v;
	swapViewHeaps(*v);
	bindViewTargets(*v, true);
	setViewCamera(camera);
	m_skyboxDrawnThisFrame = !v->desc.skybox;
	m_clusterFrameAlloc = cluster;
	if (!v->desc.shadows) { m_shadowEnabled = false; }
	restoreMainState();
	return true;
}

/// @brief 副ビューを resolve と tonemap まで済ませ、描画先を主ビューへ戻す
void endView()
{
	View3D* v = m_activeView;
	if (v == nullptr) { return; }
	resolveAndTonemapView(*v);
	swapViewHeaps(*v);
	m_activeView = nullptr;
	restoreMainViewState();
	bindMainTargets();
	restoreMainState();
}

/// @brief 副ビューの出力 (sRGB の色)。MaterialMaps::albedo に渡すと材質の色として描ける。
///        書いたフレームの中では endView の後に使う。同じ副ビューの中でその副ビュー自身を貼ることはできない
[[nodiscard]] const dx12::Dx12Texture2D* viewTexture(int id) const noexcept
{
	const View3D* v = viewAt(id);
	return (v != nullptr && v->texture.isReady()) ? &v->texture : nullptr;
}

/// @brief このフレームの最後 (後処理の後、HUD の前) に副ビューの出力を出力画面の矩形 (画素) へ貼る
void compositeView(int id, float x, float y, float width, float height)
{
	if (viewAt(id) == nullptr || !m_frameActive || !(width > 0.0f) || !(height > 0.0f)) { return; }
	m_viewComposites.push_back({id, x, y, width, height});
}

/// @brief 副ビュー viewId の出力を基本色のテクスチャにしてメッシュを描く (画面の中のモニター、鏡、小窓の板)。
///        材質の色はテクスチャに掛ける。描く先が同じ副ビューなら何も描かない (自分の出力は読めない)
void drawMeshWithView(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, int viewId)
{
	const View3D* v = viewAt(viewId);
	if (v == nullptr || v == m_activeView || !v->texture.isReady()) { return; }
	MaterialMaps maps;
	maps.albedo = &v->texture;
	drawMeshEx(mesh, world, material, &maps, DrawTint{});
}

[[nodiscard]] bool viewActive() const noexcept { return m_activeView != nullptr; }

/// @brief 副ビューの出力を読み戻す (RGBA8、sRGB の値)。診断とテスト用で GPU の完了を待つ。フレームの外で呼ぶ
[[nodiscard]] std::vector<std::uint8_t> readViewPixels(int id)
{
	const View3D* v = viewAt(id);
	if (v == nullptr || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	return gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), v->ldr.Get(),
	                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 4);
}

private:

static constexpr int kMaxViewExtent = 8192;
/// SRV の表 2 つ: [0..2] = tonemap (HDR, 遮蔽と bloom の空き)、[3..5] = 貼り付け (LDR を sRGB のまま読む, 空き 2)
static constexpr UINT kViewSrvCount = 6;

struct View3D
{
	View3DDesc desc;
	gfx::GpuResource color, normal, depth, hdr, ldr;
	ComPtr<ID3D12DescriptorHeap> colorRtv, normalRtv, dsv, ldrRtv, srv;
	dx12::Dx12Texture2D texture;   ///< ldr を _UNORM_SRGB で読む (材質の色)
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

/// 副ビューの間だけ差し替える主ビューの状態
struct SavedMainView
{
	glm::mat4 view{1.0f};
	glm::mat4 proj{1.0f};
	sgc::Vec3f eye{};
	Frustum frustum;
	bool skyDrawn = false;
	bool shadowEnabled = false;
	dx12::UploadAllocation cluster;
};

std::vector<std::unique_ptr<View3D>> m_views;   ///< 空きは nullptr
std::vector<std::uint32_t> m_viewGenerations;   ///< 添字ごとに、手放すたびに 1 進める
static constexpr std::uint32_t kViewGenerationMask = 0x7FFF;
std::vector<RetiredView> m_viewGraveyard;
std::vector<ViewComposite> m_viewComposites;
View3D* m_activeView = nullptr;
SavedMainView m_savedMainView;
std::optional<gfx::Dx12Shader> m_viewCompositePS;
ComPtr<ID3D12PipelineState> m_viewCompositePSO;

/// 番号は下位 16 bit が添字 + 1、上位が世代。手放した番号を持ち続けた呼び出し側が、新しい副ビューを触らないため
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

/// @brief 副ビューの間に、1 フレームの終わりにまとめて描くものを積まれたら捨てる
[[nodiscard]] bool rejectInView(const char* what)
{
	if (m_activeView == nullptr) { return false; }
	debug::warnOnce(std::string("dx12.view.unsupported.") + what,
	                std::string(what) + " は副ビューでは描かない (主ビューだけの経路)");
	return true;
}

void saveMainViewState()
{
	m_savedMainView.view = m_viewMatrix;
	m_savedMainView.proj = m_projMatrix;
	m_savedMainView.eye = m_cameraPosition;
	m_savedMainView.frustum = m_frustum;
	m_savedMainView.skyDrawn = m_skyboxDrawnThisFrame;
	m_savedMainView.shadowEnabled = m_shadowEnabled;
	m_savedMainView.cluster = m_clusterFrameAlloc;
}

void restoreMainViewState()
{
	m_viewMatrix = m_savedMainView.view;
	m_projMatrix = m_savedMainView.proj;
	m_cameraPosition = m_savedMainView.eye;
	m_frustum = m_savedMainView.frustum;
	m_skyboxDrawnThisFrame = m_savedMainView.skyDrawn;
	m_shadowEnabled = m_savedMainView.shadowEnabled;
	m_clusterFrameAlloc = m_savedMainView.cluster;
}

/// @brief 描画の経路は RTV / DSV のヒープを名前で引くので、副ビューの間はヒープごと入れ替える
void swapViewHeaps(View3D& v)
{
	std::swap(m_msaaColorRtvHeap, v.colorRtv);
	std::swap(m_normalRTVHeap, v.normalRtv);
	std::swap(m_dsvHeap, v.dsv);
}

/// @brief 副ビューのカメラ。TAA のずらしは掛けない (副ビューは TAA を通らない)
void setViewCamera(const Camera3D& camera)
{
	m_viewMatrix = lookAt(camera.position(), camera.target(), camera.up());
	m_projMatrix = perspective(camera.fov(), camera.aspectRatio(), camera.nearClip(), camera.farClip());
	m_cameraPosition = camera.position();
	m_frustum.extractFromCamera(camera);
}

/// @brief 局所光 0 個・デカールなしの CbCluster。froxel の数と IBL は主ビューと同じ (beginView が主ビューの CB を差し替える前に呼ぶ)
[[nodiscard]] dx12::UploadAllocation viewClusterCB()
{
	DX12CbCluster cb;
	if (m_clusterFrameAlloc.valid()) { std::memcpy(&cb, m_clusterFrameAlloc.cpuPtr, sizeof(cb)); }
	cb.grid[3] = 0;
	cb.ibl[3] = 1.0f;
	for (auto& k : cb.spotShadowLight) { k = 0xFFFFFFFFu; }
	return m_uploadRing.upload(&cb, sizeof(cb), 256);
}

void setTargetViewport(float width, float height)
{
	const D3D12_VIEWPORT vp{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	const D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	m_graphicsCmdList->RSSetViewports(1, &vp);
	m_graphicsCmdList->RSSetScissorRects(1, &sc);
}

/// @brief 今のヒープ (副ビューの間は副ビューのもの) の MSAA の色・法線・深度を張る。clear なら塗る
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

/// @brief MSAA の色を HDR へ resolve し、主ビューと同じ tonemap (遮蔽と bloom なし) で LDR へ焼く
void resolveAndTonemapView(View3D& v)
{
	auto* cl = m_graphicsCmdList.Get();
	viewBarrier(v.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
	cl->ResolveSubresource(v.hdr.Get(), 0, v.color.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
	viewBarrier(v.color.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (m_tonemapPSO && m_tonemapRootSig)
	{
		const auto rtv = v.ldrRtv->GetCPUDescriptorHandleForHeapStart();
		cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		setTargetViewport(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
		drawViewFullscreen(v, m_tonemapPSO.Get(), 0, uploadViewTonemapCB());
	}
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
}

/// @brief tonemap の root sig で全画面三角形を描く。table は副ビューの SRV ヒープの先頭からの番号
void drawViewFullscreen(const View3D& v, ID3D12PipelineState* pso, UINT table, D3D12_GPU_VIRTUAL_ADDRESS cb)
{
	auto* cl = m_graphicsCmdList.Get();
	cl->SetGraphicsRootSignature(m_tonemapRootSig.Get());
	cl->SetPipelineState(pso);
	ID3D12DescriptorHeap* heaps[] = {v.srv.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	auto gpu = v.srv->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(table) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cl->SetGraphicsRootDescriptorTable(0, gpu);
	if (cb != 0) { cl->SetGraphicsRootConstantBufferView(1, cb); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadViewTonemapCB()
{
	struct alignas(256) CbViewTonemap
	{
		TonemapCB tonemap;
		float trailing[56]{};
	};
	CbViewTonemap cb{};
	cb.tonemap.exposure = m_tonemapExposure;
	cb.tonemap.gamma = m_tonemapGamma;
	cb.tonemap.saturation = m_gradeSaturation;
	cb.tonemap.contrast = m_gradeContrast;
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief endFrame (後処理の後、HUD の前) から呼ぶ。積まれた副ビューを出力画面の矩形へ貼る
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
		if (v == nullptr) { continue; }
		const float sx = c.normalized ? static_cast<float>(desc.Width) : 1.0f;
		const float sy = c.normalized ? static_cast<float>(desc.Height) : 1.0f;
		const D3D12_VIEWPORT vp{c.x * sx, c.y * sy, c.w * sx, c.h * sy, 0.0f, 1.0f};
		m_graphicsCmdList->RSSetViewports(1, &vp);
		drawViewFullscreen(*v, m_viewCompositePSO.Get(), 3, 0);
	}
	m_viewComposites.clear();
	// 後のパス (HUD・Live2D) が viewport を張らずに描いても出力全体に出るよう戻す
	setTargetViewport(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}

/// @brief beginFrame から呼ぶ。手放した副ビューを、GPU が読み終えてから (FRAME_COUNT + 1 フレーム後に) 消す
void collectRetiredViews()
{
	m_viewComposites.clear();
	if (m_activeView != nullptr)
	{
		// 前のフレームが endFrame まで来なかった (装置の喪失など)。ヒープと主ビューの状態だけ戻す
		swapViewHeaps(*m_activeView);
		restoreMainViewState();
		m_activeView = nullptr;
	}
	std::erase_if(m_viewGraveyard, [this](const RetiredView& r) { return m_frameCounter > r.frame + FRAME_COUNT; });
}
