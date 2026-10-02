#pragma once

/// @file IRenderer3D.hpp
/// @brief 3D レンダラーの共通インターフェース宣言

#include <span>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/RendererEnums3D.hpp>
#include <mitiru/render/ISceneFx.hpp>
#include <mitiru/render/experimental/IExperimentalRenderer3D.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

// ShaderMode3D、OutlineMode、OUTLINE_MODE_COUNT は RendererEnums3D.hpp で定義する

/// @brief DX11 / DX12 レンダラーの共通インターフェース
/// @details フレーム開始、カメラとライトの設定、メッシュ描画、フレーム終了の順に呼ぶ。
class IRenderer3D
{
public:
	virtual ~IRenderer3D() = default;

	[[nodiscard]] virtual bool isInitialized() const noexcept = 0;

	/// @brief 内部 RT を backbuffer の物理 px サイズに合わせる
	/// @details swapchain のリサイズ後、次の `beginFrame()` より前に呼ぶ。
	virtual void resize(int /*width*/, int /*height*/) {}

	virtual void beginFrame(const sgc::Colorf& clearColor = {0.2f, 0.2f, 0.3f, 1.0f}) = 0;

	virtual void endFrame() = 0;

	virtual void setCamera(const Camera3D& camera) = 0;

	virtual void setLight(const Light& light) = 0;

	/// @brief 最大 `kMaxLights` 個のライトを設定する
	/// @details 空配列は環境光だけを使う。単一ライトのバックエンドは先頭だけを使う。上限を超えたときの扱いはバックエンドごとに異なる。
	virtual void setLights(std::span<const Light> lights)
	{
		if (!lights.empty())
		{
			setLight(lights.front());
		}
	}

	/// @brief GPU 側の cbuffer に合わせたライト数の上限
	static constexpr int kMaxLights = 8;

	// ── マルチライト経路 ──

	/// @brief マルチライト経路を切り替える
	/// @details 有効時は Phong 系で `setLights()` の全ライトを使う。対応するバックエンドは DX11 と DX12。
	virtual void setUseMultiLight(bool /*useMulti*/) {}

	[[nodiscard]] virtual bool useMultiLight() const noexcept { return false; }

	virtual void drawMesh(const Mesh& mesh,
	                      const sgc::Mat4f& worldTransform,
	                      const Material& material = {}) = 0;

	/// @brief Engine が毎フレームの最初に呼ぶ
	virtual void resetFrameActive() noexcept = 0;

	/// @brief このフレームで 3D 描画が行われたかを返す
	[[nodiscard]] virtual bool isFrameActive() const noexcept = 0;

	[[nodiscard]] virtual int drawCallCount() const noexcept = 0;

	// ── 視錐台カリング ──

	/// @brief 視錐台カリングを切り替える
	/// @details DX11 と DX12 では既定で有効。無効化はカリング結果の比較に使う。
	virtual void setFrustumCullingEnabled(bool /*enabled*/) noexcept {}

	[[nodiscard]] virtual bool isFrustumCullingEnabled() const noexcept { return false; }

	[[nodiscard]] virtual int culledCount() const noexcept { return 0; }

	// ── オクルージョンカリング ──
	// `mitiru::render::OcclusionCuller` の CPU Hi-Z で、前フレームの深度から完全に隠れたメッシュを除く。
	// 視錐台カリングと両方が有効なときは、視錐台、オクルージョンの順に判定する。

	/// @brief オクルージョンカリングを切り替える
	/// @details 前フレームの深度を使うため、効果は 1 フレーム遅れる。
	virtual void setOcclusionCullingEnabled(bool /*enabled*/) noexcept {}

	[[nodiscard]] virtual bool isOcclusionCullingEnabled() const noexcept { return false; }

	[[nodiscard]] virtual int occludedCount() const noexcept { return 0; }

	/// @brief 同じメッシュを複数のワールド行列で描画する
	/// @details 既定では `drawMesh()` を繰り返す。GPU instancing 対応バックエンドは 1 ドローコールにまとめる。
	virtual void drawMeshInstanced(const Mesh& mesh,
	                               std::span<const sgc::Mat4f> worlds,
	                               const Material* material)
	{
		const Material& mat = material ? *material : Material{};
		for (const auto& world : worlds)
		{
			drawMesh(mesh, world, mat);
		}
	}

	// ── 2D オーバーレイ ──

	/// @brief 3D の確定後に 2D を重ねられるかを返す
	/// @details 対応時は `draw()` 中の 2D を蓄積し、`Screen::present3DOverlay()` で 3D の上に描く。
	[[nodiscard]] virtual bool hasOverlaySupport() const noexcept { return false; }

