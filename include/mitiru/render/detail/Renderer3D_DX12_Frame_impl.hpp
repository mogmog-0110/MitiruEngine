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

	// 前フレームで D3D12 が溜めた検証メッセージをファイルへ書き出す
	// (ENG-105 v2 MSAA debug)。Release build / debug layer 無効では no-op。
	pollD3D12Validation();
	++m_frameCounter;

	m_drawCallCount = 0;
	m_frameActive = true;
	m_outlineCommands.clear();
	m_skyboxDrawnThisFrame = false;
	// 前フレームの shadow casters をスナップして当フレーム描画分をクリア
	m_shadowCommandsPrev = std::move(m_shadowCommands);
	m_shadowCommands.clear();
	m_shadowDrawnThisFrame = false;

	auto* swapChain = m_device->getSwapChain();
	if (!swapChain)
	{
		return;
	}

	const uint32_t frameIndex = swapChain->currentBackBufferIndex();

	// GPU は前フレームの ring を読み終えているので reset OK
	m_uploadRing.beginFrame(frameIndex);
	// アルベド SRV カーソルもフレームの先頭で 0 に
	m_albedoSrvCursor = 0;

	/// Dx12Device::beginFrame()が既にフェンス待機済み
	/// GPU完了後に前フレームの一時リソースを解放する
	m_perFrameTempResources[frameIndex].clear();

	/// コマンドアロケータをリセットする
	HRESULT hr = m_commandAllocators[frameIndex]->Reset();
	if (FAILED(hr))
	{
		return;
	}

	/// コマンドリストをリセットし、メインPSOをバインドする
	hr = m_graphicsCmdList->Reset(
		m_commandAllocators[frameIndex].Get(),
		m_mainPSO.Get());
	if (FAILED(hr))
	{
		return;
	}

	/// バックバッファを取得する
	auto* backBufferBase = swapChain->backBuffer();
	auto* backBuffer = static_cast<gfx::Dx12RenderTarget*>(backBufferBase);
	if (!backBuffer)
	{
		return;
	}

	/// バリア: Present -> RenderTarget
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffer->nativeResource();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &barrier);

	/// shadow map を毎フレーム depth=1.0 にクリアする (ENG-103)。
	/// shadow が無効でも clear だけは走らせる必要がある — clear しないと
	/// texture が 0 のまま残って PS の SampleCmp が「フラスタム内 = 全部影」
	/// を返してシーン中央が真っ黒になる。renderShadowPass() 内で
	/// `m_shadowEnabled && casters あり` のときだけ caster を発射し、
	/// それ以外はクリアのみで終わる。
	/// shadow pass は viewport/RTV/PSO を変更するため、その後でメイン RT を
	/// 再 bind する必要がある。
	renderShadowPass();

	/// MSAA レンダーターゲットと深度バッファをバインドする (ENG-105 v2)
	/// メインパスは 4x MSAA color + 4x MSAA normal + 4x MSAA depth に描画する。
	/// outline / FXAA / overlay2D 前に ResolveSubresource で backbuffer に焼く。
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
	// backbuffer はクリア不要 — Resolve で MSAA color が上書きする

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

	/// FresnelモードではメインPSOを差し替える
	if (m_outlineMode == OutlineMode::Fresnel && m_fresnelMainPSO)
	{
		m_graphicsCmdList->SetPipelineState(m_fresnelMainPSO.Get());
	}
}

/// @brief カメラを設定する
/// @param camera 3Dカメラ
inline void Renderer3D_DX12::setCamera(const Camera3D& camera)
{
	// sgcの行列を経由せず、glmで直接計算する（行列規約の不整合を回避）
	m_viewMatrix = lookAt(camera.position(), camera.target(), camera.up());
	m_projMatrix = perspective(camera.fov(), camera.aspectRatio(),
		camera.nearClip(), camera.farClip());
	m_cameraPosition = camera.position();
}

