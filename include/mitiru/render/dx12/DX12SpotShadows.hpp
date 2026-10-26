// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。影を落とすスポットライト
//
// castShadow のスポットのうち、見えていて提出順で先頭の kMaxSpotShadows 灯が影を落とす。
// 影マップは 1 枚のアトラス (kSpotShadowMapSize の正方形を横に並べる) に描く。
// 光が出そろう endFrame で影を落とす光を選び、平行光の影と同じ補助リストにこのフレームの caster で焼く (DX12ShadowList.hpp)。
// 枠 k の行列と光の番号は同じ endFrame に CbCluster へ書くので、影の形と光の姿勢は描画と同じフレームのものになる。

static constexpr int kMaxSpotShadows = 4;
static constexpr int kSpotShadowMapSize = 1024;

/// @brief アトラスの 1 枠ぶん (光の view / proj と、PS が使う view * proj、どの光のものかの目印)
struct SpotShadowView
{
	sgc::Mat4f view;
	sgc::Mat4f proj;
	glm::mat4 viewProj{1.0f};
	sgc::Vec3f position{};
	sgc::Vec3f direction{0.0f, -1.0f, 0.0f};
	float range = 0.0f;
};

dx12::Dx12ShadowMap m_spotShadowAtlas;
std::array<SpotShadowView, kMaxSpotShadows> m_spotShadowViews{};   ///< endFrame で選んだ光の枠。同じ endFrame に焼く
int m_spotShadowCount = 0;            ///< 直前の endFrame で選んだ枠の数
bool m_spotShadowRequested = false;   ///< このフレームに影を落とすスポットが積まれた

/// @brief 影の caster を記録するか (平行光の影か、このフレームか前フレームに影を落とすスポットがあるとき)
[[nodiscard]] bool wantsShadowCasters() const noexcept
{
	// 影の caster は主ビューと、影を読む副ビューの描画から作る (同じ物は addShadowCaster が重ねない)。
	// スポットは描画の後に積まれることもあるので、前フレームに影を落としていれば記録しておく
	return (m_shadowEnabled || m_spotShadowRequested || m_spotShadowCount > 0 || m_pointShadowRequested || m_pointShadowActive) &&
	       m_shadowCasterEnabled &&
	       (m_activeView == nullptr || m_activeView->desc.shadows);
}

/// @brief スポットの光から見た view / proj。円錐を少し広めに包む正方形の透視
[[nodiscard]] static SpotShadowView spotShadowView(const LocalLight& l)
{
	const sgc::Vec3f pos{l.position[0], l.position[1], l.position[2]};
	const sgc::Vec3f dir = sgc::Vec3f{l.direction[0], l.direction[1], l.direction[2]}.normalized();
	const sgc::Vec3f up = (std::abs(dir.y) > 0.99f) ? sgc::Vec3f{0.0f, 0.0f, 1.0f} : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	const float fov = std::min(std::clamp(l.outerConeDeg, 1.0f, 85.0f) * 2.2f, 170.0f) * 0.017453292519943295f;
	const float farZ = std::max(l.range, 0.1f);
	SpotShadowView v;
	v.position = pos;
	v.direction = dir;
	v.range = farZ;
	const glm::mat4 view = lookAt(pos, pos + dir, up);
	const glm::mat4 proj = perspective(fov, 1.0f, std::max(farZ * 0.005f, 0.02f), farZ);
	v.viewProj = proj * view;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			v.view.m[r][c] = view[c][r];
			v.proj.m[r][c] = proj[c][r];
		}
	}
	return v;
}

/// @brief endFrame の影の補助リストで、selectSpotShadows が選んだ光の影をこのフレームの caster で焼く。
///        選んだ枠はアトラスごとクリアするので、caster が無くても前のフレームの影は残らない
void renderSpotShadowPass(ID3D12GraphicsCommandList* cmd)
{
	if (m_spotShadowCount == 0 || !m_spotShadowAtlas.isInitialized()) { return; }
	m_spotShadowAtlas.beginShadowPass(cmd);
	for (int k = 0; k < m_spotShadowCount && !m_shadowCommands.empty(); ++k)
	{
		m_spotShadowAtlas.setColumn(cmd, k);
		const auto& v = m_spotShadowViews[static_cast<std::size_t>(k)];
		renderShadowCascade(cmd, v.view, v.proj, true);
	}
	m_spotShadowAtlas.endShadowPass(cmd);
}

/// @brief endFrame で、選んだ見える光 (m_visibleLights) のうち影を落とすスポットを決め、枠 k の行列と光の番号を cb に書く
void selectSpotShadows(DX12CbCluster& cb)
{
	for (auto& i : cb.spotShadowLight) { i = 0xFFFFFFFFu; }
	// m_lightSortScratch の先頭 visibleCount 個が、描画に使う光の元の番号 (並びは m_visibleLights と同じ)
	std::array<std::pair<int, int>, kMaxSpotShadows> chosen{};   // (元の番号 = 提出順, 見える光の番号)
	int count = 0;
	for (int v = 0; v < m_visibleLightCount; ++v)
	{
		const int src = m_lightSortScratch[static_cast<std::size_t>(v)].second;
		const LocalLight& l = m_localLights[static_cast<std::size_t>(src)];
		if (l.type != LocalLightType::Spot || l.castShadow == 0) { continue; }
		if (count < kMaxSpotShadows) { chosen[static_cast<std::size_t>(count++)] = {src, v}; continue; }
		// 提出順で先頭の kMaxSpotShadows 灯を残す (一番遅いものと入れ替える)
		auto latest = std::max_element(chosen.begin(), chosen.end());
		if (src < latest->first)
		{
			latest->first = src;
			latest->second = v;
		}
	}
	std::sort(chosen.begin(), chosen.begin() + count);
	for (int k = 0; k < count; ++k)
	{
		const auto [src, v] = chosen[static_cast<std::size_t>(k)];
		const SpotShadowView view = spotShadowView(m_localLights[static_cast<std::size_t>(src)]);
		m_spotShadowViews[static_cast<std::size_t>(k)] = view;
		cb.spotShadowLight[k] = static_cast<std::uint32_t>(v);
		toColumnMajor(cb.spotShadowViewProj[k], view.viewProj);
	}
	m_spotShadowCount = count;
	m_spotShadowRequested = false;
	cb.spotShadowParams[0] = 1.0f / static_cast<float>(kSpotShadowMapSize * kMaxSpotShadows);
	cb.spotShadowParams[1] = 1.0f / static_cast<float>(kSpotShadowMapSize);
}

/// @brief 場面の表の t11 (スポットの影のアトラス)
void writeSpotShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	writeShadowSrv(m_spotShadowAtlas, cpu);
}

public:
/// @brief 直前の endFrame でアトラスに描いたスポットの影の数 (テスト・診断用)
[[nodiscard]] int spotShadowCount() const noexcept { return m_spotShadowCount; }

private:
