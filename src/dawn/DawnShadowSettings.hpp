#pragma once

// 太陽の影の設定と、1 フレームぶんの光の行列・定数の決め方 (DX12 の DX12PipelineStates_Shadow.inl の
// applyAutoCascadeFit / buildShadowCB と Renderer3D_DX12.hpp の applyShadowCascadeMode に相当)。

#include <algorithm>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Shadow.hpp>

#include "DawnRenderTypes.hpp"
#include "DawnShadowPass.hpp"

namespace mitiru::render::dawn_detail
{

class DawnShadowSettings
{
public:
	void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
	void setDirection(const sgc::Vec3f& dir) noexcept { m_shadow.setLightDirection(dir); }
	void setSoftness(float texels) noexcept { m_softness = (texels < 0.0f) ? 0.0f : ((texels > 8.0f) ? 8.0f : texels); }
	void setBias(float worldUnits) noexcept { m_biasWorld = (worldUnits > 0.0f) ? worldUnits : 0.0f; }

	void setCascadedEnabled(bool enabled) noexcept
	{
		m_requested = enabled ? std::max(m_requested, 2) : 1;   // 3 を指定済みなら下げない
		applyMode();
	}
	void setCascadeCount(int count) noexcept
	{
		m_requested = std::clamp(count, 1, 3);
		applyMode();
	}
	void setAutoFit(bool enabled, float maxDistance) noexcept
	{
		m_autoFitRequested = enabled;
		if (maxDistance > 0.0f) { m_shadow.config().cascadeMaxDistance = maxDistance; }
		applyMode();
	}
	[[nodiscard]] bool cascaded() const noexcept { return m_cascaded; }

	/// 光の行列を決めて cb の影の欄を埋める。haveCasters が false か影が無効なら cascades = 0 (影なし)。
	/// focus はカスケードを手動で置くときの中心 (影を落とす物の重心)
	[[nodiscard]] ShadowFrame build(const Camera3D& camera, bool haveCasters, const sgc::Vec3f& focus, int mapSize,
	                                int farMapSize, FrameCB& cb);

private:
	// 影を頼まない (1 段) ときは画質の上限まで段を使い、カメラの視錐台に合わせる。20 m 四方を 1 枚で覆うと
	// カメラの近くで 1 texel が画面の数画素になり、影の縁が段になってぼける
	void applyMode() noexcept
	{
		DirectionalShadowConfig& cfg = m_shadow.config();
		const bool engineDefault = m_requested <= 1;
		cfg.cascadeCount = engineDefault ? kCascadeCap : std::min(m_requested, kCascadeCap);
		cfg.autoFitCascades = engineDefault || m_autoFitRequested;
		m_cascaded = cfg.cascadeCount > 1;
	}

	static constexpr int kCascadeCap = 3;
	DirectionalShadow m_shadow;
	bool m_enabled = false;
	bool m_cascaded = false;
	bool m_autoFitRequested = false;
	int m_requested = 1;
	float m_softness = 1.0f;
	float m_biasWorld = 0.0f;
};

} // namespace mitiru::render::dawn_detail
