#include "DawnRenderer3D.hpp"

namespace mitiru::render::dawn_detail
{

// DX12 の注入 (DX12FogShaders.hpp)と同じ量を使う。主光源の色 × 強さ × 倍率と、環境光の平均 × 倍率。
void DawnRenderer3D::fillFog(FrameCB& cb) const
{
	if (!m_fog.enabled) { return; }
	cb.fogA[0] = m_fog.density;
	cb.fogA[1] = m_fog.heightFalloff;
	cb.fogA[2] = m_fog.baseHeight;
	cb.fogA[3] = m_fog.constantDensity;
	cb.fogB[0] = m_fog.albedo.r;
	cb.fogB[1] = m_fog.albedo.g;
	cb.fogB[2] = m_fog.albedo.b;
	cb.fogB[3] = m_fog.anisotropy;
	const sgc::Colorf lc = m_look.lightLinearSet ? m_look.lightLinear : linearColor(m_look.light.color);
	const float sun = m_look.light.intensity * m_fog.sunScale;
	cb.fogC[0] = lc.r * sun;
	cb.fogC[1] = lc.g * sun;
	cb.fogC[2] = lc.b * sun;
	cb.fogC[3] = m_fog.shadows ? 1.0f : 0.0f;
	const sgc::Colorf up = m_look.ambientLinearSet ? m_look.ambientUpLinear : linearColor(m_look.hemisphereSet() ? m_look.ambientSkySet : m_look.ambient);
	const sgc::Colorf down = m_look.ambientLinearSet ? m_look.ambientDownLinear : linearColor(m_look.hemisphereSet() ? m_look.ambientGroundSet : m_look.ambient);
	cb.fogD[0] = 0.5f * (up.r + down.r) * m_fog.ambientScale;
	cb.fogD[1] = 0.5f * (up.g + down.g) * m_fog.ambientScale;
	cb.fogD[2] = 0.5f * (up.b + down.b) * m_fog.ambientScale;
	cb.fogD[3] = std::max(m_fog.range, 1.0f);
}

// 空気遠近の後、解決済みの HDR に体積フォグを重ねる。
void DawnRenderer3D::encodeFog(wgpu::CommandEncoder& encoder)
{
	wgpu::RenderPassColorAttachment color{};
	color.view = m_t.resolvedView;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	const std::uint32_t zero = 0;
	pass.SetPipeline(m_pipelines.fog());
	pass.SetBindGroup(0, m_sceneBindGroup, 1, &zero);
	pass.SetBindGroup(1, m_fogGroup);
	pass.Draw(3);
	pass.End();
}

// 粒を進めて、このフレームの板ポリを作る。カメラの右と上は、カメラの向きと up から作る。
void DawnRenderer3D::updateParticles()
{
	ParticleView view;
	view.eye = m_camera.position();
	const sgc::Vec3f fwd = (m_camera.target() - view.eye).normalized();
	view.right = fwd.cross(m_camera.up()).normalized();
	view.up = view.right.cross(fwd);
	view.lightDir = m_look.light.direction;
	const sgc::Colorf lc = m_look.lightLinearSet ? m_look.lightLinear : linearColor(m_look.light.color);
	view.lightColor = {lc.r * m_look.light.intensity, lc.g * m_look.light.intensity, lc.b * m_look.light.intensity, 1.0f};
	view.ambient = m_look.ambientLinearSet ? m_look.ambientUpLinear : linearColor(m_look.hemisphereSet() ? m_look.ambientSkySet : m_look.ambient);
	toColumnMajor(m_viewProj, view.viewProj);
	view.farClip = m_camera.farClip();
	view.hdrScale = m_hdrScale;
	m_particles->update(view);
}

// 同じ path は読み直さず、空の path では焼いた光を外す。
void DawnRenderer3D::requestLightingBake(const char* path)
{
	const std::string wanted = (path != nullptr) ? path : "";
	if (wanted == m_bakeWanted) { return; }
	m_bakeWanted = wanted;
	if (wanted.empty()) { m_bake->clear(); }
	else
	{
		std::string error;
		const auto bake = gi::readLightingBake(wanted, error);
		if (!bake || !m_bake->load(*bake))
		{
			debug::warnOnce("dawn.lightingBake.load." + wanted, "焼いた光 " + wanted + " を読めないので、間接光は使いません (" + error + ")");
			m_bake->clear();
		}
	}
	refreshBindGroups(RefreshPhase::Lazy);
}

// 空から主光源の色 (太陽の透過率)と半球の環境光を求める。式は DX12 の applySkyToLight と同じ。空がないときは何もしない。
void DawnRenderer3D::updateSkyDrive()
{
	m_look.lightLinearSet = false;
	m_look.ambientLinearSet = false;
	if (!m_sky.enabled) { return; }
	const sgc::Vec3f sunDir = (m_look.light.direction * -1.0f).normalized();
	const float alt = atmosphere::cameraAltitudeKm(m_sky, m_camera.position().y);
	if (m_sky.driveSunLight)
	{
		const sgc::Vec3f t = atmosphere::sunTransmittance(m_sky.atmosphere, alt, sunDir);
		const sgc::Colorf base = linearColor(m_look.light.color);
		m_look.lightLinear = {base.r * t.x, base.g * t.y, base.b * t.z, base.a};
		m_look.lightLinearSet = true;
	}
	if (!m_sky.driveAmbient) { return; }
	if ((sunDir - m_skyDriveSun).lengthSquared() > 1e-6f || std::fabs(alt - m_skyDriveAltitude) > 1e-4f)
	{
		const auto amb = atmosphere::skyAmbient(m_sky, alt, sunDir);
		m_look.ambientUpLinear = {amb.sky.x, amb.sky.y, amb.sky.z, 1.0f};
		m_look.ambientDownLinear = {amb.ground.x, amb.ground.y, amb.ground.z, 1.0f};
		m_skyDriveSun = sunDir;
		m_skyDriveAltitude = alt;
	}
	m_look.ambientLinearSet = true;
}

} // namespace mitiru::render::dawn_detail
