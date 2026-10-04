// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12ViewShadows.hpp の後に include)。副ビューの TAA と SSAO
//
// 主ビューが TAA なら、副ビューも自分の履歴・ずらし・動きベクトルで TAA を掛ける。描画の対はビューごとの履歴で取るので、
// 主ビューと副ビューで同じ物を描いても互いの動きに混ざらない。動きベクトルと SSAO のパスは主ビューの関数を、
// 資源を入れ替えた副ビューで呼ぶ。TAA は tonemap の後に副ビューの資源で直接掛ける。
// 履歴は、副ビューを作った時、前のフレームに描かなかった時、resetTemporalHistory と AA の切り替えの時に捨てる。
// ずらしの相も 0 に戻すので、同じ入力の headless 撮影は同じ絵になる。

public:

/// @brief 副ビュー id の直前のフレームの動きベクトル (UV 単位、前 − 今)。診断とテスト用で GPU の完了を待つ。
///        フレームの外で呼ぶ。TAA を掛けていなければ空
[[nodiscard]] std::vector<float> readViewVelocityForDiagnostics(int id)
{
	const View3D* v = viewAt(id);
	if (v == nullptr || !v->temporal || !v->temporal->velocity || m_device == nullptr) { return {}; }
	m_device->waitForGpu();
	return halfsToFloats(gfx::readbackTexture2D(m_d3dDevice, m_device->commandQueue(), v->temporal->velocity.Get(),
	                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 4));
}

private:

/// 副ビューの SRV ヒープは主ビューの表のうち、動きベクトル (0) と TAA (1、2) の 3 表だけ持つ
static constexpr UINT kViewTemporalTables = kTemporalTableTaa + 2;

/// @brief beginView の最初 (入れ替えの前) に呼び、このフレームの影・TAA・SSAO を決める
void prepareViewEffects(View3D& v)
{
	prepareViewShadows(v);
	prepareViewTemporal(v);
	prepareViewAtmosphere(v);
	v.frame.ao = v.desc.ambientOcclusion && m_aoEnabled && m_aoStrength > 0.0f && m_ssaoPSO && m_ssaoBlurPSO &&
	             ensureViewSsao(v);
}

void prepareViewTemporal(View3D& v)
{
	v.frame.taa = v.desc.antiAlias && v.desc.temporal && m_aaMode == AntiAliasing3D::Taa && m_taaPSO && ensureViewTemporal(v);
	if (!v.frame.taa) { return; }
	ViewTemporal& t = *v.temporal;
	const bool continuous = t.lastFrame + 1 == m_frameCounter;
	if (!continuous) { resetViewTemporal(t); }
	t.prevViewProj = t.viewProj;
	t.prevValid = continuous;
	t.motion.beginFrame();
	t.jitter = jitterToNdc(temporalJitter(t.frameIndex, kTemporalJitterPhases), static_cast<float>(v.desc.width),
	                       static_cast<float>(v.desc.height));
	t.lastFrame = m_frameCounter;
}

static void resetViewTemporal(ViewTemporal& t) noexcept
{
	t.historyValid = false;
	t.frameIndex = 0;
	t.prevValid = false;
	t.motion.clear();
}

void resetViewTemporalHistories() noexcept
{
	for (auto& v : m_views)
	{
		if (v && v->temporal) { resetViewTemporal(*v->temporal); }
	}
}

/// @brief 動きベクトルのパスが読み書きする資源と、ずらし・前の viewProj・描画の記録を主ビューのものと入れ替える
void swapViewTemporal(ViewTemporal& t)
{
	std::swap(m_velocityObjectTex, t.velocityObject);
	std::swap(m_velocityDepth, t.velocityDepth);
	std::swap(m_velocityTex, t.velocity);
	std::swap(m_temporalRtvHeap, t.rtv);
	std::swap(m_temporalDsvHeap, t.dsv);
	std::swap(m_temporalSrvHeap, t.srv);
	std::swap(m_motionHistory, t.motion);
	std::swap(m_jitterNdc, t.jitter);
	std::swap(m_prevViewProjNoJitter, t.prevViewProj);
	std::swap(m_prevViewProjValid, t.prevValid);
}

