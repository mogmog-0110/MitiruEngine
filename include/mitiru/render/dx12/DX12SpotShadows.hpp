// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から include)。影を落とすスポットライト
//
// castShadow のスポットのうち、見えていて提出順で先頭の kMaxSpotShadows 灯が影を落とす。
// 影マップは 1 枚のアトラス (kSpotShadowMapSize の正方形を横に並べる) に描く。
// 平行光の影と同じく、フレームの頭で前フレームの caster を描くので、影の形と光の姿勢は 1 フレーム遅れる。
// 光に名前は無いので、アトラスの枠は「前フレームに描いた光と位置・向きが近い今の光」に渡す (消えた光や
// 順番の入れ替わりで、別の光の影を貼らないため)。近い光が無ければそのフレームは影なしで照らす。

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
std::array<SpotShadowView, kMaxSpotShadows> m_spotShadowNext{};   ///< endFrame で決め、次の beginFrame で描く
int m_spotShadowNextCount = 0;
std::array<SpotShadowView, kMaxSpotShadows> m_spotShadowDrawn{};  ///< このフレームのアトラスに入っている枠
int m_spotShadowDrawnCount = 0;
bool m_spotShadowRequested = false;   ///< このフレームに影を落とすスポットが積まれた

/// @brief 影の caster を記録するか (平行光の影か、このフレームか前フレームに影を落とすスポットがあるとき)
[[nodiscard]] bool wantsShadowCasters() const noexcept
{
	return (m_shadowEnabled || m_spotShadowRequested || m_spotShadowNextCount > 0) && m_shadowCasterEnabled;
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

/// @brief beginFrame (平行光の影の後) で呼ぶ。前フレームに決めたスポットの影を前フレームの caster で描く
void renderSpotShadowPass()
{
	m_spotShadowDrawnCount = 0;
	if (m_spotShadowNextCount == 0 || !m_spotShadowAtlas.isInitialized() || m_shadowCommandsPrev.empty()) { return; }
	auto* cmd = m_graphicsCmdList.Get();
	m_spotShadowAtlas.beginShadowPass(cmd);
	for (int k = 0; k < m_spotShadowNextCount; ++k)
	{
		m_spotShadowAtlas.setColumn(cmd, k);
		const auto& v = m_spotShadowNext[static_cast<std::size_t>(k)];
		renderShadowCascade(v.view, v.proj, true);
		m_spotShadowDrawn[static_cast<std::size_t>(k)] = v;
	}
	m_spotShadowAtlas.endShadowPass(cmd);
	m_spotShadowDrawnCount = m_spotShadowNextCount;
}

/// @brief endFrame で、選んだ見える光 (m_visibleLights) のうち影を落とすスポットを決める。
///        描く行列は次のフレーム用に残し、このフレームの PS にはアトラスにある行列と光の番号を渡す
void selectSpotShadows(DX12CbCluster& cb)
{
	for (auto& i : cb.spotShadowLight) { i = 0xFFFFFFFFu; }
	m_spotShadowNextCount = 0;
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
	std::array<bool, kMaxSpotShadows> used{};
	for (int k = 0; k < count; ++k)
	{
		const auto [src, v] = chosen[static_cast<std::size_t>(k)];
		const SpotShadowView now = spotShadowView(m_localLights[static_cast<std::size_t>(src)]);
		m_spotShadowNext[static_cast<std::size_t>(k)] = now;
		const int tile = matchDrawnSpotShadow(now, used);
		if (tile < 0) { continue; }
		used[static_cast<std::size_t>(tile)] = true;
		cb.spotShadowLight[tile] = static_cast<std::uint32_t>(v);
		toColumnMajor(cb.spotShadowViewProj[tile], m_spotShadowDrawn[static_cast<std::size_t>(tile)].viewProj);
	}
	m_spotShadowNextCount = count;
	m_spotShadowRequested = false;
	cb.spotShadowParams[0] = 1.0f / static_cast<float>(kSpotShadowMapSize * kMaxSpotShadows);
	cb.spotShadowParams[1] = 1.0f / static_cast<float>(kSpotShadowMapSize);
}

/// @brief アトラスの枠のうち、now と同じ光を前フレームに描いたもの (位置と向きが近い、まだ使っていない枠)。無ければ -1
[[nodiscard]] int matchDrawnSpotShadow(const SpotShadowView& now, const std::array<bool, kMaxSpotShadows>& used) const
{
	int best = -1;
	float bestDist = std::max(0.5f, now.range * 0.1f);
	for (int t = 0; t < m_spotShadowDrawnCount; ++t)
	{
		const SpotShadowView& d = m_spotShadowDrawn[static_cast<std::size_t>(t)];
		if (used[static_cast<std::size_t>(t)] || d.direction.dot(now.direction) < 0.95f) { continue; }
		const float dist = (d.position - now.position).length();
		if (dist <= bestDist)
		{
			best = t;
			bestDist = dist;
		}
	}
	return best;
}

/// @brief 場面の表の t11 (スポットの影のアトラス)
void writeSpotShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
	writeShadowSrv(m_spotShadowAtlas, cpu);
}

public:
/// @brief このフレームのアトラスに描いてあるスポットの影の数 (テスト・診断用)
[[nodiscard]] int spotShadowCount() const noexcept { return m_spotShadowDrawnCount; }

private:
