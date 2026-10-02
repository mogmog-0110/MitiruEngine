#pragma once

/// @file IRenderer3D.hpp
/// @brief 3D レンダラーの共通インターフェース宣言

#include <cmath>
#include <span>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Atmosphere.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/DrawParams3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/LocalLights.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/PostEffectSettings.hpp>
#include <mitiru/render/TrailRibbon.hpp>
#include <mitiru/render/VolumetricFog.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/RendererEnums3D.hpp>
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
	virtual void drawTrail(const TrailPointPod* /*points*/, int /*count*/, const TrailStylePod& /*style*/) {}

	/// @brief 以後の描画の動きベクトルの鍵 (0 で描画の順に戻す)。フレーム頭で 0 に戻る
	virtual void setMotionKey(std::uint32_t /*key*/) {}

	/// @brief false の間の描画は画面上で動かない物として扱う (カメラに付いた武器など)。フレーム頭で true に戻る
	virtual void setMotionVectorCaster(bool /*enabled*/) {}

	virtual void setAntiAliasing(AntiAliasing3D /*mode*/) {}
	virtual void setMotionBlur(float /*strength*/) {}
	virtual void setAmbientOcclusionMethod(AmbientOcclusionMethod /*method*/) {}

	/// @brief TAA と動きのぼけの履歴を捨てる (カメラが別の場所へ飛んだフレーム)
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
};

} // namespace mitiru::render
