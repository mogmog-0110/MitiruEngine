#pragma once

/// @file Renderer3D_DX12_Frame_impl.hpp
/// @brief Renderer3D_DX12 のフレーム描画・状態設定の実装本体（Renderer3D_DX12.hpp から機械的分割）

#include <mitiru/render/Renderer3D_DX12.hpp>

#ifdef _WIN32

namespace mitiru::render
{

/// @brief フレーム開始処理
/// @param clearColor バックバッファのクリア色
inline void Renderer3D_DX12::beginFrame(const sgc::Colorf& clearColor)
{
	MITIRU_ZONE_NAMED("Render::Dx12::BeginFrame");
	if (!m_initialized)
	{
		return;
	}
	// device が lost ならフレーム頭のフェンス待ちは済んでいない。GPU が読み残している
	// アロケータを Reset しないよう、このフレームは何も記録しない (#75)
	if (m_device == nullptr || m_device->isDeviceLost())
	{
		return;
	}

	// 前フレームで D3D12 が溜めた検証メッセージをファイルへ書き出す
	// (ENG-105 v2 MSAA debug)。Release build / debug layer 無効では no-op。
	pollD3D12Validation();
	++m_frameCounter;

	m_drawCallCount = 0;
	m_culledCount = 0;
	m_occludedCount = 0;
	m_frameActive = true;
	m_transparentCommands.clear();
	m_skyboxDrawnThisFrame = false;
	m_skinnedPoolCursor = 0;  // スキン描画 pool を巻き戻す
	collectRetiredGameMeshes();
	m_shadowCasterEnabled = true;
	m_outlineCasterEnabled = true;
	// 前フレームの shadow casters をスナップショットとして残し、当フレームの描画分をクリアする
	m_shadowCommandsPrev = std::move(m_shadowCommands);
	m_shadowCommands.clear();
	rotateShadowInstances();
	m_shadowDrawnThisFrame = false;

	// windowless (G2) では m_device->getSwapChain() が null になる。フレーム番号も
	// バックバッファ取得も m_device 経由にすることで、以降はスワップチェーンの
	// 有無を意識せず同じコードパスで描ける。
	const uint32_t frameIndex = m_device->currentFrameIndex();
	m_frameCursor = frameIndex;   // clod パスの upload ring 用
	beginTemporalFrame(frameIndex);
	clearTrails();

	// このスロットのオクルージョン resolve 読み戻しを消費する。
	// Dx12Device::beginFrame() がこのスロットの GPU 完了をフェンス待機
	// 済みのため、追加の同期なしで安全に Map できる。
	consumeOcclusionReadback(frameIndex);

	// GPU は前フレームの ring を読み終えているので reset してよい
	m_uploadRing.beginFrame(frameIndex);
	// SRV の cursor を自 frame の partition の先頭へ置く (in-flight の前フレーム分の descriptor を上書きしない)
	beginFrameMaterialTables(frameIndex);
	beginFrameLocalLights();
	// N フレームのあいだ参照されていない mesh VB/IB を解放する
	evictStaleMeshBuffers();

	/// Dx12Device::beginFrame() がすでにフェンス待機を済ませている。
	/// GPU 完了後に前フレームの一時リソースを解放する
	m_perFrameTempResources[frameIndex].clear();

	/// コマンドアロケータをリセットする
	HRESULT hr = m_commandAllocators[frameIndex]->Reset();
	if (FAILED(hr))
	{
		return;
	}

	/// コマンドリストをリセットし、メイン PSO をバインドする
	hr = m_graphicsCmdList->Reset(
		m_commandAllocators[frameIndex].Get(),
		m_mainPSO.Get());
	if (FAILED(hr))
	{
		return;
	}

	/// バックバッファを取得する (存在確認のみ)。
	/// Present↔RenderTarget の遷移は Dx12Device::beginFrame/endFrame が一元管理する。
	/// ここで再度 Present→RenderTarget バリアを張ると、同じ backbuffer に二重の遷移が
	/// 記録され (device がフレーム頭で既に RENDER_TARGET にしている)、debug layer が
	/// コマンドリストを丸ごと破棄して clear 色しか出なくなる。3D の描画は、すでに
	/// RENDER_TARGET 状態のバックバッファへ resolve/tonemap/overlay するだけでよい。
	auto* backBuffer = m_device->currentBackBuffer();
	if (!backBuffer)
	{
		return;
	}

	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Begin);
	m_frameTimer.beginFrame(frameIndex);
	m_frameTimer.begin(m_graphicsCmdList.Get(), kFrameTimerMain);

