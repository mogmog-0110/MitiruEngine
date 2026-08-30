/// @file Renderer3D_WebGL.hpp
/// @brief WebGL2 (Emscripten) 用の 3D レンダラ。DX12 版と同じ絵を web で出す。
///
/// これが無い間、web ビルドでは `createRenderer3DFor` が nullptr を返し、3D の
/// 地形・柵・小物がまるごと画面から消えていた (2D スプライトと HTML の HUD だけが
/// 残る)。DX12 版と**同じ見た目**を目標にしているので、トゥーンの帯・影の色味・
/// アウトラインの出方は `dx12/DX12ShaderModePS.hpp` と `ToonShaders3D.hpp` の式を
/// そのまま移植してある。式を変えるときは両方を一緒に直すこと。
///
/// パス構成 (DX12 版と同じ順):
///   1. オフスクリーンへ空のグラデーション (深度書き込み無し)
///   2. オフスクリーンへジオメトリ (MRT: 色 + 法線/NdotV、深度はテクスチャ)
///   3. 既定のフレームバッファへ合成 = 色 + 深度から検出したアウトライン
///
/// 2D と HTML の HUD は 3D のあとに既定のフレームバッファへ描かれるので、
/// endFrame() の時点で合成まで終えておく必要がある。
#pragma once

#ifdef __EMSCRIPTEN__

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <GLES3/gl3.h>
#include <emscripten/html5_webgl.h>

#include <mitiru/render/Shadow.hpp>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/GltfLoader.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

namespace detail
{

/// 4x4 行列 (列優先。GLSL の mat4 とメモリ配置を合わせてある)。
struct Mat4
{
	float m[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static Mat4 identity() noexcept { return Mat4{}; }

	/// @brief this * rhs (列優先なので「rhs を先に適用」の順で合成される)。
	[[nodiscard]] Mat4 mul(const Mat4& rhs) const noexcept
	{
		Mat4 out;
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				float s = 0.0f;
				for (int k = 0; k < 4; ++k) { s += m[k * 4 + r] * rhs.m[c * 4 + k]; }
				out.m[c * 4 + r] = s;
			}
		}
		return out;
	}
};

inline Mat4 translation(const sgc::Vec3f& t) noexcept
{
	Mat4 o;
	o.m[12] = t.x; o.m[13] = t.y; o.m[14] = t.z;
	return o;
}

inline Mat4 scaling(const sgc::Vec3f& s) noexcept
{
	Mat4 o;
	o.m[0] = s.x; o.m[5] = s.y; o.m[10] = s.z;
	return o;
}

/// 度で受ける XYZ 回転。drawMesh の rotDeg と同じ {pitch, yaw, roll} の順で掛ける。
inline Mat4 rotation(const sgc::Vec3f& deg) noexcept
{
	constexpr float kRad = 3.14159265358979f / 180.0f;
	const float cx = std::cos(deg.x * kRad), sx = std::sin(deg.x * kRad);
	const float cy = std::cos(deg.y * kRad), sy = std::sin(deg.y * kRad);
	const float cz = std::cos(deg.z * kRad), sz = std::sin(deg.z * kRad);

	Mat4 rx; rx.m[5] = cx; rx.m[6] = sx; rx.m[9] = -sx; rx.m[10] = cx;
	Mat4 ry; ry.m[0] = cy; ry.m[2] = -sy; ry.m[8] = sy; ry.m[10] = cy;
	Mat4 rz; rz.m[0] = cz; rz.m[1] = sz; rz.m[4] = -sz; rz.m[5] = cz;
	return rz.mul(ry).mul(rx);
}

inline sgc::Vec3f normalize(const sgc::Vec3f& v) noexcept
{
	const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (n <= 1e-6f) { return sgc::Vec3f{0.0f, 0.0f, 1.0f}; }
	return sgc::Vec3f{v.x / n, v.y / n, v.z / n};
}

inline sgc::Vec3f cross(const sgc::Vec3f& a, const sgc::Vec3f& b) noexcept
{
	return sgc::Vec3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float dot(const sgc::Vec3f& a, const sgc::Vec3f& b) noexcept
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Mat4 lookAt(const sgc::Vec3f& eye, const sgc::Vec3f& target, const sgc::Vec3f& up) noexcept
{
	const sgc::Vec3f f = normalize(sgc::Vec3f{target.x - eye.x, target.y - eye.y, target.z - eye.z});
	const sgc::Vec3f s = normalize(cross(f, up));
	const sgc::Vec3f u = cross(s, f);

	Mat4 o;
	o.m[0] = s.x; o.m[4] = s.y; o.m[8]  = s.z;
	o.m[1] = u.x; o.m[5] = u.y; o.m[9]  = u.z;
	o.m[2] = -f.x; o.m[6] = -f.y; o.m[10] = -f.z;
	o.m[12] = -dot(s, eye);
	o.m[13] = -dot(u, eye);
	o.m[14] = dot(f, eye);
	return o;
}

/// 深度は [-1, 1] (GL の既定)。アウトラインの線形化もこの規約で書いてある。
inline Mat4 perspective(float fovRad, float aspect, float nearZ, float farZ) noexcept
{
	const float t = 1.0f / std::tan(fovRad * 0.5f);
	Mat4 o;
	o.m[0] = t / (aspect > 1e-6f ? aspect : 1.0f);
	o.m[5] = t;
	o.m[10] = (farZ + nearZ) / (nearZ - farZ);
	o.m[11] = -1.0f;
	o.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
	o.m[15] = 0.0f;
	return o;
}

/// glTF ノードの局所姿勢 T*R*S。回転は quaternion (xyzw)。
inline Mat4 trs(const sgc::Vec3f& t, const sgc::Vec4f& q, const sgc::Vec3f& sc) noexcept
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	Mat4 r;
	r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w);     r.m[2] = 2 * (x * z - y * w);
	r.m[4] = 2 * (x * y - z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[6] = 2 * (y * z + x * w);
	r.m[8] = 2 * (x * z + y * w);     r.m[9] = 2 * (y * z - x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
	return translation(t).mul(r).mul(scaling(sc));
}

}  // namespace detail

/// @brief WebGL2 の 3D レンダラ。
class Renderer3D_WebGL final : public IRenderer3D
{
public:
	Renderer3D_WebGL() = default;
	~Renderer3D_WebGL() override { destroy(); }

	Renderer3D_WebGL(const Renderer3D_WebGL&) = delete;
	Renderer3D_WebGL& operator=(const Renderer3D_WebGL&) = delete;

	/// @brief シェーダとオフスクリーンを作る。失敗したら isInitialized() が false のまま。
	void initialize(int width, int height)
	{
		m_width = width > 0 ? width : 1;
		m_height = height > 0 ? height : 1;

		m_toon = linkProgram(kToonVS, kToonFS);
		m_composite = linkProgram(kFullscreenVS, kCompositeFS);
		m_sky = linkProgram(kFullscreenVS, kSkyFS);
		m_shadowProg = linkProgram(kShadowVS, kShadowFS);
		if (m_toon == 0 || m_composite == 0 || m_sky == 0) { return; }

		// 合成パスは頂点バッファを持たない (gl_VertexID から三角形を組む) が、
		// WebGL2 では VAO 無しの描画が許されないので空の VAO を 1 つ用意する。
		glGenVertexArrays(1, &m_emptyVao);
		glGenBuffers(1, &m_instVbo);

		// 白 1x1。テクスチャの無いマテリアルでも同じシェーダで描くための下敷き。
		glGenTextures(1, &m_whiteTex);
		glBindTexture(GL_TEXTURE_2D, m_whiteTex);
		const std::uint8_t white[4] = {255, 255, 255, 255};
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		createTargets();
		createShadowTarget();
		m_initialized = (m_fbo != 0);
	}

	[[nodiscard]] bool isInitialized() const noexcept override { return m_initialized; }

	void resize(int width, int height) override
	{
		if (width <= 0 || height <= 0) { return; }
		if (width == m_width && height == m_height) { return; }
		m_width = width;
		m_height = height;
		destroyTargets();
		createTargets();
	}

