// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される


/// @brief 影マップの depth-only PSO の共通の状態 (VS と入力レイアウト以外)。インスタンス描画の影も同じ状態で作る
void fillShadowPsoState(D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc) const
{
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.PS = D3D12_SHADER_BYTECODE{nullptr, 0};

	psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	// shadow acne 抑制
	psoDesc.RasterizerState.DepthBias            = 1000;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	psoDesc.RasterizerState.DepthClipEnable      = TRUE;

	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;

	psoDesc.DepthStencilState.DepthEnable    = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable  = FALSE;

	psoDesc.SampleMask            = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets      = 0;  // depth-only
	psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count      = 1;
}

/// @brief シャドウマップ用 PSO (depth-only、PS なし) を作る
/// @details VS はメインの DX12_DEFAULT_VS_3D を流用。CbTransform の view/projection を
///          light view / light projection に差し替えて drawMesh と同じ経路で
///          描画する。PSO が PS を持たないため、RTV を 0 にして DSV だけバインドする。
void createShadowPSO()
{
	if (!m_toonVS) return;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	fillShadowPsoState(psoDesc);
	psoDesc.VS = m_toonVS->shaderBytecode();

	D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
	UINT inputCount = 0;
	getInputLayout(inputLayout, inputCount);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements        = inputCount;

	HRESULT hr = m_d3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(m_shadowPSO.GetAddressOf()));
	if (FAILED(hr))
	{
		throw std::runtime_error("CreateGraphicsPipelineState (shadow) failed");
	}

	// setShadowBias (v44) 用の両面版。片面では板 (createPlane・立ち絵) や底の無い器が、表を光へ向けた
	// ときしか影を落とさない。余白を小さくする指定と組にして、薄い物の影も落とす
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_shadowPSOTwoSided.GetAddressOf()))))
	{
		m_shadowPSOTwoSided.Reset();
	}

	// スポットの影 (透視) 用。透視の深度は奥ほど詰まり、斜めの面の傾きの余白が奥行き数十 cm にもなって影が消えるので、
	// ラスタライザの余白は付けず PS の余白 (localShadow) に任せる
	fillShadowPsoState(psoDesc);
	psoDesc.VS = m_toonVS->shaderBytecode();
	setPerspectiveShadowBias(psoDesc);
	if (FAILED(m_d3dDevice->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_spotShadowPSO.GetAddressOf()))))
	{
		m_spotShadowPSO.Reset();
	}
}

static void setPerspectiveShadowBias(D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc) noexcept
{
	psoDesc.RasterizerState.DepthBias            = 0;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
}

[[nodiscard]] static CullAABB emptyCullAABB() noexcept
{
	constexpr float big = std::numeric_limits<float>::max();
	return {big, big, big, -big, -big, -big};
}

[[nodiscard]] static CullAABB unionCullAABB(const CullAABB& a, const CullAABB& b) noexcept
{
	return {std::min(a.minX, b.minX), std::min(a.minY, b.minY), std::min(a.minZ, b.minZ),
	        std::max(a.maxX, b.maxX), std::max(a.maxY, b.maxY), std::max(a.maxZ, b.maxZ)};
}

/// @brief 箱が影の投影の切り取り範囲 (D3D の -w<=x,y<=w、0<=z<=w) のどれか 1 面の完全に外にあるか
/// @details 8 隅すべてが同じ面の外なら、ラスタライザはその caster から 1 画素も描かない。描かずに飛ばしても
///          影マップは変わらない。正射影 (カスケード) にも透視 (スポット) にもそのまま使える。
[[nodiscard]] static bool outsideShadowClip(const CullAABB& b, const glm::mat4& vp) noexcept
{
	if (b.minX > b.maxX) { return true; }   // 1 つも描いていない空の箱
	int outside[6] = {};
	for (int i = 0; i < 8; ++i)
	{
		const glm::vec4 c = vp * glm::vec4((i & 1) ? b.maxX : b.minX, (i & 2) ? b.maxY : b.minY, (i & 4) ? b.maxZ : b.minZ, 1.0f);
		outside[0] += (c.x > c.w); outside[1] += (c.x < -c.w);
		outside[2] += (c.y > c.w); outside[3] += (c.y < -c.w);
		outside[4] += (c.z > c.w); outside[5] += (c.z < 0.0f);
	}
	for (const int n : outside) { if (n == 8) { return true; } }
	return false;
}

/// @brief 1 カスケード分の深度パスを描画する（caster 一覧を指定 view/proj で焼く）
/// @details renderShadowPass() から 1〜2 回呼ばれる (B13)。呼び出し先の shadow map は
///          呼び出し側が既に beginShadowPass 済み（DSV/viewport bind 完了）であること、
///          呼び出し後に endShadowPass することの両方が呼び出し側の責務。
/// @param perspective スポットの影 (透視)。ラスタライザの余白の無い PSO で描く
void renderShadowCascade(const sgc::Mat4f& lightView, const sgc::Mat4f& lightProj, bool perspective = false)
{
	const bool twoSided = m_shadowBiasWorld > 0.0f && m_shadowPSOTwoSided;
	ID3D12PipelineState* const basePso = perspective ? m_spotShadowPSO.Get()
	                                   : twoSided    ? m_shadowPSOTwoSided.Get() : m_shadowPSO.Get();
	if (basePso == nullptr) { return; }
	const int instancedPso = perspective ? 2 : (twoSided ? 1 : 0);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(basePso);
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b3 (CbShadow): VS が LightSpacePos 算出で読むため depth-only パスでも
	// bind 必須。未 bind の root CBV アクセスは UB で、WARP では
	// device removed (0x887A0005) になる。
	const auto shadowPassCb = uploadShadowCB();
	if (shadowPassCb != 0)
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(3, shadowPassCb);
	}

	bool instancedBound = false;
	const glm::mat4 lightViewProj = toGlm(lightProj) * toGlm(lightView);
	for (const auto& caster : m_shadowCommandsPrev)
	{
		if (!caster.mesh || caster.mesh->vertexCount() == 0) continue;
		if (outsideShadowClip(caster.bounds, lightViewProj)) { ++m_shadowCastersSkipped; continue; }
		const bool instanced = caster.instanceCount > 0;
		const auto cbAddr = uploadShadowTransform(instanced ? sgc::Mat4f::identity() : caster.world, lightView, lightProj);
		if (cbAddr == 0) continue;
		if (instanced)
		{
			if (!bindShadowInstances(caster.instanceFirst, caster.instanceCount, instancedPso)) continue;
			instancedBound = true;
		}
		else if (instancedBound)
		{
			m_graphicsCmdList->SetPipelineState(basePso);
			instancedBound = false;
		}
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbAddr);
		drawCachedMesh(*caster.mesh, instanced ? caster.instanceCount : 1u);
	}
}

/// @brief 影の深度パスの CbTransform (world を光の view / proj で写す)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadShadowTransform(const sgc::Mat4f& world, const sgc::Mat4f& lightView,
                                                              const sgc::Mat4f& lightProj)
{
	DX12CbTransform cb;
	toColumnMajor(cb.world,      toGlm(world));
	toColumnMajor(cb.view,       toGlm(lightView));
	toColumnMajor(cb.projection, toGlm(lightProj));
	auto a = m_uploadRing.upload(&cb, sizeof(DX12CbTransform), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief 前フレームに上げた VB / IB のキャッシュでメッシュを描く (影のパス用。キャッシュに無ければ描かない)
void drawCachedMesh(const Mesh& mesh, UINT instances)
{
	const void* key = static_cast<const void*>(&mesh);
	const auto vbIt = m_meshVBCache.find(key);
	if (vbIt == m_meshVBCache.end() || !vbIt->second.resource) return;
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vbIt->second.resource->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbIt->second.size;
	vbv.StrideInBytes  = sizeof(Vertex3D);
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	const auto indexCount = static_cast<UINT>(mesh.indexCount());
	const auto ibIt = m_meshIBCache.find(key);
	if (indexCount > 0 && ibIt != m_meshIBCache.end() && ibIt->second.resource)
	{
		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = ibIt->second.resource->GetGPUVirtualAddress();
		ibv.SizeInBytes    = ibIt->second.size;
		ibv.Format         = DXGI_FORMAT_R32_UINT;
		m_graphicsCmdList->IASetIndexBuffer(&ibv);
		m_graphicsCmdList->DrawIndexedInstanced(indexCount, instances, 0, 0, 0);
		return;
	}
	m_graphicsCmdList->DrawInstanced(static_cast<UINT>(mesh.vertexCount()), instances, 0, 0);
}

/// @brief シャドウパスを描画する（前フレームの shadow casters を用いる）
/// @details beginFrame の早い段階で呼ぶ。command list が recording 中である必要あり。
///          メインパスのターゲット復元は呼び出し側で行うこと。
///
///          重要: shadow が無効 / casters 不在のフレームでも必ず depth クリア
///          (= 1.0) は実行する。clear しないと texture が 0 のままになり、PS の
///          SampleCmpLevelZero が「ライト視錐台内の全 pixel = 影」を返して
///          シーン中央付近の geometry が ambient (~0.20) の分しか明るくならず真っ黒
///          になる (ENG-103)。一部 GPU では R8G8B8A8 を白として bind しても
///          comparison sampler 経由では 1.0 を返さないため、R32_FLOAT 深度
///          そのものを 1.0 にクリアしておく方が確実。
///
///          B13: setCascadedShadowEnabled(true) の間は m_shadowMap (カスケード 0、
///          cascadeNearHalfExtent の狭い ortho box) に加えて m_shadowMapFar
///          (カスケード 1、orthoHalfExtent の従来 box) も同じ caster 一覧で焼く。
///          無効時は m_shadowMap だけを従来どおり orthoHalfExtent で焼く。
/// @brief autoFitCascades のフレームだけ、カスケード境界と ortho の大きさをカメラから決め直す。
///        影パスと本描画の CB の両方から呼び、同じフレームでは同じ値を見る (カメラ依存のみで冪等)
void applyAutoCascadeFit() noexcept
{
	if (!m_cascadedShadowEnabled || !m_directionalShadow.config().autoFitCascades) return;
	const sgc::Vec3f forward = (m_clodCamera.target() - m_clodCamera.position()).normalized();
	m_directionalShadow.fitCascadesToCamera(m_clodCamera.fov(), m_clodCamera.aspectRatio(),
	                                        m_clodCamera.nearClip(), m_clodCamera.position(), forward);
}

void renderShadowPass()
{
	if (!m_shadowMap.isInitialized() || !m_shadowPSO)
	{
		// しるしの番号を固定するため、描かないカスケードのぶんも置く (Dx12PassMarkers.hpp)
		markPass3D(dx12::Pass3D::ShadowCascade0);
		markPass3D(dx12::Pass3D::ShadowCascade1);
		markPass3D(dx12::Pass3D::ShadowCascade2);
		return;
	}
	applyAutoCascadeFit();
	drawShadowCascades(shadowCasterCentroid(), true);
	m_shadowDrawnThisFrame = true;
}

/// @brief 前フレームの caster の重心。主ビューの手動のカスケードはここを中心に置く。caster が無ければ原点
[[nodiscard]] sgc::Vec3f shadowCasterCentroid() const noexcept
{
	if (!m_shadowEnabled || m_shadowCommandsPrev.empty()) { return {0.0f, 0.0f, 0.0f}; }
	sgc::Vec3f focus{0.0f, 0.0f, 0.0f};
	for (const auto& c : m_shadowCommandsPrev)
	{
		focus.x += c.world.m[0][3];
		focus.y += c.world.m[1][3];
		focus.z += c.world.m[2][3];
	}
	const float invN = 1.0f / static_cast<float>(m_shadowCommandsPrev.size());
	return {focus.x * invN, focus.y * invN, focus.z * invN};
}

/// @brief 今の影マップ (副ビューの間はそのビューのもの) の全カスケードを焼く。caster が無くてもクリアはする
/// @param markPasses 主ビューだけ DRED のしるしを置く (しるしは 1 フレームに決まった数だけ置く)
void drawShadowCascades(const sgc::Vec3f& focus, bool markPasses)
{
	const bool drawCasters
		= m_shadowEnabled && !m_shadowCommandsPrev.empty();
	const bool cascaded
		= m_cascadedShadowEnabled && m_shadowMapFar.isInitialized();
	const auto mark = [&](dx12::Pass3D pass) { if (markPasses) { markPass3D(pass); } };
	const auto lightView = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(0, focus));

	// カスケード 0 (単一カスケード時は唯一のマップ)。
	// beginShadowPass が毎回 depth=1.0 クリアを行うため、caster 不在 / shadow 無効の
	// フレームでも必ず呼ぶ (ENG-103: PS の SampleCmpLevelZero に「影なし」を見せるため)。
	mark(dx12::Pass3D::ShadowCascade0);
	m_shadowMap.beginShadowPass(m_graphicsCmdList.Get());
	if (drawCasters)
	{
		renderShadowCascade(lightView, m_directionalShadow.cascadeProjection(0, lightView));
	}
	m_shadowMap.endShadowPass(m_graphicsCmdList.Get());

	// カスケード 1 (遠距離)。begin/draw/end を独立して行う (カスケード 0 の DSV/viewport
	// bind を上書きしたまま draw しないよう、必ず自分の beginShadowPass の直後に描画する)。
	mark(dx12::Pass3D::ShadowCascade1);
	if (cascaded)
	{
		m_shadowMapFar.beginShadowPass(m_graphicsCmdList.Get());
		if (drawCasters)
		{
			const auto lightViewFar = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(1, focus));
			renderShadowCascade(lightViewFar, m_directionalShadow.cascadeProjection(1, lightViewFar));
		}
	}
	mark(dx12::Pass3D::ShadowCascade2);
	if (cascaded)
	{
		if (drawCasters && m_directionalShadow.config().cascadeCount >= 3)
		{
			// カスケード 2 はアトラスの右列へ (renderShadowCascade は viewport を触らない)。
			m_shadowMapFar.setColumn(m_graphicsCmdList.Get(), 1);
			const auto lightViewFar2 = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(2, focus));
			renderShadowCascade(lightViewFar2, m_directionalShadow.cascadeProjection(2, lightViewFar2));
		}
		m_shadowMapFar.endShadowPass(m_graphicsCmdList.Get());
	}
	// 描画先と viewport は呼び出し側が戻す (このメソッドが責任を持つのは影のパスだけ)
}

