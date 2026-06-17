#pragma once
// mitiru::Screen 用の 3D facade 実装 — 直接 include しない。core/Screen.hpp 経由。
//
// draw(Screen&) の中で camera3D → drawMesh を呼ぶだけで GPU 3D が出る薄い層。
// 最初の drawMesh が遅延 beginFrame し、Engine が描画後に endFrame/finalize する
// (Engine_Frame::tickRenderPhase 末尾)。3D 非対応 (host 未注入 / DX11 / headless) は no-op。

#include <cmath>

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>

namespace mitiru
{

namespace detail
{
/// 組み込みメッシュを 1 回だけ生成して使い回す (アドレスが安定 = レンダラーの
/// メッシュキャッシュが効く)。先頭文字で種別判定: s=sphere / p=plane / 既定=cube。
inline const render::Mesh& builtin3DMesh(const char* shape) noexcept
{
	static const render::Mesh cube   = render::Mesh::createCube(1.0f);
	static const render::Mesh sphere = render::Mesh::createSphere(0.5f, 32);
	static const render::Mesh plane  = render::Mesh::createPlane(1.0f, 1.0f);
	if (shape != nullptr)
	{
		if (shape[0] == 's' || shape[0] == 'S') { return sphere; }
		if (shape[0] == 'p' || shape[0] == 'P') { return plane; }
	}
	return cube;
}
} // namespace detail

inline bool Screen::has3D() const noexcept
{
	return m_renderer3D != nullptr && m_renderer3D->isInitialized();
}

inline void Screen::camera3D(const sgc::Vec3f& eye, const sgc::Vec3f& target,
                             float fovDeg) noexcept
{
	m_cam3DEye    = eye;
	m_cam3DTarget = target;
	m_cam3DFovDeg = fovDeg;
}

inline void Screen::light3D(const sgc::Vec3f& direction,
                            const sgc::Colorf& color) noexcept
{
	m_light3DDir   = direction;
	m_light3DColor = color;
}

inline void Screen::drawMesh(const char* shape, const sgc::Vec3f& position,
                             const sgc::Vec3f& scale, const sgc::Vec3f& rotDeg,
                             const sgc::Colorf& color)
{
	if (!has3D()) { return; }

	// 最初の drawMesh でフレームを開く (clear 色は screen->clear() と共有)。
	if (!m_3dStarted)
	{
		m_renderer3D->beginFrame(m_clearColor);
		const float aspect = (m_height > 0)
			? static_cast<float>(m_width) / static_cast<float>(m_height)
			: 16.0f / 9.0f;
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		const render::Camera3D cam(m_cam3DEye, m_cam3DTarget, {0.0f, 1.0f, 0.0f},
		                           m_cam3DFovDeg * kDeg, aspect, 0.1f, 500.0f);
		m_renderer3D->setCamera(cam);
		m_renderer3D->setLight(
			render::Light::directional(m_light3DDir, m_light3DColor));
		// 既定は普通の Phong シェーディング (なめらかな陰影)。トゥーン調の
		// セル塗り + 輪郭線は出さない。
		m_renderer3D->setShaderMode(render::ShaderMode3D::Phong);
		m_renderer3D->setOutlineEnabled(false);
		// 影を有効化 (オブジェクトが地面に接地して見える)。光と同じ向きで落とす。
		m_renderer3D->setShadowEnabled(true);
		m_renderer3D->setShadowDirection(m_light3DDir);
		m_3dStarted = true;
	}

	constexpr float kDeg = 3.14159265358979f / 180.0f;
	const sgc::Mat4f world =
		sgc::Mat4f::translation(position) *
		sgc::Mat4f::rotationY(rotDeg.y * kDeg) *
		sgc::Mat4f::rotationX(rotDeg.x * kDeg) *
		sgc::Mat4f::rotationZ(rotDeg.z * kDeg) *
		sgc::Mat4f::scaling(scale);

	render::Material material;
	material.diffuse = color;
	m_renderer3D->drawMesh(detail::builtin3DMesh(shape), world, material);
}

} // namespace mitiru
