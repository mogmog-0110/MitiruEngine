#pragma once

/// @file IRenderer3D.hpp
/// @brief 3D レンダラーの共通インターフェース宣言

#include <cmath>
#include <span>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Atmosphere.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/IndirectLighting.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/LocalLights.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/HitFeel.hpp>
#include <mitiru/render/ImpactFrame.hpp>
#include <mitiru/render/OccluderFade.hpp>
#include <mitiru/render/OutdoorDrawPod.hpp>
#include <mitiru/render/PostEffectSettings.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>
#include <mitiru/render/TrailRibbon.hpp>
#include <mitiru/render/VolumetricFog.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/RendererEnums3D.hpp>
#include <mitiru/render/SceneLook.hpp>
#include <mitiru/render/SkinnedLodLook.hpp>
#include <mitiru/render/View3DPod.hpp>
#include <mitiru/render/ISceneFx.hpp>
#include <mitiru/render/experimental/IExperimentalRenderer3D.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::animation
{
struct AnimPoseParams;
struct AnimIkRequest;
} // namespace mitiru::animation

namespace mitiru::render
{

struct DecalDesc;
struct ParticleEmitterDesc;
struct HitFeel;
struct ToonStyle;
struct ImpactFrame;
struct ParticleFlatShade;
enum class VfxTextureKind : std::uint8_t;

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
	// feature: none (描画先の大きさの通知。機能の有無に関わらない)
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
		if (lights.size() > 1) { features::note(features::Feature::MultiLight); }
	}

	/// @brief GPU 側の cbuffer に合わせたライト数の上限
	static constexpr int kMaxLights = 8;

	// ── マルチライト経路 ──

	/// @brief マルチライト経路を切り替える
	/// @details 有効時は Phong 系で `setLights()` の全ライトを使う。対応するバックエンドは DX11 と DX12。
	virtual void setUseMultiLight(bool useMulti) { if (useMulti) { features::note(features::Feature::MultiLight); } }

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
	// feature: none (描かない物を減らす切り替え。絵は変わらない)
	virtual void setFrustumCullingEnabled(bool /*enabled*/) noexcept {}

	// feature: none (カリングの状態の問い合わせ)
	[[nodiscard]] virtual bool isFrustumCullingEnabled() const noexcept { return false; }

	// feature: none (カリングの統計)
	[[nodiscard]] virtual int culledCount() const noexcept { return 0; }

	// ── オクルージョンカリング ──
	// `mitiru::render::OcclusionCuller` の CPU Hi-Z で、前フレームの深度から完全に隠れたメッシュを除く。
	// 視錐台カリングと両方が有効なときは、視錐台、オクルージョンの順に判定する。

	/// @brief オクルージョンカリングを切り替える
	/// @details 前フレームの深度を使うため、効果は 1 フレーム遅れる。
	// feature: none (描かない物を減らす切り替え。絵は変わらない)
	virtual void setOcclusionCullingEnabled(bool /*enabled*/) noexcept {}

	// feature: none (カリングの状態の問い合わせ)
	[[nodiscard]] virtual bool isOcclusionCullingEnabled() const noexcept { return false; }

	// feature: none (カリングの統計)
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
	// feature: none (2D を 3D の上に重ねる経路。3D の機能ではない)
	[[nodiscard]] virtual bool hasOverlaySupport() const noexcept { return false; }

	/// @brief DX12 では `Dx12Device*`、それ以外では nullptr を返す
	// feature: none (DX12 の窓口)
	[[nodiscard]] virtual void* nativeDevice() const noexcept { return nullptr; }

	/// @brief DX12 では `Dx12SwapChain*`、それ以外では nullptr を返す
	// feature: none (DX12 の窓口)
	[[nodiscard]] virtual void* nativeSwapChain() const noexcept { return nullptr; }

	/// @brief コマンドリストを閉じて実行する
	/// @details DX12 では `endFrame()` の後に呼ぶ。
	// feature: none (フレームの締め)
	virtual void finalizeFrame() {}

	// ── queryInterface 相当 ──

	/// @brief 絵の設定用インターフェースを返す
	// feature: none (インターフェースの取得)
	[[nodiscard]] virtual ISceneFx* sceneFx() noexcept { return nullptr; }
	[[nodiscard]] virtual const ISceneFx* sceneFx() const noexcept { return nullptr; }

	/// @brief 実験的機能のインターフェースを返す
	// feature: none (インターフェースの取得)
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

	// experimental() が空の backend (Dawn) では、描けなかった依頼を features::note で数える

	virtual bool loadSplatScene(const char* path)
	{
		if (auto* ex = experimental()) { return ex->loadSplatScene(path); }
		features::note(features::Feature::SplatLive2d);
		return false;
	}
	virtual void drawSplats()
	{
		if (auto* ex = experimental()) { ex->drawSplats(); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void splatBounds(float& cx, float& cy, float& cz, float& r) const
	{
		if (const auto* ex = experimental()) { ex->splatBounds(cx, cy, cz, r); return; }
		features::note(features::Feature::SplatLive2d);
		cx = cy = cz = 0.0f; r = 1.0f;
	}

	virtual void drawLive2D(const char* model3jsonPath)
	{
		if (auto* ex = experimental()) { ex->drawLive2D(model3jsonPath); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void live2dLookAt(float nx, float ny)
	{
		if (auto* ex = experimental()) { ex->live2dLookAt(nx, ny); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void live2dTap()
	{
		if (auto* ex = experimental()) { ex->live2dTap(); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void live2dStage(const char* bg, const char* gear, const char* close)
	{
		if (auto* ex = experimental()) { ex->live2dStage(bg, gear, close); } else { features::note(features::Feature::SplatLive2d); }
	}

	virtual void enableNeuralFx(bool enabled, float strength = 0.5f)
	{
		if (auto* ex = experimental()) { ex->enableNeuralFx(enabled, strength); }
		else if (enabled) { features::note(features::Feature::NeuralFx); }
	}
	virtual void enableRelight(bool enabled, float lightX = 0.4f, float lightY = 0.4f,
	                    float strength = 0.6f, float rim = 0.5f)
	{
		if (auto* ex = experimental()) { ex->enableRelight(enabled, lightX, lightY, strength, rim); }
		else if (enabled) { features::note(features::Feature::NeuralFx); }
	}
	virtual void setRelightDepthModel(const char* path)
	{
		if (auto* ex = experimental()) { ex->setRelightDepthModel(path); } else { features::note(features::Feature::NeuralStyleDepth); }
	}

	virtual void requestDevelop(const char* modelPath)
	{
		if (auto* ex = experimental()) { ex->requestDevelop(modelPath); } else { features::note(features::Feature::NeuralStyleDepth); }
	}
	// feature: none (Engine が毎フレーム呼ぶ進行の合図。ゲームの頼みではないので数えない)
	virtual void tickDevelop()
	{
		if (auto* ex = experimental()) { ex->tickDevelop(); }
	}
	virtual void clearDevelop()
	{
		if (auto* ex = experimental()) { ex->clearDevelop(); } else { features::note(features::Feature::NeuralStyleDepth); }
	}
	[[nodiscard]] virtual bool styleReady() const
	{
		if (const auto* ex = experimental()) { return ex->styleReady(); }
		features::note(features::Feature::NeuralStyleDepth);
		return false;
	}
	[[nodiscard]] virtual const std::uint8_t* styleImageData() const
	{
		if (const auto* ex = experimental()) { return ex->styleImageData(); }
		features::note(features::Feature::NeuralStyleDepth);
		return nullptr;
	}
	[[nodiscard]] virtual int styleImageW() const
	{
		if (const auto* ex = experimental()) { return ex->styleImageW(); }
		features::note(features::Feature::NeuralStyleDepth);
		return 0;
	}
	[[nodiscard]] virtual int styleImageH() const
	{
		if (const auto* ex = experimental()) { return ex->styleImageH(); }
		features::note(features::Feature::NeuralStyleDepth);
		return 0;
	}
	virtual void setStyleStrength(float strength)
	{
		if (auto* ex = experimental()) { ex->setStyleStrength(strength); } else { features::note(features::Feature::NeuralStyleDepth); }
	}

	virtual void bakeStyleToSplats()
	{
		if (auto* ex = experimental()) { ex->bakeStyleToSplats(); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void resetSplatColors()
	{
		if (auto* ex = experimental()) { ex->resetSplatColors(); } else { features::note(features::Feature::SplatLive2d); }
	}
	[[nodiscard]] virtual float bakedFraction() const
	{
		if (const auto* ex = experimental()) { return ex->bakedFraction(); }
		features::note(features::Feature::SplatLive2d);
		return 0.0f;
	}

	virtual void captureTargetFromStyle()
	{
		if (auto* ex = experimental()) { ex->captureTargetFromStyle(); } else { features::note(features::Feature::SplatLive2d); }
	}
	virtual void setShowTarget(bool b)
	{
		if (auto* ex = experimental()) { ex->setShowTarget(b); } else { features::note(features::Feature::SplatLive2d); }
	}
	[[nodiscard]] virtual bool hasTarget() const
	{
		if (const auto* ex = experimental()) { return ex->hasTarget(); }
		features::note(features::Feature::SplatLive2d);
		return false;
	}
	[[nodiscard]] virtual float matchScore() const
	{
		if (const auto* ex = experimental()) { return ex->matchScore(); }
		features::note(features::Feature::SplatLive2d);
		return 0.0f;
	}

	virtual bool worldToScreen(float wx, float wy, float wz, float& u, float& v) const
	{
		if (const auto* ex = experimental()) { return ex->worldToScreen(wx, wy, wz, u, v); }
		features::note(features::Feature::ScreenProjection);
		u = v = -1.0f;
		return false;
	}

	virtual void drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position,
	              float rotYDeg, float scale, float timeSec)
	{
		if (auto* ex = experimental()) { ex->drawSolid(bakeManifestPath, position, rotYDeg, scale, timeSec); }
		else { features::note(features::Feature::SplatLive2d); }
	}

	virtual void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale)
	{
		if (auto* ex = experimental()) { ex->drawModel(path, position, rotYDeg, scale); } else { features::note(features::Feature::Clod); }
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
		else { features::note(features::Feature::ModelGltf); }
	}

	virtual void drawModelRot(const char* path, const sgc::Vec3f& position,
	                  const sgc::Vec3f& rotDeg, float scale)
	{
		if (auto* ex = experimental()) { ex->drawModelRot(path, position, rotDeg, scale); } else { features::note(features::Feature::ModelGltf); }
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

	/// @brief ディスク上で書き換わったモデル (UTF-8 のパス) を忘れ、次の drawModel 系で読み直させる。
	/// @details `mitiru_host --watch-assets` が呼ぶ。ゲーム DLL は呼ばないので、末尾に足しても kCurrentApiVersion は上げない。
	/// @return 忘れた登録の数 (読み込んでいなければ 0)
	virtual int reloadModel(const char* /*changedPathUtf8*/) { return 0; }

	// ── ここから下は ABI v48 (ADR 0056) でゲーム DLL へ開いた。Screen の inline が呼ぶので並びを変えない ──

	/// @brief このフレームの局所光 (点光源・スポット) を積む。beginFrame で空に戻る
	/// @details 1 フレームに kMaxLocalLights まで受け付け、見えるものを近い順に kMaxVisibleLocalLights まで使う。
	///          対応は DX12 だけで、他のバックエンドは何もしない
	virtual void submitLocalLights(const LocalLight* /*lights*/, int /*count*/) {}

	/// @brief glTF モデルを world 行列 (非一様スケール・3 軸回転を含む) と色の調整つきで描く
	/// @details 既定は world から平行移動・Y 回転・x 軸の長さだけを取り出して drawSkinnedModel へ渡し、tint は使わない
	virtual void drawModelPosed(const char* path, const sgc::Mat4f& world, const ModelPose& pose, const DrawTint& tint)
	{
		(void)tint;
		const sgc::Vec3f pos{world.m[0][3], world.m[1][3], world.m[2][3]};
		const float scale = sgc::Vec3f{world.m[0][0], world.m[1][0], world.m[2][0]}.length();
		const float yawDeg = std::atan2(-world.m[2][0], world.m[0][0]) * 57.29577951308232f;
		drawSkinnedModel(path, pos, yawDeg, scale, pose.clipA, pose.timeA, pose.clipB, pose.timeB, pose.blend);
	}

	/// @brief 同じメッシュをインスタンスごとの行列と色で描く
	/// @details 既定は drawMesh を繰り返す。DX12 は 1 バッチ 1 ドローにまとめ、影も落とす
	virtual void drawMeshInstances(const Mesh& mesh, const MeshInstance* instances, int count,
	                               const Material& material)
	{
		for (int i = 0; i < count; ++i)
		{
			Material m = material;
			m.diffuse = tinted(material.diffuse, instances[i].tint);
			drawMesh(mesh, instanceWorld(instances[i]), m);
		}
	}

	/// @brief 剛体の glTF モデルをインスタンスごとの行列と色で描く (アニメはレストポーズ)
	virtual void drawModelInstances(const char* path, const MeshInstance* instances, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			DrawTint tint;
			for (int k = 0; k < 4; ++k) { tint.mul[k] = instances[i].tint[k]; }
			drawModelPosed(path, instanceWorld(instances[i]), ModelPose{}, tint);
		}
	}

	/// @brief AnimPoseParams の姿勢を host が評価し、IK を掛けて描く (pose が nullptr ならレストポーズ)
	/// @details DLL が同じ params と依頼で評価した姿勢と一致する。既定はレストポーズで drawModelPosed へ渡す
	virtual void drawModelAnimPose(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
	                               const animation::AnimIkRequest* ik, int ikCount, const DrawTint& tint)
	{
		(void)pose; (void)ik; (void)ikCount;
		drawModelPosed(path, world, ModelPose{}, tint);
	}

	/// @brief ノードごとのモデル空間の行列 (AnimPose::model と同じ並び) で描く。個数がノード数と違えば描かない
	virtual void drawModelNodeMatrices(const char* /*path*/, const sgc::Mat4f& /*world*/, const sgc::Mat4f* /*nodeModel*/,
	                                   int /*count*/, const DrawTint& /*tint*/) {}

	/// @brief 剣筋の帯を積む。点は呼び出しの間だけ読む
	virtual void drawTrail(const TrailPointPod* /*points*/, int count, const TrailStylePod& /*style*/)
	{
		if (count > 0) { features::note(features::Feature::Trails); }
	}

	/// @brief 以後の描画の動きベクトルの鍵 (0 で描画の順に戻す)。フレーム頭で 0 に戻る
	virtual void setMotionKey(std::uint32_t key) { if (key != 0) { features::note(features::Feature::TaaMotionVectors); } }

	/// @brief false の間の描画は画面上で動かない物として扱う (カメラに付いた武器など)。フレーム頭で true に戻る
	virtual void setMotionVectorCaster(bool enabled) { if (!enabled) { features::note(features::Feature::TaaMotionVectors); } }

	virtual void setAntiAliasing(AntiAliasing3D /*mode*/) {}
	virtual void setMotionBlur(float strength) { if (strength > 0.0f) { features::note(features::Feature::MotionBlur); } }
	virtual void setAmbientOcclusionMethod(AmbientOcclusionMethod method)
	{
		if (method == AmbientOcclusionMethod::Gtao) { features::note(features::Feature::AmbientOcclusion); }
	}

	/// @brief TAA と動きのぼけの履歴を捨てる (カメラが別の場所へ飛んだフレーム)
	// 履歴を持たない backend は捨てる物が無いので、TAA を頼まれない限り数えない
	virtual void resetTemporalHistory() {}

	/// @brief PBR の環境光に使う cubemap
	virtual void setEnvironment(const Cubemap& cubemap) { if (auto* fx = sceneFx()) { fx->setEnvironment(cubemap); } }

	/// @brief ゲームが作ったメッシュを id で登録する。中身は呼び出しの間に写す。同じ id は差し替える。対応しなければ false
	virtual bool registerGameMesh(std::uint32_t /*id*/, const Vertex3D* /*vertices*/, int /*vertexCount*/,
	                              const std::uint32_t* /*indices*/, int /*indexCount*/) { return false; }
	/// @brief 登録を消す。描いたフレームの GPU の仕事が終わってから解放する
	virtual void releaseGameMesh(std::uint32_t /*id*/) {}
	/// @brief 登録したメッシュ (drawMesh / drawMeshInstances にそのまま渡す)。無ければ nullptr
	[[nodiscard]] virtual const Mesh* findGameMesh(std::uint32_t /*id*/) const { return nullptr; }

	/// @brief 物理ベースの空と空気遠近 (ADR 0057)。対応しないバックエンドは何もしない
	virtual void setSky(const SkySettings& /*sky*/) {}
	/// @brief 体積フォグ (ADR 0057)。対応しないバックエンドは何もしない
	virtual void setVolumetricFog(const VolumetricFogSettings& /*fog*/) {}

	// ── ここから下は ABI v49 (ADR 0062) でゲーム DLL へ開いた。Screen の inline が呼ぶので並びを変えない ──
	// どれも描画だけの依頼で、DX12 以外は何もしない (VFX テクスチャと副ビューは -1 / false を返す)

	/// @brief このフレームのデカールを積む (VFX.md)。beginFrame で空に戻る
	virtual void submitDecals(const DecalDesc* /*decals*/, int /*count*/) {}
	/// @brief このフレームの粒のエミッターを積む。渡さなかった key のエミッターは消える
	virtual void submitParticles(const ParticleEmitterDesc* /*emitters*/, int /*count*/) {}
	/// @brief 画像ファイルを VFX テクスチャの層に登録する。同じ path と種類は同じ番号。読めなければ -1
	virtual int registerVfxTextureFile(const char* /*path*/, VfxTextureKind /*kind*/) { return -1; }
	/// @brief このフレームだけ効く当たりの演出
	virtual void setHitFeel(const HitFeel& feel) { if (hitFeelActive(feel)) { features::note(features::Feature::HitFeel); } }

	/// @brief 屋外の 1 枚 (world.json) を描く。置き場所はファイルで決まる (ADR 0060)
	virtual void drawOutdoor(const char* /*worldPath*/, const OutdoorDrawPod& /*pod*/) { features::note(features::Feature::OutdoorWorld); }

	/// @brief 副ビューの slot (0..kMaxView3DSlots-1) を作る。中身が前と同じなら何もしない。作れなければ false
	virtual bool setViewSlot(int /*slot*/, const View3DPod& /*pod*/) { return false; }
	/// @brief 以後の描画を slot の副ビューへ向ける。描けなければ false (描画は主ビューへ入る)
	virtual bool beginViewSlot(int /*slot*/, const Camera3D& /*camera*/) { return false; }
	virtual void endViewSlot() {}
	/// @brief このフレームの最後に slot の出力を画面の矩形へ貼る。矩形は出力の幅と高さに対する割合 (0..1)
	virtual void compositeViewSlot(int /*slot*/, float /*u*/, float /*v*/, float /*w*/, float /*h*/) {}
	/// @brief slot の出力を基本色のテクスチャにしてメッシュを描く
	virtual void drawMeshWithViewSlot(const Mesh& /*mesh*/, const sgc::Mat4f& /*world*/, const Material& /*material*/,
	                                  int /*slot*/) {}
	virtual void releaseViewSlot(int /*slot*/) {}

	/// @brief glTF / FBX のモデル、world.json、region.json を手放す (ADR 0061、0064)。次に同じ path を描くと読み直す
	virtual bool releaseModel(const char* /*path*/)
	{
		features::note(features::Feature::ModelRelease);
		return false;
	}

	// ── ここから下は ABI v50 (ADR 0067)。並びを変えない ──

	/// @brief スキンのキャラの LOD の選び方 (ADR 0065)。次に呼ばれるまで保つ。DX12 以外は何もしない
	virtual void setSkinnedLodLook(const SkinnedLodLook& /*look*/) {}

	// ── ここから下は ABI v51 (ADR 0071)。並びを変えない ──

	/// @brief 登録したメッシュの頂点を同じ数で差し替え、前の形からの動きを TAA と動きのぼけへ渡す (ADR 0069)。
	///        数が登録と違うか、登録が無ければ false で何もしない。ゲームのメッシュを持たないバックエンドは false
	virtual bool updateGameMesh(std::uint32_t /*id*/, const Vertex3D* /*vertices*/, int /*vertexCount*/) { return false; }

	/// @brief 破片をまとめて描く。既定は 1 個ずつ drawMeshInstances へ渡す (DX12 は meshId ごとに 1 回の instanced draw)
	virtual void drawMeshPieces(const PieceInstancePod* pieces, int count)
	{
		Material material;
		material.diffuse = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};
		for (int i = 0; i < count; ++i)
		{
			if (const Mesh* mesh = findGameMesh(pieces[i].meshId))
			{
				const MeshInstance inst = pieceInstance(pieces[i]);
				drawMeshInstances(*mesh, &inst, 1, material);
			}
		}
	}

	/// @brief ゲームが SceneLook で頼む間接光 (ADR 0070)。flags が 0 なら host の既定に戻す。DX12 以外は何もしない
	virtual void setIndirectLightLook(const IndirectLightLook& /*look*/) {}

	/// @brief 焼いた光のファイル (*.lighting.bin) を選ぶ。同じ path なら何もしない。空か nullptr で外す。
	///        読み込みはワーカーで進め、読み終わるまで前の光のまま描く。DX12 以外は何もしない
	virtual void requestLightingBake(const char* /*path*/) {}

	// ── ここから下は ABI v53 (ADR 0075)。並びを変えない ──

	/// @brief drawMesh に色の調整を付けた版。tint.add は照明と影の後に足すので、影の中でも同じだけ明るい
	/// @details 既定は tint.mul を拡散色に掛けて drawMesh へ渡し、add は捨てる
	virtual void drawMeshTinted(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const DrawTint& tint)
	{
		Material m = material;
		m.diffuse = tinted(material.diffuse, tint.mul);
		drawMesh(mesh, world, m);
	}

	/// @brief drawMeshInstances に、全インスタンスへ同じ色の調整を付けた版 (インスタンスの tint は tint.mul にさらに掛ける)
	/// @details 既定は tint.mul を拡散色に掛けて drawMeshInstances へ渡し、add は捨てる
	virtual void drawMeshInstancesTinted(const Mesh& mesh, const MeshInstance* instances, int count, const Material& material,
	                                     const DrawTint& tint)
	{
		Material m = material;
		m.diffuse = tinted(material.diffuse, tint.mul);
		drawMeshInstances(mesh, instances, count, m);
	}

	// ── ここから下は ABI v54 (ADR 0076)。並びを変えない ──

	/// @brief 頂点の数と並びが毎フレーム変わるメッシュ (粒から張り直す面) の中身を差し替える。
	///        前のフレームの位置は prevPositions[i] で頂点ごとに受け、TAA と動きのぼけへ渡す (nullptr なら頂点は動かなかった扱い)。
	///        登録の数を容量として先頭だけを書き、残りの添字は潰れた三角形で埋める。容量を超えたら広げ、登録が無ければ作る。
	///        添字が頂点の数を超えるか 3 の倍数でなければ false で何もしない。ゲームのメッシュを持たないバックエンドは false
	virtual bool updateGameMeshTopology(std::uint32_t /*id*/, const Vertex3D* /*vertices*/, int /*vertexCount*/,
	                                    const std::uint32_t* /*indices*/, int /*indexCount*/,
	                                    const sgc::Vec3f* /*prevPositions*/)
	{
		return false;
	}

	/// @brief 的を隠す面を網点で抜く指定 (ADR 0077)。そのフレームだけ効き、beginFrame で消える。描く前でも後でもよい
	virtual void setOccluderFade(const OccluderFade& fade) { if (fade.targetCount > 0) { features::note(features::Feature::OccluderFade); } }
	/// @brief 以後の描画を透かしの的として扱う。的は抜かず、隠れた所に OccluderFade::silhouette の影絵を描く。
	///        beginFrame で false に戻る
	virtual void setFadeTarget(bool enabled) { if (enabled) { features::note(features::Feature::OccluderFade); } }

	/// @brief 以後の描画の段・縁の帯・つや・輪郭線 (ADR 0079)。nullptr で場面の値へ戻す。beginFrame で場面の値へ戻る
	virtual void setToonStyle(const ToonStyle* /*style*/) {}
	/// @brief このフレームの止め絵 (ADR 0079)。そのフレームだけ効く
	virtual void setImpactFrame(const ImpactFrame& frame) { if (impactFrameActive(frame)) { features::note(features::Feature::ImpactFrame); } }
	/// @brief submitParticles に、HardDisc の粒の 2 色目を同じ並びで付けた版 (ADR 0079)。shades が nullptr なら 1 色
	/// @details 既定は shades を捨てて submitParticles へ渡す
	virtual void submitParticlesShaded(const ParticleEmitterDesc* emitters, const ParticleFlatShade* /*shades*/, int count)
	{
		submitParticles(emitters, count);
	}

	// ── ここから下は ABI v55 (ADR 0080)。並びを変えない ──

	/// @brief ゲームの画質の段が頼む 3D の内部解像度の倍率 (0.5..1.0)。0 で利用者の設定 (graphics.upscale) に戻す。
	///        資源を作り替えるので、次の beginFrame で当てる
	virtual void setRequestedRenderScale(float /*scale*/) {}
	/// @brief ゲームの画質の段が頼む太陽の影の絵の一辺 (256..4096 の 2 の冪)。0 で起動時の大きさに戻す。
	///        遠いカスケードはその半分。大きさが変わったときだけ次の beginFrame で作り直す
	virtual void setRequestedShadowMapSize(int size) { if (size > 0) { features::note(features::Feature::RenderScale); } }

	// ── ここから下は ABI v62 (ADR 0099)。並びを変えない。入口だけで、既定の本体は描かずに 1 度だけ知らせる。
	//    後続の担当が backend の override で埋め、そのとき機能表 (RenderFeatures.hpp) に行を足して、ここの注記を外す ──

	/// @brief 自作の材質 name (assets/materials/<name>.slang) を登録する。同じ name は同じ番号 (1 から)。使えなければ -1。
	///        登録の時にコンパイルとパイプラインの作成を済ませる。既定は描けない backend として数えて -1
	virtual int registerMaterial3D(const char* /*name*/)
	{
		features::note(features::Feature::UserMaterial);
		return -1;
	}
	/// @brief 以後の描画の材質を id (registerMaterial3D の返り値) にする。id 0 で場面の陰影に戻る。params は呼び出しの間に写す
	///        float の並びで、材質の .mat.json が名前と範囲を決める。timeSec は GameMemory の秒 (壁時計を渡さない)。フレーム頭で戻る
	virtual void setUserMaterial(int id, const float* /*params*/, int /*count*/, float /*timeSec*/)
	{
		if (id > 0) { features::note(features::Feature::UserMaterial); }
	}

	/// @brief drawModelAnimPose に、モーフターゲットの重みを付けたオーバーロード。weights はモデルごとのスロット表 (animation::MorphSlotTable) の並びで、
	///        呼び出しの間に写す。count が多い分は捨て、少ない分は 0。count が 0 なら drawModelAnimPose と同じ (glTF の既定の重みも当てない)。
	///        重みは -16..16 に丸め、NaN は 0。既定は重みを捨てて drawModelAnimPose へ渡し、数える
	virtual void drawModelMorph(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
	                            const animation::AnimIkRequest* ik, int ikCount, const DrawTint& tint,
	                            const float* weights, int count)
	{
		if (weights != nullptr && count > 0) { features::note(features::Feature::MorphTargets); }
		drawModelAnimPose(path, world, pose, ik, ikCount, tint);
	}

	/// @brief 環境を HDR の画像 name (assets 以下) で頼む。空か nullptr で今の勾配に戻す。intensity は明るさ、yawDeg は回す角度
	virtual void setEnvironmentHdr(const char* name, float /*intensity*/, float /*yawDeg*/)
	{
		if (name != nullptr && name[0] != '\0')
		{
			features::note(features::Feature::HdrEnvironment);
			debug::warnOnce("abi62.hdr_environment", "この描画先は HDR の環境 (environmentHdr3D) を描けないので、環境は今のままです");
		}
	}

	// feature: none (v62 の入口だけ。wetness / cloudCover / colorLut を実装する担当が、それぞれ機能表に行を足す)
	/// @brief SceneLook の v62 で名前を付けた欄 (wetness / cloudCover / colorLut) と予約を、ensure3DFrame が丸ごと渡す。
	///        sceneLook3D を呼んだゲームだけ、フレームの頭に 1 回
	virtual void applySceneLookExtras(const SceneLook& look)
	{
		if (look.wetness != 0) { debug::warnOnce("abi62.wetness", "濡れた面 (SceneLook::wetness) はまだ入口だけで、絵は変わりません"); }
		if (look.cloudCover != 0) { debug::warnOnce("abi62.cloud_cover", "雲の層 (SceneLook::cloudCover) はまだ入口だけで、絵は変わりません"); }
		if (look.colorLut != 0) { debug::warnOnce("abi62.color_lut", "色の LUT (SceneLook::colorLut) は DX12 と Dawn の描画でだけ効くので、この描画では絵は変わりません"); }
	}

	// feature: none (拡張の口。ADR 0099 の表に無い id は何もせず -1)
	/// @brief 後続の追加を vtable を変えずに入れる口。id は ADR 0099 の表で割り当て、data は POD で呼び出しの間だけ読み書きする。
	///        対応しない id は何もせず -1、処理したら 0 以上
	virtual std::int32_t invokeExtension(std::uint32_t /*id*/, void* /*data*/, std::uint32_t /*size*/) { return -1; }
};

} // namespace mitiru::render