	void beginFrame(const sgc::Colorf& clearColor = {0.2f, 0.2f, 0.3f, 1.0f}) override
	{
		if (!m_initialized) { return; }
		m_frameActive = true;
		m_drawCalls = 0;
		m_shadowCasterEnabled = true;
		m_toonFrameUniforms = false;
		m_shadowCommandsPrev = std::move(m_shadowCommands);
		m_shadowCommands.clear();


		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glViewport(0, 0, m_width, m_height);
		const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
		glDrawBuffers(2, bufs);

		glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glDisable(GL_BLEND);

		// 空はここでは描かない。Screen::ensure3DFrame は beginFrame の「後」に
		// setCamera を呼ぶので、ここで描くと 1 フレーム古い (初回は単位行列の)
		// カメラで視線方向を組むことになる。画角が狂うと空の階調が合わない。
		m_skyDrawn = false;
	}

	void endFrame() override
	{
		if (!m_initialized) { return; }
		flushBatches();
		composite();
		m_frameActive = false;
	}

	void setCamera(const Camera3D& camera) override
	{
		m_toonFrameUniforms = false;
		m_view = detail::lookAt(camera.position(), camera.target(), camera.up());
		m_proj = detail::perspective(camera.fov(), camera.aspectRatio(),
		                             camera.nearClip(), camera.farClip());
		m_cameraPos = camera.position();
		m_nearZ = camera.nearClip();
		m_farZ = camera.farClip();
		// カメラが決まった直後に空を敷く。深度は書かないので、このあとの
		// 幾何より前に出しても前後関係は壊れない。
		if (m_frameActive && m_skyEnabled && !m_skyDrawn) { drawSky(); m_skyDrawn = true; }
	}

	void setLight(const Light& light) override
	{
		m_toonFrameUniforms = false;
		m_lightDir = light.direction;
		m_lightColor = light.color;
	}

	void setAmbientColor(const sgc::Colorf& color) override
	{
		m_ambient = color;
		m_toonFrameUniforms = false;
	}
	[[nodiscard]] sgc::Colorf ambientColor() const noexcept override { return m_ambient; }

	/// @brief 空。縦グラデーションの上下 2 色だけを使う。
	///
	/// 立方体テクスチャを貼らず、視線の高さから直接グラデーションを描く。
	/// engine の skybox3D は `Cubemap::verticalGradient` しか渡してこないので、
	/// 見た目は同じでテクスチャ 1 枚ぶん軽い。
	void setSkybox(const Cubemap& cubemap) override
	{
		m_skyTop = faceCenterColor(cubemap, 2);      // +Y
		m_skyBottom = faceCenterColor(cubemap, 3);   // -Y
		m_skyEnabled = true;
	}

	void setSkyboxEnabled(bool enabled) override { m_skyEnabled = enabled; }
	[[nodiscard]] bool isSkyboxEnabled() const noexcept override { return m_skyEnabled; }

	void setOutlineEnabled(bool enabled) override { m_outline = enabled; }
	[[nodiscard]] bool isOutlineEnabled() const noexcept override { return m_outline; }
	void setOutlineMode(OutlineMode mode) override { m_outlineMode = mode; }
	[[nodiscard]] OutlineMode outlineMode() const noexcept override { return m_outlineMode; }

	void setOutlineParams(float widthPx, float threshold) override
	{
		m_outlineWidth = widthPx;
		m_outlineThreshold = threshold;
	}

	void setToonShadowTint(const sgc::Colorf& tint) override { m_shadowTint = tint; }

	void setFog(bool enabled, const sgc::Colorf& color, float nearDist, float farDist) override
	{
		m_toonFrameUniforms = false;
		m_fog = enabled;
		m_fogColor = color;
		m_fogNear = nearDist;
		m_fogFar = farDist;
	}

	/// @details DX12 と同じ ACES filmic の入力側の倍率。0 以下は 1.0 に丸める。
	void setTonemapExposure(float exposure) override
	{
		m_tonemapExposure = exposure > 0.0f ? exposure : 1.0f;
	}
	[[nodiscard]] float tonemapExposure() const noexcept override { return m_tonemapExposure; }

	void setTonemapGamma(float gamma) override
	{
		m_tonemapGamma = gamma > 0.0f ? gamma : 2.2f;
	}
	[[nodiscard]] float tonemapGamma() const noexcept override { return m_tonemapGamma; }

	void setShadowEnabled(bool enabled) noexcept override
	{
		m_shadowEnabled = enabled;
		m_toonFrameUniforms = false;
	}
	void setShadowDirection(const sgc::Vec3f& dir) noexcept override
	{
		m_toonFrameUniforms = false;
		m_directionalShadow.setLightDirection(dir);
	}
	/// @details 以後の drawMesh が影を落とすか。DX12 と同じく beginFrame で true に戻る。
	void setShadowCaster(bool enabled) noexcept override { m_shadowCasterEnabled = enabled; }

	/// @details kMaxLights を超えた分は捨てる。useMultiLight が false の間は
	///          蓄えるだけで、描画は単灯のまま (DX12 と同じ扱い)。
	void setLights(std::span<const Light> lights) override
	{
		m_toonFrameUniforms = false;
		m_lights.assign(lights.begin(),
		                lights.begin() + std::min<std::size_t>(lights.size(), kMaxLights));
		if (!m_lights.empty()) { setLight(m_lights.front()); }
	}
	void setUseMultiLight(bool useMulti) noexcept override
	{
		m_useMultiLight = useMulti;
		m_toonFrameUniforms = false;
	}
	[[nodiscard]] bool useMultiLight() const noexcept override { return m_useMultiLight; }

	void setShaderMode(ShaderMode3D mode) noexcept override
	{
		m_shaderMode = mode;
		m_toonFrameUniforms = false;
	}

	/// @details 射影後に w で割り、[-1,1] を [0,1] の画面 uv へ写す。カメラの
	///          後ろ (w <= 0) は画面に無いので false を返す。
	[[nodiscard]] bool worldToScreen(float wx, float wy, float wz,
	                                 float& u, float& v) const override
	{
		const detail::Mat4 vp = m_proj.mul(m_view);
		const float x = vp.m[0] * wx + vp.m[4] * wy + vp.m[8] * wz + vp.m[12];
		const float y = vp.m[1] * wx + vp.m[5] * wy + vp.m[9] * wz + vp.m[13];
		const float w = vp.m[3] * wx + vp.m[7] * wy + vp.m[11] * wz + vp.m[15];
		if (w <= 0.0f) { u = v = -1.0f; return false; }
		u = (x / w) * 0.5f + 0.5f;
		v = 1.0f - ((y / w) * 0.5f + 0.5f);
		return true;
	}

	void drawMesh(const Mesh& mesh, const sgc::Mat4f& worldTransform,
	              const Material& material) override
	{
		if (!m_initialized || !m_frameActive) { return; }
		const GpuMesh* gm = upload(mesh);
		if (gm == nullptr) { return; }

		// sgc::Mat4f は行優先。GLSL へは列優先で渡すので転置して積む。
		detail::Mat4 world;
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c) { world.m[c * 4 + r] = worldTransform.m[r][c]; }
		}
		if (m_shadowCasterEnabled) { m_shadowCommands.push_back({gm, world}); }
		// Material 側の alphaMode も同じ扱いにする。モデル経由だけ抜けて
		// drawMesh が抜けないと、同じ絵柄が呼び方で変わる。
		const float cutoff = (material.alphaMode == Material::AlphaMode::Mask)
			? material.alphaCutoff : 0.0f;
		submit(*gm, world, material.diffuse, albedoOf(material), material.doubleSided,
		       cutoff, material.alphaMode == Material::AlphaMode::Blend);
	}

	/// @brief glb / gltf を描く (rotY のみ)。
	void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
	               float scale) override
	{
		drawModelRot(path, position, sgc::Vec3f{0.0f, rotYDeg, 0.0f}, scale);
	}

	void drawModelRot(const char* path, const sgc::Vec3f& position,
	                  const sgc::Vec3f& rotDeg, float scale) override
	{
		if (!m_initialized || !m_frameActive || path == nullptr) { return; }
		const GpuModel* model = loadModel(path);
		if (model == nullptr) { return; }

		// DX12 の drawModelRotImpl と同じ Ry→Rx→Rz の順。
		const detail::Mat4 world =
			detail::translation(position)
				.mul(detail::rotation(sgc::Vec3f{0.0f, rotDeg.y, 0.0f}))
				.mul(detail::rotation(sgc::Vec3f{rotDeg.x, 0.0f, 0.0f}))
				.mul(detail::rotation(sgc::Vec3f{0.0f, 0.0f, rotDeg.z}))
				.mul(detail::scaling(sgc::Vec3f{scale, scale, scale}));

		for (const auto& prim : model->prims)
		{
			submit(prim.gpu, world.mul(prim.nodeWorld), prim.color, prim.tex,
			       prim.doubleSided, prim.alphaCutoff, prim.blend);
		}
	}

	void resetFrameActive() noexcept override { m_frameActive = false; }
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }
	[[nodiscard]] int drawCallCount() const noexcept override { return m_drawCalls; }