	/// shadow map を毎フレーム depth=1.0 にクリアする (ENG-103)。
	/// shadow が無効でも clear だけは走らせる必要がある。clear しないと
	/// texture が 0 のまま残って PS の SampleCmp が「フラスタム内 = 全部影」
	/// を返してシーン中央が真っ黒になる。renderShadowPass() 内で
	/// `m_shadowEnabled && casters あり ` のときだけ caster を発射し、
	/// それ以外はクリアのみで終わる。
	/// shadow pass は viewport/RTV/PSO を変更するため、その後でメイン RT を
	/// 再 bind する必要がある。
	renderShadowPass();
	renderSpotShadowPass();

	/// MSAA レンダーターゲットと深度バッファをバインドする (ENG-105 v2)
	/// メインパスは 4x MSAA color + 4x MSAA normal + 4x MSAA depth に描画する。
	/// outline / FXAA / overlay2D の前に ResolveSubresource で backbuffer へ書き込む。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Opaque);
	auto msaaColorRtv  = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto normalRtvHandle = m_normalRTVHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = { msaaColorRtv, normalRtvHandle };
	auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	m_graphicsCmdList->OMSetRenderTargets(2, rtvHandles, FALSE, &dsvHandle);

	/// MSAA color、法線、深度をクリアする
	const float cc[4] = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
	m_graphicsCmdList->ClearRenderTargetView(msaaColorRtv, cc, 0, nullptr);
	m_graphicsCmdList->ClearDepthStencilView(
		dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	const float normalClear[4] = {0.5f, 0.5f, 0.5f, 0.0f};
	m_graphicsCmdList->ClearRenderTargetView(normalRtvHandle, normalClear, 0, nullptr);
	// backbuffer はクリア不要。Resolve で MSAA color が上書きする

	/// ビューポートとシザー矩形を設定する
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = m_config.viewportWidth;
	viewport.Height = m_config.viewportHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = {};
	scissor.left = 0;
	scissor.top = 0;
	scissor.right = static_cast<LONG>(m_config.viewportWidth);
	scissor.bottom = static_cast<LONG>(m_config.viewportHeight);
	m_graphicsCmdList->RSSetScissorRects(1, &scissor);

	/// ルートシグネチャとプリミティブトポロジを設定する
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/// Fresnel モードではメイン PSO を差し替える
	if (m_outlineMode == OutlineMode::Fresnel && m_fresnelMainPSO)
	{
		m_graphicsCmdList->SetPipelineState(m_fresnelMainPSO.Get());
	}
}

/// @brief カメラを設定する
/// @param camera 3D カメラ
inline void Renderer3D_DX12::setCamera(const Camera3D& requested)
{
	// 揺れは目と注視点を画面の右・上へ平行移動して作る。射影をずらすと、射影から位置を戻す
	// SSAO / DOF / clod の各経路で食い違うが、カメラごと動かせば全経路で同じように絵が揺れる
	Camera3D camera = requested;
	if (m_cameraShakeX != 0.0f || m_cameraShakeY != 0.0f)
	{
		const sgc::Vec3f eye = requested.position();
		const sgc::Vec3f tgt = requested.target();
		const sgc::Vec3f f = (tgt - eye).normalized();
		const sgc::Vec3f r = f.cross(requested.up()).normalized();
		const sgc::Vec3f u = r.cross(f);
		// 注視点の距離で画面 1 枚ぶんの高さ。絵を +x へ動かすにはカメラを -x へ動かす
		const float viewH = 2.0f * (tgt - eye).length() * std::tan(requested.fov() * 0.5f);
		const float viewW = viewH * requested.aspectRatio();
		const sgc::Vec3f shift = r * (-m_cameraShakeX * viewW) + u * (m_cameraShakeY * viewH);
		camera = Camera3D(eye + shift, tgt + shift, requested.up(), requested.fov(),
		                  requested.aspectRatio(), requested.nearClip(), requested.farClip());
	}

	// sgc の行列を経由せず、glm で直接計算する（行列規約の不整合を避けるため）
	m_viewMatrix = lookAt(camera.position(), camera.target(), camera.up());
	setTemporalCamera(perspective(camera.fov(), camera.aspectRatio(),
		camera.nearClip(), camera.farClip()));
	m_cameraPosition = camera.position();
	m_clodCamera = camera;   // clod パスは自前の行列規約で再構成する
	// カリング判定は描画用の射影と切り離し、camera 自身の GL 規約の
	// viewProjectionMatrix() から視錐台を作る（Frustum::extractFromCamera 参照）。
	m_frustum.extractFromCamera(camera);
}

/// @brief メッシュを描画する
inline void Renderer3D_DX12::drawMesh(const Mesh& mesh,
                                      const sgc::Mat4f& worldTransform,
                                      const Material& material)
{
	drawMeshEx(mesh, worldTransform, material, nullptr, DrawTint{});
}

/// @brief 視錐台・オクルージョンで落とすか (落とした数も数える)
inline bool Renderer3D_DX12::cullMesh(const Mesh& mesh, const sgc::Mat4f& world)
{
	if (m_frustumCullingEnabled && !m_frustum.isMeshVisible(mesh.localAABB(), world))
	{
		++m_culledCount;
		return true;
	}
	if (m_occlusionCullingEnabled && m_occlusionCuller.hasDepth() &&
	    m_occlusionCuller.isOccluded(worldOcclusionAABB(mesh.localAABB(), world), occlusionViewProj().data()))
	{
		++m_occludedCount;
		return true;
	}
	return false;
}

/// @brief skybox が必要なら最初の不透明 draw の前に描く (GPU リソースは recording 中のここで遅延構築する)
inline void Renderer3D_DX12::drawSkyboxBeforeFirstDraw()
{
	if (m_skyboxEnabled && !m_skyboxDrawnThisFrame && m_skyboxCubemap.valid())
	{
		ensureSkyboxPipelineDx12();
		ensureSkyboxTextureDx12();
		drawSkyboxIfNeededDx12();
	}
}

/// @brief メッシュの VB/IB を張って描く (インデックスが無ければ頂点だけ)
inline bool Renderer3D_DX12::drawMeshBuffers(const Mesh& mesh)
{
	const auto& verts = mesh.vertices();
	const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex3D));
	auto* vb = acquireMeshBuffer(m_meshVBCache, mesh, verts.data(), vbSize);
	if (!vb) { return false; }
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = vb->GetGPUVirtualAddress();
	vbv.SizeInBytes = vbSize;
	vbv.StrideInBytes = sizeof(Vertex3D);
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	const auto& indices = mesh.indices();
	if (indices.empty())
	{
		m_graphicsCmdList->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
		return true;
	}
	const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));
	auto* ib = acquireMeshBuffer(m_meshIBCache, mesh, indices.data(), ibSize);
	if (!ib) { return false; }
	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = ib->GetGPUVirtualAddress();
	ibv.SizeInBytes = ibSize;
	ibv.Format = DXGI_FORMAT_R32_UINT;
	m_graphicsCmdList->IASetIndexBuffer(&ibv);
	m_graphicsCmdList->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
	return true;
}

