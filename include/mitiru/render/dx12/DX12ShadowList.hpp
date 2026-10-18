// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12SpotShadows.hpp の後に include)。影の深度の補助リスト
//
// 描画はフレームの途中で命令を積むので、このフレームに描いた物の形 (updateMesh3D で変えた頂点や、動かした行列) が
// 出そろうのは endFrame になる。そこで影の深度はメインとは別のコマンドリストに積み、finalizeFrame で
// 「スキニング → 影の深度 → 局所光の割り当て → メイン」の順に同じ提出で流す。影マップに焼く形と画面に描く形が
// 同じフレームのものになるので、動く物や形の変わる物が、前のフレームの自分の形が落とす影を受けない。
// 影を読む枠 (CbShadow) は描画が参照するので setCamera の時点で決め、深度パスはその時の行列 (ShadowBake) で焼く。

ComPtr<ID3D12CommandAllocator>    m_shadowCmdAllocators[FRAME_COUNT];
ComPtr<ID3D12GraphicsCommandList> m_shadowCmdList;
bool                              m_shadowListRecorded = false;

/// @brief 影の深度の補助リストを作る。作れないと影マップをクリアできず画面が影で埋まるので、初期化を失敗させる
void createShadowCommandList()
{
	for (auto& a : m_shadowCmdAllocators)
	{
		if (FAILED(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.ReleaseAndGetAddressOf()))))
		{
			throw std::runtime_error("CreateCommandAllocator (shadow depth) failed");
		}
	}
	if (FAILED(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_shadowCmdAllocators[0].Get(), nullptr,
	                                          IID_PPV_ARGS(m_shadowCmdList.ReleaseAndGetAddressOf()))))
	{
		throw std::runtime_error("CreateCommandList (shadow depth) failed");
	}
	m_shadowCmdList->SetName(L"Renderer3D shadow depth");
	m_shadowCmdList->Close();
}

/// @brief endFrame で呼ぶ (影を落とすスポットを選ぶ recordClusterBuild の後)。主ビュー・副ビュー・スポットの影の深度を
///        このフレームの caster で補助リストに積む。影マップは caster が無くても毎フレームクリアする
void recordShadowDepthPasses()
{
	MITIRU_ZONE_NAMED("Render::Dx12::ShadowDepth");
	m_shadowListRecorded = false;
	m_shadowCastersSkipped = 0;
	// setCamera を呼ばずに終わったフレームは、描画が読んだ前のフレームの枠のまま焼く (枠を今決め直すと読む枠と食い違う)
	m_mainShadowPending = false;
	auto* alloc = m_shadowCmdAllocators[m_frameCursor % FRAME_COUNT].Get();
	if (!m_shadowCmdList || FAILED(alloc->Reset()) || FAILED(m_shadowCmdList->Reset(alloc, nullptr))) { return; }
	auto* cmd = m_shadowCmdList.Get();
	m_frameTimer.begin(cmd, kFrameTimerShadow);
	if (m_shadowPSO) { drawShadowCascades(cmd, m_mainShadowBake, m_shadowMap, m_shadowMapFar); }
	recordViewShadowPasses(cmd);
	renderSpotShadowPass(cmd);
	m_frameTimer.end(cmd, kFrameTimerShadow);
	m_shadowListRecorded = SUCCEEDED(cmd->Close());
}