/// @brief アルベド SRV 用 shader-visible heap を作る
/// @details kAlbedoSrvPerFrame × FRAME_COUNT を確保し frame index で partition
///          する。GPU が in-flight の前フレーム分 descriptor を読んでいる間に
///          CreateShaderResourceView で上書きしないため。beginFrame で cursor を
///          自 frame partition の先頭にリセットする。
void createAlbedoSrvHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC hd = {};
	hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hd.NumDescriptors = kAlbedoSrvPerFrame * FRAME_COUNT;
	hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(m_d3dDevice->CreateDescriptorHeap(
			&hd, IID_PPV_ARGS(m_albedoSrvHeap.GetAddressOf()))))
	{
		throw std::runtime_error("CreateDescriptorHeap albedo SRV failed");
	}
	m_albedoSrvCapacity  = kAlbedoSrvPerFrame;
	m_albedoSrvIncrement = m_d3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_albedoSrvBase      = 0;
	m_albedoSrvCursor    = 0;
}

/// @brief CbShadow (b3)。light-space view * proj を ring buffer から確保
/// @details B13: lightViewProj (カスケード 0 = VS が読んで LightSpacePos を計算) に加え、
///          lightViewProjFar (カスケード 1) と cascadeSplitDistance を持つ。
///          PS 側は WorldPos から lightViewProjFar を使って独自に light-space 座標を
///          計算する (samplePCFTex 参照)。VS は既存どおり lightViewProj だけを読むため、
///          この 2 フィールドの追加は VS の挙動に影響しない。
/// @return GPU virtual address (0 で失敗)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadShadowCB()
{
	// 副ビューは影のパスで 1 度だけ決めた CB を使う。同じフレームにカメラを変えて begin し直しても、焼いた影マップと食い違わない
	if (m_activeView != nullptr && m_activeView->frame.shadowCb != 0) { return m_activeView->frame.shadowCb; }
	if (m_shadowEnabled && !m_shadowCommandsPrev.empty()) { applyAutoCascadeFit(); }
	return writeShadowCB(shadowCasterCentroid());
}

