// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12Effekseer.hpp の後に include)。副ビューの仕上げ
//
// endFrame の中、主ビューの粒のパスの後で、このフレームに begin した副ビューを 1 枚ずつ仕上げる。副ビューの資源を
// 主ビューの名前へ入れ替えて (enterView)、主ビューと同じパスの関数を呼ぶ。パスはそれぞれ今のビュー (currentPass) に
// 積まれたものだけを描く。動きベクトルと SSAO も入れ替えたまま描き、その後で resolve・bloom・tonemap・TAA か FXAA を
// 副ビューの資源で済ませる。

/// @brief 今のビューの WBOIT。副ビューでは初回に同じ大きさで作り、作れなければ nullptr を返して半透明を描かない
[[nodiscard]] dx12::WeightedBlendedOIT* currentOit()
{
	if (m_activeView == nullptr) { return m_oit.isInitialized() ? &m_oit : nullptr; }
	View3D& v = *m_activeView;
	if (!v.oit)
	{
		v.oit = std::make_unique<dx12::WeightedBlendedOIT>();
		try
		{
			v.oit->initialize(m_d3dDevice, static_cast<UINT>(v.desc.width), static_cast<UINT>(v.desc.height),
			                  DXGI_FORMAT_R16G16B16A16_FLOAT, MSAA_SAMPLE_COUNT);
		}
		catch (const std::exception&)
		{
			debug::verboseOnce("dx12.view.oit", "副ビューの半透明の描画先を作れなかったので、副ビューの半透明と粒は描きません。");
		}
	}
	return v.oit->isInitialized() ? v.oit.get() : nullptr;
}

/// @brief endFrame から呼び、このフレームに描いた副ビューを begin した順に仕上げる
void finishViews()
{
	bool any = false;
	for (const int pass : m_viewsThisFrame)
	{
		View3D* v = viewOfPass(pass);
		if (v == nullptr || v->frame.frame != m_frameCounter) { continue; }
		finishView(*v);
		any = true;
	}
	if (!any) { return; }
	// 後の主ビューのパスに備え、描き先と viewport を主ビューの状態へ戻す
	bindMainTargets();
	restoreMainState();
}

/// @brief 副ビュー 1 枚の空、水面、半透明、Effekseer、剣筋、粒、動きベクトル、SSAO を描き、resolve から TAA か FXAA まで済ませる
void finishView(View3D& v)
{
	saveMainViewState();
	enterView(v);
	m_skyboxDrawnThisFrame = !v.desc.skybox || v.frame.skyDrawn;
	if (!v.frame.shadows) { m_shadowEnabled = false; }
	drawSkyboxBeforeFirstDraw();   // 何も描かなかったビューにも空を張る
	if (v.frame.atmo) { drawViewAtmosphere(v, *v.atmo); }
	renderWaterPass();
	if (m_oitTransparentPSO)
	{
		renderTransparentPass(m_msaaColorRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		                      m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}
#if defined(MITIRU_HAS_EFFEKSEER)
	renderEffekseerPass();
#endif
	drawTrailPass();
	drawParticlesForCurrentPass();
	const bool ao = drawViewTemporalInputs(v);
	leaveView(v);
	restoreMainViewState();
	resolveAndPostView(v, ao);
	v.hasOutput = true;
}

/// @brief 副ビューの仕上げ後に呼び、このフレームに積んだ水面と剣筋を捨てる
void clearFrameQueues()
{
	m_waterQueue.clear();
	clearTrails();
	m_viewsThisFrame.clear();
}

/// @brief MSAA の色を HDR へ resolve し、空気遠近とフォグを掛け、主ビューの設定で bloom、tonemap (ao なら遮蔽を掛ける)、
///        TAA か FXAA の順に処理する
void resolveAndPostView(View3D& v, bool ao)
{
	auto* cl = m_graphicsCmdList.Get();
	viewBarrier(v.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
	cl->ResolveSubresource(v.hdr.Get(), 0, v.color.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
	viewBarrier(v.color.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	drawViewAerialFogComposite(v);
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const bool bloom = drawViewBloom(v);
	const bool taa = v.frame.taa;
	const bool fxaa = !taa && viewWantsFxaa(v);
	if (m_tonemapPSO && m_tonemapRootSig)
	{
		const bool post = taa || fxaa;
		ID3D12Resource* target = post ? v.fxaaSource.Get() : v.ldr.Get();
		const auto rtv = (post ? v.fxaaRtv : v.ldrRtv)->GetCPUDescriptorHandleForHeapStart();
		viewBarrier(target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		setTargetViewport(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
		drawViewFullscreen(v, m_tonemapPSO.Get(), 0, uploadViewTonemapCB(bloom, ao));
		viewBarrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		if (fxaa) { drawViewFxaa(v); }
		if (taa) { drawViewTaa(v); }
	}
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
}

/// @brief 主ビューで bloom が有効なとき、副ビューと同じ大きさで bloom を掛ける。初回は作成し、処理したら true を返す
[[nodiscard]] bool drawViewBloom(View3D& v)
{
	if (!v.desc.bloom || !bloomWanted()) { return false; }
	if (!v.bloom.ready())
	{
		if (!buildBloomChain(v.bloom, v.hdr.Get(), static_cast<UINT>(v.desc.width), static_cast<UINT>(v.desc.height)))
		{
			debug::verboseOnce("dx12.view.bloom", "副ビューの bloom の描画先を作れなかったので、副ビューは bloom なしで描きます。");
			return false;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE slot = v.srv->GetCPUDescriptorHandleForHeapStart();
		slot.ptr += m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2;
		writeBloomSrv(v.bloom.tex[2].Get(), slot);
	}
	recordBloomChain(v.bloom, static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
	return true;
}

[[nodiscard]] bool viewWantsFxaa(const View3D& v) const noexcept
{
	const bool mainAa = m_aaMode == AntiAliasing3D::MsaaFxaa || m_aaMode == AntiAliasing3D::Taa;
	return v.desc.antiAlias && mainAa && m_fxaaPSO && m_fxaaRootSig;
}

/// @brief tonemap の結果を FXAA に通して ldr へ書く。PSO と強さは主ビューと同じものを使う
void drawViewFxaa(const View3D& v)
{
	auto* cl = m_graphicsCmdList.Get();
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto rtv = v.ldrRtv->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setTargetViewport(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
	cl->SetPipelineState(m_fxaaPSO.Get());
	cl->SetGraphicsRootSignature(m_fxaaRootSig.Get());
	ID3D12DescriptorHeap* heaps[] = {v.srv.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootDescriptorTable(0, viewSrvGpu(v, kViewSrvFxaaSource));
	const auto cb = uploadFxaaCB(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
	if (cb != 0) { cl->SetGraphicsRootConstantBufferView(1, cb); }
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS uploadViewTonemapCB(bool bloom, bool ao)
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
	cb.tonemap.aoOn = ao ? 1.0f : 0.0f;
	cb.tonemap.bloomOn = bloom ? 1.0f : 0.0f;
	cb.tonemap.bloomStrength = m_bloomStrength;
	const auto a = m_uploadRing.upload(&cb, sizeof(cb), 256);
	return a.valid() ? a.gpuAddr : 0;
}