/// @brief 材質のマップと色の調整つきでメッシュを描く (drawMesh・glTF モデルの共通の入口)
inline void Renderer3D_DX12::drawMeshEx(const Mesh& mesh, const sgc::Mat4f& worldTransform, const Material& material,
                                        const MaterialMaps* maps, const DrawTint& tint)
{
	if (!m_initialized || !m_graphicsCmdList || mesh.vertexCount() == 0) { return; }
	if (cullMesh(mesh, worldTransform)) { return; }
	drawSkyboxBeforeFirstDraw();

	/// 半透明は即時描画せず、不透明の後に OIT パスへ回す (順序非依存)。
	/// glTF が Blend を宣言していれば拡散色が不透明でも半透明として扱う。
	/// Mask は抜き (PS の clip) なので不透明パスのまま。
	const float alpha = material.diffuse.a * tint.mul[3];
	const bool wantsBlend = (material.alphaMode == Material::AlphaMode::Blend) ||
	                        (material.alphaMode != Material::AlphaMode::Mask && alpha < 1.0f);
	if (wantsBlend && m_oitTransparentPSO)
	{
		m_transparentCommands.push_back({&mesh, worldTransform, material, maps, tint});
		return;
	}

	if (auto* pso = selectMainPSO(material.doubleSided))
	{
		m_graphicsCmdList->SetPipelineState(pso);
	}
	if (!bindForwardDraw(worldTransform, material, maps, tint) || !drawMeshBuffers(mesh)) { return; }
	++m_drawCallCount;
	recordMotionDraw(mesh, worldTransform);

	/// シャドウキャスターを記録する（次フレームの shadow pass で使う）
	if (wantsShadowCasters())
	{
		m_shadowCommands.push_back({&mesh, worldTransform, 0, 0});
	}
}

