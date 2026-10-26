// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12SpotShadows.hpp の後に include)。影を落とす点光源
//
// 同時に 1 灯だけ、6 面の影を落とす。1024^2 の D32 を 6 面で 24 MiB 取るので、宣言の行 (point_shadow) を
// 宣言したゲームだけがアトラスを作る。行列と光の番号は光が出そろう endFrame に CbCluster へ書き、深度も同じ
// endFrame の caster で焼くので、影はそのフレームの光の位置から落ちる。

static constexpr int kPointShadowMapSize = 1024;

dx12::Dx12ShadowMap m_pointShadowAtlas;
std::array<sgc::Mat4f, kPointShadowFaceCount> m_pointShadowViews{};
std::array<sgc::Mat4f, kPointShadowFaceCount> m_pointShadowProjs{};
bool m_pointShadowActive = false;      ///< 直前の endFrame で影を落とす点光源を選んだ
bool m_pointShadowRequested = false;   ///< このフレームに影を落とす点光源が積まれた

/// 旧体制 (宣言なし) の DLL は、この行を知らずに作られているので 24 MiB を払わせず、影も付けない
[[nodiscard]] bool pointShadowDeclared() const noexcept
{
	return m_needs.declaredMode && m_needs.wants(features::Feature::PointShadow);
}

/// 透視の深度は遠いほど詰まるので、余白は深度の残り (1 - z) に比例させる (スポットの localShadow と同じ式)
static constexpr float kPointShadowDepthBias = 0.006f;

[[nodiscard]] bool createPointShadowAtlas()
{
	return m_pointShadowAtlas.isInitialized() ||
	       m_pointShadowAtlas.initialize(m_d3dDevice, kPointShadowMapSize, kPointShadowFaceCount);
}

/// @brief 見える光のうち、影を落とす点光源を提出順で先頭の 1 灯だけ決め、6 面の行列と光の番号を cb に書く
void selectPointShadows(DX12CbCluster& cb)
{
	m_pointShadowRequested = false;
	m_pointShadowActive = false;
	if (!m_pointShadowAtlas.isInitialized()) { return; }
	int chosenSrc = -1;
	int chosenVisible = -1;
	for (int v = 0; v < m_visibleLightCount; ++v)
	{
		const int src = m_lightSortScratch[static_cast<std::size_t>(v)].second;
		const LocalLight& l = m_localLights[static_cast<std::size_t>(src)];
		if (l.type != LocalLightType::Point || l.castShadow == 0) { continue; }
		if (chosenSrc < 0 || src < chosenSrc)
		{
			chosenSrc = src;
			chosenVisible = v;
		}
	}
	if (chosenSrc < 0) { return; }
	const LocalLight& l = m_localLights[static_cast<std::size_t>(chosenSrc)];
	const sgc::Vec3f pos{l.position[0], l.position[1], l.position[2]};
	for (int f = 0; f < kPointShadowFaceCount; ++f)
	{
		glm::mat4 view, proj;
		pointShadowFaceMatrices(pos, l.range, f, view, proj);
		auto& sv = m_pointShadowViews[static_cast<std::size_t>(f)];
		auto& sp = m_pointShadowProjs[static_cast<std::size_t>(f)];
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				sv.m[r][c] = view[c][r];
				sp.m[r][c] = proj[c][r];
			}
		}
		toColumnMajor(cb.pointShadowViewProj[f], proj * view);
	}
	cb.pointShadowLight[0] = static_cast<std::uint32_t>(chosenVisible);
	cb.pointShadowParams[0] = 1.0f / static_cast<float>(kPointShadowMapSize * kPointShadowFaceCount);
	cb.pointShadowParams[1] = 1.0f / static_cast<float>(kPointShadowMapSize);
	cb.pointShadowParams[2] = 1.0f / static_cast<float>(kPointShadowFaceCount);
	cb.pointShadowParams[3] = kPointShadowDepthBias;
	m_pointShadowActive = true;
}

void renderPointShadowPass(ID3D12GraphicsCommandList* cmd)
{
	if (!m_pointShadowActive || !m_pointShadowAtlas.isInitialized()) { return; }
	m_pointShadowAtlas.beginShadowPass(cmd);
	for (int f = 0; f < kPointShadowFaceCount && !m_shadowCommands.empty(); ++f)
	{
		m_pointShadowAtlas.setColumn(cmd, f);
		renderShadowCascade(cmd, m_pointShadowViews[static_cast<std::size_t>(f)],
		                    m_pointShadowProjs[static_cast<std::size_t>(f)], true);
	}
	m_pointShadowAtlas.endShadowPass(cmd);
}

/// @brief 場面の表の t44
/// 作っていない間はスポットの影のアトラスを指す。光の番号が空なので読まれないが、比較のサンプラーで読める深度の絵にしておく。
/// 白いテクスチャを代わりに作ると、最初の場面の表で確保が 1 つ増える
void writePointShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	writeShadowSrv(m_pointShadowAtlas.isInitialized() ? m_pointShadowAtlas : m_spotShadowAtlas, cpu);
}

public:
[[nodiscard]] bool pointShadowActive() const noexcept { return m_pointShadowActive; }

private:
