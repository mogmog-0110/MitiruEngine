// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12ViewTemporal.hpp の後に include)。副ビューの空気遠近と体積フォグ
//
// 副ビューは空気遠近の froxel (32^3) と体積フォグの froxel (ビューの 8 画素四方 × 64 枚、注入 2 枚と積分 1 枚)、
// それを指す記述子の組を持つ。空の LUT (透過・多重散乱・空の見え方) は主ビューが同じフレームに作ったものを読む。
// 仕上げ (finishView) で資源を入れ替え、主ビューと同じ関数で空気遠近・フォグの注入と積分・空を描き、
// resolve の後にビューの HDR へ合成する。フォグの履歴とずらしの相はビューごとに持つ。

private:

/// @brief beginView の最初 (入れ替えの前) に呼ぶ。空を張るビュー (desc.skybox) で空か体積フォグが有効なら、資源を用意して
///        frame.atmo を立てる。空の無い小窓 (真上からの地図など) には空気遠近もフォグも掛けない
void prepareViewAtmosphere(View3D& v)
{
	v.frame.atmo = v.desc.skybox && (m_sky.enabled || m_fog.enabled) && ensureViewAtmosphere(v);
	if (!v.frame.atmo) { return; }
	ViewAtmosphere& a = *v.atmo;
	// 主ビューのフォグの履歴を捨てた (設定を変えた) フレームと、前のフレームに描かなかった時は、ビューの履歴も捨てる
	if (a.lastFrame + 1 != m_frameCounter || !m_fogHistoryValid) { a.fogHistoryValid = false; }
	a.lastFrame = m_frameCounter;
	a.frameCb = 0;
	a.fogCb = 0;
}

/// @brief 記述子の組と空気遠近の froxel を初めて要る時に、フォグの froxel はフォグを初めて使う時に作る
[[nodiscard]] bool ensureViewAtmosphere(View3D& v)
{
	if (!v.atmo)
	{
		auto a = std::make_unique<ViewAtmosphere>();
		const bool ok = createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kAtmoSetCount * kAtmoSetSize, true, a->heap) &&
		                createAtmoTexture(kAtmoApSlices, kAtmoApSlices, kAtmoApSlices, L"Renderer3D view aerial perspective",
		                                  a->aerial);
		if (!ok)
		{
			debug::verboseOnce("dx12.view.atmosphere", "副ビューの空とフォグの資源を作れなかったので、副ビューは空とフォグなしで描きます。");
			return false;
		}
		v.atmo = std::move(a);
	}
	if (!v.atmo->heap) { return false; }
	return !m_fog.enabled || v.atmo->fogIntegrated || createViewFog(v);
}

[[nodiscard]] bool createViewFog(View3D& v)
{
	ViewAtmosphere& a = *v.atmo;
	const auto w = static_cast<UINT>(fog::froxelCount(v.desc.width));
	const auto h = static_cast<UINT>(fog::froxelCount(v.desc.height));
	constexpr UINT d = fog::kFogSlices;
	const bool ok = createAtmoTexture(w, h, d, L"Renderer3D view fog inject 0", a.fogInject[0]) &&
	                createAtmoTexture(w, h, d, L"Renderer3D view fog inject 1", a.fogInject[1]) &&
	                createAtmoTexture(w, h, d, L"Renderer3D view fog integrated", a.fogIntegrated);
	if (!ok)
	{
		a.fogIntegrated.Reset();
		debug::verboseOnce("dx12.view.fog", "副ビューのフォグの資源を作れなかったので、副ビューはフォグなしで描きます。");
		return false;
	}
	a.fogDims[0] = w;
	a.fogDims[1] = h;
	a.fogDims[2] = d;
	a.fogHistoryValid = false;
	return true;
}

/// @brief enterView と leaveView から。空気遠近とフォグの資源と状態を主ビューのものと入れ替える
void swapViewAtmosphere(ViewAtmosphere& a)
{
	std::swap(m_atmoHeap, a.heap);
	std::swap(m_atmoAerial, a.aerial);
	std::swap(m_fogInject[0], a.fogInject[0]);
	std::swap(m_fogInject[1], a.fogInject[1]);
	std::swap(m_fogIntegrated, a.fogIntegrated);
	std::swap(m_fogDims, a.fogDims);
	std::swap(m_fogWrite, a.fogWrite);
	std::swap(m_fogHistoryValid, a.fogHistoryValid);
	std::swap(m_fogFrameIndex, a.fogFrameIndex);
	std::swap(m_fogPrevViewProj, a.fogPrevViewProj);
	std::swap(m_fogPrevCamera, a.fogPrevCamera);
}