/// @brief 半透明メッシュ 1 個を accum/reveal へ記録する (PSO は呼び出し側が設定済み)。
/// @details 不透明と同じ root 引数と VB/IB を使い、PSO だけ透明用。
inline void Renderer3D_DX12::recordTransparentMesh(const TransparentDraw& draw)
{
	if (draw.mesh->vertexCount() == 0) { return; }
	if (!bindForwardDraw(draw.world, draw.material, draw.maps, draw.tint) || !drawMeshBuffers(*draw.mesh)) { return; }
	++m_drawCallCount;
}

/// @brief 半透明 OIT パス。溜めた透明メッシュを accum/reveal へ蓄積し MSAA color へ合成する。
inline void Renderer3D_DX12::renderTransparentPass(D3D12_CPU_DESCRIPTOR_HANDLE msaaColorRtv,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	// accum=0 / reveal=1 にクリアし、accum+reveal+(読み取り専用 depth) を bind する。
	m_oit.beginAccumulate(m_graphicsCmdList.Get(), &dsv);

	D3D12_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MaxDepth = 1.0f;
	m_graphicsCmdList->RSSetViewports(1, &vp);
	D3D12_RECT sc = {0, 0, static_cast<LONG>(m_config.viewportWidth),
	                       static_cast<LONG>(m_config.viewportHeight)};
	m_graphicsCmdList->RSSetScissorRects(1, &sc);

	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_graphicsCmdList->SetPipelineState(m_oitTransparentPSO.Get());
	m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const auto& cmd : m_transparentCommands)
	{
		recordTransparentMesh(cmd);
	}

	// accum/reveal を resolve して MSAA color (HDR) へ over 合成する。
	m_oit.composite(m_graphicsCmdList.Get(), msaaColorRtv);
}

