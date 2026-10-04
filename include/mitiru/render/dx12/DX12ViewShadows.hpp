// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12Views.hpp の後に include)。副ビューの影
//
// 副ビューは自分の影マップ (近距離 1 枚 + 中距離と遠距離の 2 列アトラス、主ビューと同じ形) を持ち、
// このフレームで最初の beginView の時にビューのカメラで焼く。分割画面の 2 人が遠く離れても、それぞれの足元に影が付く。
// caster は全てのビューで描いた物の和 (前フレーム分) を使い、カスケードごとに投影の外の物は描かない (outsideShadowClip)。
// 自動フィットならビューの視錐台にカスケードを合わせ、手動の大きさならビューの注視点を中心に置く。
// 設定 (向き・段数・自動フィット・柔らかさ・余白) は主ビューのものを毎フレーム写すので、全てのビューに同じに効く。

private:

static constexpr int kMinViewShadowMapSize = 256;
static constexpr int kMaxViewShadowMapSize = 4096;

/// @brief 副ビューの影マップの一辺。0 なら主ビューと同じ。他は 2 の冪へ切り上げて範囲に収める
[[nodiscard]] int viewShadowMapSize(const View3DDesc& desc) const noexcept
{
	const int mainSize = m_directionalShadow.config().mapSize;
	if (desc.shadowMapSize <= 0) { return mainSize; }
	int size = kMinViewShadowMapSize;
	while (size < desc.shadowMapSize && size < kMaxViewShadowMapSize) { size *= 2; }
	return size;
}

/// @brief このフレームの影の準備。beginView の最初 (入れ替えの前) に呼び、影を焼けるなら frame.shadows を立てる。
///        影マップは初めて要る時に作る。遠距離のアトラスはカスケードを使う時だけ作る
void prepareViewShadows(View3D& v)
{
	v.frame.shadowCb = 0;
	v.frame.shadows = false;
	if (!v.desc.shadows || !m_shadowEnabled || !m_shadowPSO) { return; }
	const int size = viewShadowMapSize(v.desc);
	if (!v.shadowNear.isInitialized() && !v.shadowNear.initialize(m_d3dDevice, size))
	{
		debug::verboseOnce("dx12.view.shadow", "副ビューの影マップを作れなかったので、副ビューは影なしで描きます。");
		return;
	}
	if (m_cascadedShadowEnabled && !v.shadowFar.isInitialized())
	{
		(void)v.shadowFar.initialize(m_d3dDevice, size, 2);
	}
	v.shadow = m_directionalShadow;
	v.shadow.config().mapSize = v.shadowNear.mapSize();
	if (v.shadowFar.isInitialized()) { v.shadow.config().farMapSize = v.shadowFar.mapSize(); }
	v.frame.shadows = true;
}

/// @brief enterView と leaveView から。影マップと行列の計算を主ビューのものと入れ替える
void swapViewShadows(View3D& v)
{
	std::swap(m_shadowMap, v.shadowNear);
	std::swap(m_shadowMapFar, v.shadowFar);
	std::swap(m_directionalShadow, v.shadow);
}

/// @brief 入れ替え済みの副ビューで、ビューのカメラに合わせた影マップを焼き、ビューの CbShadow を決める。
///        描画先と viewport は呼び出し側 (bindViewTargets) が戻す
void renderViewShadowPass(View3D& v)
{
	MITIRU_ZONE_NAMED("Render::Dx12::ViewShadowPass");
	applyAutoCascadeFit();
	const sgc::Vec3f focus = m_clodCamera.target();
	// 影のパスの深度描画も b3 を読むので、CB を先に決める
	v.frame.shadowCb = writeShadowCB(focus);
	drawShadowCascades(focus, false);
}