/// @brief メッシュを描画する
/// @param mesh 描画対象メッシュ
/// @param worldTransform ワールド変換行列
/// @param material マテリアル
inline void Renderer3D_DX12::drawMesh(const Mesh& mesh,
                                      const sgc::Mat4f& worldTransform,
                                      const Material& material)
{
	if (!m_initialized || !m_graphicsCmdList)
	{
		return;
	}

	if (mesh.vertexCount() == 0)
	{
		return;
	}

	/// skybox が必要なら最初の drawMesh の前に描画する
	/// （GPU リソースは遅延構築。command list が recording 中であるため
	///  ここでまとめて初期化＋描画ができる）
	if (m_skyboxEnabled && !m_skyboxDrawnThisFrame && m_skyboxCubemap.valid())
	{
		ensureSkyboxPipelineDx12();
		ensureSkyboxTextureDx12();
		drawSkyboxIfNeededDx12();
	}

	/// PSO 選択（ShaderMode + multi-light + outline mode の組み合わせ）
	const bool useMulti = m_useMultiLight && !m_lights.empty() && m_multiLightPSO;
	if (auto* pso = selectMainPSO())
	{
		m_graphicsCmdList->SetPipelineState(pso);
	}

	/// 定数バッファを描画ごとに ring buffer から切り出す
	const auto cbTransformAddr = uploadTransformCB(worldTransform);
	const auto cbLightingAddr  = uploadLightingCB(material);
	if (cbTransformAddr == 0 || cbLightingAddr == 0)
	{
		return;
	}

	D3D12_GPU_VIRTUAL_ADDRESS cbLightArrayAddr = 0;
	if (useMulti)
	{
		cbLightArrayAddr = uploadLightArrayCB();
		if (cbLightArrayAddr == 0) { return; }
	}

	/// 頂点バッファをキャッシュまたはアップロードする
	const auto& verts = mesh.vertices();
	const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex3D));
	const void* meshKey = static_cast<const void*>(&mesh);

	auto& cachedVB = m_meshVBCache[meshKey];
	if (!cachedVB.resource || cachedVB.size != vbSize)
	{
		cachedVB.resource = createUploadBuffer(vbSize);
		cachedVB.size = vbSize;
		if (cachedVB.resource)
		{
			uploadToBuffer(cachedVB.resource.Get(), verts.data(), vbSize);
		}
	}
	if (!cachedVB.resource) { return; }

	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = cachedVB.resource->GetGPUVirtualAddress();
	vbv.SizeInBytes = vbSize;
	vbv.StrideInBytes = sizeof(Vertex3D);
	m_graphicsCmdList->IASetVertexBuffers(0, 1, &vbv);

	/// 定数バッファをバインドする（ring buffer 内のアドレス）
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(0, cbTransformAddr);
	m_graphicsCmdList->SetGraphicsRootConstantBufferView(1, cbLightingAddr);
	if (useMulti)
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(2, cbLightArrayAddr);
	}
	// ring buffer は frame-fence で再利用されるため明示的な resource 保持は不要

	/// CbShadow (b3) — light-space view * proj
	const auto cbShadowAddr = uploadShadowCB();
	if (cbShadowAddr != 0)
	{
		m_graphicsCmdList->SetGraphicsRootConstantBufferView(3, cbShadowAddr);
	}

	/// SRV table { t0=albedo, t1=shadow }: 毎 draw で shader-visible heap に
	/// 2 連続 SRV を append、descriptor table 4 にバインドする
	ID3D12DescriptorHeap* heaps[] = { m_albedoSrvHeap.Get() };
	m_graphicsCmdList->SetDescriptorHeaps(1, heaps);
	const auto srvGpu = writeMainSrvTable(material.albedoTexture);
	if (srvGpu.ptr != 0)
	{
		m_graphicsCmdList->SetGraphicsRootDescriptorTable(4, srvGpu);
	}

	/// インデックス付きまたは非インデックスの描画を実行する
	const auto& indices = mesh.indices();
	if (!indices.empty())
	{
		const UINT ibSize = static_cast<UINT>(
			indices.size() * sizeof(uint32_t));

		auto& cachedIB = m_meshIBCache[meshKey];
		if (!cachedIB.resource || cachedIB.size != ibSize)
		{
			cachedIB.resource = createUploadBuffer(ibSize);
			cachedIB.size = ibSize;
			if (cachedIB.resource)
			{
				uploadToBuffer(cachedIB.resource.Get(), indices.data(), ibSize);
			}
		}
		if (!cachedIB.resource) { return; }

		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = cachedIB.resource->GetGPUVirtualAddress();
		ibv.SizeInBytes = ibSize;
		ibv.Format = DXGI_FORMAT_R32_UINT;
		m_graphicsCmdList->IASetIndexBuffer(&ibv);

		m_graphicsCmdList->DrawIndexedInstanced(
			static_cast<UINT>(indices.size()), 1, 0, 0, 0);
	}
	else
	{
		m_graphicsCmdList->DrawInstanced(
			static_cast<UINT>(verts.size()), 1, 0, 0);
	}

	++m_drawCallCount;

	/// アウトラインパス用にコマンドを記録する
	if (m_config.enableOutline && m_outlinePSO)
	{
		OutlineDrawCommand cmd;
		cmd.mesh = &mesh;
		cmd.worldTransform = worldTransform;
		m_outlineCommands.push_back(cmd);
	}

	/// シャドウキャスター記録（次フレームの shadow pass で使われる）
	if (m_shadowEnabled)
	{
		m_shadowCommands.push_back({&mesh, worldTransform});
	}
}

/// @brief フレーム終了処理（アウトラインパス + バリア + コマンド実行）
inline void Renderer3D_DX12::endFrame()
{
	MITIRU_ZONE_NAMED("Render::Dx12::EndFrame");
	if (!m_initialized || !m_graphicsCmdList)
	{
		return;
	}

	/// MSAA color RT (FP16) を HDR intermediate に Resolve し、
	/// tonemap PS で backbuffer (LDR R8G8B8A8) に焼く (ENG-105 v2 + ENG-106)。
	/// 以降の outline post-process / FXAA / overlay2D は LDR backbuffer
	/// 前提で動く。tonemap 失敗時 (PSO 未生成) はフォールバックで
	/// HDR intermediate を backbuffer に CopyResource したいところだが
	/// フォーマット不一致のため不可 → tonemap PSO 生成失敗時は黒画面。
	resolveMSAAColorToHDR();
	applyTonemap();

	/// ポストプロセス アウトラインパス（深度エッジ検出）
	if (m_config.enableOutline && m_outlinePostPSO && m_depthSRVHeap)
	{
		drawPostProcessOutline();
	}

	/// アウトラインパス完了後にコマンドバッファをクリアする
	m_outlineCommands.clear();

	/// FXAA ポストプロセス AA (ENG-104)
	/// outline までの 3D シーン色に対して fast approximate AA を適用する。
	/// renderOverlay2D() より「前」に走らせて HUD/UI text に FXAA ブラーを
	/// かけないようにする (2D 文字は pixel-perfect なまま残す)。
	drawFXAAPass();

	/// 2Dオーバーレイパス（HUD/UI描画）
	if (m_overlayScreen && m_overlay2DPSO)
	{
		renderOverlay2D();
	}

	// ここではコマンドリストを閉じない。
	// Engineが ImGui 描画をコマンドリストに追記した後、
	// finalizeFrame() で閉じる。
	// ただし、Engine経由で使われない場合（単体テスト等）に備えて
	// m_needsFinalize フラグで制御する。
	m_needsFinalize = true;
}

/// @brief コマンドリストを閉じてGPU実行する（Engine::endFrame前に呼ぶ）
/// @details endFrame()で3D描画完了後、ImGui描画を追記してからこれを呼ぶ。
inline void Renderer3D_DX12::finalizeFrame()
{
	MITIRU_ZONE_NAMED("Render::Dx12::FinalizeFrame");
	if (!m_needsFinalize || !m_graphicsCmdList)
	{
		return;
	}
	m_needsFinalize = false;

	/// バリア: RenderTarget -> Present
	auto* swapChain = m_device->getSwapChain();
	auto* backBuffer = static_cast<gfx::Dx12RenderTarget*>(
		swapChain->backBuffer());

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffer->nativeResource();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_graphicsCmdList->ResourceBarrier(1, &barrier);

	/// コマンドリストを閉じて実行する
	m_graphicsCmdList->Close();
	ID3D12CommandList* lists[] = {m_graphicsCmdList.Get()};
	m_device->commandQueue()->ExecuteCommandLists(1, lists);

	/// 一時アップロードバッファを現在のフレームスロットに退避する
	const uint32_t frameIndex = m_device->getSwapChain()->currentBackBufferIndex();
	m_perFrameTempResources[frameIndex] = std::move(m_frameTempResources);
}

/// @brief メインPSOとルートシグネチャに戻す
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
/// @details kMaxLights を超える分は捨てる。useMultiLight=true の時のみ
///          drawMesh で b2 にアップロードされる。
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
	// 互換: 先頭ライトを既存の単一光源パスにも反映する
	if (!m_lights.empty())
	{
		m_light = m_lights.front();
	}
}

/// @brief 現在の (shaderMode, useMultiLight, outlineMode) に対する PSO を選ぶ
inline ID3D12PipelineState* Renderer3D_DX12::selectMainPSO() const noexcept
{
	// Fresnel は OutlineMode 由来で main PSO を差し替える既存仕様
	if (m_outlineMode == OutlineMode::Fresnel && m_fresnelMainPSO)
	{
		return m_fresnelMainPSO.Get();
	}
	// multi-light は ShaderMode より優先
	if (m_useMultiLight && !m_lights.empty() && m_multiLightPSO)
	{
		return m_multiLightPSO.Get();
	}
	switch (m_shaderMode)
	{
	case ShaderMode3D::Phong: if (m_phongPSO) return m_phongPSO.Get(); break;
	case ShaderMode3D::Unlit: if (m_unlitPSO) return m_unlitPSO.Get(); break;
	case ShaderMode3D::Flat:  if (m_flatPSO)  return m_flatPSO.Get();  break;
	case ShaderMode3D::Toon:
	default: break;
	}
	return m_mainPSO.Get(); // フォールバック: toon
}

} // namespace mitiru::render

#endif // _WIN32