private:
	/// GPU へ載せた 1 メッシュぶん。Mesh のアドレスで引く。
	struct GpuMesh
	{
		GLuint vao = 0;
		GLuint vbo = 0;
		GLuint ibo = 0;
		GLsizei indexCount = 0;
	};

	/// glb 1 ファイルぶん。prim ごとに素材とノード姿勢 (レストポーズ) を焼いてある。
	/// スキンアニメは web では未対応。庭の家具 (柵・地面・草) は全部剛体で足りる。
	struct GpuPrim
	{
		GpuMesh gpu;
		detail::Mat4 nodeWorld;
		sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
		GLuint tex = 0;          // 0 = 白 1x1 を使う
		bool doubleSided = false;
		/// 0 なら抜かない。glTF の alphaMode=MASK のときだけ閾値が入る。
		/// これを落とすと、葉や柵の透明な画素がそのまま黒く描かれる。
		float alphaCutoff = 0.0f;
		/// alphaMode=BLEND。半透明として合成する。
		bool blend = false;
	};
	struct GpuModel
	{
		std::vector<GpuPrim> prims;
		std::vector<GLuint> ownedTextures;
	};

	// ── リソース ──────────────────────────────────────────────
	/// @details 一辺は DirectionalShadowConfig の既定と同じ 1024。比較サンプラを
	///          有効にして sampler2DShadow で読む (DX12 の SampleCmp と同じ形)。
	void createShadowTarget()
	{
		if (m_shadowProg == 0) { return; }
		glGenTextures(1, &m_shadowTex);
		glBindTexture(GL_TEXTURE_2D, m_shadowTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0,
		             GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

		glGenFramebuffers(1, &m_shadowFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowTex, 0);
		glDrawBuffers(0, nullptr);
		glReadBuffer(GL_NONE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			glDeleteFramebuffers(1, &m_shadowFbo);
			glDeleteTextures(1, &m_shadowTex);
			m_shadowFbo = 0;
			m_shadowTex = 0;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void createTargets()
	{
		glGenFramebuffers(1, &m_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

		// 色は HDR (FP16) で受ける。DX12 も FP16 の中間ターゲットへ描いてから
		// tonemap で LDR に焼く。RGBA8 で受けると 1.0 を超える光がその場で
		// 潰れ、あとから ACES を掛けても中間調が眠くなるだけになる。
		// EXT_color_buffer_float は WebGL2 の必須ではないので、無ければ RGBA8 に戻す。
		m_hdrColor = emscripten_webgl_enable_extension(
			emscripten_webgl_get_current_context(), "EXT_color_buffer_float") != 0;
		glGenTextures(1, &m_colorTex);
		glBindTexture(GL_TEXTURE_2D, m_colorTex);
		if (m_hdrColor)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
			             GL_HALF_FLOAT, nullptr);
		}
		else
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA,
			             GL_UNSIGNED_BYTE, nullptr);
		}
		setClampNearest();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

		glGenTextures(1, &m_normalTex);
		glBindTexture(GL_TEXTURE_2D, m_normalTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA,
		             GL_UNSIGNED_BYTE, nullptr);
		setClampNearest();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_normalTex, 0);

		// 深度は「読める」必要がある (アウトラインが差分を取る)。レンダーバッファでは
		// サンプルできないのでテクスチャで作る。
		glGenTextures(1, &m_depthTex);
		glBindTexture(GL_TEXTURE_2D, m_depthTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width, m_height, 0,
		             GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		setClampNearest();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			destroyTargets();
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	static void setClampNearest() noexcept
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	void destroyTargets() noexcept
	{
		if (m_colorTex) { glDeleteTextures(1, &m_colorTex); m_colorTex = 0; }
		if (m_normalTex) { glDeleteTextures(1, &m_normalTex); m_normalTex = 0; }
		if (m_depthTex) { glDeleteTextures(1, &m_depthTex); m_depthTex = 0; }
		if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
	}

	void destroy() noexcept
	{
		for (auto& [key, gm] : m_meshes)
		{
			glDeleteBuffers(1, &gm.vbo);
			glDeleteBuffers(1, &gm.ibo);
			glDeleteVertexArrays(1, &gm.vao);
		}
		m_meshes.clear();
		if (m_instVbo) { glDeleteBuffers(1, &m_instVbo); m_instVbo = 0; }
		for (auto& [tex, id] : m_albedoCache) { glDeleteTextures(1, &id); }
		if (m_shadowFbo) { glDeleteFramebuffers(1, &m_shadowFbo); m_shadowFbo = 0; }
		if (m_shadowTex) { glDeleteTextures(1, &m_shadowTex); m_shadowTex = 0; }
		m_albedoCache.clear();
		for (auto& [key, model] : m_models)
		{
			if (!model) { continue; }
			for (auto& prim : model->prims)
			{
				glDeleteBuffers(1, &prim.gpu.vbo);
				glDeleteBuffers(1, &prim.gpu.ibo);
				glDeleteVertexArrays(1, &prim.gpu.vao);
			}
			for (GLuint t : model->ownedTextures) { glDeleteTextures(1, &t); }
		}
		m_models.clear();
		destroyTargets();
		if (m_whiteTex) { glDeleteTextures(1, &m_whiteTex); m_whiteTex = 0; }
		if (m_emptyVao) { glDeleteVertexArrays(1, &m_emptyVao); m_emptyVao = 0; }
		if (m_toon) { glDeleteProgram(m_toon); m_toon = 0; }
		if (m_composite) { glDeleteProgram(m_composite); m_composite = 0; }
		if (m_sky) { glDeleteProgram(m_sky); m_sky = 0; }
	}

	/// メッシュを GPU へ載せる (2 回目以降は使い回す)。
	const GpuMesh* upload(const Mesh& mesh)
	{
		if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) { return nullptr; }
		const auto key = reinterpret_cast<std::uintptr_t>(&mesh);
		if (const auto it = m_meshes.find(key); it != m_meshes.end()) { return &it->second; }

		return &(m_meshes[key] = uploadRaw(mesh.vertices(), mesh.indices()));
	}

	// ── 描画 ──────────────────────────────────────────────────
	/// @details 即座には描かず、メッシュと状態が同じものをまとめて 1 本の
	///          インスタンス描画にする。レンガや柵の柱のように同じ形を並べる
	///          ゲームでは、これだけで描画本数が 2 桁変わる。
	///          半透明だけは重なりの順が絵に出るので、積まずにその場で描く。
	void submit(const GpuMesh& gm, const detail::Mat4& world, const sgc::Colorf& diffuse,
	            GLuint tex, bool doubleSided, float alphaCutoff = 0.0f, bool blend = false)
	{
		const BatchKey key{&gm, tex, doubleSided, alphaCutoff};
		if (blend)
		{
			flushBatches();
			drawInstanced(key, &world, &diffuse, 1, true);
			return;
		}
		auto& b = m_batches[key];
		b.worlds.push_back(world);
		b.colors.push_back(diffuse);
	}


	/// 多灯の配列を送る。useMultiLight が false のときは 0 を送り、シェーダ側は
	/// 単灯の経路へ落ちる。
	void uploadLightArray()
	{
		const int n = m_useMultiLight
			? static_cast<int>(std::min<std::size_t>(m_lights.size(), kMaxLights)) : 0;
		glUniform1i(loc(m_toon, "uLightCount"), n);
		for (int i = 0; i < n; ++i)
		{
			const Light& L = m_lights[static_cast<std::size_t>(i)];
			const auto& u = kLightUniforms[static_cast<std::size_t>(i)];
			glUniform1i(loc(m_toon, u.type),
			            L.type == LightType::Directional ? 0
			            : (L.type == LightType::Point ? 1 : 2));
			glUniform3f(loc(m_toon, u.pos), L.position.x, L.position.y, L.position.z);
			glUniform3f(loc(m_toon, u.dir), L.direction.x, L.direction.y, L.direction.z);
			glUniform3f(loc(m_toon, u.col),
			            L.color.r * L.intensity, L.color.g * L.intensity,
			            L.color.b * L.intensity);
			glUniform1f(loc(m_toon, u.range), L.range);
			glUniform1f(loc(m_toon, u.cone),
			            std::cos(L.spotAngle * 3.14159265f / 180.0f));
		}
	}

	/// 光の側から深度だけを焼く。DX12 の renderShadowPass と同じ構成で、
	/// 焦点は前フレームの caster の重心。caster が無い、または影が無効な
	/// フレームでも depth=1.0 のクリアだけは行う。クリアを飛ばすと前の内容が
	/// 残り、比較サンプルが場所によって影を返して画面が黒く沈む。
	void renderShadowPass()
	{
		if (m_shadowFbo == 0) { return; }

		glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
		glViewport(0, 0, kShadowSize, kShadowSize);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glClearDepthf(1.0f);
		glClear(GL_DEPTH_BUFFER_BIT);

		if (!m_shadowEnabled || m_shadowCommandsPrev.empty())
		{
			m_lightViewProj = detail::Mat4{};
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return;
		}

		sgc::Vec3f focus{0.0f, 0.0f, 0.0f};
		for (const auto& c : m_shadowCommandsPrev)
		{
			focus.x += c.world.m[12];
			focus.y += c.world.m[13];
			focus.z += c.world.m[14];
		}
		const float invN = 1.0f / static_cast<float>(m_shadowCommandsPrev.size());
		focus = {focus.x * invN, focus.y * invN, focus.z * invN};

		m_directionalShadow.setLightDirection(m_lightDir);
		const sgc::Mat4f view = m_directionalShadow.lightViewMatrix(focus);
		const sgc::Mat4f proj = m_directionalShadow.lightProjectionMatrix();
		m_lightViewProj = toColumnMajor(mul(proj, view));

		glUseProgram(m_shadowProg);
		setMat4(m_shadowProg, "uLightViewProj", m_lightViewProj);
		// 自分の面に自分の影が乗る (acne) のを、裏面だけ焼いて避ける。
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		for (const auto& c : m_shadowCommandsPrev)
		{
			setMat4(m_shadowProg, "uWorld", c.world);
			glBindVertexArray(c.mesh->vao);
			glDrawElements(GL_TRIANGLES, c.mesh->indexCount, GL_UNSIGNED_INT, nullptr);
		}
		glBindVertexArray(0);
		glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/// sgc::Mat4f (行優先) を GLSL 向けの列優先へ。
	[[nodiscard]] static detail::Mat4 toColumnMajor(const sgc::Mat4f& m) noexcept
	{
		detail::Mat4 out;
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c) { out.m[c * 4 + r] = m.m[r][c]; }
		}
		return out;
	}

	[[nodiscard]] static sgc::Mat4f mul(const sgc::Mat4f& a, const sgc::Mat4f& b) noexcept
	{
		sgc::Mat4f o{};
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float s = 0.0f;
				for (int k = 0; k < 4; ++k) { s += a.m[r][k] * b.m[k][c]; }
				o.m[r][c] = s;
			}
		}
		return o;
	}

	/// フレーム内で変わらない uniform を、必要なときだけ送る。
	void uploadFrameUniforms()
	{
		if (m_toonFrameUniforms) { return; }
		setMat4(m_toon, "uViewProj", m_proj.mul(m_view));
		setVec3(m_toon, "uLightDir", detail::normalize(m_lightDir));
		setVec3(m_toon, "uLightColor",
		        sgc::Vec3f{m_lightColor.r, m_lightColor.g, m_lightColor.b});
		setVec3(m_toon, "uAmbient", sgc::Vec3f{m_ambient.r, m_ambient.g, m_ambient.b});
		setVec3(m_toon, "uCameraPos", m_cameraPos);
		setVec3(m_toon, "uShadowTint",
		        sgc::Vec3f{m_shadowTint.r, m_shadowTint.g, m_shadowTint.b});
		setVec3(m_toon, "uFogColor", sgc::Vec3f{m_fogColor.r, m_fogColor.g, m_fogColor.b});
		glUniform3f(loc(m_toon, "uFogParams"), m_fogNear, m_fogFar, m_fog ? 1.0f : 0.0f);
		setMat4(m_toon, "uLightViewProj", m_lightViewProj);
		uploadLightArray();
		glUniform1i(loc(m_toon, "uShaderMode"), static_cast<int>(m_shaderMode));
		const bool shadowReady = m_shadowEnabled && m_shadowTex != 0
		                      && !m_shadowCommandsPrev.empty();
		glUniform1f(loc(m_toon, "uShadowOn"), shadowReady ? 1.0f : 0.0f);
		glUniform1f(loc(m_toon, "uShadowTexel"), 1.0f / static_cast<float>(kShadowSize));
		glUniform1i(loc(m_toon, "uAlbedo"), 0);
		glUniform1i(loc(m_toon, "uShadow"), 3);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, m_shadowTex);
		glActiveTexture(GL_TEXTURE0);
		m_toonFrameUniforms = true;
	}

	struct BatchKey
	{
		const GpuMesh* mesh;
		GLuint tex;
		bool doubleSided;
		float alphaCutoff;
		bool operator==(const BatchKey& o) const noexcept
		{
			return mesh == o.mesh && tex == o.tex && doubleSided == o.doubleSided
			    && alphaCutoff == o.alphaCutoff;
		}
	};
	struct BatchKeyHash
	{
		std::size_t operator()(const BatchKey& k) const noexcept
		{
			return std::hash<const void*>{}(k.mesh) ^ (std::hash<GLuint>{}(k.tex) << 1)
			     ^ (static_cast<std::size_t>(k.doubleSided) << 17)
			     ^ (std::hash<float>{}(k.alphaCutoff) << 3);
		}
	};
	struct Batch
	{
		std::vector<detail::Mat4> worlds;
		std::vector<sgc::Colorf> colors;
	};

	/// 溜めた不透明の描画をまとめて出す。endFrame と、半透明が来たときに呼ぶ。
	void flushBatches()
	{
		for (auto& [key, b] : m_batches)
		{
			if (b.worlds.empty()) { continue; }
			drawInstanced(key, b.worlds.data(), b.colors.data(), b.worlds.size(), false);
			b.worlds.clear();
			b.colors.clear();
		}
	}

	/// インスタンス属性を積んで 1 本で描く。1 個でも同じ経路を通す。
	void drawInstanced(const BatchKey& key, const detail::Mat4* worlds,
	                   const sgc::Colorf* colors, std::size_t count, bool blend)
	{
		if (count == 0) { return; }
		glUseProgram(m_toon);
		uploadFrameUniforms();
		glUniform1f(loc(m_toon, "uAlphaCutoff"), key.alphaCutoff);
		glBindTexture(GL_TEXTURE_2D, key.tex != 0 ? key.tex : m_whiteTex);

		glBindVertexArray(key.mesh->vao);
		uploadInstanceData(worlds, colors, count);

		if (key.doubleSided) { glDisable(GL_CULL_FACE); }
		if (blend)
		{
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		glDrawElementsInstanced(GL_TRIANGLES, key.mesh->indexCount, GL_UNSIGNED_INT, nullptr,
		                        static_cast<GLsizei>(count));
		if (blend) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
		if (key.doubleSided) { glEnable(GL_CULL_FACE); }
		++m_drawCalls;
	}

	/// インスタンスの行列と色を 1 本の VBO へ載せ、属性 4..8 に割り当てる。
	void uploadInstanceData(const detail::Mat4* worlds, const sgc::Colorf* colors,
	                        std::size_t count)
	{
		m_instScratch.clear();
		m_instScratch.reserve(count * 20);
		for (std::size_t i = 0; i < count; ++i)
		{
			for (int k = 0; k < 16; ++k) { m_instScratch.push_back(worlds[i].m[k]); }
			m_instScratch.push_back(colors[i].r);
			m_instScratch.push_back(colors[i].g);
			m_instScratch.push_back(colors[i].b);
			m_instScratch.push_back(colors[i].a);
		}
		glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(m_instScratch.size() * sizeof(float)),
		             m_instScratch.data(), GL_DYNAMIC_DRAW);
		constexpr GLsizei kStride = 20 * sizeof(float);
		for (int i = 0; i < 5; ++i)
		{
			const GLuint idx = static_cast<GLuint>(4 + i);
			glEnableVertexAttribArray(idx);
			glVertexAttribPointer(idx, 4, GL_FLOAT, GL_FALSE, kStride,
			                      reinterpret_cast<const void*>(
			                          static_cast<std::uintptr_t>(i) * 4 * sizeof(float)));
			glVertexAttribDivisor(idx, 1);
		}
	}

	/// 空のグラデーション。最遠面として描くので深度は書かない。
	void drawSky()
	{
		glUseProgram(m_sky);
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		glUniform3f(loc(m_sky, "uTop"), m_skyTop.r, m_skyTop.g, m_skyTop.b);
		glUniform3f(loc(m_sky, "uBottom"), m_skyBottom.r, m_skyBottom.g, m_skyBottom.b);
		// m_view は world→view の列優先 (detail::lookAt を参照)。回転部の行が
		// ワールドの基底。前方は右手系なので第 3 行の符号を反転する。
		const float* v = m_view.m;
		setVec3(m_sky, "uRight", sgc::Vec3f{v[0], v[4], v[8]});
		setVec3(m_sky, "uUp", sgc::Vec3f{v[1], v[5], v[9]});
		setVec3(m_sky, "uFwd", sgc::Vec3f{-v[2], -v[6], -v[10]});
		const float tanY = m_proj.m[5] != 0.0f ? 1.0f / m_proj.m[5] : 1.0f;
		const float tanX = m_proj.m[0] != 0.0f ? 1.0f / m_proj.m[0] : tanY;
		glUniform2f(loc(m_sky, "uTan"), tanX, tanY);
		glBindVertexArray(m_emptyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	/// オフスクリーンを既定のフレームバッファへ。ここでアウトラインを乗せる。
	void composite()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_width, m_height);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);

		glUseProgram(m_composite);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_colorTex);
		glUniform1i(loc(m_composite, "uColor"), 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_depthTex);
		glUniform1i(loc(m_composite, "uDepth"), 1);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_normalTex);
		glUniform1i(loc(m_composite, "uNormal"), 2);

		glUniform2f(loc(m_composite, "uTexel"), 1.0f / static_cast<float>(m_width),
		            1.0f / static_cast<float>(m_height));
		glUniform1f(loc(m_composite, "uOutlineWidth"), m_outlineWidth);
		glUniform1f(loc(m_composite, "uThreshold"), m_outlineThreshold);
		glUniform1f(loc(m_composite, "uOutlineOn"), m_outline ? 1.0f : 0.0f);
		glUniform2f(loc(m_composite, "uClip"), m_nearZ, m_farZ);
		glUniform1f(loc(m_composite, "uExposure"), m_tonemapExposure);
		// tonemap は常に掛ける。アルベドを sRGB で読んで線形空間で陰影を計算する以上、
		// 最後に sRGB へ戻す工程は必須。FP16 が使えるかは 1.0 を超える光を
		// 残せるかどうかの違いでしかない。
		glUniform1f(loc(m_composite, "uTonemapOn"), 1.0f);
		glUniform1f(loc(m_composite, "uInvGamma"), 1.0f / m_tonemapGamma);

		glBindVertexArray(m_emptyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glEnable(GL_DEPTH_TEST);
	}

	// ── glb 読み込み ──────────────────────────────────────────
	/// パスで引く。読めなかったファイルも nullptr を記憶して、毎フレームの再試行で
	/// ディスクを叩き続けないようにする。
	const GpuModel* loadModel(const std::string& path)
	{
		if (const auto it = m_models.find(path); it != m_models.end())
		{
			return it->second ? it->second.get() : nullptr;
		}

		auto scene = loadGltfSceneFromFile(path);
		if (!scene)
		{
			std::fprintf(stderr, "Renderer3D_WebGL: モデルが読めません: %s\n", path.c_str());
			m_models.emplace(path, nullptr);
			return nullptr;
		}

		auto model = std::make_unique<GpuModel>();

		// ノードのレストポーズをワールドへ畳む (親→子の順は保証されないので都度遡る)。
		std::vector<detail::Mat4> nodeWorld(scene->nodes.size());
		for (std::size_t i = 0; i < scene->nodes.size(); ++i)
		{
			detail::Mat4 w = detail::Mat4::identity();
			for (int n = static_cast<int>(i); n >= 0; n = scene->nodes[static_cast<std::size_t>(n)].parent)
			{
				const auto& nd = scene->nodes[static_cast<std::size_t>(n)];
				w = detail::trs(nd.translation, nd.rotation, nd.scale).mul(w);
			}
			nodeWorld[i] = w;
		}

		for (std::size_t mi = 0; mi < scene->meshes.size(); ++mi)
		{
			// この mesh を指すノードの姿勢。見つからなければ単位行列で置く。
			detail::Mat4 world = detail::Mat4::identity();
			for (std::size_t ni = 0; ni < scene->nodes.size(); ++ni)
			{
				if (scene->nodes[ni].mesh == static_cast<int>(mi)) { world = nodeWorld[ni]; break; }
			}

			for (const auto& prim : scene->meshes[mi].primitives)
			{
				if (prim.vertices.empty() || prim.indices.empty()) { continue; }
				GpuPrim gp;
				gp.gpu = uploadRaw(prim.vertices, prim.indices);
				gp.nodeWorld = world;
				if (prim.materialIndex >= 0 &&
				    static_cast<std::size_t>(prim.materialIndex) < scene->materials.size())
				{
					const auto& mat = scene->materials[static_cast<std::size_t>(prim.materialIndex)];
					gp.color = mat.baseColor;
					gp.doubleSided = mat.doubleSided;
					if (mat.alphaMode == GltfAlphaMode::Mask) { gp.alphaCutoff = mat.alphaCutoff; }
					gp.blend = (mat.alphaMode == GltfAlphaMode::Blend);
					if (mat.baseColorTexture.valid())
					{
						gp.tex = uploadTexture(mat.baseColorTexture, mat.nearestFilter);
						model->ownedTextures.push_back(gp.tex);
					}
				}
				model->prims.push_back(gp);
			}
		}

		const GpuModel* out = model.get();
		m_models.emplace(path, std::move(model));
		return out;
	}

	[[nodiscard]] GpuMesh uploadRaw(const std::vector<Vertex3D>& vertices,
	                                const std::vector<std::uint32_t>& indices)
	{
		GpuMesh gm;
		glGenVertexArrays(1, &gm.vao);
		glBindVertexArray(gm.vao);
		glGenBuffers(1, &gm.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex3D)),
		             vertices.data(), GL_STATIC_DRAW);
		glGenBuffers(1, &gm.ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
		             indices.data(), GL_STATIC_DRAW);
		bindVertexLayout();
		glBindVertexArray(0);
		gm.indexCount = static_cast<GLsizei>(indices.size());
		return gm;
	}

	static void bindVertexLayout() noexcept
	{
		constexpr GLsizei kStride = sizeof(Vertex3D);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride,
		                      reinterpret_cast<const void*>(offsetof(Vertex3D, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride,
		                      reinterpret_cast<const void*>(offsetof(Vertex3D, normal)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
		                      reinterpret_cast<const void*>(offsetof(Vertex3D, texCoord)));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, kStride,
		                      reinterpret_cast<const void*>(offsetof(Vertex3D, color)));
	}

	/// @brief Material.albedoTexture を GL テクスチャへ解決する (無ければ 0)。
	/// @details 同じ `Texture*` は 1 度しか上げない。DX12 側の getOrUploadAlbedoSrv と
	///          同じ方針で、`setTexture` の global state とは独立させる。
	///          キーはポインタなので、Texture を破棄して同じ番地に別の Texture を
	///          置くと古い GL テクスチャを引く。engine の使い方では sprite は実行中
	///          生き続けるので、その前提を崩す用途が出たら破棄側で clearAlbedoCache する。
	[[nodiscard]] GLuint albedoOf(const Material& material)
	{
		const Texture* tex = material.albedoTexture;
		if (tex == nullptr || !tex->valid()) { return 0; }
		const auto it = m_albedoCache.find(tex);
		if (it != m_albedoCache.end()) { return it->second; }

		GLuint t = 0;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		// アルベドは sRGB で読む。DX12 も SRV を R8G8B8A8_UNORM_SRGB にして、
		// 陰影を線形空間で計算してから tonemap で sRGB へ戻している。ここを
		// GL_RGBA8 にすると線形化されず、最後のガンマが二重に乗って眠くなる。
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, tex->width(), tex->height(), 0, GL_RGBA,
		             GL_UNSIGNED_BYTE, tex->pixels().data());
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		m_albedoCache.emplace(tex, t);
		return t;
	}

	[[nodiscard]] static GLuint uploadTexture(const CpuTexture& cpu, bool nearest)
	{
		GLuint t = 0;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, cpu.width, cpu.height, 0, GL_RGBA,
		             GL_UNSIGNED_BYTE, cpu.rgba.data());
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		                nearest ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		return t;
	}

	// ── 小物 ──────────────────────────────────────────────────

	/// 4x4 の逆行列。空のシェーダが画素から視線方向へ戻すのに使う。フレームに
	/// 1 回しか呼ばないので、素直な余因子展開でよい。特異なら単位行列を返す。
	[[nodiscard]] static detail::Mat4 inverse4(const detail::Mat4& in) noexcept
	{
		const float* m = in.m;
		float inv[16];
		inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
		         + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
		inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
		         - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
		inv[8]  =  m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
		         + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
		inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
		         - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
		inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
		         - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
		inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
		         + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
		inv[9]  = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
		         - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
		inv[13] =  m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
		         + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
		inv[2]  =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
		         + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
		inv[6]  = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
		         - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
		inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
		         + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
		inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
		         - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
		inv[3]  = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
		         - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
		inv[7]  =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
		         + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
		inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
		         - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
		inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
		         + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

		float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
		detail::Mat4 out;
		if (det == 0.0f) { return out; }
		det = 1.0f / det;
		for (int i = 0; i < 16; ++i) { out.m[i] = inv[i] * det; }
		return out;
	}

	[[nodiscard]] static sgc::Colorf faceCenterColor(const Cubemap& cubemap, int faceIndex)
	{
		const Texture& face = cubemap.face(faceIndex);
		if (!face.valid()) { return sgc::Colorf{0.5f, 0.7f, 1.0f, 1.0f}; }
		const int cx = face.width() / 2;
		const int cy = face.height() / 2;
		const std::size_t i = (static_cast<std::size_t>(cy) * static_cast<std::size_t>(face.width())
		                       + static_cast<std::size_t>(cx)) * 4u;
		const auto& px = face.pixels();
		if (i + 3 >= px.size()) { return sgc::Colorf{0.5f, 0.7f, 1.0f, 1.0f}; }
		return sgc::Colorf{static_cast<float>(px[i]) / 255.0f,
		                   static_cast<float>(px[i + 1]) / 255.0f,
		                   static_cast<float>(px[i + 2]) / 255.0f, 1.0f};
	}

	/// @details glGetUniformLocation は WebGL では JS 境界を跨ぐ同期問い合わせで、
	///          描画のたびに引くと数を数えられるほど効く (1 draw あたり 20 回超 ×
	///          数十 draw = 毎秒数万回)。名前はすべて文字列リテラルなので、
	///          ポインタそのものを鍵にすれば文字列比較すら要らない。
	[[nodiscard]] GLint loc(GLuint program, const char* name) const noexcept
	{
		const std::uint64_t key = (static_cast<std::uint64_t>(program) << 48)
			^ reinterpret_cast<std::uintptr_t>(name);
		const auto it = m_uniformCache.find(key);
		if (it != m_uniformCache.end()) { return it->second; }
		const GLint v = glGetUniformLocation(program, name);
		m_uniformCache.emplace(key, v);
		return v;
	}

	void setMat4(GLuint program, const char* name, const detail::Mat4& m) const noexcept
	{
		glUniformMatrix4fv(loc(program, name), 1, GL_FALSE, m.m);
	}

	void setVec3(GLuint program, const char* name, const sgc::Vec3f& v) const noexcept
	{
		glUniform3f(loc(program, name), v.x, v.y, v.z);
	}

	[[nodiscard]] static GLuint compile(GLenum type, const char* src) noexcept
	{
		const GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = GL_FALSE;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (ok == GL_FALSE)
		{
			char log[1024]{};
			glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
			std::fprintf(stderr, "Renderer3D_WebGL: シェーダのコンパイルに失敗: %s\n", log);
			glDeleteShader(s);
			return 0;
		}
		return s;
	}

	[[nodiscard]] static GLuint linkProgram(const char* vs, const char* fs) noexcept
	{
		const GLuint v = compile(GL_VERTEX_SHADER, vs);
		const GLuint f = compile(GL_FRAGMENT_SHADER, fs);
		if (v == 0 || f == 0) { return 0; }
		const GLuint p = glCreateProgram();
		glAttachShader(p, v);
		glAttachShader(p, f);
		glLinkProgram(p);
		GLint ok = GL_FALSE;
		glGetProgramiv(p, GL_LINK_STATUS, &ok);
		glDeleteShader(v);
		glDeleteShader(f);
		if (ok == GL_FALSE)
		{
			char log[1024]{};
			glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
			std::fprintf(stderr, "Renderer3D_WebGL: シェーダのリンクに失敗: %s\n", log);
			glDeleteProgram(p);
			return 0;
		}
		return p;
	}

	// ── シェーダ (GLSL ES 3.00) ────────────────────────────────
	static constexpr const char* kToonVS = R"glsl(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;

// ワールド行列と色はインスタンス属性。同じ形を並べる描画 (レンガ、柵の柱) を
// 1 本にまとめるため。1 個だけの描画も同じ経路を通す。
layout(location = 4) in vec4 aWorld0;
layout(location = 5) in vec4 aWorld1;
layout(location = 6) in vec4 aWorld2;
layout(location = 7) in vec4 aWorld3;
layout(location = 8) in vec4 aInstColor;

uniform mat4 uViewProj;
uniform mat4 uLightViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColor;
out vec4 vInst;
out vec4 vLightPos;

void main()
{
    mat4 m = mat4(aWorld0, aWorld1, aWorld2, aWorld3);
    vec4 world = m * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    // 一様スケールしか使わないので、法線は回転部だけで足りる。
    vNormal = mat3(m) * aNormal;
    vUv = aUv;
    vColor = aColor;
    vInst = aInstColor;
    vLightPos = uLightViewProj * world;
    gl_Position = uViewProj * world;
}
)glsl";

	/// 影マップ用。位置だけ変換して深度を書く。色は出さない。
	static constexpr const char* kShadowVS = R"glsl(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 4) in vec4 aWorld0;
layout(location = 5) in vec4 aWorld1;
layout(location = 6) in vec4 aWorld2;
layout(location = 7) in vec4 aWorld3;
uniform mat4 uLightViewProj;
void main()
{
    mat4 m = mat4(aWorld0, aWorld1, aWorld2, aWorld3);
    vec4 clip = uLightViewProj * m * vec4(aPos, 1.0);
    // 行列は DX 規約 (z は [0,1])。GL のクリップ空間は [-1,1] なので写し直す。
    clip.z = clip.z * 2.0 - clip.w;
    gl_Position = clip;
}
)glsl";

	static constexpr const char* kShadowFS = R"glsl(#version 300 es
precision highp float;
void main() {}
)glsl";

	/// DX12 の DX12_TOON_PS_3D と同じ式。影も 3x3 PCF で同じ形にしてある。
	static constexpr const char* kToonFS = R"glsl(#version 300 es
precision highp float;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec4 vColor;
in vec4 vInst;
in vec4 vLightPos;

// GLSL ES はサンプラの精度に既定を持たない型がある。明示しないとコンパイルが通らない。
uniform highp sampler2DShadow uShadow;
uniform float uShadowOn;
uniform float uShadowTexel;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;

// 多灯。uLightCount が 0 のときは単灯 (uLightDir/uLightColor) で描く。
// 上限は IRenderer3D::kMaxLights と同じ 8。
const int kMaxLights = 8;
uniform int uLightCount;
uniform int uLightType[kMaxLights];      // 0=平行 1=点 2=スポット
uniform vec3 uLightPos[kMaxLights];
uniform vec3 uLightDirs[kMaxLights];
uniform vec3 uLightCols[kMaxLights];
uniform float uLightRange[kMaxLights];
uniform float uLightCos[kMaxLights];     // スポットの半角の cos
uniform int uShaderMode;                 // ShaderMode3D と同じ並び
uniform vec3 uCameraPos;
uniform vec3 uShadowTint;
uniform vec3 uFogColor;
uniform vec3 uFogParams;   // x=開始 y=終了 z=有効
uniform sampler2D uAlbedo;
uniform float uAlphaCutoff;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