/// @brief 入れ替え済みの副ビューで、記述子が指す資源 (空の LUT・ビューの影マップ・froxel・深度) が変わっていれば書き直す。
///        前に GPU が読んだ組を書き直す時は、読み終わるのを待つ
void refreshViewAtmosphereViews(ViewAtmosphere& written)
{
	const std::array<const ID3D12Resource*, 7> now = {
		m_atmoTransmittance.Get(), m_atmoMultiScatter.Get(), m_atmoSkyView.Get(), m_shadowMap.nativeResource(),
		m_shadowMapFar.nativeResource(), m_fogIntegrated.Get(), m_depthBuffer.Get()};
	if (now == written.written) { return; }
	if (written.used && m_device != nullptr) { m_device->waitForGpu(); }
	for (UINT set = 0; set < kAtmoSetCount; ++set) { clearAtmosphereSet(set); }
	writeAtmosphereViews();
	if (m_fogIntegrated) { writeFogViews(); }
	written.written = now;
}

/// @brief 入れ替え済みの副ビューで、空気遠近の froxel・フォグの注入と積分・空を描く。主ビューの空と霧が
///        このフレームに用意したパイプラインと LUT を使うので、主ビューの drawAtmospherePasses の後に呼ぶ
void drawViewAtmosphere(View3D& v, ViewAtmosphere& state)
{
	const D3D12_GPU_VIRTUAL_ADDRESS mainCb = m_atmoFrameCB;
	if (mainCb == 0) { return; }
	refreshViewAtmosphereViews(state);
	state.used = true;
	uploadAtmosphereFrameCB();
	if (m_atmoFrameCB != 0)
	{
		const bool sky = m_sky.enabled && m_skyDrawPSO && m_atmoAerialPSO;
		const bool fog = m_fog.enabled && m_fogInjectPSO && m_fogIntegrated;
		bindAtmosphereCompute();
		if (sky)
		{
			dispatchAtmo(m_atmoAerialPSO.Get(), kAtmoSetAerial, m_atmoAerial.Get(), kAtmoApSlices / 4, kAtmoApSlices / 4,
			             kAtmoApSlices / 4);
		}
		if (fog) { recordVolumetricFog(); }
		if (sky) { drawSkyIntoScene(); }
		state.frameCb = m_atmoFrameCB;
		state.fogCb = uploadFogCB();   // 合成はフォグが無効でも霧の CB を読む (主ビューの drawAerialFogComposite と同じ)
	}
	m_atmoFrameCB = mainCb;
	restoreMainState();
}

/// @brief resolve の直後 (ビューの HDR が RESOLVE_DEST) に呼ぶ。空気遠近とフォグをビューの HDR に掛ける。入れ替えを戻した後で呼ぶ
void drawViewAerialFogComposite(View3D& v)
{
	if (!v.frame.atmo || !v.atmo || v.atmo->frameCb == 0 || v.atmo->fogCb == 0 || !m_fogCompositePSO || !v.hdrRtv) { return; }
	ViewAtmosphere& a = *v.atmo;
	const bool ap = m_sky.enabled && m_skyDrawPSO && m_sky.aerialPerspectiveScale > 0.0f;
	const bool fogOn = m_fog.enabled && a.fogIntegrated && a.fogHistoryValid;
	if (!ap && !fogOn) { return; }
	auto* cl = m_graphicsCmdList.Get();
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
	viewBarrier(v.depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto rtv = v.hdrRtv->GetCPUDescriptorHandleForHeapStart();
	cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	setTargetViewport(static_cast<float>(v.desc.width), static_cast<float>(v.desc.height));
	ID3D12DescriptorHeap* heaps[] = {a.heap.Get()};
	cl->SetDescriptorHeaps(1, heaps);
	cl->SetGraphicsRootSignature(m_atmoGraphicsRS.Get());
	cl->SetPipelineState(m_fogCompositePSO.Get());
	cl->SetGraphicsRootConstantBufferView(0, a.frameCb);
	cl->SetGraphicsRootConstantBufferView(1, a.fogCb);
	D3D12_GPU_DESCRIPTOR_HANDLE table = a.heap->GetGPUDescriptorHandleForHeapStart();
	table.ptr += static_cast<UINT64>(kAtmoSetComposite * kAtmoSetSize) *
	             m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	cl->SetGraphicsRootDescriptorTable(2, table);
	cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cl->IASetVertexBuffers(0, 0, nullptr);
	cl->DrawInstanced(3, 1, 0, 0);
	viewBarrier(v.depth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	viewBarrier(v.hdr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	m_graphicsCmdList->SetGraphicsRootSignature(m_rootSignature.Get());
}
