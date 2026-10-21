#include "DawnShadowSettings.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mitiru::render::dawn_detail
{

namespace
{

// 行 row の先頭 3 成分の長さ。光の行列は正射影なので、1 texel の世界の幅と、深さの 1 世界単位あたりの変化が取れる
float rowLength(const sgc::Mat4f& m, int row)
{
	return std::sqrt(m.m[row][0] * m.m[row][0] + m.m[row][1] * m.m[row][1] + m.m[row][2] * m.m[row][2]);
}

void writeCascade(FrameCB& cb, int index, const sgc::Mat4f& lightViewProj, float texel01)
{
	toColumnMajor(lightViewProj, cb.shadowVP[index]);
	cb.shadowC[index][0] = 2.0f * texel01 / std::max(rowLength(lightViewProj, 0), 1e-6f);
	cb.shadowC[index][1] = rowLength(lightViewProj, 2);
}

} // namespace

ShadowFrame DawnShadowSettings::build(const Camera3D& camera, bool haveCasters, const sgc::Vec3f& focus, int mapSize,
                                      int farMapSize, FrameCB& cb)
{
	ShadowFrame frame;
	const DirectionalShadowConfig& cfg = m_shadow.config();
	const float nearTexel = 1.0f / static_cast<float>(std::max(mapSize, 1));
	const float farTexel = 1.0f / static_cast<float>(std::max(farMapSize, 1));
	cb.shadowTexel[0] = nearTexel;
	cb.shadowTexel[1] = farTexel;
	cb.shadowTexel[2] = 0.0f;
	cb.shadowSplit[0] = cb.shadowSplit[1] = 1.0e9f;
	cb.shadowSplit[2] = m_softness;
	cb.shadowSplit[3] = 0.0f;
	if (m_biasWorld > 0.0f)
	{
		const float range = cfg.farClip - cfg.nearClip;
		cb.shadowSplit[3] = (range > 1e-4f) ? m_biasWorld / range : 0.0f;
	}
	if (!m_enabled || !haveCasters) { return frame; }

	if (m_cascaded && cfg.autoFitCascades)
	{
		const sgc::Vec3f forward = (camera.target() - camera.position()).normalized();
		m_shadow.fitCascadesToCamera(camera.fov(), camera.aspectRatio(), camera.nearClip(), camera.position(), forward);
	}
	const int count = m_cascaded ? std::clamp(cfg.cascadeCount, 2, 3) : 1;
	for (int c = 0; c < count; ++c)
	{
		frame.view[c] = m_shadow.lightViewMatrix(m_shadow.cascadeFocus(c, focus));
		frame.proj[c] = m_shadow.cascadeProjection(c, frame.view[c]);
		writeCascade(cb, c, frame.proj[c] * frame.view[c], c == 0 ? nearTexel : farTexel);
	}
	for (int c = count; c < 3; ++c)
	{
		std::copy(std::begin(cb.shadowVP[0]), std::end(cb.shadowVP[0]), std::begin(cb.shadowVP[c]));
		std::copy(std::begin(cb.shadowC[0]), std::end(cb.shadowC[0]), std::begin(cb.shadowC[c]));
	}
	if (count >= 2) { cb.shadowSplit[0] = cfg.cascadeSplitDistance; }
	if (count >= 3) { cb.shadowSplit[1] = cfg.cascadeSplitDistance2; }
	frame.cascades = count;
	cb.shadowTexel[2] = static_cast<float>(count);
	return frame;
}

} // namespace mitiru::render::dawn_detail
