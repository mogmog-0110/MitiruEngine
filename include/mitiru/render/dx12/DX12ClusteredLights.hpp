// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。局所光と froxel への割り当て
//
// 描画はフレームの途中で命令を積むので、光の一覧と割り当ては endFrame で決まる。そこで割り当ての compute は
// メインとは別のコマンドリストに積み、finalizeFrame で「割り当て → メイン」の順に同じ提出で流す。
// 描画は光のバッファ (upload ring) と CbCluster の場所をフレームの頭で取っておき、中身は endFrame で埋める。

std::vector<LocalLight>            m_localLights;        ///< このフレームに積まれた光 (beginFrame で空)
std::vector<std::pair<float, int>> m_lightSortScratch;
std::vector<LocalLightGpu>         m_visibleLights;
dx12::UploadAllocation             m_lightBufferAlloc;   ///< t6。kMaxVisibleLocalLights 個ぶん
dx12::UploadAllocation             m_clusterFrameAlloc;  ///< b4
gfx::GpuResource                   m_clusterMasks;       ///< t7 / u0。froxel ごとに kClusterMaskWords 語
ComPtr<ID3D12RootSignature>        m_clusterBuildRS;
ComPtr<ID3D12PipelineState>        m_clusterBuildPSO;
ComPtr<ID3D12CommandAllocator>     m_lightCmdAllocators[FRAME_COUNT];
ComPtr<ID3D12GraphicsCommandList>  m_lightCmdList;
bool                               m_lightListRecorded = false;
int                                m_visibleLightCount = 0;
gfx::GpuResource                   m_clusterMaskReadback;
bool                               m_clusterMaskCaptureRequested = false;
bool                               m_clusterMaskCaptured = false;

/// @brief 割り当ての compute と、そのコマンドリスト・ビット集合のバッファを作る。失敗しても描画は局所光なしで続く
void createClusteredLightResources()
{
	const UINT64 maskBytes = sizeof(std::uint32_t) * kClusterCount * kClusterMaskWords;
	if (FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_DEFAULT, maskBytes, D3D12_RESOURCE_STATE_COMMON,
	                                m_clusterMasks, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
	{
		return;
	}
	m_clusterMasks->SetName(L"Renderer3D cluster light masks");
	D3D12_ROOT_PARAMETER p[3] = {};
	p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	D3D12_ROOT_SIGNATURE_DESC rsd = {};
	rsd.NumParameters = 3;
	rsd.pParameters = p;
	ComPtr<ID3DBlob> sig, cs, err;
	if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
	    FAILED(m_d3dDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
	                                            IID_PPV_ARGS(m_clusterBuildRS.ReleaseAndGetAddressOf()))) ||
	    FAILED(gfx::compileDx12Shader(DX12_CLUSTER_BUILD_CS, "CSMain", "cs_5_0", 0, cs.GetAddressOf(), err.GetAddressOf())))
	{
		debug::warnOnce("dx12.clusterLights.init", "局所光の割り当て (compute) を作れない — 点光源とスポットは描かない");
		return;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_clusterBuildRS.Get();
	pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	if (FAILED(m_d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(m_clusterBuildPSO.ReleaseAndGetAddressOf()))))
	{
		return;
	}
	createLightCommandList();
}

void createLightCommandList()
{
	for (auto& a : m_lightCmdAllocators)
	{
		if (FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.ReleaseAndGetAddressOf()))))
		{
			m_clusterBuildPSO.Reset();
			return;
		}
	}
	if (FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_lightCmdAllocators[0].Get(), nullptr,
	                                          IID_PPV_ARGS(m_lightCmdList.ReleaseAndGetAddressOf()))))
	{
		m_clusterBuildPSO.Reset();
		return;
	}
	m_lightCmdList->SetName(L"Renderer3D cluster lights");
	m_lightCmdList->Close();
}

/// @brief beginFrame で呼ぶ。光を空にし、光のバッファと CbCluster の場所をこのフレームの ring から取る
void beginFrameLocalLights()
{
	m_localLights.clear();
	m_lightListRecorded = false;
	m_visibleLightCount = 0;
	m_lightBufferAlloc = m_uploadRing.allocate(sizeof(LocalLightGpu) * kMaxVisibleLocalLights, 256);
	m_clusterFrameAlloc = m_uploadRing.allocate(sizeof(DX12CbCluster), 256);
	writeClusterFrameCB(0, false);
}

/// @brief CbCluster を埋める。描画はこの場所を指しているので、光が出そろった endFrame でもう一度書く
/// @param selectShadows 影を落とすスポットを決める (endFrame の 1 回だけ)
void writeClusterFrameCB(int lightCount, bool selectShadows)
{
	if (!m_clusterFrameAlloc.valid()) { return; }
	const ClusterView v = ClusterView::fromCamera(m_clodCamera);
	const float logRatio = std::log(v.farZ / v.nearZ);
	DX12CbCluster cb;
	cb.grid[0] = kClusterGridX;
	cb.grid[1] = kClusterGridY;
	cb.grid[2] = kClusterGridZ;
	cb.grid[3] = static_cast<std::uint32_t>(lightCount);
	cb.depth[0] = v.nearZ;
	cb.depth[1] = v.farZ;
	cb.depth[2] = static_cast<float>(kClusterGridZ) / logRatio;
	cb.depth[3] = -static_cast<float>(kClusterGridZ) * std::log(v.nearZ) / logRatio;
	cb.screen[0] = static_cast<float>(kClusterGridX) / m_config.viewportWidth;
	cb.screen[1] = static_cast<float>(kClusterGridY) / m_config.viewportHeight;
	cb.forward[0] = v.forward.x;
	cb.forward[1] = v.forward.y;
	cb.forward[2] = v.forward.z;
	cb.ibl[0] = m_sceneTableHasIbl ? 1.0f : 0.0f;
	cb.ibl[1] = 1.0f;
	cb.ibl[2] = static_cast<float>(kPbrPrefilterMipCount - 1);
	if (selectShadows) { selectSpotShadows(cb); }
	std::memcpy(m_clusterFrameAlloc.cpuPtr, &cb, sizeof(cb));
}

/// @brief 描画の root 引数 6 (b4)・7 (t6)・8 (t7) を張る
void bindFrameLighting()
{
	if (m_clusterFrameAlloc.valid()) { m_graphicsCmdList->SetGraphicsRootConstantBufferView(6, m_clusterFrameAlloc.gpuAddr); }
	if (m_lightBufferAlloc.valid()) { m_graphicsCmdList->SetGraphicsRootShaderResourceView(7, m_lightBufferAlloc.gpuAddr); }
	if (m_clusterMasks) { m_graphicsCmdList->SetGraphicsRootShaderResourceView(8, m_clusterMasks->GetGPUVirtualAddress()); }
}

/// @brief setUseMultiLight(true) の間は setLights の点光源・スポットも局所光として使う
void appendLegacyLights()
{
	if (!m_useMultiLight) { return; }
	for (const Light& l : m_lights)
	{
		if (l.type == LightType::Directional) { continue; }
		const float half = l.spotAngle * 0.5f;
		LocalLight ll = (l.type == LightType::Spot)
			? LocalLight::spot(l.position, l.direction, l.range, half * 0.9f, half, l.color, l.intensity)
			: LocalLight::point(l.position, l.range, l.color, l.intensity);
		m_localLights.push_back(ll);
	}
}

/// @brief endFrame で呼ぶ。見える光を選んでバッファへ書き、割り当ての compute を補助リストに積む
void recordClusterBuild()
{
	appendLegacyLights();
	const ClusterView view = ClusterView::fromCamera(m_clodCamera);
	const int dropped = selectVisibleLocalLights(m_localLights, view, m_lightSortScratch, m_visibleLights);
	if (dropped > 0)
	{
		debug::warnOnce("dx12.localLights.visibleCap",
		                "見えている局所光が " + std::to_string(kMaxVisibleLocalLights) + " を超えた — 遠いものから描かない");
	}
	const bool usable = m_clusterBuildPSO && m_lightCmdList && m_lightBufferAlloc.valid() && !m_visibleLights.empty();
	m_visibleLightCount = usable ? static_cast<int>(m_visibleLights.size()) : 0;
	writeClusterFrameCB(m_visibleLightCount, true);
	if (!usable) { return; }
	std::memcpy(m_lightBufferAlloc.cpuPtr, m_visibleLights.data(), sizeof(LocalLightGpu) * m_visibleLights.size());

	DX12CbClusterBuild cb;
	cb.grid[0] = kClusterGridX;
	cb.grid[1] = kClusterGridY;
	cb.grid[2] = kClusterGridZ;
	cb.grid[3] = static_cast<std::uint32_t>(m_visibleLightCount);
	cb.frustum[0] = view.tanHalfX;
	cb.frustum[1] = view.tanHalfY;
	cb.frustum[2] = view.nearZ;
	cb.frustum[3] = view.farZ;
	const auto cbAlloc = m_uploadRing.upload(&cb, sizeof(cb), 256);
	if (!cbAlloc.valid()) { return; }
	encodeClusterBuild(cbAlloc.gpuAddr);
}

void encodeClusterBuild(D3D12_GPU_VIRTUAL_ADDRESS cbAddr)
{
	auto* alloc = m_lightCmdAllocators[m_frameCursor % FRAME_COUNT].Get();
	if (FAILED(alloc->Reset()) || FAILED(m_lightCmdList->Reset(alloc, m_clusterBuildPSO.Get()))) { return; }
	auto* cmd = m_lightCmdList.Get();
	m_frameTimer.begin(cmd, kFrameTimerLights);
	cmd->SetComputeRootSignature(m_clusterBuildRS.Get());
	cmd->SetComputeRootConstantBufferView(0, cbAddr);
	cmd->SetComputeRootShaderResourceView(1, m_lightBufferAlloc.gpuAddr);
	cmd->SetComputeRootUnorderedAccessView(2, m_clusterMasks->GetGPUVirtualAddress());
	cmd->Dispatch((kClusterCount + 63) / 64, 1, 1);

	// ビット集合は提出の終わりで COMMON に戻るので、頭の UAV は暗黙の昇格に任せ、読む側へだけ遷移を張る
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = m_clusterMasks.Get();
	b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	b.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;   // 体積フォグの compute も読む
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	if (m_clusterMaskCaptureRequested && copyClusterMasksForCapture(cmd)) { b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; }
	cmd->ResourceBarrier(1, &b);
	m_frameTimer.end(cmd, kFrameTimerLights);
	m_lightListRecorded = SUCCEEDED(cmd->Close());
}

/// @brief テスト用: 割り当ての結果を読み戻しバッファへ写す (UAV → COPY_SOURCE の遷移込み)
[[nodiscard]] bool copyClusterMasksForCapture(ID3D12GraphicsCommandList* cmd)
{
	const UINT64 bytes = sizeof(std::uint32_t) * kClusterCount * kClusterMaskWords;
	if (!m_clusterMaskReadback &&
	    FAILED(gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST,
	                                m_clusterMaskReadback)))
	{
		return false;
	}
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = m_clusterMasks.Get();
	b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &b);
	cmd->CopyBufferRegion(m_clusterMaskReadback.Get(), 0, m_clusterMasks.Get(), 0, bytes);
	m_clusterMaskCaptureRequested = false;
	m_clusterMaskCaptured = true;
	return true;
}

public:
/// @brief このフレームの局所光を積む (IRenderer3D)
void submitLocalLights(const LocalLight* lights, int count) override
{
	if (lights == nullptr || count <= 0) { return; }
	const int room = kMaxLocalLights - static_cast<int>(m_localLights.size());
	if (count > room)
	{
		debug::warnOnce("dx12.localLights.cap",
		                "局所光は 1 フレームに " + std::to_string(kMaxLocalLights) + " 個まで — 超えた分は捨てる");
		count = std::max(room, 0);
	}
	m_localLights.insert(m_localLights.end(), lights, lights + count);
	for (int i = 0; i < count; ++i)
	{
		m_spotShadowRequested |= (lights[i].type == LocalLightType::Spot && lights[i].castShadow != 0);
	}
}

/// @brief 直前に割り当てた (描画に使った) 局所光の数
[[nodiscard]] int visibleLocalLightCount() const noexcept { return m_visibleLightCount; }

/// @brief 次のフレームの割り当て結果を読み戻すよう頼む (テスト用)。GPU 完了後に capturedClusterMasks で読む
void requestClusterMaskCapture() noexcept
{
	m_clusterMaskCaptureRequested = true;
	m_clusterMaskCaptured = false;
}

/// @brief 読み戻した froxel のビット集合 (froxel ごとに kClusterMaskWords 語)。まだ無ければ空
[[nodiscard]] std::vector<std::uint32_t> capturedClusterMasks() const
{
	std::vector<std::uint32_t> out;
	if (!m_clusterMaskCaptured || !m_clusterMaskReadback) { return out; }
	const std::size_t words = static_cast<std::size_t>(kClusterCount) * kClusterMaskWords;
	const D3D12_RANGE range = {0, words * sizeof(std::uint32_t)};
	void* p = nullptr;
	if (FAILED(m_clusterMaskReadback->Map(0, &range, &p))) { return out; }
	out.assign(static_cast<const std::uint32_t*>(p), static_cast<const std::uint32_t*>(p) + words);
	const D3D12_RANGE noWrite = {0, 0};
	m_clusterMaskReadback->Unmap(0, &noWrite);
	return out;
}

/// @brief 直前に描画へ使った局所光 (選別・並べ替え後。テストが GPU の割り当てと照合する)
[[nodiscard]] const std::vector<LocalLightGpu>& visibleLocalLights() const noexcept { return m_visibleLights; }

private:
