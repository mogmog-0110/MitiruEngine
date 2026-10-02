#pragma once
// mitiru::Screen 用の 3D facade 実装。直接 include しない。core/Screen.hpp 経由。
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
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/SceneLook.hpp>

namespace mitiru
{

namespace detail
{
inline bool equalsNoCase(const char* a, const char* b) noexcept
{
	for (; *a != '\0' && *b != '\0'; ++a, ++b)
	{
		const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
		if (ca != *b) { return false; }
	}
	return *a == *b;
}

/// 組み込みメッシュを 1 回だけ生成して使い回す (アドレスが安定 = レンダラーの
/// メッシュキャッシュを使える)。名前は大文字小文字を問わず完全一致。組み込みでなければ nullptr。
inline const render::Mesh* findBuiltin3DMesh(const char* shape) noexcept
{
	static const render::Mesh cube   = render::Mesh::createCube(1.0f);
	static const render::Mesh sphere = render::Mesh::createSphere(0.5f, 32);
	static const render::Mesh plane  = render::Mesh::createPlane(1.0f, 1.0f);
	if (shape == nullptr) { return nullptr; }
	if (equalsNoCase(shape, "sphere")) { return &sphere; }
	if (equalsNoCase(shape, "plane")) { return &plane; }
	if (equalsNoCase(shape, "cube")) { return &cube; }
	return nullptr;
}

/// registerMesh3D の名前から番号を作る (FNV-1a 32)。0 は失敗に取っておくので 1 へずらす
[[nodiscard]] inline std::uint32_t meshNameId(const char* name) noexcept
{
	std::uint32_t h = 2166136261u;
	for (const char* p = name; p != nullptr && *p != '\0'; ++p)
	{
		h ^= static_cast<unsigned char>(*p);
		h *= 16777619u;
	}
	return (h == 0) ? 1u : h;
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
	m_cam3DUp     = {0.0f, 1.0f, 0.0f};
}

inline void Screen::camera3D(const sgc::Vec3f& eye, const sgc::Vec3f& target,
                             float fovDeg, float rollDeg) noexcept
{
	m_cam3DEye    = eye;
	m_cam3DTarget = target;
	m_cam3DFovDeg = fovDeg;
	// up を視線軸まわりに rollDeg 回す: up' = up·cosθ + (f×up)·sinθ
	constexpr float kDeg = 3.14159265358979f / 180.0f;
	sgc::Vec3f f{target.x - eye.x, target.y - eye.y, target.z - eye.z};
	const float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
	if (fl < 1e-6f) { m_cam3DUp = {0.0f, 1.0f, 0.0f}; return; }
	f = {f.x / fl, f.y / fl, f.z / fl};
	// 真上/真下を向いた時は基準 up を +z へ切り替える (縮退回避)
	const sgc::Vec3f base = (f.y > 0.999f || f.y < -0.999f)
		? sgc::Vec3f{0.0f, 0.0f, 1.0f} : sgc::Vec3f{0.0f, 1.0f, 0.0f};
	const sgc::Vec3f fxu{f.y * base.z - f.z * base.y,
	                     f.z * base.x - f.x * base.z,
	                     f.x * base.y - f.y * base.x};
	const float c = std::cos(rollDeg * kDeg);
	const float s = std::sin(rollDeg * kDeg);
	m_cam3DUp = {base.x * c + fxu.x * s,
	             base.y * c + fxu.y * s,
	             base.z * c + fxu.z * s};
}

inline void Screen::light3D(const sgc::Vec3f& direction,
                            const sgc::Colorf& color) noexcept
{
	m_light3DDir   = direction;
	m_light3DColor = color;
}

inline void Screen::skybox3D(const sgc::Colorf& zenith, const sgc::Colorf& nadir) noexcept
{
	// cubemap 構築 + GPU upload は重いので、色が変わった時だけ再反映 flag を立てる。
	if (!m_sky3DRequested || zenith != m_sky3DZenith || nadir != m_sky3DNadir)
	{
		m_sky3DRequested = true;
		m_sky3DApplied   = false;
		m_sky3DZenith    = zenith;
		m_sky3DNadir     = nadir;
	}
}

inline void Screen::toon3D(bool enabled, const sgc::Colorf& shadowTint) noexcept
{
	m_toon3D        = enabled;
	m_toonShadowTint = shadowTint;
}

inline void Screen::fog3D(bool enabled, const sgc::Colorf& color, float nearDist,
                          float farDist) noexcept
{
	m_fog3D      = enabled;
	m_fog3DColor = color;
	m_fog3DNear  = nearDist;
	m_fog3DFar   = farDist;
}

inline void Screen::shadowCaster3D(bool enabled)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setShadowCaster(enabled);
}

inline void Screen::outlineCaster3D(bool enabled)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setOutlineCaster(enabled);
}