/// @brief フレーム終了処理（アウトラインパス + バリア + コマンド実行）
inline void Renderer3D_DX12::endFrame()
{
	MITIRU_ZONE_NAMED("Render::Dx12::EndFrame");
	if (!m_initialized || !m_graphicsCmdList || m_device == nullptr || m_device->isDeviceLost())
	{
		return;
	}

	// このフレームの局所光が出そろったので、froxel への割り当てを補助リストに積む (finalizeFrame でメインより先に流す)
	recordClusterBuild();

	// clod 世界ジオメトリ: offscreen に描いて depth-tested inject で
	// MSAA HDR + depth へ合成する (以降の OIT / resolve はこの上に重なる)。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Clod);
	renderClodPass();

#ifdef MITIRU_HAS_MAKINA
	// Makina の CSG ソリッド: MSAA HDR + depth へ直接レイマーチする。clod と同じく
	// 不透明の一部なので OIT より前。深度を書くので、後続の半透明は正しく隠れる。
	renderCsgPass();
#endif

	// 空 (主パスの空いた所)・空気遠近の froxel・体積フォグの compute。半透明は空の上に重なる
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Sky);
	drawAtmospherePasses();

	// 半透明 OIT: 不透明 (MSAA color + depth) の後、resolve/tonemap の前に HDR で合成する。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Transparent);
	if (!m_transparentCommands.empty() && m_oitTransparentPSO &&
	    m_msaaColorRtvHeap && m_dsvHeap)
	{
		renderTransparentPass(
			m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}
#if defined(MITIRU_HAS_EFFEKSEER)
	// Effekseer: 半透明の後・resolve の前。深度で隠れ、HDR のまま tonemap される
	renderEffekseerPass();
#endif
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Trails);
	timePostPass(PostGpuPass::Trails, [this] { drawTrailPass(); });
	/// 動きベクトル: TAA か動きのぼけが有効なときだけ。深度は主パスの DEPTH_WRITE のまま受け取って返す
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Velocity);
	timePostPass(PostGpuPass::Velocity, [this] { drawVelocityPasses(); });

	/// MSAA color RT (FP16) を HDR intermediate に Resolve し、
	/// tonemap PS で backbuffer (LDR R8G8B8A8) に書き出す (ENG-105 v2 + ENG-106)。
	/// 以降の outline post-process / FXAA / overlay2D は LDR backbuffer
	/// を前提に動く。tonemap 失敗時 (PSO 未生成) はフォールバックで
	/// HDR intermediate を backbuffer に CopyResource したいところだが
	/// フォーマット不一致のため不可 → tonemap PSO 生成失敗時は黒画面。
	/// SSAO (v40): 深度と法線 RT から遮蔽率を作り、tonemap が HDR 色に掛ける。
	/// 深度が DEPTH_WRITE・法線が RENDER_TARGET の位置 (outline パスと同じ前提) で呼ぶ
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Ssao);
	timePostPass(PostGpuPass::AmbientOcclusion, [this] { drawAmbientOcclusionPasses(); });
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::MsaaResolve);
	resolveMSAAColorToHDR();
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::AerialFog);
	timePostPass(PostGpuPass::AerialComposite, [this] { drawAerialFogComposite(); });
	/// bloom (v41): resolve 済み HDR から明部を落として戻し、tonemap が t2 で読んで露出の前に足す
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Bloom);
	drawBloomPasses();
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Tonemap);
	applyTonemap();

	/// ポストプロセスのアウトラインパス（深度エッジ検出）
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Outline);
	if (m_config.enableOutline && m_outlinePostPSO && m_depthSRVHeap)
	{
		drawPostProcessOutline();
	}

	/// オクルージョン深度 resolve（`kOcclusionUpdateInterval` フレームに 1 回だけ）。
	/// アウトラインパスと同じく深度を DEPTH_WRITE→PIXEL_SHADER_RESOURCE→DEPTH_WRITE
	/// で往復するため、深度が DEPTH_WRITE に戻っているこの位置で呼ぶ。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::OcclusionReadback);
	if (m_occlusionCullingEnabled)
	{
		++m_occlusionFrameCounter;
		if (m_occlusionFrameCounter % kOcclusionUpdateInterval == 0)
		{
			recordOcclusionResolvePass();
		}
	}

	/// 被写界深度 (v44)。深度を読むので、outline とオクルージョンが深度を DEPTH_WRITE に戻した後に置く
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::DepthOfField);
	drawDofPass();

	/// AA (FXAA か TAA、ENG-104) と動きのぼけ。outline までの 3D シーン色に掛ける。
	/// renderOverlay2D() より「前」に実行し、HUD/UI text にぼけが
	/// かからないようにする (2D 文字は pixel-perfect なまま残す)。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::AntiAliasing);
	timePostPass(PostGpuPass::AntiAliasing, [this] { drawAntiAliasingPass(); });
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::MotionBlur);
	timePostPass(PostGpuPass::MotionBlur, [this] { drawMotionBlurPass(); });
	/// 内部解像度で描いている間は、動きのぼけまでを済ませた絵を TAAU で出力の大きさへ戻す
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::Upscale);
	timePostPass(PostGpuPass::Upscale, [this] { drawUpscalePass(); });

	/// ニューラル現像 (M3): 現像済み 2D 画像をバックバッファへ全画面 α 合成する
	/// (FXAA 後・overlay2D 前 = HUD は 2D 絵の上に残る)。strength=0 のとき no-op。
	dx12::markPass(m_graphicsCmdList.Get(), dx12::Pass3D::StyleLive2DNeural);
	blitStyleDx12();

	/// Live2D (自前 D3D12 レンダラ): tonemap 後の backbuffer へ 2D オーバーレイ描画。
	/// (HUD overlay より前 = HUD は Live2D の上に残る)。要求が無ければ no-op。
	drawLive2DDx12();

	/// DirectML in-pipeline ニューラル後処理: backbuffer を CPU 往復なしで推論加工。
	/// (Live2D の後・HUD overlay の前 = HUD は加工されない)。無効なら no-op。
	neuralFxTickDx12();

	/// ニューラル・リライティング (Live2D の後・HUD overlay の前)。無効なら no-op。
	relightTickDx12();

	// HUD/2D は Engine が finalizeFrame 後に Screen::present3DOverlay() で描く。
	m_frameTimer.end(m_graphicsCmdList.Get(), kFrameTimerMain);

	// ここではコマンドリストを閉じず、finalizeFrame() で閉じる。
	// Engine 経由で使われない場合（単体テスト等）に備えて m_needsFinalize フラグで制御する。
	m_needsFinalize = true;
}

#ifdef MITIRU_HAS_MAKINA
inline void Renderer3D_DX12::renderCsgPass()
{
	if (m_csgQueue.empty())
	{
		return;
	}
	MITIRU_ZONE_NAMED("Render::Dx12::CsgPass");
	auto* cmd = m_graphicsCmdList.Get();
	if (cmd == nullptr || !m_msaaColorRtvHeap || !m_dsvHeap)
	{
		m_csgQueue.clear();
		return;
	}

	// パスは自分の PSO とシザーを持つが、ターゲットの束縛は呼び出し側が行う
	// (CsgRenderPass::draw の契約)。OIT と同じ MSAA HDR + depth に書く。
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
		m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	D3D12_VIEWPORT vp = {};
	vp.Width = m_config.viewportWidth;
	vp.Height = m_config.viewportHeight;
	vp.MaxDepth = 1.0f;
	cmd->RSSetViewports(1, &vp);

	const sgc::Vec3f lightDir = m_light.direction;

	for (const QueuedSolid& q : m_csgQueue)
	{
		CsgEntry& entry = m_csgCache[q.manifest];
		if (entry.failed)
		{
			continue;
		}
		if (!entry.pass.ready())
		{
			// 初回だけ読む。マニフェストにシーン名が書いてあるので、シーンは隣から読む。
			const std::size_t slash = q.manifest.find_last_of("/\\");
			const std::string dir =
				slash == std::string::npos ? std::string(".") : q.manifest.substr(0, slash);
			if (!entry.bake.loadFromFile(q.manifest) ||
			    !entry.solid.loadFromFile(dir + "/" + entry.bake.sceneName()) ||
			    !entry.pass.initialize(m_d3dDevice, entry.solid, entry.bake, {}))
			{
				entry.failed = true;
				std::fprintf(stderr,
				             "[mitiru][csg] could not stand up '%s': %s\n",
				             q.manifest.c_str(),
				             entry.pass.error().empty() ? "load failed"
				                                        : entry.pass.error().c_str());
				continue;
			}
		}
		csg::CsgDrawDesc desc;
		desc.position = q.position;
		desc.rotationYDeg = q.rotYDeg;
		desc.scale = q.scale;
		// live に焼いた立体 (D-15) だけが今フレームの姿を必要とする。焼き込みの bake には
		// 渡さない。渡しても読まれないが、毎フレーム平坦化の手間をかける理由が無い。
		const makina::EvalProgram* program =
			entry.bake.live() ? &entry.solid.programAt(q.timeSec) : nullptr;
		if (!entry.pass.draw(cmd, m_clodCamera, lightDir, desc,
		                     static_cast<int>(m_config.viewportWidth),
		                     static_cast<int>(m_config.viewportHeight), program))
		{
			std::fprintf(stderr, "[mitiru][csg] draw refused: %s\n",
			             entry.pass.error().c_str());
		}
	}
	m_csgQueue.clear();
}
#endif

/// @brief clod 世界ジオメトリパス: 記録 → depth-tested inject 合成
inline void Renderer3D_DX12::renderClodPass()
{
	if (!m_clod.supported() || !m_clod.hasWork() || !m_clodInjectPSO ||
	    !m_msaaColorRtvHeap || !m_dsvHeap)
	{
		m_clod.endFrame();
		return;
	}
	MITIRU_ZONE_NAMED("Render::Dx12::ClodPass");
	auto* cmd = m_graphicsCmdList.Get();
	const auto width = static_cast<uint32_t>(m_config.viewportWidth);
	const auto height = static_cast<uint32_t>(m_config.viewportHeight);
	const float dir[3] = { m_light.direction.x, m_light.direction.y, m_light.direction.z };
	const float col[3] = { m_light.color.r, m_light.color.g, m_light.color.b };
	m_clod.setProjectionJitter(m_jitterNdc.x, m_jitterNdc.y);
	m_clod.record(cmd, m_clodCamera, dir, col, 0.30f, width, height, m_frameCursor);

	// inject: clod の color + visbuffer 深度を MSAA HDR + depth へ (両方向 depth test)
	if (m_clodInjectKey != m_clod.colorTexture())
	{
		const UINT inc =
			m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		auto cpu = m_clodInjectHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = 1;
		m_d3dDevice->CreateShaderResourceView(m_clod.colorTexture(), &sv, cpu);
		cpu.ptr += inc;
		D3D12_SHADER_RESOURCE_VIEW_DESC bv = {};
		bv.Format = DXGI_FORMAT_UNKNOWN;
		bv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		bv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		bv.Buffer.NumElements = width * height;
		bv.Buffer.StructureByteStride = 8;
		m_d3dDevice->CreateShaderResourceView(m_clod.visBuffer(), &bv, cpu);
		m_clodInjectKey = m_clod.colorTexture();
	}

	m_clod.transitionForInject(cmd);
	cmd->SetGraphicsRootSignature(m_clodInjectRS.Get());
	cmd->SetPipelineState(m_clodInjectPSO.Get());
	ID3D12DescriptorHeap* heaps[] = { m_clodInjectHeap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, m_clodInjectHeap->GetGPUDescriptorHandleForHeapStart());
	const uint32_t dims[2] = { width, height };
	cmd->SetGraphicsRoot32BitConstants(1, 2, dims, 0);
	const auto rtv = m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	const D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
	const D3D12_RECT sc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
	m_clod.transitionAfterInject(cmd);
	m_clod.endFrame();
}

/// @brief コマンドリストを閉じて GPU で実行する（Engine::endFrame 前に呼ぶ）
inline void Renderer3D_DX12::finalizeFrame()
{
	MITIRU_ZONE_NAMED("Render::Dx12::FinalizeFrame");
	// 揺れはそのフレームの setCamera にだけ反映する。残すと、次のフレームで揺れの量が渡される前に
	// カメラを置く経路 (host 側の別描画など) が前フレームの揺れで描かれる
	m_cameraShakeX = 0.0f;
	m_cameraShakeY = 0.0f;
	if (!m_needsFinalize || !m_graphicsCmdList)
	{
		return;
	}
	m_needsFinalize = false;
	if (m_device == nullptr || m_device->isDeviceLost())
	{
		return;
	}

	/// RenderTarget→Present の遷移は直後に呼ばれる Dx12Device::endFrame が行う。
	/// ここで張ると二重バリアになるので、描画コマンドを閉じて実行するだけにする。
	m_graphicsCmdList->Close();
	// 局所光の割り当てはメインの描画が読むので、同じ提出の先に置く
	ID3D12CommandList* lists[] = {m_lightCmdList.Get(), m_graphicsCmdList.Get()};
	if (m_lightListRecorded)
	{
		m_device->commandQueue()->ExecuteCommandLists(2, lists);
	}
	else
	{
		m_device->commandQueue()->ExecuteCommandLists(1, lists + 1);
	}
	m_lightListRecorded = false;

	/// 一時アップロードバッファを現在のフレームスロットに退避する
	const uint32_t frameIndex = m_device->currentFrameIndex();
	m_perFrameTempResources[frameIndex] = std::move(m_frameTempResources);
}