// DX12 の samplePCF と同じ。光の錐台の外は影なしで返す。深度も見るのは、
// 遠クリップ面の外には影マップに何も無いため。
float samplePCF(vec3 ndc)
{
    if (uShadowOn < 0.5) { return 1.0; }
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
        || ndc.z < 0.0 || ndc.z > 1.0) { return 1.0; }

    float ref = ndc.z - 0.001;
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            shadow += texture(uShadow,
                vec3(uv + vec2(float(x), float(y)) * uShadowTexel, ref));
        }
    }
    return shadow / 9.0;
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec4 tex = texture(uAlbedo, vUv);
    // アルファ抜き。DX12 の sampleAlbedo の clip() と同じ。捨てないと透明な
    // 画素が albedo=0 のまま描かれて、葉や柵の裏が黒く塗り潰される。
    if (uAlphaCutoff > 0.0 && tex.a < uAlphaCutoff) { discard; }
    vec3 albedo = vInst.rgb * vColor.rgb * tex.rgb;

    vec3 lsNdc = vLightPos.xyz / max(vLightPos.w, 1e-6);
    float castShadow = samplePCF(lsNdc);

    // 陰影の帯。Toon は 2 値に近い階調、Phong は素の Lambert、Unlit は素通し。
    // ここは DX12 の ShaderMode PS 群と同じ分岐にしてある。
    vec3 color;
    if (uShaderMode == 2)          // Unlit
    {
        color = albedo;
    }
    else if (uLightCount > 0)
    {
        vec3 lit = vec3(0.0);
        for (int i = 0; i < kMaxLights; ++i)
        {
            if (i >= uLightCount) { break; }
            vec3 Li = (uLightType[i] == 0)
                ? normalize(-uLightDirs[i])
                : normalize(uLightPos[i] - vWorldPos);
            float atten = 1.0;
            if (uLightType[i] != 0)
            {
                float d = length(uLightPos[i] - vWorldPos);
                atten = clamp(1.0 - d / max(uLightRange[i], 0.001), 0.0, 1.0);
                if (uLightType[i] == 2)
                {
                    float c = dot(normalize(-uLightDirs[i]), Li);
                    atten *= step(uLightCos[i], c);
                }
            }
            float lam = clamp(dot(N, Li), 0.0, 1.0) * atten;
            if (i == 0) { lam *= castShadow; }
            float b = (uShaderMode == 1) ? smoothstep(0.44, 0.56, lam) : lam;
            lit += mix(uShadowTint, vec3(1.0), b) * uLightCols[i];
        }
        color = albedo * lit + uAmbient * albedo * 0.30;
    }
    else
    {
        float lambert = clamp(dot(N, L), 0.0, 1.0) * castShadow;
        float band = (uShaderMode == 1 || uShaderMode == 0)
            ? smoothstep(0.44, 0.56, lambert) : lambert;
        vec3 tone = mix(uShadowTint, vec3(1.0), band);
        color = albedo * tone * uLightColor + uAmbient * albedo * 0.30;
    }

    if (uFogParams.z > 0.5)
    {
        float dist = length(uCameraPos - vWorldPos);
        float f = clamp((dist - uFogParams.x) / max(uFogParams.y - uFogParams.x, 0.001), 0.0, 1.0);
        color = mix(color, uFogColor, f);
    }

    outColor = vec4(color, vInst.a * vColor.a * tex.a);
    outNormal = vec4(N * 0.5 + 0.5, clamp(dot(N, V), 0.0, 1.0));
}
)glsl";

	/// 頂点バッファ無しの全画面三角形。合成と空で共有する。
	static constexpr const char* kFullscreenVS = R"glsl(#version 300 es