inline void Screen::outline3D(bool enabled, float widthPx, float threshold,
                              bool depthOnly) noexcept
{
	m_outline3D       = enabled;
	m_outlineWidthPx  = widthPx;
	m_outlineThresh   = threshold;
	m_outlineDepthOnly = depthOnly;
	m_sceneOutlineModeSet = false;  // 個別 API 優先に戻す (sceneLook3D 由来の mode を無効化)
}

inline void Screen::sceneLook3D(const render::SceneLook& look) noexcept
{
	m_sceneLookSet        = true;
	m_outline3D           = look.outline;
	m_outlineWidthPx      = look.outlineWidthPx;
	m_outlineThresh       = look.outlineThreshold;
	m_sceneOutlineModeSet = true;
	m_sceneOutlineMode    = look.outlineMode;

	m_fog3D      = look.fog;
	m_fog3DColor = sgc::Colorf{look.fogColor[0], look.fogColor[1], look.fogColor[2], 1.0f};
	m_fog3DNear  = look.fogNear;
	m_fog3DFar   = look.fogFar;

	m_sceneExposure       = look.exposure;
	m_sceneGamma          = look.gamma;
	m_sceneAmbient        = sgc::Colorf{look.ambient[0], look.ambient[1], look.ambient[2], 1.0f};
	m_sceneShadowCaster   = look.shadowCaster;
	m_sceneOutlineCaster  = look.outlineCaster;
	m_sceneShadowCascaded = look.shadowCascaded;
	m_sceneShadowAutoFit  = look.shadowCascadeAutoFit;
	m_sceneShadowDistance = look.shadowDistance;
	m_sceneShadowCascadeCount = look.shadowCascadeCount;
	m_sceneShadowEnabled  = look.shadow;
	m_sceneShadowDirSet   = look.shadow;
	m_sceneShadowDir = sgc::Vec3f{look.shadowDirection[0], look.shadowDirection[1],
	                              look.shadowDirection[2]};

	m_sceneAo                = look.ao;
	m_sceneAoRadius          = look.aoRadius;
	m_sceneAoStrength        = look.aoStrength;
	m_sceneToonBands         = look.toonBands;
	m_sceneToonSoftness      = look.toonSoftness;
	m_sceneToonMidTint       = sgc::Colorf{look.toonMidTint[0], look.toonMidTint[1], look.toonMidTint[2], 1.0f};
	m_sceneToonSpecular      = look.toonSpecular;
	m_sceneToonSpecularPower = look.toonSpecularPower;

	m_sceneBloom          = look.bloom;
	m_sceneBloomThreshold = look.bloomThreshold;
	m_sceneBloomStrength  = look.bloomStrength;
	m_sceneShadowSoftness = look.shadowSoftness;
	m_sceneSaturation     = look.saturation;
	m_sceneContrast       = look.contrast;

	m_sceneOutlineFadeNear = look.outlineFadeNear;
	m_sceneOutlineFadeFar  = look.outlineFadeFar;
	m_sceneOutlineFadeMin  = look.outlineFadeMin;

	m_sceneAmbientSky    = sgc::Colorf{look.ambientSky[0], look.ambientSky[1], look.ambientSky[2], 1.0f};
	m_sceneAmbientGround = sgc::Colorf{look.ambientGround[0], look.ambientGround[1], look.ambientGround[2], 1.0f};
	m_sceneRimStrength   = look.rimStrength;
	m_sceneRimPower      = look.rimPower;
	m_sceneRimColor      = sgc::Colorf{look.rimColor[0], look.rimColor[1], look.rimColor[2], 1.0f};
	m_sceneOutlineDarken = look.outlineDarken;

	m_sceneDofStart    = look.dofStart;
	m_sceneDofEnd      = look.dofEnd;
	m_sceneDofStrength = look.dofStrength;
	m_sceneShadowBias  = look.shadowBias;

	m_sceneAaMode       = look.aaMode;
	m_sceneAoMethod     = look.aoMethod;
	m_sceneShadingModel = look.shadingModel;
	m_sceneMotionBlur   = look.motionBlur;
	m_sceneSky          = look.sky;
	m_sceneVolumetricFog = look.volumetricFog;
}

/// @brief 最初の 3D 描画でフレームを開く (clear 色は screen->clear() と共有)
inline void Screen::ensure3DFrame()
{
	if (m_3dStarted) { return; }
	m_renderer3D->beginFrame(m_clearColor);
	const float aspect = (m_height > 0)
		? static_cast<float>(m_width) / static_cast<float>(m_height)
		: 16.0f / 9.0f;
	constexpr float kDeg = 3.14159265358979f / 180.0f;
	const render::Camera3D cam(m_cam3DEye, m_cam3DTarget, m_cam3DUp,
	                           m_cam3DFovDeg * kDeg, aspect, m_cam3DNear, m_cam3DFar);
	m_renderer3D->setCamera(cam);
	m_renderer3D->setLight(
		render::Light::directional(m_light3DDir, m_light3DColor));
	m_renderer3D->setShaderMode(m_toon3D ? render::ShaderMode3D::Toon
	                                     : render::ShaderMode3D::Phong);
	if (m_toon3D) { m_renderer3D->setToonShadowTint(m_toonShadowTint); }
	m_renderer3D->setFog(m_fog3D, m_fog3DColor, m_fog3DNear, m_fog3DFar);
	m_renderer3D->setOutlineEnabled(m_outline3D);
	if (m_outline3D)
	{
		const render::OutlineMode outlineMode = m_sceneOutlineModeSet
			? static_cast<render::OutlineMode>(m_sceneOutlineMode)
			: (m_outlineDepthOnly ? render::OutlineMode::DepthSobel : render::OutlineMode::DepthColorCombo);
		m_renderer3D->setOutlineMode(outlineMode);
		m_renderer3D->setOutlineParams(m_outlineWidthPx, m_outlineThresh);
	}
	// 絵の設定 (§1-12、ABI v35): sceneLook3D() を呼んだ時だけ反映する。renderer 初期化時の
	// 既定値 (backend ごとに違う。例: defaultAmbient) を SceneLook の既定値で上書きしないため、
	// 未使用のゲームは呼び出し自体が起きず今までどおり renderer 側の既定のまま。
	if (m_sceneLookSet)
	{
		m_renderer3D->setTonemapExposure(m_sceneExposure);
		m_renderer3D->setTonemapGamma(m_sceneGamma);
		m_renderer3D->setAmbientColor(m_sceneAmbient);
		m_renderer3D->setCascadedShadowEnabled(m_sceneShadowCascaded);
		m_renderer3D->setCascadedShadowAutoFit(m_sceneShadowAutoFit, m_sceneShadowDistance);
		m_renderer3D->setShadowCascadeCount(
			m_sceneShadowCascaded ? (m_sceneShadowCascadeCount >= 2 ? m_sceneShadowCascadeCount : 2) : 1);
		m_renderer3D->setShadowCaster(m_sceneShadowCaster);
		m_renderer3D->setOutlineCaster(m_sceneOutlineCaster);
		m_renderer3D->setAmbientOcclusion(m_sceneAo, m_sceneAoRadius, m_sceneAoStrength);
		m_renderer3D->setToonRamp(m_sceneToonBands, m_sceneToonSoftness, m_sceneToonMidTint);
		m_renderer3D->setToonSpecular(m_sceneToonSpecular, m_sceneToonSpecularPower);
		m_renderer3D->setBloom(m_sceneBloom, m_sceneBloomThreshold, m_sceneBloomStrength);
		m_renderer3D->setShadowSoftness(m_sceneShadowSoftness);
		m_renderer3D->setColorGrade(m_sceneSaturation, m_sceneContrast);
		m_renderer3D->setOutlineFade(m_sceneOutlineFadeNear, m_sceneOutlineFadeFar,
		                             m_sceneOutlineFadeMin);
		m_renderer3D->setHemisphereAmbient(m_sceneAmbientSky, m_sceneAmbientGround);
		m_renderer3D->setRimLight(m_sceneRimStrength, m_sceneRimPower, m_sceneRimColor);
		m_renderer3D->setOutlineDarken(m_sceneOutlineDarken);
		m_renderer3D->setDepthOfField(m_sceneDofStart, m_sceneDofEnd, m_sceneDofStrength);
		m_renderer3D->setShadowBias(m_sceneShadowBias);
	}
	// 影を有効化 (オブジェクトが地面に接地して見える)。sceneLook3D() が明示した向きが
	// あればそれを、無ければ従来どおり光と同じ向きで落とす。
	m_renderer3D->setShadowEnabled(m_sceneLookSet ? m_sceneShadowEnabled : true);
	m_renderer3D->setShadowDirection(m_sceneShadowDirSet ? m_sceneShadowDir : m_light3DDir);
	// skybox3D() 済みなら最遠面の空を張る (色が変わった時だけ cubemap を作り直す)。
	if (m_sky3DRequested && !m_sky3DApplied)
	{
		m_renderer3D->setSkybox(
			render::Cubemap::verticalGradient(64, m_sky3DZenith, m_sky3DNadir));
		m_sky3DApplied = true;
	}
	applyLookV48();
	m_3dStarted = true;
}

inline void Screen::drawMesh(const char* shape, const sgc::Vec3f& position,
                             const sgc::Vec3f& scale, const sgc::Vec3f& rotDeg,
                             const sgc::Colorf& color)
{
	if (!has3D()) { return; }
	ensure3DFrame();

	constexpr float kDeg = 3.14159265358979f / 180.0f;
	const sgc::Mat4f world =
		sgc::Mat4f::translation(position) *
		sgc::Mat4f::rotationY(rotDeg.y * kDeg) *
		sgc::Mat4f::rotationX(rotDeg.x * kDeg) *
		sgc::Mat4f::rotationZ(rotDeg.z * kDeg) *
		sgc::Mat4f::scaling(scale);

	render::Material material;
	material.diffuse = color;
	m_renderer3D->drawMesh(resolveMesh3D(shape), world, material);
}

inline void Screen::drawMesh(const char* shape, const sgc::Vec3f& position,
                             const sgc::Vec3f& scale, const sgc::Vec3f& rotDeg,
                             const render::Texture& texture, const sgc::Colorf& tint)
{
	if (!has3D()) { return; }
	ensure3DFrame();

	constexpr float kDeg = 3.14159265358979f / 180.0f;
	const sgc::Mat4f world =
		sgc::Mat4f::translation(position) *
		sgc::Mat4f::rotationY(rotDeg.y * kDeg) *
		sgc::Mat4f::rotationX(rotDeg.x * kDeg) *
		sgc::Mat4f::rotationZ(rotDeg.z * kDeg) *
		sgc::Mat4f::scaling(scale);

	render::Material material;
	material.diffuse = tint;
	material.albedoTexture = &texture;
	// 不透明パスはアルファで混ぜるが深度は板全体に書くので、絵の周りの抜けた所が見えない板になって
	// 後ろの物を隠し、深度を読む SSAO と被写界深度もそこを板の距離と取り違える。完全に抜けた画素だけ捨てる
	material.alphaMode   = render::Material::AlphaMode::Mask;
	material.alphaCutoff = 1.0f / 255.0f;
	m_renderer3D->drawMesh(resolveMesh3D(shape), world, material);
}

inline void Screen::drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position,
                              float rotYDeg, float scale)
{
	drawSolid(bakeManifestPath, position, rotYDeg, scale, 0.0f);
}

inline void Screen::drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position,
                              float rotYDeg, float scale, float timeSec)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawSolid(bakeManifestPath, position, rotYDeg, scale, timeSec);
}

inline void Screen::drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
                              float scale)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawModel(path, position, rotYDeg, scale);
}

inline void Screen::drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
                              float scale, const char* clipName, float clipTimeSec)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawSkinnedModel(path, position, rotYDeg, scale, clipName, clipTimeSec,
	                               nullptr, 0.0f, 0.0f);
}

inline void Screen::drawModelBlend(const char* path, const sgc::Vec3f& position, float rotYDeg,
                                   float scale, const char* clipA, float timeA,
                                   const char* clipB, float timeB, float mix)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawSkinnedModel(path, position, rotYDeg, scale, clipA, timeA, clipB,
	                               timeB, mix);
}

inline void Screen::drawModel(const char* path, const sgc::Vec3f& position,
                              const sgc::Vec3f& rotDeg, float scale)
{
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->drawModelRot(path, position, rotDeg, scale);
}

inline bool Screen::loadSplatScene(const char* path)
{
	if (m_renderer3D == nullptr) { return false; }
	return m_renderer3D->loadSplatScene(path);
}

inline bool Screen::splatBounds(sgc::Vec3f& center, float& radius)
{
	if (m_renderer3D == nullptr) { return false; }
	float cx = 0.0f, cy = 0.0f, cz = 0.0f, r = 1.0f;
	m_renderer3D->splatBounds(cx, cy, cz, r);
	center = sgc::Vec3f{cx, cy, cz};
	radius = r;
	return true;
}

inline void Screen::drawSplats()
{
	if (!has3D()) { return; }

	// drawMesh と同じく最初の 3D 呼び出しでフレームを開く + カメラを設定する。
	if (!m_3dStarted)
	{
		m_renderer3D->beginFrame(m_clearColor);
		const float aspect = (m_height > 0)
			? static_cast<float>(m_width) / static_cast<float>(m_height)
			: 16.0f / 9.0f;
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		const render::Camera3D cam(m_cam3DEye, m_cam3DTarget, {0.0f, 1.0f, 0.0f},
		                           m_cam3DFovDeg * kDeg, aspect, m_cam3DNear, m_cam3DFar);
		m_renderer3D->setCamera(cam);
		m_3dStarted = true;
	}
	m_renderer3D->drawSplats();
}

inline void Screen::drawLive2D(const char* model3jsonPath)
{
	if (!has3D()) { return; }

	// drawSplats と同じく最初の 3D 呼び出しでフレームを開く (endFrame で tonemap→Live2D 合成が走る)。
	if (!m_3dStarted)
	{
		m_renderer3D->beginFrame(m_clearColor);
		const float aspect = (m_height > 0)
			? static_cast<float>(m_width) / static_cast<float>(m_height)
			: 16.0f / 9.0f;
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		const render::Camera3D cam(m_cam3DEye, m_cam3DTarget, {0.0f, 1.0f, 0.0f},
		                           m_cam3DFovDeg * kDeg, aspect, m_cam3DNear, m_cam3DFar);
		m_renderer3D->setCamera(cam);
		m_3dStarted = true;
	}
	m_renderer3D->drawLive2D(model3jsonPath);
}