/// @brief 今の影の設定 (副ビューの間はそのビューのもの) と focus で CbShadow を ring に置く
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS writeShadowCB(const sgc::Vec3f& focus)
{
	struct alignas(256) CbShadow {
		float lightViewProj[4][4]{};
		float lightViewProjFar[4][4]{};
		float cascadeSplitDistance = 0.0f;
		float cascadeSplitDistance2 = 1.0e9f;   // 3 カスケード時のみ有限 (それ以外は PS が列 1 を読まない)
		float shadowSoftness = 1.0f;            // PCF 3x3 のタップ間隔 (texel、v41)。元は詰め物だったので layout 不変
		float shadowBiasNdc = 0.0f;            // 0 = PS が従来の余白を使う (v44)。元は詰め物なので layout 不変
		float lightViewProjFar2[4][4]{};
	};
	CbShadow cb;
	cb.shadowSoftness = m_shadowSoftness;
	if (m_shadowBiasWorld > 0.0f)
	{
		const auto& sc = m_directionalShadow.config();
		const float range = sc.farClip - sc.nearClip;
		cb.shadowBiasNdc = (range > 1e-4f) ? m_shadowBiasWorld / range : 0.0f;
	}

	if (m_shadowEnabled && !m_shadowCommandsPrev.empty())
	{
		// 影のパスと同じ focus で lightVP を組む
		const auto V0s = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(0, focus));
		const auto V = toGlm(V0s);
		const auto P0 = toGlm(m_directionalShadow.cascadeProjection(0, V0s));
		toColumnMajor(cb.lightViewProj, P0 * V);

		if (m_cascadedShadowEnabled && m_shadowMapFar.isInitialized())
		{
			const auto V1s = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(1, focus));
			const auto V1 = toGlm(V1s);
			const auto P1 = toGlm(m_directionalShadow.cascadeProjection(1, V1s));
			toColumnMajor(cb.lightViewProjFar, P1 * V1);
			cb.cascadeSplitDistance = m_directionalShadow.config().cascadeSplitDistance;
			if (m_directionalShadow.config().cascadeCount >= 3)
			{
				const auto V2s = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(2, focus));
				const auto P2 = toGlm(m_directionalShadow.cascadeProjection(2, V2s));
				toColumnMajor(cb.lightViewProjFar2, P2 * toGlm(V2s));
				cb.cascadeSplitDistance2 = m_directionalShadow.config().cascadeSplitDistance2;
			}
		}
		else
		{
			// カスケード無効: 分割距離を非常に大きくして PS が常にカスケード 0 を選ぶようにする
			toColumnMajor(cb.lightViewProjFar, P0 * V);
			cb.cascadeSplitDistance = 1.0e9f;
		}
	}
	else
	{
		// shadow 無効: 恒等行列。HLSL 側で shadow factor=1.0 になる
		glm::mat4 I(1.0f);
		toColumnMajor(cb.lightViewProj, I);
		toColumnMajor(cb.lightViewProjFar, I);
		cb.cascadeSplitDistance = 1.0e9f;
	}

	auto a = m_uploadRing.upload(&cb, sizeof(CbShadow), 256);
	return a.valid() ? a.gpuAddr : 0;
}

// ─────────────────────────────────────────────────────────────
//  ユーティリティ
// ─────────────────────────────────────────────────────────────

/// @brief mesh の VB/IB cache entry を取得する（失効時は作り直す）
/// @details 失効 = サイズ変化 or Mesh::revision 変化（内容改変・アドレス再利用）。
///          **同サイズの内容改変** (毎フレーム CPU が頂点を書き換える mesh) は
///          FRAME_COUNT 周期の slot 回転 + memcpy のみで済ませ、committed resource を
///          毎フレーム作らない。slot N の次の書き込みは N+FRAME_COUNT フレーム後で、
///          Dx12Device::beginFrame の fence がその間の GPU 読み完了を保証している
///          (upload ring が依存しているのと同じ保証)。
///          サイズ変化時の旧リソースは in-flight が読み終わるまで deferRelease で生存させる。
/// @return 使用可能なリソース（生成失敗時 nullptr）
[[nodiscard]] ID3D12Resource* acquireMeshBuffer(
	std::unordered_map<const void*, CachedBuffer>& cache,
	const Mesh& mesh, const void* data, UINT sizeBytes)
{
	auto& entry = cache[static_cast<const void*>(&mesh)];
	entry.lastUsedFrame = m_frameCounter;

	if (entry.resource && entry.size == sizeBytes && entry.revision == mesh.revision())
	{
		return entry.resource.Get();  // 不変 (静的 mesh の通常経路)
	}

	// slot に書いた frame W の内容は W+1 の shadow pass まで読まれる。デバイスのフェンスが
	// 保証するのは「今の frame - FRAME_COUNT 以前は完了」だけなので、W+1 <= now - FRAME_COUNT を要求する。
	const auto slotIsFree = [&](uint32_t k) {
		return !entry.slots[k] || m_frameCounter >= entry.slotFrame[k] + kMeshSlotCount;
	};

	if (entry.resource && entry.size == sizeBytes &&
	    slotIsFree((entry.activeSlot + 1) % kMeshSlotCount))
	{
		// 同サイズで内容だけ変わった動的 mesh → slot 回転 (warm-up 後は生成ゼロ)。
		// 使っている slot は entry.resource へ move して持ち、slots[activeSlot] は空にしておく。
		// 複製で差し替えると、手放す参照ごとに解放の待ち行列へ積む確保が走る (mesh ごと、毎フレーム)
		const uint32_t next = (entry.activeSlot + 1) % kMeshSlotCount;
		if (!entry.slots[next])
		{
			entry.slots[next] = createUploadBuffer(sizeBytes);
			++m_meshBufferCreates;
		}
		if (entry.slots[next])
		{
			uploadToBuffer(entry.slots[next].Get(), data, sizeBytes);
			entry.slots[entry.activeSlot] = std::move(entry.resource);
			entry.resource = std::move(entry.slots[next]);
			entry.activeSlot = next;
			entry.slotFrame[next] = m_frameCounter;
			entry.revision = mesh.revision();
			return entry.resource.Get();
		}
		// slot 生成失敗 → 従来 slow path へ落とす
	}

	// 初回 or サイズ変化: 全 slot を退役して作り直す (メモリは投入済みフレームが終わるまで返らない)
	for (auto& s : entry.slots) { s.Reset(); }
	entry.resource   = createUploadBuffer(sizeBytes);
	++m_meshBufferCreates;
	entry.slotFrame[0] = m_frameCounter;   // 次の同サイズ改変から slot 0 (今は resource が持つ) を起点に回転する
	entry.activeSlot = 0;
	entry.size       = sizeBytes;
	entry.revision   = mesh.revision();
	if (entry.resource)
	{
		uploadToBuffer(entry.resource.Get(), data, sizeBytes);
	}
	return entry.resource.Get();
}