out vec2 vUv;
void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

	static constexpr const char* kSkyFS = R"glsl(#version 300 es
precision highp float;
in vec2 vUv;
uniform vec3 uTop;
uniform vec3 uBottom;
// カメラの基底と画角。cubemap と同じく視線方向で引くために使う。
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFwd;
uniform vec2 uTan;      // x = tan(fovY/2) * aspect, y = tan(fovY/2)
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
// 空の色は cubemap の面の中心から取っている。DX12 はその cubemap を
// R8G8B8A8_UNORM_SRGB で読むので、サンプル時に線形化されている。ここは
// 生の値が uniform で来るため、同じ場所に揃えるために自分で線形化する。
vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
    // 画面の下から上へ。
    //
    // DX12 は空を cubemap として視線方向でサンプルする。同じ形に揃えようとして
    // カメラの基底から視線を組んだが、実測で Windows 版との差が縮まらなかった
    // (差の総和 498 → 636)。原因を掴めていないので、元の画面座標の勾配を残す。
    // 直すときは win_caps の実測 (空は上端 191,203,215 から地平 199,206,212 と
    // ほぼ平坦) を目標にすること。
    // DX12 は空を cubemap として視線方向でサンプルする。画面の上下で線形に混ぜると
    // 遷移が画面いっぱいに伸び、実測で上空だけずれた (地平は一致、上空 +15)。
    // 面の選び方と面内の座標は Cubemap::verticalGradient の作りに合わせる。
    // vUv.y は下向きに増える (1 が画面の下端) ので、ndc へは反転して渡す。
    // kFullscreenVS が gl_Position = vUv*2-1 を書くので、vUv.y = 1 が画面の上端。
    // ここを反転すると空が上下逆になり、朝の空が地平の暖色で埋まる (実際にやった)。
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 dir = normalize(uFwd + uRight * ndc.x * uTan.x + uUp * ndc.y * uTan.y);

    float maxXZ = max(abs(dir.x), abs(dir.z));
    float t = (abs(dir.y) > maxXZ)
        ? (dir.y > 0.0 ? 0.0 : 1.0)
        : clamp(0.5 * (1.0 - dir.y / max(maxXZ, 1e-6)), 0.0, 1.0);

    outColor = vec4(srgbToLinear(mix(uTop, uBottom, t)), 1.0);
    // 空には輪郭を出したくない。NdotV=0 にしておくと合成のマスクで落ちる。
    outNormal = vec4(0.5, 0.5, 1.0, 0.0);
}
)glsl";

	/// 色 + 深度アウトラインの合成。式は DX12 の OUTLINE_POST_PS と同じ:
	/// 二次差分でエッジを取り、NdotV で凹面を抑え、被覆率をアルファにする。
	static constexpr const char* kCompositeFS = R"glsl(#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform vec2 uTexel;
