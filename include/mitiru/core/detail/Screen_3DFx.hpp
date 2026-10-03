#pragma once
// mitiru::Screen の ABI v49 (ADR 0062)・v50 (ADR 0067)・v51 (ADR 0071) の 3D 入口。直接 include しない。core/Screen.hpp 経由。
//
// VFX・屋外・副ビュー・モデルの解放・スキンの LOD・形の変わるメッシュ・破片・焼いた光。どれも IRenderer3D の末尾 virtual を呼ぶだけの描画の依頼で、3D 非対応
// (host 未注入 / DX11 / headless) は何もしない。

#include <mitiru/render/Decals.hpp>
#include <mitiru/render/GpuParticles.hpp>
#include <mitiru/render/HitFeel.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/VfxTextures.hpp>

namespace mitiru
{

inline void Screen::decals3D(const render::DecalDesc* decals, int count)
{
	if (!has3D() || decals == nullptr || count <= 0) { return; }
	ensure3DFrame();   // beginFrame がデカールの列を空にするので、開いた後に積む
	m_renderer3D->submitDecals(decals, count);
}

inline void Screen::particles3D(const render::ParticleEmitterDesc* emitters, int count)
{
	if (!has3D() || emitters == nullptr || count <= 0) { return; }
	ensure3DFrame();
	m_renderer3D->submitParticles(emitters, count);
}

inline int Screen::vfxTexture3D(const char* path, render::VfxTextureKind kind)
{
	return (m_renderer3D != nullptr && path != nullptr) ? m_renderer3D->registerVfxTextureFile(path, kind) : -1;
}

inline int Screen::vfxTexture3D(const char* path)
{
	return vfxTexture3D(path, render::VfxTextureKind::Color);
}

inline void Screen::hitFeel3D(const render::HitFeel& feel)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setHitFeel(feel);
}

inline void Screen::drawOutdoor(const char* worldPath, const render::OutdoorDrawPod& pod)
{
	if (!has3D() || worldPath == nullptr) { return; }
	ensure3DFrame();
	m_renderer3D->drawOutdoor(worldPath, pod);
}

inline bool Screen::view3D(int slot, const render::View3DPod& pod)
{
	return m_renderer3D != nullptr && m_renderer3D->setViewSlot(slot, pod);
}

inline bool Screen::beginView3D(int slot, const sgc::Vec3f& eye, const sgc::Vec3f& target, const sgc::Vec3f& up,
                                float fovDeg, float nearDist, float farDist)
{
	if (!has3D()) { return false; }
	ensure3DFrame();   // 副ビューは主ビューのフレームの中で描く
	constexpr float kDeg = 3.14159265358979f / 180.0f;
	const float n = (nearDist > 0.0f) ? nearDist : 0.1f;
	const float f = (farDist > n) ? farDist : n + 1.0f;
	// 縦横比は host が副ビューの大きさから決め直す
	const render::Camera3D cam(eye, target, up, fovDeg * kDeg, 1.0f, n, f);
	return m_renderer3D->beginViewSlot(slot, cam);
}

inline void Screen::endView3D()
{
	if (has3D()) { m_renderer3D->endViewSlot(); }
}

inline void Screen::compositeView3D(int slot, float x, float y, float w, float h)
{
	if (!has3D() || m_width <= 0 || m_height <= 0) { return; }
	ensure3DFrame();
	const float sx = 1.0f / static_cast<float>(m_width);
	const float sy = 1.0f / static_cast<float>(m_height);
	m_renderer3D->compositeViewSlot(slot, x * sx, y * sy, w * sx, h * sy);
}

inline void Screen::drawMeshWithView3D(const char* shape, const sgc::Vec3f& position, const sgc::Vec3f& scale,
                                       const sgc::Vec3f& rotDeg, const sgc::Colorf& color, int slot)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	render::Material material;
	material.diffuse = color;
	m_renderer3D->drawMeshWithViewSlot(resolveMesh3D(shape), detail::meshWorld(position, scale, rotDeg), material, slot);
}

inline void Screen::releaseView3D(int slot)
{
	if (m_renderer3D != nullptr) { m_renderer3D->releaseViewSlot(slot); }
}

inline bool Screen::releaseModel(const char* path)
{
	return m_renderer3D != nullptr && path != nullptr && m_renderer3D->releaseModel(path);
}

inline void Screen::skinnedLod(const render::SkinnedLodLook& look)
{
	if (m_renderer3D != nullptr) { m_renderer3D->setSkinnedLodLook(look); }
}

inline void Screen::updateMesh3D(const char* name, const render::Vertex3D* vertices, int vertexCount)
{
	if (m_renderer3D == nullptr || name == nullptr || vertices == nullptr || vertexCount <= 0) { return; }
	(void)m_renderer3D->updateGameMesh(detail::meshNameId(name), vertices, vertexCount);
}

inline void Screen::drawMeshPieces(const render::PieceInstancePod* pieces, int count)
{
	if (!has3D() || pieces == nullptr || count <= 0) { return; }
	ensure3DFrame();
	m_renderer3D->drawMeshPieces(pieces, count);
}

inline void Screen::lightingBake3D(const char* path)
{
	if (m_renderer3D != nullptr) { m_renderer3D->requestLightingBake(path); }
}

} // namespace mitiru