/// @brief メイン PSO とルートシグネチャに戻す
inline void Renderer3D_DX12::restoreMainState()
{
	if (m_graphicsCmdList && m_mainPSO && m_rootSignature)
	{
		m_graphicsCmdList->SetPipelineState(m_mainPSO.Get());
		m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
		m_graphicsCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}

/// @brief 複数ライトを設定する（DX12）
/// @details kMaxLights を超える分は捨てる。先頭は主光源になり、点光源・スポットは
///          useMultiLight=true の間だけ毎フレームの局所光に加わる (appendLegacyLights)。
inline void Renderer3D_DX12::setLights(std::span<const Light> lights)
{
	m_lights.clear();
	const int n = std::min(
		static_cast<int>(lights.size()), kMaxLights);
	m_lights.reserve(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i)
	{
		m_lights.push_back(lights[i]);
	}
	// 互換のため、先頭ライトを既存の単一光源パスにも反映する
	if (!m_lights.empty())
	{
		m_light = m_lights.front();
		m_lightBaseColor = m_light.color;
		applySkyToLight();
	}
}

/// @brief 現在の (shaderMode, outlineMode) に対する PSO を選ぶ
inline ID3D12PipelineState* Renderer3D_DX12::selectMainPSO(bool doubleSided) const noexcept
{
	// 両面は同じ PS のカリング無しの双子を使う。双子が無い場合だけ片面を使う。
	const auto pick = [doubleSided](const ComPtr<ID3D12PipelineState>& one,
	                                const ComPtr<ID3D12PipelineState>& both)
		-> ID3D12PipelineState*
	{
		if (doubleSided && both) { return both.Get(); }
		return one.Get();
	};

	// Fresnel は OutlineMode 由来で main PSO を差し替える既存仕様
	// (Fresnel は輪郭専用で両面の双子を持たない)
	if (m_outlineMode == OutlineMode::Fresnel && m_fresnelMainPSO)
	{
		return m_fresnelMainPSO.Get();
	}
	switch (m_shaderMode)
	{
	case ShaderMode3D::PBR:
		if (m_pbrPSO) { return pick(m_pbrPSO, m_pbrPSONoCull); }
		break;
	case ShaderMode3D::Phong:
		if (m_phongPSO) { return pick(m_phongPSO, m_phongPSONoCull); }
		debug::warnOnce("dx12.shadermode.phong_pso_missing",
		                "Phong PSO が未生成 — Toon にフォールバック");
		break;
	case ShaderMode3D::Unlit:
		if (m_unlitPSO) { return pick(m_unlitPSO, m_unlitPSONoCull); }
		debug::warnOnce("dx12.shadermode.unlit_pso_missing",
		                "Unlit PSO が未生成 — Toon にフォールバック");
		break;
	case ShaderMode3D::Flat:
		if (m_flatPSO) { return pick(m_flatPSO, m_flatPSONoCull); }
		debug::warnOnce("dx12.shadermode.flat_pso_missing",
		                "Flat PSO が未生成 — Toon にフォールバック");
		break;
	case ShaderMode3D::Toon:
		break;
	default:
		// Posterize/Halftone/Hatching/GradientMap/Silhouette 等は DX12 未実装（B9）
		debug::warnOnce("dx12.shadermode.unimplemented." + std::to_string(static_cast<int>(m_shaderMode)),
		                "DX12 が未実装の ShaderMode — Toon にフォールバック: mode=" +
		                    std::to_string(static_cast<int>(m_shaderMode)));
		break;
	}
	return pick(m_mainPSO, m_mainPSONoCull); // フォールバック: toon
}

} // namespace mitiru::render

#endif // _WIN32