inline void Screen::enableNeuralFx(bool enabled, float strength)
{
	if (m_renderer3D) { m_renderer3D->enableNeuralFx(enabled, strength); }
}

inline void Screen::enableRelight(bool enabled, float lightX, float lightY, float strength, float rim)
{
	if (m_renderer3D) { m_renderer3D->enableRelight(enabled, lightX, lightY, strength, rim); }
}

inline void Screen::setRelightDepthModel(const char* path)
{
	if (m_renderer3D) { m_renderer3D->setRelightDepthModel(path); }
}

inline void Screen::live2dLookAt(float nx, float ny)
{
	if (m_renderer3D) { m_renderer3D->live2dLookAt(nx, ny); }
}

inline void Screen::live2dTap()
{
	if (m_renderer3D) { m_renderer3D->live2dTap(); }
}

inline void Screen::live2dStage(const char* bg, const char* gear, const char* close)
{
	if (m_renderer3D) { m_renderer3D->live2dStage(bg, gear, close); }
}

// ── ニューラル現像 (M3): 3D フレームを ONNX+DirectML で 2D 絵画へ ──────────
// 重い ORT は IRenderer3D の裏 (host 側) でのみコンパイルされる。ここは仮想呼び出し
// + drawPixelGrid だけの薄い facade なので、ゲーム DLL は ORT に依存しない。

inline void Screen::developToStyle(const char* onnxPath)
{
	if (m_renderer3D != nullptr) { m_renderer3D->requestDevelop(onnxPath); }
}

inline bool Screen::styleReady() const
{
	return (m_renderer3D != nullptr) && m_renderer3D->styleReady();
}

inline void Screen::clearStyle()
{
	if (m_renderer3D != nullptr) { m_renderer3D->clearDevelop(); }
}

inline void Screen::bakeStyle()
{
	if (m_renderer3D != nullptr) { m_renderer3D->bakeStyleToSplats(); }
}

inline void Screen::resetSplats()
{
	if (m_renderer3D != nullptr) { m_renderer3D->resetSplatColors(); }
}

inline float Screen::bakedFraction()
{
	return (m_renderer3D != nullptr) ? m_renderer3D->bakedFraction() : 0.0f;
}

inline void Screen::captureTarget()
{
	if (m_renderer3D != nullptr) { m_renderer3D->captureTargetFromStyle(); }
}

inline void Screen::showTarget(bool on)
{
	if (m_renderer3D != nullptr) { m_renderer3D->setShowTarget(on); }
}

inline bool Screen::hasTarget()
{
	return (m_renderer3D != nullptr) && m_renderer3D->hasTarget();
}

inline float Screen::matchScore()
{
	return (m_renderer3D != nullptr) ? m_renderer3D->matchScore() : 0.0f;
}

inline bool Screen::projectToScreen(const sgc::Vec3f& world, float& sx, float& sy)
{
	float u = -1.0f, v = -1.0f;
	const bool on = (m_renderer3D != nullptr) && m_renderer3D->worldToScreen(world.x, world.y, world.z, u, v);
	sx = u * static_cast<float>(m_width);
	sy = v * static_cast<float>(m_height);
	return on;
}

inline void Screen::drawStyle(float strength)
{
	// 実際の全画面 α 合成は renderer の post-process (blitStyleDx12, FXAA 後・overlay 前) が
	// 物理解像度で行う。ここは強度を渡すだけ。毎フレーム呼ぶこと (0=3D / 1=完全 2D)。
	if (m_renderer3D != nullptr) { m_renderer3D->setStyleStrength(strength); }
}

} // namespace mitiru
