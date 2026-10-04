#pragma once
// mitiru::Screen の ABI v48 (ADR 0056) の 3D 入口。直接 include しない。core/Screen.hpp 経由。
//
// どれも IRenderer3D の末尾 virtual を呼ぶだけで、3D 非対応 (host 未注入 / DX11 / headless) は何もしない。

#include <sgc/math/Mat4.hpp>

#include <mitiru/animation/AnimIkRequest.hpp>
#include <mitiru/animation/AnimPose.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/LocalLights.hpp>
#include <mitiru/render/RendererEnums3D.hpp>
#include <mitiru/render/TrailRibbon.hpp>

namespace mitiru
{

namespace detail
{
/// SceneLook::shadingModel の番号を ShaderMode3D へ変える (0 は呼ばないので来ない)
[[nodiscard]] inline render::ShaderMode3D shadingModelToMode(std::uint8_t model) noexcept
{
	switch (model)
	{
	case 2:  return render::ShaderMode3D::Toon;
	case 3:  return render::ShaderMode3D::PBR;
	case 4:  return render::ShaderMode3D::Unlit;
	default: return render::ShaderMode3D::Phong;
	}
}

/// SceneLook::aaMode の番号を AntiAliasing3D へ変える (1 = FXAA、2 = MSAA だけ、3 = TAA)
[[nodiscard]] inline render::AntiAliasing3D aaModeToEnum(std::uint8_t mode) noexcept
{
	if (mode == 3) { return render::AntiAliasing3D::Taa; }
	return (mode == 2) ? render::AntiAliasing3D::Msaa : render::AntiAliasing3D::MsaaFxaa;
}

/// SceneLook::sky を SkySettings にする。大気の組成は既定のまま
[[nodiscard]] inline render::SkySettings toSkySettings(const render::SkyLook& s) noexcept
{
	render::SkySettings out;
	out.enabled = s.enabled != 0;
	out.sunIlluminance = s.sunIlluminance;
	out.sunDiskAngularRadius = s.sunDiskRadius;
	out.metersPerUnit = s.metersPerUnit;
	out.groundHeight = s.groundHeight;
	out.aerialPerspectiveScale = s.aerialPerspectiveScale;
	out.toonBands = s.toonBands;
	out.driveSunLight = s.driveSunLight != 0;
	out.driveAmbient = s.driveAmbient != 0;
	return out;
}

/// SceneLook::volumetricFog を VolumetricFogSettings にする。履歴を混ぜる割合は既定のまま
[[nodiscard]] inline render::VolumetricFogSettings toFogSettings(const render::VolumetricFogLook& f) noexcept
{
	render::VolumetricFogSettings out;
	out.enabled = f.enabled != 0;
	out.shadows = f.shadows != 0;
	out.density = f.density;
	out.heightFalloff = f.heightFalloff;
	out.baseHeight = f.baseHeight;
	out.constantDensity = f.constantDensity;
	out.albedo = sgc::Colorf{f.albedo[0], f.albedo[1], f.albedo[2], 1.0f};
	out.anisotropy = f.anisotropy;
	out.range = f.range;
	out.sunScale = f.sunScale;
	out.localLightScale = f.localLightScale;
	out.ambientScale = f.ambientScale;
	return out;
}
} // namespace detail

inline void Screen::camera3D(const sgc::Vec3f& eye, const sgc::Vec3f& target, const sgc::Vec3f& up, Deg fov,
                             float nearDist, float farDist) noexcept
{
	camera3D(eye, target, fov);
	const sgc::Vec3f f = target - eye;
	const sgc::Vec3f side = f.cross(up);
	const bool usable = up.lengthSquared() > 1e-12f && side.lengthSquared() > 1e-10f * f.lengthSquared() * up.lengthSquared();
	m_cam3DUp   = usable ? up.normalized() : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	m_cam3DNear = (nearDist > 0.0f) ? nearDist : 0.1f;
	m_cam3DFar  = (farDist > m_cam3DNear) ? farDist : m_cam3DNear + 1.0f;
}

inline void Screen::applyLookV48()
{
	if (m_env3DRequested && !m_env3DApplied)
	{
		m_renderer3D->setEnvironment(render::Cubemap::verticalGradient(64, m_env3DZenith, m_env3DNadir));
		m_env3DApplied = true;
	}
	if (!m_sceneLookSet) { return; }
	// 0 と負の値は player の設定 (host の --aa / --motion-blur) に任せて触らない
	if (m_sceneAaMode != 0) { m_renderer3D->setAntiAliasing(detail::aaModeToEnum(m_sceneAaMode)); }
	if (m_sceneMotionBlur >= 0.0f) { m_renderer3D->setMotionBlur(m_sceneMotionBlur); }
	m_renderer3D->setAmbientOcclusionMethod(m_sceneAoMethod == 1 ? render::AmbientOcclusionMethod::Gtao
	                                                             : render::AmbientOcclusionMethod::Ssao);
	if (m_sceneShadingModel != 0) { m_renderer3D->setShaderMode(detail::shadingModelToMode(m_sceneShadingModel)); }
	m_renderer3D->setSky(detail::toSkySettings(m_sceneSky));
	m_renderer3D->setVolumetricFog(detail::toFogSettings(m_sceneVolumetricFog));
	m_renderer3D->setIndirectLightLook(m_sceneIndirect);
}

inline void Screen::localLights3D(const render::LocalLight* lights, int count)
{
	if (!has3D() || lights == nullptr || count <= 0) { return; }
	ensure3DFrame();   // beginFrame が光の表を空にするので、開いた後に積む
	m_renderer3D->submitLocalLights(lights, count);
}

inline void Screen::pointLight3D(const sgc::Vec3f& position, float range, const sgc::Colorf& color, float intensity)
{
	const render::LocalLight light = render::LocalLight::point(position, range, color, intensity);
	localLights3D(&light, 1);
}

inline void Screen::spotLight3D(const sgc::Vec3f& position, const sgc::Vec3f& direction, float range, Deg inner,
                                Deg outer, const sgc::Colorf& color, float intensity, bool castShadow)
{
	render::LocalLight light = render::LocalLight::spot(position, direction, range, inner.degrees(), outer.degrees(),
	                                                    color, intensity);
	light.castShadow = castShadow ? 1u : 0u;
	localLights3D(&light, 1);
}

inline void Screen::environment3D(const sgc::Colorf& zenith, const sgc::Colorf& nadir) noexcept
{
	if (!m_env3DRequested || zenith != m_env3DZenith || nadir != m_env3DNadir)
	{
		m_env3DRequested = true;
		m_env3DApplied   = false;
		m_env3DZenith    = zenith;
		m_env3DNadir     = nadir;
	}
}

inline void Screen::drawModelPose(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
                                  const animation::AnimIkRequest* ik, std::uint32_t ikCount,
                                  const render::DrawTint& tint)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawModelAnimPose(path, world, pose, ik, static_cast<int>(ikCount), tint);
}

inline void Screen::drawModel(const char* path, const sgc::Mat4f& world, const sgc::Colorf& tint)
{
	render::DrawTint t;
	t.mul[0] = tint.r; t.mul[1] = tint.g; t.mul[2] = tint.b; t.mul[3] = tint.a;
	drawModelPose(path, world, nullptr, nullptr, 0, t);
}

inline void Screen::drawModelNodeMatrices(const char* path, const sgc::Mat4f& world, const sgc::Mat4f* nodeModel,
                                          std::uint32_t count, const render::DrawTint& tint)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawModelNodeMatrices(path, world, nodeModel, static_cast<int>(count), tint);
}

inline void Screen::drawMeshInstanced(const char* shape, const render::MeshInstance* instances, int count,
                                      const sgc::Colorf& color)
{
	if (!has3D() || instances == nullptr || count <= 0) { return; }
	ensure3DFrame();
	render::Material material;
	material.diffuse = color;
	m_renderer3D->drawMeshInstances(resolveMesh3D(shape), instances, count, material);
}

inline const render::Mesh& Screen::resolveMesh3D(const char* shape) const
{
	if (const render::Mesh* builtin = detail::findBuiltin3DMesh(shape)) { return *builtin; }
	if (m_renderer3D != nullptr && shape != nullptr)
	{
		if (const render::Mesh* own = m_renderer3D->findGameMesh(detail::meshNameId(shape))) { return *own; }
	}
	// 窓なしの 2D の実行 (CPU で描く画面) は登録したメッシュを持たないので、名前が無くて当然であり知らせない
	if (!hasSoftwareFramebuffer())
	{
		mitiru::debug::warnOnce(std::string("screen.drawMesh.unknownShape.") + (shape != nullptr ? shape : ""),
			std::string("drawMesh の \"") + (shape != nullptr ? shape : "") + "\" は組み込みの形 (cube / sphere / plane) "
			"でも registerMesh3D で登録した名前でもないので、cube で描きます。名前を確かめてください。");
	}
	return *detail::findBuiltin3DMesh("cube");
}

inline std::uint32_t Screen::registerMesh3D(const char* name, const render::Vertex3D* vertices, int vertexCount,
                                            const std::uint32_t* indices, int indexCount)
{
	if (m_renderer3D == nullptr || name == nullptr || detail::findBuiltin3DMesh(name) != nullptr) { return 0; }
	const std::uint32_t id = detail::meshNameId(name);
	return m_renderer3D->registerGameMesh(id, vertices, vertexCount, indices, indexCount) ? id : 0;
}

inline void Screen::releaseMesh3D(const char* name)
{
	if (m_renderer3D != nullptr && name != nullptr) { m_renderer3D->releaseGameMesh(detail::meshNameId(name)); }
}

inline bool Screen::hasMesh3D(const char* name) const
{
	if (name == nullptr) { return false; }
	if (detail::findBuiltin3DMesh(name) != nullptr) { return true; }
	return m_renderer3D != nullptr && m_renderer3D->findGameMesh(detail::meshNameId(name)) != nullptr;
}

inline void Screen::drawModelInstanced(const char* path, const render::MeshInstance* instances, int count)
{
	if (!has3D() || instances == nullptr || count <= 0) { return; }
	ensure3DFrame();
	m_renderer3D->drawModelInstances(path, instances, count);
}

inline void Screen::drawTrail(const render::TrailPointPod* points, std::uint32_t count,
                              const render::TrailStylePod& style)
{
	if (!has3D() || points == nullptr || count < 2) { return; }
	ensure3DFrame();
	m_renderer3D->drawTrail(points, static_cast<int>(count), style);
}

inline void Screen::setMotionKey(std::uint32_t key)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setMotionKey(key);
}

inline void Screen::setMotionVectorCaster(bool enabled)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setMotionVectorCaster(enabled);
}

} // namespace mitiru