/// @brief TAA の資源を初めて要る時に作る。作れなければ以後は FXAA で描く
[[nodiscard]] bool ensureViewTemporal(View3D& v)
{
	if (v.temporal) { return v.temporal->srv != nullptr; }
	v.temporal = std::make_unique<ViewTemporal>();
	ViewTemporal& t = *v.temporal;
	const auto w = static_cast<UINT>(v.desc.width);
	const auto h = static_cast<UINT>(v.desc.height);
	constexpr auto kRt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const bool ok =
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, kRt, L"Renderer3D view velocity (objects)", t.velocityObject) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		                      L"Renderer3D view velocity depth", t.velocityDepth) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, kRt, L"Renderer3D view velocity", t.velocity) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D view TAA history 0", t.history[0]) &&
		createTemporalTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, kRt, L"Renderer3D view TAA history 1", t.history[1]) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kTemporalRtvHistory + 2, false, t.rtv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, t.dsv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewTemporalTables * kTemporalTableSize, true, t.srv);
	if (!ok)
	{
		t.srv.Reset();
		debug::verboseOnce("dx12.view.taa", "副ビューの TAA の資源を作れなかったので、副ビューは FXAA で描きます。");
		return false;
	}
	writeViewTemporalViews(v);
	return true;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE viewTemporalRtv(const ViewTemporal& t, UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = t.rtv->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return h;
}

/// @brief SRV を heap の index 番目へ書く。msaa なら Texture2DMS として読む
void writeViewSrv(ID3D12DescriptorHeap* heap, UINT index, ID3D12Resource* res, DXGI_FORMAT format, bool msaa = false)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = format;
	sd.ViewDimension = msaa ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (!msaa) { sd.Texture2D.MipLevels = 1; }
	D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(index) * m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_d3dDevice->CreateShaderResourceView(res, &sd, h);
}