	/// @brief DX12 では `Dx12Device*`、それ以外では nullptr を返す
	[[nodiscard]] virtual void* nativeDevice() const noexcept { return nullptr; }

	/// @brief DX12 では `Dx12SwapChain*`、それ以外では nullptr を返す
	[[nodiscard]] virtual void* nativeSwapChain() const noexcept { return nullptr; }

	/// @brief コマンドリストを閉じて実行する
	/// @details DX12 では `endFrame()` の後に呼ぶ。
	virtual void finalizeFrame() {}

	// ── queryInterface 相当 ──

	/// @brief 絵の設定用インターフェースを返す
	[[nodiscard]] virtual ISceneFx* sceneFx() noexcept { return nullptr; }
	[[nodiscard]] virtual const ISceneFx* sceneFx() const noexcept { return nullptr; }

	/// @brief 実験的機能のインターフェースを返す
	[[nodiscard]] virtual IExperimentalRenderer3D* experimental() noexcept { return nullptr; }
	[[nodiscard]] virtual const IExperimentalRenderer3D* experimental() const noexcept { return nullptr; }

	// ── `sceneFx()` と `experimental()` への後方互換転送 ──
	// 具象クラスで転送関数と同名の override が並ぶときは、`using ISceneFx::...` などで override を優先する。
	// virtual にしているのは、ISceneFx/IExperimentalRenderer3D を実装せずこの旧 API を直接
	// オーバーライドする外部派生クラスでも、IRenderer3D& 経由の呼び出しが正しくディスパッチされるため。