/// @brief GPU にしか中身の無いメッシュ (Mesh::setGpuOnlyCounts) に描画の側が結び付けたバッファ。無ければ nullptr
[[nodiscard]] ID3D12Resource* boundGpuBuffer(std::unordered_map<const void*, CachedBuffer>& cache, const Mesh& mesh)
{
	const auto it = cache.find(static_cast<const void*>(&mesh));
	if (it == cache.end() || it->second.revision != mesh.revision()) { return nullptr; }
	it->second.lastUsedFrame = m_frameCounter;
	return it->second.resource.Get();
}

/// @brief 長期間参照の無い mesh VB/IB を退役させる
/// @details beginFrame で毎フレーム呼ぶ。破棄済み Mesh の entry を掃除して
///          GPU メモリ漏れとアドレス再利用時の stale ヒットを防ぐ。
void evictStaleMeshBuffers()
{
	constexpr uint64_t kKeepFrames = FRAME_COUNT + 2;
	auto sweep = [&](std::unordered_map<const void*, CachedBuffer>& cache)
	{
		for (auto it = cache.begin(); it != cache.end();)
		{
			if (m_frameCounter - it->second.lastUsedFrame > kKeepFrames)
			{
				it = cache.erase(it);
			}
			else
			{
				++it;
			}
		}
	};
	sweep(m_meshVBCache);
	sweep(m_meshIBCache);
}

/// @brief アップロードヒープにバッファリソースを生成する
/// @param sizeBytes バッファサイズ（バイト）
/// @return 生成されたリソース（失敗時は空）
[[nodiscard]] gfx::GpuResource createUploadBuffer(UINT64 sizeBytes) const
{
	/// 256 バイトアラインメントを保証する（CBV 用）
	const UINT64 alignedSize = (sizeBytes + 255) & ~255ULL;

	gfx::GpuResource resource;
	(void)gfx::createGpuBuffer(m_d3dDevice, D3D12_HEAP_TYPE_UPLOAD, alignedSize,
		D3D12_RESOURCE_STATE_GENERIC_READ, resource);
	return resource;
}

/// @brief バッファリソースにデータをアップロードする
/// @param resource 転送先リソース
/// @param data 転送元データ
/// @param sizeBytes データサイズ（バイト）
static void uploadToBuffer(ID3D12Resource* resource,
                           const void* data,
                           UINT sizeBytes)
{
	void* mapped = nullptr;
	D3D12_RANGE readRange = {0, 0};
	HRESULT hr = resource->Map(0, &readRange, &mapped);
	if (FAILED(hr))
	{
		return;
	}

	std::memcpy(mapped, data, sizeBytes);

	resource->Unmap(0, nullptr);
}
