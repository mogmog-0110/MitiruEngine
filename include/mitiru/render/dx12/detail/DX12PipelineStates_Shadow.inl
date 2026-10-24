// Renderer3D_DX12 のクラス本体の断片。DX12PipelineStates.hpp から include される

/// @brief CbShadow (b3) の中身。PS の cbuffer と同じ並び
struct ShadowCbData
{
	float lightViewProj[4][4]{};
	float lightViewProjFar[4][4]{};
	float cascadeSplitDistance = 0.0f;
	float cascadeSplitDistance2 = 1.0e9f;   // 3 カスケード時のみ有限 (それ以外は PS が列 1 を読まない)
	float shadowSoftness = 1.0f;            // PCF 3x3 のタップ間隔 (texel、v41)。元は詰め物だったので layout 不変
	float shadowBiasNdc = 0.0f;            // 0 = PS が従来の余白を使う (v44)。元は詰め物なので layout 不変
	float lightViewProjFar2[4][4]{};
	float shadowTexel[4]{};                 // x = 1 / カスケード 0 の一辺 y = 1 / カスケード 1・2 の一辺
};

/// @brief CbShadow に書いたカスケードの光の view / proj。影の深度パスはこの行列で焼く
/// @details 枠は setCamera で決め、深度は endFrame で焼く。その間にゲームが光の向きやカスケードの設定を変えても、
///          焼く枠と読む枠が食い違わないよう、決めた時の行列をそのまま持っておく
struct ShadowBake
{
	sgc::Mat4f view[3];
	sgc::Mat4f proj[3];
	int  cascades = 0;       ///< 焼くカスケードの数。0 なら影なしの枠で、深度はクリアだけする
	bool farAtlas = false;   ///< カスケード 1・2 のアトラスを使う (クリアもする)
};

ShadowCbData              m_mainShadowCb{};        ///< 主ビューの CbShadow。このフレームの描画は全部これで影を読む
ShadowBake                m_mainShadowBake{};      ///< m_mainShadowCb と同じ行列。endFrame の深度パスが焼く
D3D12_GPU_VIRTUAL_ADDRESS m_mainShadowCbAddr = 0;
std::uint64_t             m_mainShadowCbFrame = 0;  ///< m_mainShadowCbAddr を ring に置いたフレーム (m_frameCounter)


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

/// @brief 1 カスケード分の深度パスを描画する (このフレームの caster 一覧を指定 view/proj で焼く)
/// @details 描画先の shadow map は呼び出し側が beginShadowPass 済みで、呼び出し後に endShadowPass する。
/// @param perspective スポットの影 (透視)。ラスタライザの余白の無い PSO で描く
void renderShadowCascade(ID3D12GraphicsCommandList* cmd, const sgc::Mat4f& lightView, const sgc::Mat4f& lightProj,
                         bool perspective = false)
{
	const bool twoSided = m_shadowBiasWorld > 0.0f && m_shadowPSOTwoSided;
	ID3D12PipelineState* const basePso = perspective ? m_spotShadowPSO.Get()
	                                   : twoSided    ? m_shadowPSOTwoSided.Get() : m_shadowPSO.Get();
	if (basePso == nullptr) { return; }
	const int instancedPso = perspective ? 2 : (twoSided ? 1 : 0);
	cmd->SetGraphicsRootSignature(m_rootSignature.Get());
	cmd->SetPipelineState(basePso);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b3 (CbShadow): VS が LightSpacePos 算出で読むため depth-only パスでも
	// bind 必須。未 bind の root CBV アクセスは UB で、WARP では
	// device removed (0x887A0005) になる。
	const auto shadowPassCb = uploadShadowCB();
	if (shadowPassCb != 0)
	{
		cmd->SetGraphicsRootConstantBufferView(3, shadowPassCb);
	}

	bool instancedBound = false;
	const glm::mat4 lightViewProj = toGlm(lightProj) * toGlm(lightView);
	for (const auto& caster : m_shadowCommands)
	{
		if (!caster.mesh || caster.mesh->vertexCount() == 0) continue;
		if (outsideShadowClip(caster.bounds, lightViewProj)) { ++m_shadowCastersSkipped; continue; }
		const bool instanced = caster.instanceCount > 0;
		const auto cbAddr = uploadShadowTransform(instanced ? sgc::Mat4f::identity() : caster.world, lightView, lightProj);
		if (cbAddr == 0) continue;
		if (instanced)
		{
			if (!bindShadowInstances(cmd, caster.instanceFirst, caster.instanceCount, instancedPso)) continue;
			instancedBound = true;
		}
		else if (instancedBound)
		{
			cmd->SetPipelineState(basePso);
			instancedBound = false;
		}
		cmd->SetGraphicsRootConstantBufferView(0, cbAddr);
		drawCachedMesh(cmd, *caster.mesh, instanced ? caster.instanceCount : 1u);
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

/// @brief このフレームに上げた VB / IB のキャッシュでメッシュを描く (影のパス用。キャッシュに無ければ描かない)
void drawCachedMesh(ID3D12GraphicsCommandList* cmd, const Mesh& mesh, UINT instances)
{
	const void* key = static_cast<const void*>(&mesh);
	const auto vbIt = m_meshVBCache.find(key);
	if (vbIt == m_meshVBCache.end() || !vbIt->second.resource) return;
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vbIt->second.resource->GetGPUVirtualAddress();
	vbv.SizeInBytes    = vbIt->second.size;
	vbv.StrideInBytes  = sizeof(Vertex3D);
	cmd->IASetVertexBuffers(0, 1, &vbv);

	const auto indexCount = static_cast<UINT>(mesh.indexCount());
	const auto ibIt = m_meshIBCache.find(key);
	if (indexCount > 0 && ibIt != m_meshIBCache.end() && ibIt->second.resource)
	{
		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = ibIt->second.resource->GetGPUVirtualAddress();
		ibv.SizeInBytes    = ibIt->second.size;
		ibv.Format         = DXGI_FORMAT_R32_UINT;
		cmd->IASetIndexBuffer(&ibv);
		cmd->DrawIndexedInstanced(indexCount, instances, 0, 0, 0);
		return;
	}
	cmd->DrawInstanced(static_cast<UINT>(mesh.vertexCount()), instances, 0, 0);
}

/// @brief autoFitCascades のフレームだけ、カスケード境界と ortho の大きさを今のカメラから決め直す。
///        CbShadow を決める時 (主ビューと副ビュー) にだけ呼ぶ
void applyAutoCascadeFit() noexcept
{
	if (!m_cascadedShadowEnabled || !m_directionalShadow.config().autoFitCascades) return;
	const sgc::Vec3f forward = (m_clodCamera.target() - m_clodCamera.position()).normalized();
	m_directionalShadow.fitCascadesToCamera(m_clodCamera.fov(), m_clodCamera.aspectRatio(),
	                                        m_clodCamera.nearClip(), m_clodCamera.position(), forward);
}

/// @brief 主ビューの CbShadow を決める。このフレームのカメラが決まった後、最初の描画より前に呼ぶ
///        (beginFrame か、待たせた時は setCamera)。深度は endFrame に同じ行列で焼く (DX12ShadowList.hpp)
void prepareMainShadowCb()
{
	applyAutoCascadeFit();
	m_mainShadowCb = buildShadowCB(shadowCasterCentroid(), &m_mainShadowBake);
	m_mainShadowCbAddr = 0;
	// しるしの番号を固定するため、深度パスを別のリストに積んでもカスケードのぶんを置く (Dx12PassMarkers.hpp)
	markPass3D(dx12::Pass3D::ShadowCascade0);
	markPass3D(dx12::Pass3D::ShadowCascade1);
	markPass3D(dx12::Pass3D::ShadowCascade2);
}

/// @brief 前フレームの caster の重心。主ビューの手動のカスケードはここを中心に置く。caster が無ければ原点
[[nodiscard]] sgc::Vec3f shadowCasterCentroid() const noexcept
{
	if (!m_shadowEnabled || m_shadowCommandsPrev.empty()) { return {0.0f, 0.0f, 0.0f}; }
	sgc::Vec3f focus{0.0f, 0.0f, 0.0f};
	std::size_t casters = 0;
	for (const auto& c : m_shadowCommandsPrev)
	{
		if (c.fromDraws)
		{
			// まとめた drawMesh は、描いた順に 1 個ずつ足して、まとめない時と同じ値にする
			for (std::uint32_t i = 0; i < c.instanceCount; ++i)
			{
				const InstanceDataDx12& inst = m_shadowInstancesPrev[c.instanceFirst + i];
				focus.x += inst.row0[3];
				focus.y += inst.row1[3];
				focus.z += inst.row2[3];
			}
			casters += c.instanceCount;
			continue;
		}
		focus.x += c.world.m[0][3];
		focus.y += c.world.m[1][3];
		focus.z += c.world.m[2][3];
		++casters;
	}
	const float invN = 1.0f / static_cast<float>(casters);
	return {focus.x * invN, focus.y * invN, focus.z * invN};
}

/// @brief near / far の影マップの全カスケードを bake の行列で焼く。caster が無くてもクリアはする
/// @details beginShadowPass が毎回 depth=1.0 にクリアするので、caster 不在 / shadow 無効のフレームでも必ず呼ぶ。
///          clear しないと texture が 0 のまま残り、PS の SampleCmpLevelZero が「ライト視錐台内の全 pixel = 影」を
///          返して真っ黒になる (ENG-103)。焼くかは bake (影が有効だった CbShadow を決めた時) だけで決め、endFrame の
///          m_shadowEnabled は見ない。副ビューを閉じた後は主ビューの値に戻っていて、読む枠とも食い違いうるため
void drawShadowCascades(ID3D12GraphicsCommandList* cmd, const ShadowBake& bake, dx12::Dx12ShadowMap& nearMap,
                        dx12::Dx12ShadowMap& farMap)
{
	const bool drawCasters = !m_shadowCommands.empty() && bake.cascades > 0;
	// 影を落とす物が無く、前のフレームで 1.0 に塗り終えた枚数は、塗り直さない (毎フレームのバリア 2 回とクリア 1 回が浮く)
	if (drawCasters || !nearMap.isClear())
	{
		nearMap.beginShadowPass(cmd);
		if (drawCasters) { renderShadowCascade(cmd, bake.view[0], bake.proj[0]); }
		nearMap.endShadowPass(cmd);
		nearMap.setClear(!drawCasters && nearMap.isInitialized());
	}
	if (!bake.farAtlas || !farMap.isInitialized()) { return; }
	const bool drawFar = drawCasters && bake.cascades >= 2;
	if (!drawFar && farMap.isClear()) { return; }
	farMap.beginShadowPass(cmd);
	if (drawFar) { renderShadowCascade(cmd, bake.view[1], bake.proj[1]); }
	if (drawCasters && bake.cascades >= 3)
	{
		// カスケード 2 はアトラスの右列へ (renderShadowCascade は viewport を触らない)
		farMap.setColumn(cmd, 1);
		renderShadowCascade(cmd, bake.view[2], bake.proj[2]);
	}
	farMap.endShadowPass(cmd);
	farMap.setClear(!drawFar);
}

/// @brief drawShadowCascades がこの組の影マップに何かを積むか (積まないなら補助リストを開かなくてよい)
[[nodiscard]] bool shadowCascadesHaveWork(const ShadowBake& bake, const dx12::Dx12ShadowMap& nearMap,
                                          const dx12::Dx12ShadowMap& farMap) const noexcept
{
	const bool drawCasters = !m_shadowCommands.empty() && bake.cascades > 0;
	if (nearMap.isInitialized() && (drawCasters || !nearMap.isClear())) { return true; }
	if (!bake.farAtlas || !farMap.isInitialized()) { return false; }
	return (drawCasters && bake.cascades >= 2) || !farMap.isClear();
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

/// @brief CbShadow (b3) の ring 上の置き場。副ビューは prepareViewShadowCb で、主ビューは prepareMainShadowCb で決めた値を使う
/// @return GPU virtual address (0 で失敗)
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadShadowCB()
{
	if (m_activeView != nullptr && m_activeView->frame.shadowCb != 0) { return m_activeView->frame.shadowCb; }
	if (m_mainShadowCbAddr == 0 || m_mainShadowCbFrame != m_frameCounter)
	{
		const auto a = m_uploadRing.upload(&m_mainShadowCb, sizeof(ShadowCbData), 256);
		m_mainShadowCbAddr = a.valid() ? a.gpuAddr : 0;
		m_mainShadowCbFrame = m_frameCounter;
	}
	return m_mainShadowCbAddr;
}

/// @brief 今の影の設定 (副ビューの間はそのビューのもの) と focus で CbShadow を ring に置く
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS writeShadowCB(const sgc::Vec3f& focus, ShadowBake* bake)
{
	const ShadowCbData cb = buildShadowCB(focus, bake);
	const auto a = m_uploadRing.upload(&cb, sizeof(ShadowCbData), 256);
	return a.valid() ? a.gpuAddr : 0;
}

/// @brief 今の影の設定 (副ビューの間はそのビューのもの) と focus で CbShadow の中身を組む
/// @param bake 渡せば、深度パスが焼く光の view / proj を同じ値で書く
[[nodiscard]] ShadowCbData buildShadowCB(const sgc::Vec3f& focus, ShadowBake* bake = nullptr) const
{
	ShadowCbData cb;
	ShadowBake   baked;
	cb.shadowSoftness = m_shadowSoftness;
	cb.shadowTexel[0] = 1.0f / static_cast<float>(std::max(m_shadowMap.mapSize(), 1));
	cb.shadowTexel[1] = 1.0f / static_cast<float>(std::max(m_shadowMapFar.isInitialized() ? m_shadowMapFar.mapSize() : 1, 1));
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
		const auto P0s = m_directionalShadow.cascadeProjection(0, V0s);
		const auto P0 = toGlm(P0s);
		toColumnMajor(cb.lightViewProj, P0 * V);
		baked.view[0] = V0s;
		baked.proj[0] = P0s;
		baked.cascades = 1;

		if (m_cascadedShadowEnabled && m_shadowMapFar.isInitialized())
		{
			const auto V1s = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(1, focus));
			const auto P1s = m_directionalShadow.cascadeProjection(1, V1s);
			toColumnMajor(cb.lightViewProjFar, toGlm(P1s) * toGlm(V1s));
			cb.cascadeSplitDistance = m_directionalShadow.config().cascadeSplitDistance;
			baked.view[1] = V1s;
			baked.proj[1] = P1s;
			baked.cascades = 2;
			if (m_directionalShadow.config().cascadeCount >= 3)
			{
				const auto V2s = m_directionalShadow.lightViewMatrix(m_directionalShadow.cascadeFocus(2, focus));
				const auto P2s = m_directionalShadow.cascadeProjection(2, V2s);
				toColumnMajor(cb.lightViewProjFar2, toGlm(P2s) * toGlm(V2s));
				cb.cascadeSplitDistance2 = m_directionalShadow.config().cascadeSplitDistance2;
				baked.view[2] = V2s;
				baked.proj[2] = P2s;
				baked.cascades = 3;
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
	baked.farAtlas = m_cascadedShadowEnabled && m_shadowMapFar.isInitialized();
	if (bake != nullptr) { *bake = baked; }
	return cb;
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

	// slot に書いた frame W の内容は W+1 の動きベクトルのパスまで読まれる。デバイスのフェンスが
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