	virtual void setSkybox(const Cubemap& cubemap) { if (auto* fx = sceneFx()) { fx->setSkybox(cubemap); } }
	virtual void setSkyboxEnabled(bool enabled) { if (auto* fx = sceneFx()) { fx->setSkyboxEnabled(enabled); } }
	[[nodiscard]] virtual bool isSkyboxEnabled() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr && fx->isSkyboxEnabled();
	}

	virtual void setAmbientColor(const sgc::Colorf& color) { if (auto* fx = sceneFx()) { fx->setAmbientColor(color); } }
	[[nodiscard]] virtual sgc::Colorf ambientColor() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr ? fx->ambientColor() : sgc::Colorf{0.15f, 0.15f, 0.15f, 1.0f};
	}

	virtual void setShaderMode(ShaderMode3D mode) { if (auto* fx = sceneFx()) { fx->setShaderMode(mode); } }

	virtual void setShadowEnabled(bool enabled) { if (auto* fx = sceneFx()) { fx->setShadowEnabled(enabled); } }
	virtual void setShadowDirection(const sgc::Vec3f& dir) { if (auto* fx = sceneFx()) { fx->setShadowDirection(dir); } }
	virtual void setShadowCaster(bool enabled) { if (auto* fx = sceneFx()) { fx->setShadowCaster(enabled); } }

	virtual void setCascadedShadowEnabled(bool enabled) { if (auto* fx = sceneFx()) { fx->setCascadedShadowEnabled(enabled); } }
	[[nodiscard]] virtual bool isCascadedShadowEnabled() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr && fx->isCascadedShadowEnabled();
	}

	virtual void setOutlineEnabled(bool enabled) { if (auto* fx = sceneFx()) { fx->setOutlineEnabled(enabled); } }
	[[nodiscard]] virtual bool isOutlineEnabled() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr && fx->isOutlineEnabled();
	}
	virtual void setOutlineMode(OutlineMode mode) { if (auto* fx = sceneFx()) { fx->setOutlineMode(mode); } }
	[[nodiscard]] virtual OutlineMode outlineMode() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr ? fx->outlineMode() : OutlineMode::DepthSobel;
	}
	virtual void setOutlineParams(float widthPx, float threshold)
	{
		if (auto* fx = sceneFx()) { fx->setOutlineParams(widthPx, threshold); }
	}

	virtual void setToonShadowTint(const sgc::Colorf& tint) { if (auto* fx = sceneFx()) { fx->setToonShadowTint(tint); } }
	virtual void setFog(bool enabled, const sgc::Colorf& color, float nearDist, float farDist)
	{
		if (auto* fx = sceneFx()) { fx->setFog(enabled, color, nearDist, farDist); }
	}

	virtual void setTonemapExposure(float exposure) { if (auto* fx = sceneFx()) { fx->setTonemapExposure(exposure); } }
	[[nodiscard]] virtual float tonemapExposure() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr ? fx->tonemapExposure() : 1.0f;
	}
	virtual void setTonemapGamma(float gamma) { if (auto* fx = sceneFx()) { fx->setTonemapGamma(gamma); } }
	[[nodiscard]] virtual float tonemapGamma() const noexcept
	{
		const auto* fx = sceneFx();
		return fx != nullptr ? fx->tonemapGamma() : 2.2f;
	}

	virtual bool loadSplatScene(const char* path)
	{
		auto* ex = experimental();
		return ex != nullptr && ex->loadSplatScene(path);
	}
	virtual void drawSplats() { if (auto* ex = experimental()) { ex->drawSplats(); } }
	virtual void splatBounds(float& cx, float& cy, float& cz, float& r) const
	{
		if (const auto* ex = experimental()) { ex->splatBounds(cx, cy, cz, r); return; }
		cx = cy = cz = 0.0f; r = 1.0f;
	}

	virtual void drawLive2D(const char* model3jsonPath) { if (auto* ex = experimental()) { ex->drawLive2D(model3jsonPath); } }
	virtual void live2dLookAt(float nx, float ny) { if (auto* ex = experimental()) { ex->live2dLookAt(nx, ny); } }
	virtual void live2dTap() { if (auto* ex = experimental()) { ex->live2dTap(); } }
	virtual void live2dStage(const char* bg, const char* gear, const char* close)
	{
		if (auto* ex = experimental()) { ex->live2dStage(bg, gear, close); }
	}

	virtual void enableNeuralFx(bool enabled, float strength = 0.5f)
	{
		if (auto* ex = experimental()) { ex->enableNeuralFx(enabled, strength); }
	}
	virtual void enableRelight(bool enabled, float lightX = 0.4f, float lightY = 0.4f,
	                    float strength = 0.6f, float rim = 0.5f)
	{
		if (auto* ex = experimental()) { ex->enableRelight(enabled, lightX, lightY, strength, rim); }
	}
	virtual void setRelightDepthModel(const char* path) { if (auto* ex = experimental()) { ex->setRelightDepthModel(path); } }

	virtual void requestDevelop(const char* modelPath) { if (auto* ex = experimental()) { ex->requestDevelop(modelPath); } }
	virtual void tickDevelop() { if (auto* ex = experimental()) { ex->tickDevelop(); } }
	virtual void clearDevelop() { if (auto* ex = experimental()) { ex->clearDevelop(); } }
	[[nodiscard]] virtual bool styleReady() const
	{
		const auto* ex = experimental();
		return ex != nullptr && ex->styleReady();
	}
	[[nodiscard]] virtual const std::uint8_t* styleImageData() const
	{
		const auto* ex = experimental();
		return ex != nullptr ? ex->styleImageData() : nullptr;
	}
	[[nodiscard]] virtual int styleImageW() const { const auto* ex = experimental(); return ex != nullptr ? ex->styleImageW() : 0; }
	[[nodiscard]] virtual int styleImageH() const { const auto* ex = experimental(); return ex != nullptr ? ex->styleImageH() : 0; }
	virtual void setStyleStrength(float strength) { if (auto* ex = experimental()) { ex->setStyleStrength(strength); } }

	virtual void bakeStyleToSplats() { if (auto* ex = experimental()) { ex->bakeStyleToSplats(); } }
	virtual void resetSplatColors() { if (auto* ex = experimental()) { ex->resetSplatColors(); } }
	[[nodiscard]] virtual float bakedFraction() const
	{
		const auto* ex = experimental();
		return ex != nullptr ? ex->bakedFraction() : 0.0f;
	}

	virtual void captureTargetFromStyle() { if (auto* ex = experimental()) { ex->captureTargetFromStyle(); } }
	virtual void setShowTarget(bool b) { if (auto* ex = experimental()) { ex->setShowTarget(b); } }
	[[nodiscard]] virtual bool hasTarget() const { const auto* ex = experimental(); return ex != nullptr && ex->hasTarget(); }
	[[nodiscard]] virtual float matchScore() const
	{
		const auto* ex = experimental();
		return ex != nullptr ? ex->matchScore() : 0.0f;
	}

	virtual bool worldToScreen(float wx, float wy, float wz, float& u, float& v) const
	{
		if (const auto* ex = experimental()) { return ex->worldToScreen(wx, wy, wz, u, v); }
		u = v = -1.0f;
		return false;
	}

	virtual void drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position,
	              float rotYDeg, float scale, float timeSec)
	{
		if (auto* ex = experimental()) { ex->drawSolid(bakeManifestPath, position, rotYDeg, scale, timeSec); }
	}

	virtual void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale)
	{
		if (auto* ex = experimental()) { ex->drawModel(path, position, rotYDeg, scale); }
	}

	virtual void drawSkinnedModel(const char* path, const sgc::Vec3f& position,
	                      float rotYDeg, float scale,
	                      const char* clipA, float timeA,
	                      const char* clipB, float timeB, float blend01)
	{
		if (auto* ex = experimental())
		{
			ex->drawSkinnedModel(path, position, rotYDeg, scale, clipA, timeA, clipB, timeB, blend01);
		}
	}

	virtual void drawModelRot(const char* path, const sgc::Vec3f& position,
	                  const sgc::Vec3f& rotDeg, float scale)
	{
		if (auto* ex = experimental()) { ex->drawModelRot(path, position, rotDeg, scale); }
	}

	// v38 末尾追記: ISceneFx::setCascadedShadowAutoFit への転送
	virtual void setCascadedShadowAutoFit(bool enabled, float maxDistance)
	{
		if (auto* fx = sceneFx()) { fx->setCascadedShadowAutoFit(enabled, maxDistance); }
	}
	virtual void setShadowCascadeCount(int count)
	{
		if (auto* fx = sceneFx()) { fx->setShadowCascadeCount(count); }
	}

	// v39 末尾追記: ISceneFx::setOutlineCaster への転送
	virtual void setOutlineCaster(bool enabled)
	{
		if (auto* fx = sceneFx()) { fx->setOutlineCaster(enabled); }
	}

	// v40 末尾追記: ISceneFx の SSAO / トゥーン段数 / 段付きハイライトへの転送
	virtual void setAmbientOcclusion(bool enabled, float radius, float strength)
	{
		if (auto* fx = sceneFx()) { fx->setAmbientOcclusion(enabled, radius, strength); }
	}
	virtual void setToonRamp(int bands, float softness, const sgc::Colorf& midTint)
	{
		if (auto* fx = sceneFx()) { fx->setToonRamp(bands, softness, midTint); }
	}
	virtual void setToonSpecular(float strength, float power)
	{
		if (auto* fx = sceneFx()) { fx->setToonSpecular(strength, power); }
	}

	// v41 末尾追記: ISceneFx の bloom / 影の柔らかさ / 色調補正への転送
	virtual void setBloom(bool enabled, float threshold, float strength)
	{
		if (auto* fx = sceneFx()) { fx->setBloom(enabled, threshold, strength); }
	}
	virtual void setShadowSoftness(float texels)
	{
		if (auto* fx = sceneFx()) { fx->setShadowSoftness(texels); }
	}
	virtual void setColorGrade(float saturation, float contrast)
	{
		if (auto* fx = sceneFx()) { fx->setColorGrade(saturation, contrast); }
	}

	// v42 末尾追記: ISceneFx::setOutlineFade への転送
	virtual void setOutlineFade(float nearDist, float farDist, float minStrength)
	{
		if (auto* fx = sceneFx()) { fx->setOutlineFade(nearDist, farDist, minStrength); }
	}

	// v43 末尾追記: ISceneFx の半球アンビエント / 縁光 / 輪郭線の色への転送
	virtual void setHemisphereAmbient(const sgc::Colorf& sky, const sgc::Colorf& ground)
	{
		if (auto* fx = sceneFx()) { fx->setHemisphereAmbient(sky, ground); }
	}
	virtual void setRimLight(float strength, float power, const sgc::Colorf& color)
	{
		if (auto* fx = sceneFx()) { fx->setRimLight(strength, power, color); }
	}
	virtual void setOutlineDarken(float darken)
	{
		if (auto* fx = sceneFx()) { fx->setOutlineDarken(darken); }
	}
	virtual void setDepthOfField(float start, float end, float strength)
	{
		if (auto* fx = sceneFx()) { fx->setDepthOfField(start, end, strength); }
	}
	virtual void setShadowBias(float worldUnits)
	{
		if (auto* fx = sceneFx()) { fx->setShadowBias(worldUnits); }
	}

	/// @brief このフレームの setCamera を揺らす。注視点の深さにある物が画面幅・高さの (fracX, fracY) だけ動く (+y は下)。
	/// @details カメラごと平行移動するので、注視点より手前の物は大きく、奥の物は小さく動く (視差が付く)。
	///          finalizeFrame で 0 に戻る。hud.shake を 3D にも反映するために host の ModuleAdapter が毎フレーム呼ぶ。
	///          ゲーム DLL は呼ばない (古い DLL は今ある枠しか使わない) ので、末尾に足しても kCurrentApiVersion は上げない。
	virtual void setCameraShake(float /*fracX*/, float /*fracY*/) {}
};

} // namespace mitiru::render