/// @brief 主ビューの writeTemporalViews と同じ並びで、副ビューの深度と tonemap の結果を読む表を書く
void writeViewTemporalViews(const View3D& v)
{
	const ViewTemporal& t = *v.temporal;
	D3D12_RENDER_TARGET_VIEW_DESC rv = {};
	rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rv.Format = DXGI_FORMAT_R16G16_FLOAT;
	m_d3dDevice->CreateRenderTargetView(t.velocityObject.Get(), &rv, viewTemporalRtv(t, kTemporalRtvVelocityObject));
	m_d3dDevice->CreateRenderTargetView(t.velocity.Get(), &rv, viewTemporalRtv(t, kTemporalRtvVelocity));
	rv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_d3dDevice->CreateRenderTargetView(t.history[0].Get(), &rv, viewTemporalRtv(t, kTemporalRtvHistory));
	m_d3dDevice->CreateRenderTargetView(t.history[1].Get(), &rv, viewTemporalRtv(t, kTemporalRtvHistory + 1));
	D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
	dv.Format = DXGI_FORMAT_D32_FLOAT;
	dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_d3dDevice->CreateDepthStencilView(t.velocityDepth.Get(), &dv, t.dsv->GetCPUDescriptorHandleForHeapStart());

	ID3D12DescriptorHeap* heap = t.srv.Get();
	const UINT vel = kTemporalTableVelocity * kTemporalTableSize;
	writeViewSrv(heap, vel, v.depth.Get(), DXGI_FORMAT_R32_FLOAT, true);
	writeViewSrv(heap, vel + 1, t.velocityObject.Get(), DXGI_FORMAT_R16G16_FLOAT);
	writeViewSrv(heap, vel + 2, t.velocityDepth.Get(), DXGI_FORMAT_R32_FLOAT);
	writeViewSrv(heap, vel + 3, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM);
	for (UINT write = 0; write < 2; ++write)
	{
		const UINT base = (kTemporalTableTaa + write) * kTemporalTableSize;
		writeViewSrv(heap, base, v.fxaaSource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
		writeViewSrv(heap, base + 1, t.history[1 - write].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
		writeViewSrv(heap, base + 2, t.velocity.Get(), DXGI_FORMAT_R16G16_FLOAT);
		writeViewSrv(heap, base + 3, v.depth.Get(), DXGI_FORMAT_R32_FLOAT, true);
	}
}

/// @brief tonemap の結果 (fxaaSource) に TAA を掛けて ldr と履歴へ書く。入れ替えを戻した後 (leaveView の後) に呼ぶ
void drawViewTaa(View3D& v)
{
	ViewTemporal& t = *v.temporal;
	const int write = t.historyWrite;
	ID3D12Resource* history = t.history[write].Get();
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	viewBarrier(history, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	viewBarrier(v.depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {v.ldrRtv->GetCPUDescriptorHandleForHeapStart(),
	                                             viewTemporalRtv(t, kTemporalRtvHistory + static_cast<UINT>(write))};
	m_graphicsCmdList->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
	const auto w = static_cast<float>(v.desc.width);
	const auto h = static_cast<float>(v.desc.height);
	setTargetViewport(w, h);
	const CbTaa cb = taaConstants(w, h, t.historyValid);
	drawTemporalFullscreenIn(t.srv.Get(), m_taaPSO.Get(), kTemporalTableTaa + static_cast<UINT>(write), &cb, sizeof(cb));
	viewBarrier(v.depth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	viewBarrier(history, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	viewBarrier(v.ldr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	t.historyWrite = 1 - write;
	t.historyValid = true;
	++t.frameIndex;
}

/// @brief SSAO の 2 枚と記述子を初めて要る時に作り、遮蔽をビューの tonemap の表 (枠 1) へ書く
[[nodiscard]] bool ensureViewSsao(View3D& v)
{
	if (v.ssao) { return v.ssao->srv != nullptr; }
	v.ssao = std::make_unique<ViewSsao>();
	ViewSsao& s = *v.ssao;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_R8_UNORM;
	clear.Color[0] = 1.0f;
	const auto rt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const bool ok =
		createViewTexture(v, DXGI_FORMAT_R8_UNORM, 1, rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, s.tex[0]) &&
		createViewTexture(v, DXGI_FORMAT_R8_UNORM, 1, rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, s.tex[1]) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, s.rtv) &&
		createViewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6, true, s.srv);
	if (!ok)
	{
		s.srv.Reset();
		debug::verboseOnce("dx12.view.ssao", "副ビューの SSAO の資源を作れなかったので、副ビューは遮蔽なしで描きます。");
		return false;
	}
	writeViewSsaoViews(v);
	return true;
}

void writeViewSsaoViews(const View3D& v)
{
	const ViewSsao& s = *v.ssao;
	D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
	rtv.Format = DXGI_FORMAT_R8_UNORM;
	rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	const UINT rtvInc = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int i = 0; i < 2; ++i)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = s.rtv->GetCPUDescriptorHandleForHeapStart();
		h.ptr += rtvInc * static_cast<SIZE_T>(i);
		m_d3dDevice->CreateRenderTargetView(s.tex[i].Get(), &rtv, h);
	}
	for (UINT group = 0; group < 2; ++group)
	{
		writeViewSrv(s.srv.Get(), group * 3, v.depth.Get(), DXGI_FORMAT_R32_FLOAT, true);
		writeViewSrv(s.srv.Get(), group * 3 + 1, v.normal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, true);
		writeViewSrv(s.srv.Get(), group * 3 + 2, s.tex[group == 0 ? 1 : 0].Get(), DXGI_FORMAT_R8_UNORM);
	}
	writeViewSrv(v.srv.Get(), 1, s.tex[0].Get(), DXGI_FORMAT_R8_UNORM);
}

void swapViewSsao(ViewSsao& s)
{
	std::swap(m_ssaoTex[0], s.tex[0]);
	std::swap(m_ssaoTex[1], s.tex[1]);
	std::swap(m_ssaoRtvHeap, s.rtv);
	std::swap(m_ssaoSrvHeap, s.srv);
}

/// @brief 入れ替え済みの副ビューで、動きベクトルと SSAO を主ビューと同じパスで描く。SSAO を掛けたら true
[[nodiscard]] bool drawViewTemporalInputs(const View3D& v)
{
	if (v.frame.taa) { drawVelocityPasses(); }
	if (!v.frame.ao) { return false; }
	// 主ビューの tonemap が読む印は、主ビューの SSAO のパス (この後) が決め直す
	drawSsaoPasses();
	const bool applied = m_aoAppliedThisFrame;
	m_aoAppliedThisFrame = false;
	return applied;
}