uniform vec2 uClip;          // x=near y=far
uniform float uOutlineWidth;
uniform float uThreshold;
uniform float uOutlineOn;
uniform float uExposure;
uniform float uInvGamma;
uniform float uTonemapOn;
out vec4 outColor;

// ACES filmic (Narkowicz 近似)。DX12 の DX12Tonemap.hpp と同じ係数。
vec3 acesFilmic(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 tonemap(vec3 hdr)
{
    if (uTonemapOn < 0.5) { return hdr; }
    return pow(max(acesFilmic(hdr * uExposure), vec3(0.0)), vec3(uInvGamma));
}

// GL の深度は [-1,1] を [0,1] に写したもの。視空間の距離へ戻す。
float linearize(float d)
{
    float z = d * 2.0 - 1.0;
    return (2.0 * uClip.x * uClip.y) / (uClip.y + uClip.x - z * (uClip.y - uClip.x));
}

float depthAt(vec2 uv) { return linearize(texture(uDepth, uv).r); }

void main()
{
    vec3 scene = texture(uColor, vUv).rgb;

    if (uOutlineOn < 0.5)
    {
        outColor = vec4(tonemap(scene), 1.0);
        return;
    }

    vec2 o = uTexel * max(uOutlineWidth, 1.0);
    float dC = depthAt(vUv);
    float dL = depthAt(vUv - vec2(o.x, 0.0));
    float dR = depthAt(vUv + vec2(o.x, 0.0));
    float dU = depthAt(vUv - vec2(0.0, o.y));
    float dD = depthAt(vUv + vec2(0.0, o.y));

    // 一次差分は傾いた床の勾配まで拾う。二次差分は平面上で打ち消し合うので、
    // 本当の段差だけが残る (DX12 側で同じ理由でこの式にした)。
    float edge = max(abs(dL + dR - 2.0 * dC), abs(dU + dD - 2.0 * dC));

    float a = smoothstep(uThreshold, uThreshold * 1.8, edge);
    a *= smoothstep(0.10, 0.25, texture(uNormal, vUv).a);

    outColor = vec4(mix(tonemap(scene), vec3(0.1, 0.08, 0.06), a), 1.0);
}
)glsl";

	// ── 状態 ──────────────────────────────────────────────────
	bool m_initialized = false;
	bool m_frameActive = false;
	int  m_drawCalls = 0;
	int  m_width = 1;
	int  m_height = 1;

	GLuint m_toon = 0, m_composite = 0, m_sky = 0;
	/// Material.albedoTexture ごとの GL テクスチャ。キーは呼び出し側が持つ Texture*。
	std::unordered_map<const Texture*, GLuint> m_albedoCache;
	GLuint m_fbo = 0, m_colorTex = 0, m_normalTex = 0, m_depthTex = 0;
	GLuint m_whiteTex = 0, m_emptyVao = 0;

	detail::Mat4 m_view{}, m_proj{};
	sgc::Vec3f m_cameraPos{0.0f, 0.0f, 0.0f};
	float m_nearZ = 0.1f, m_farZ = 500.0f;

	/// 色ターゲットが FP16 か。false でも tonemap は掛ける。違いは 1.0 を
	/// 超える光を保てるかどうかだけ。
	bool m_hdrColor = false;
	/// 多灯の uniform 名。実行時に組み立てるとポインタが毎回変わり、loc の
	/// キャッシュが効かなくなる。リテラルで固定する。
	struct LightUniformNames { const char *type, *pos, *dir, *col, *range, *cone; };
	static constexpr LightUniformNames kLightUniforms[8] = {
		{"uLightType[0]", "uLightPos[0]", "uLightDirs[0]", "uLightCols[0]", "uLightRange[0]", "uLightCos[0]"},
		{"uLightType[1]", "uLightPos[1]", "uLightDirs[1]", "uLightCols[1]", "uLightRange[1]", "uLightCos[1]"},
		{"uLightType[2]", "uLightPos[2]", "uLightDirs[2]", "uLightCols[2]", "uLightRange[2]", "uLightCos[2]"},
		{"uLightType[3]", "uLightPos[3]", "uLightDirs[3]", "uLightCols[3]", "uLightRange[3]", "uLightCos[3]"},
		{"uLightType[4]", "uLightPos[4]", "uLightDirs[4]", "uLightCols[4]", "uLightRange[4]", "uLightCos[4]"},
		{"uLightType[5]", "uLightPos[5]", "uLightDirs[5]", "uLightCols[5]", "uLightRange[5]", "uLightCos[5]"},
		{"uLightType[6]", "uLightPos[6]", "uLightDirs[6]", "uLightCols[6]", "uLightRange[6]", "uLightCos[6]"},
		{"uLightType[7]", "uLightPos[7]", "uLightDirs[7]", "uLightCols[7]", "uLightRange[7]", "uLightCos[7]"},
	};
	mutable std::unordered_map<std::uint64_t, GLint> m_uniformCache;
	GLuint m_instVbo = 0;
	std::vector<float> m_instScratch;
	std::unordered_map<BatchKey, Batch, BatchKeyHash> m_batches;

	/// フレーム内で変わらない uniform を送り終えたか。状態を変える API が
	/// 呼ばれたら false へ戻して送り直す。
	bool m_toonFrameUniforms = false;
	bool m_skyDrawn = false;

	std::vector<Light> m_lights;
	bool m_useMultiLight = false;
	ShaderMode3D m_shaderMode = ShaderMode3D::Toon;

	static constexpr int kShadowSize = 1024;
	GLuint m_shadowProg = 0, m_shadowFbo = 0, m_shadowTex = 0;
	bool m_shadowEnabled = false;
	bool m_shadowCasterEnabled = true;
	DirectionalShadow m_directionalShadow;
	/// 影を落とす描画。DX12 と同じく前フレーム分を影パスに使う。当フレームの
	/// 一覧は描き終わるまで揃わないので、影だけ 1 フレーム遅れる。
	struct ShadowCaster { const GpuMesh* mesh; detail::Mat4 world; };
	std::vector<ShadowCaster> m_shadowCommands, m_shadowCommandsPrev;
	detail::Mat4 m_lightViewProj{};

	float m_tonemapExposure = 1.0f;
	float m_tonemapGamma = 2.2f;

	sgc::Vec3f  m_lightDir{-0.4f, -0.8f, -0.3f};
	sgc::Colorf m_lightColor{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf m_ambient{0.5f, 0.5f, 0.5f, 1.0f};
	sgc::Colorf m_shadowTint{0.55f, 0.60f, 0.80f, 1.0f};

	bool        m_skyEnabled = false;
	sgc::Colorf m_skyTop{0.35f, 0.55f, 0.95f, 1.0f};
	sgc::Colorf m_skyBottom{0.95f, 0.88f, 0.70f, 1.0f};

	bool        m_outline = false;
	OutlineMode m_outlineMode = OutlineMode::DepthSobel;
	float       m_outlineWidth = 3.0f;
	float       m_outlineThreshold = 0.045f;

	bool        m_fog = false;
	sgc::Colorf m_fogColor{0.7f, 0.8f, 0.9f, 1.0f};
	float       m_fogNear = 20.0f;
	float       m_fogFar = 80.0f;

	std::unordered_map<std::uintptr_t, GpuMesh> m_meshes;
	std::unordered_map<std::string, std::unique_ptr<GpuModel>> m_models;
};

}  // namespace mitiru::render

#endif  // __EMSCRIPTEN__
