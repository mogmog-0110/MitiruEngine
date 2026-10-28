#pragma once

// Dawn の 3D レンダラの宣言。フレームと描画は Renderer3D_Dawn.cpp、空、フォグ、粒、焼いた光は Renderer3D_DawnFx.cpp に実装する。

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/dawn/DawnDevice.hpp>
#include <mitiru/render/Atmosphere.hpp>
#include <mitiru/render/IndirectLighting.hpp>
#include <mitiru/render/VolumetricFog.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>
#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/FrustumCulling.hpp>
#include <mitiru/render/ISceneFx.hpp>
#include <mitiru/render/ImpactFrame.hpp>
#include <mitiru/render/OccluderFade.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/detail/SceneLookExtras.hpp>
#include <mitiru/render/ToonStyleGpu.hpp>
#include <mitiru/render/WetSurface.hpp>

#include "DawnBindGroupTables.hpp"
#include "DawnBloomPass.hpp"
#include "DawnContext.hpp"
#include "DawnDecals.hpp"
#include "DawnDeviceTarget.hpp"
#include "DawnGameMeshes.hpp"
#include "DawnFxaaPass.hpp"
#include "DawnSmaaPass.hpp"
#include "DawnColorFinishPass.hpp"
#include "DawnImpactPass.hpp"
#include "DawnLightingBake.hpp"
#include "DawnLinePass.hpp"
#include "DawnProfile.hpp"
#include "DawnLocalLights.hpp"
#include "DawnMaterialMaps.hpp"
#include "DawnMeshCache.hpp"
#include "DawnModelStore.hpp"
#include "DawnParticles.hpp"
#include "DawnRenderTypes.hpp"
#include "DawnSceneFx.hpp"
#include "DawnScenePipelines.hpp"
#include "DawnSceneTargets.hpp"
#include "DawnShadowPass.hpp"
#include "DawnSkinning.hpp"
#include "DawnSkyPass.hpp"
#include "DawnUserMaterials.hpp"
#include "DawnVfxTextures.hpp"
#include "DawnViewSlots.hpp"

namespace mitiru::render::dawn_detail
{

class DawnRenderer3D final : public IRenderer3D, public DawnSceneFx
{
public:
	explicit DawnRenderer3D(gfx::DawnDevice& device, const features::RenderNeeds* needs = nullptr)
		: m_device(device), m_ctx(*device.context()), m_pipelines(m_ctx), m_meshCache(m_ctx)
	{
		if (needs != nullptr) { m_needs = *needs; }
		m_initialized = createStaticObjects();
		if (m_initialized)
		{
			registerBindGroupTables();
			recreateTargets(device.width(), device.height());
		}
	}

	[[nodiscard]] bool isInitialized() const noexcept override { return m_initialized; }

	/// @brief hot reload で宣言が増えたとき、増えた機能を作る (ADR 0090)。Dawn で宣言が効くのは WBOIT だけ。旧体制は何もしない
	void declareFeatures(const features::RenderNeeds& needs)
	{
		if (!m_needs.declaredMode || !needs.declaredMode) { return; }
		m_needs.declared |= needs.declared;
		if (!m_wboit && m_wboitCapable && m_needs.wants(features::Feature::Oit))
		{
			m_wboit = true;
			prepareOitNow();
		}
		features::setActiveNeeds(m_needs);
	}
	/// @brief WBOIT の蓄積先が作ってあるか。試験が、宣言したときだけ初期化で作っていること (遅延でないこと) を見る
	[[nodiscard]] bool wboitReady() const noexcept { return m_t.oit.ready(); }
	void resize(int width, int height) override { if (m_initialized) { recreateTargets(width, height); } }
	[[nodiscard]] ISceneFx* sceneFx() noexcept override { return static_cast<DawnSceneFx*>(this); }
	[[nodiscard]] const ISceneFx* sceneFx() const noexcept override { return static_cast<const DawnSceneFx*>(this); }

	/// @brief SceneLook の v62 のフィールド (ADR 0099)。濡れ・雲・色の仕上げのエージェントが、下の applyXxx だけを実装する
	void applySceneLookExtras(const SceneLook& look) override { ::mitiru::render::detail::dispatchSceneLookExtras(*this, look); }

	/// @brief SceneLook::wetness (ADR 0099)。以後の描画の拡散色を暗くし、鏡面とトゥーンのつやを強める (WetSurface.hpp)
	void applyWetness(std::uint8_t wetness) noexcept
	{
		m_wetness = wetAmount(wetness);
		if (wetness != 0) { features::use(features::Feature::WetSurface); }
	}

	/// @brief SceneLook::cloudCover (ADR 0099)。物理ベースの空の上にだけ雲の層を描く
	void applyCloudCover(std::uint8_t cloudCover) noexcept
	{
		m_cloudCover = static_cast<float>(cloudCover) * (1.0f / 255.0f);
		if (cloudCover != 0) { features::use(features::Feature::CloudLayer); }
	}


	/// 自動露出と接触影は Dawn では描かず、頼まれた回数だけを機能表の note に数える (ADR 0109)
	void applyAutoExposure(float speed, float /*rangeEv*/, float /*biasEv*/)
	{
		if (speed > 0.0f) { features::use(features::Feature::AutoExposure); features::note(features::Feature::AutoExposure); }
	}
	void applyContactShadow(float strength, float /*distance*/)
	{
		if (strength > 0.0f) { features::use(features::Feature::ContactShadow); features::note(features::Feature::ContactShadow); }
	}

	// applyColorLut / loadColorLooks は Renderer3D_DawnFx.cpp
	void applyColorLut(std::uint8_t number);

	/// @brief assets/color_looks.json を読む。組があれば pass を作って LUT を上げる (renderer の初期化の直後に host が 1 回呼ぶ)
	bool loadColorLooks(const char* manifestPath);
	/// @brief 粒のシードにする決定論のフレーム番号 (描いた回数は使わない。リプレイで同じ粒にするため)
	void setColorFinishSeed(std::uint32_t frame) noexcept { m_colorFinishSeed = frame; }


	void beginFrame(const sgc::Colorf& clearColor) override;
	void endFrame() override { m_frameEnded = true; }
	void finalizeFrame() override;
	void setCamera(const Camera3D& camera) override;
	void setCameraShake(float fracX, float fracY) override { m_shakeX = fracX; m_shakeY = fracY; }
	void setLight(const Light& light) override;
	void drawMesh(const Mesh& mesh, const sgc::Mat4f& world, const Material& material) override
	{
		addDraw(mesh, world, material, material.diffuse, nullptr);
	}
	void drawMeshTinted(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const DrawTint& tint) override
	{
		addDraw(mesh, world, material, tinted(material.diffuse, tint.mul), tint.add);
	}
	void drawMeshInstances(const Mesh& mesh, const MeshInstance* instances, int count, const Material& material) override
	{
		MITIRU_DAWN_ZONE("api.drawMeshInstances");
		for (int i = 0; i < count; ++i)
		{
			addDraw(mesh, instanceWorld(instances[i]), material, tinted(material.diffuse, instances[i].tint), nullptr);
		}
	}
	void drawMeshInstancesTinted(const Mesh& mesh, const MeshInstance* instances, int count, const Material& material,
	                             const DrawTint& tint) override
	{
		MITIRU_DAWN_ZONE("api.drawMeshInstancesTinted");
		for (int i = 0; i < count; ++i)
		{
			const sgc::Colorf base = tinted(material.diffuse, tint.mul);
			addDraw(mesh, instanceWorld(instances[i]), material, tinted(base, instances[i].tint), tint.add);
		}
	}

	// ── モデル（glTF / glb / FBX）。姿勢の渡し方は DX12 と同じ ──
	void drawModelPosed(const char* path, const sgc::Mat4f& world, const ModelPose& pose, const DrawTint& tint) override
	{
		drawItems(m_models.clips(path, world, pose), tint);
	}
	void drawModelAnimPose(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
	                       const animation::AnimIkRequest* ik, int ikCount, const DrawTint& tint) override
	{
		drawItems(m_models.animPose(path, world, pose, ik, ikCount), tint);
	}
	void drawModelMorph(const char* path, const sgc::Mat4f& world, const animation::AnimPoseParams* pose,
	                    const animation::AnimIkRequest* ik, int ikCount, const DrawTint& tint, const float* weights,
	                    int count) override
	{
		const std::span<const float> morph = (weights != nullptr && count > 0) ? std::span<const float>(weights, static_cast<std::size_t>(count))
		                                                                      : std::span<const float>{};
		drawItems(m_models.animPose(path, world, pose, ik, ikCount, morph), tint);
	}
	void drawModelNodeMatrices(const char* path, const sgc::Mat4f& world, const sgc::Mat4f* nodeModel, int count,
	                           const DrawTint& tint) override
	{
		drawItems(m_models.nodes(path, world, nodeModel, count), tint);
	}
	void drawSkinnedModel(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale, const char* clipA,
	                      float timeA, const char* clipB, float timeB, float blend01) override
	{
		constexpr float kDegToRad = 0.017453292519943295f;
		const sgc::Mat4f world = sgc::Mat4f::translation(position) * sgc::Mat4f::rotationY(rotYDeg * kDegToRad) * sgc::Mat4f::scaling(scale);
		drawModelPosed(path, world, ModelPose{clipA, timeA, clipB, timeB, blend01}, DrawTint{});
	}
	void drawModelRot(const char* path, const sgc::Vec3f& position, const sgc::Vec3f& rotDeg, float scale) override
	{
		constexpr float kDegToRad = 0.017453292519943295f;
		const sgc::Mat4f world = sgc::Mat4f::translation(position) * sgc::Mat4f::rotationY(rotDeg.y * kDegToRad) *
		                         sgc::Mat4f::rotationX(rotDeg.x * kDegToRad) * sgc::Mat4f::rotationZ(rotDeg.z * kDegToRad) *
		                         sgc::Mat4f::scaling(scale);
		drawModelPosed(path, world, ModelPose{}, DrawTint{});
	}
	int reloadModel(const char* changedPathUtf8) override
	{
		return changedPathUtf8 != nullptr ? m_models.forget(std::filesystem::u8path(changedPathUtf8)) : 0;
	}

	void resetFrameActive() noexcept override { m_frameActive = false; }
	[[nodiscard]] bool isFrameActive() const noexcept override { return m_frameActive; }
	[[nodiscard]] int drawCallCount() const noexcept override { return m_drawCalls; }
	[[nodiscard]] int culledCount() const noexcept override { return m_culled; }
	[[nodiscard]] bool isFrustumCullingEnabled() const noexcept override { return true; }
	// 2D は 3D の確定後に重ねる。Engine は finalizeFrame の後に present3DOverlay を呼ぶ。
	[[nodiscard]] bool hasOverlaySupport() const noexcept override { return true; }

	void setToonStyle(const ToonStyle* style) override;
	void setRequestedRenderScale(float scale) override;
	// 透かし (ABI v54) と止め絵 (ADR 0079)。どちらもフレームごとの指定で、beginFrame で消える
	void setOccluderFade(const OccluderFade& fade) override { m_occluderFade = fade; }
	void setFadeTarget(bool enabled) override { m_fadeTargetDraw = enabled; }
	void setImpactFrame(const ImpactFrame& frame) override { m_impactFrame = clampImpactFrame(frame); }

	bool registerGameMesh(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
	                      int indexCount) override
	{
		return m_gameMeshes.registerMesh(id, vertices, vertexCount, indices, indexCount);
	}
	void releaseGameMesh(std::uint32_t id) override { m_gameMeshes.release(id); }
	[[nodiscard]] const Mesh* findGameMesh(std::uint32_t id) const override { return m_gameMeshes.find(id); }
	bool updateGameMesh(std::uint32_t id, const Vertex3D* vertices, int vertexCount) override
	{
		return m_gameMeshes.updateVertices(id, vertices, vertexCount);
	}
	bool updateGameMeshTopology(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices,
	                            int indexCount, const sgc::Vec3f*) override
	{
		return m_gameMeshes.updateTopology(id, vertices, vertexCount, indices, indexCount);
	}

	// 焼いた間接光。SceneLook の indirect と lightingBake3D で指定し、この呼び出しの中で読み込みを終える。
	void setIndirectLightLook(const IndirectLightLook& look) override
	{
		const ScreenSpaceReflectionSettings ssrDefault;
		ScreenSpaceReflectionSettings ssr;
		resolveIndirectLook(look, GlobalIlluminationSettings{}, ssrDefault, m_gi, ssr);
		if ((look.flags & kIndirectSsr) != 0) { features::note(features::Feature::Ssr); }
		if ((look.flags & kIndirectReflectionProbes) != 0) { features::note(features::Feature::ReflectionProbes); }
		if ((look.flags & kIndirectDynamicGi) != 0) { features::note(features::Feature::Ddgi); }
		m_gi.intensity = std::max(m_gi.intensity, 0.0f);
		m_gi.toonBands = std::clamp(m_gi.toonBands, 0, 8);
	}
	void requestLightingBake(const char* path) override;
	void setVolumetricFog(const VolumetricFogSettings& fog) override
	{
		m_fog = fog;
		if (fog.enabled && (fog.temporalBlend < 1.0f || fog.localLightScale > 0.0f)) { features::note(features::Feature::VolumetricFog); }
	}
	// 粒（particles3D）。
	void submitParticles(const ParticleEmitterDesc* emitters, int count) override { m_particles->submit(emitters, nullptr, count); }
	void submitParticlesShaded(const ParticleEmitterDesc* emitters, const ParticleFlatShade* shades, int count) override
	{
		m_particles->submit(emitters, shades, count);
	}
	// デカール（decals3D）と VFX テクスチャ（vfxTexture3D）。
	void submitDecals(const DecalDesc* decals, int count) override { m_decals->submit(decals, count); }
	void submitLocalLights(const LocalLight* lights, int count) override
	{
		m_localLights->submit(lights, count);
		bool spotShadow = false;
		bool pointShadow = false;
		for (int i = 0; lights != nullptr && i < count; ++i)
		{
			if (lights[i].castShadow == 0) { continue; }
			(lights[i].type == LocalLightType::Spot ? spotShadow : pointShadow) = true;
		}
		if (spotShadow) { features::note(features::Feature::LocalLights); }
		if (pointShadow) { features::use(features::Feature::PointShadow); features::note(features::Feature::PointShadow); }
	}
	int registerVfxTextureFile(const char* path, VfxTextureKind kind) override { return m_vfx->registerFile(path, kind); }
	// 自作の材質（ADR 0100）。焼いた .wgsl を読む。半透明の描画には効かない。
	int registerMaterial3D(const char* name) override
	{
		features::use(features::Feature::UserMaterial);
		if (m_needs.declaredMode && !m_needs.wants(features::Feature::UserMaterial))
		{
			features::note(features::Feature::UserMaterial);
			return -1;
		}
		return m_userMaterials.registerMaterial(name);
	}
	void setUserMaterial(int id, const float* params, int count, float timeSec) override { m_userMaterials.set(id, params, count, timeSec); }
	std::int32_t invokeExtension(std::uint32_t id, void* data, std::uint32_t size) override
	{
		if (id != kExtReloadUserMaterial || data == nullptr || size == 0) { return -1; }
		const char* chars = static_cast<const char*>(data);
		return m_userMaterials.reload(std::string(chars, ::strnlen(chars, size)));
	}
	void setSkinnedLodLook(const SkinnedLodLook& look) override { m_models.setLodLook(look); }

	// 副ビュー（ABI v49）。slot ごとに描画先と描く物を持ち、finalizeFrame で主ビューの後に描く。実装は Renderer3D_DawnViews.cpp。
	bool setViewSlot(int slot, const View3DPod& pod) override;
	bool beginViewSlot(int slot, const Camera3D& camera) override;
	void endViewSlot() override;
	void compositeViewSlot(int slot, float u, float v, float w, float h) override;
	void drawMeshWithViewSlot(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, int slot) override;
	void releaseViewSlot(int slot) override;
	/// slot の出力（最終色の形式、sRGB の値）。UI の `view3d:N` と試験が読む。作っていなければ空。
	[[nodiscard]] const DawnViewSlot* viewSlot(int slot) const noexcept;
	/// モデルの読み込みの入口 (Engine::preloadAsset が Renderer3D_Dawn.hpp の関数越しに使う)
	[[nodiscard]] DawnModelStore& modelStore() noexcept { return m_models; }
	[[nodiscard]] const DawnModelStore& modelStore() const noexcept { return m_models; }
	[[nodiscard]] const DawnSkinStats& skinStats() const noexcept { return m_skinning->stats(); }
	[[nodiscard]] std::uint32_t cpuSkinBodies() const noexcept { return static_cast<std::uint32_t>(m_cpuSkinCursor); }
	[[nodiscard]] bool skinComputeReady() const noexcept { return m_skinning->ready(); }
	/// 主ビューの bind group の組の登録簿。書き替えた回数と、記録の前に初めて見つかった違いの数を試験と診断が読む
	[[nodiscard]] const DescriptorTableRegistry& bindGroupTables() const noexcept { return m_tables; }
	void setAntiAliasing(AntiAliasing3D mode) override
	{
		// 副ビューは SMAA の間も FXAA で描く。主ビューの SMAA は表を持つビルドで、選んだ最初に作る
		m_fxaaOn = mode == AntiAliasing3D::MsaaFxaa || mode == AntiAliasing3D::MsaaSmaa;
		m_smaaOn = mode == AntiAliasing3D::MsaaSmaa && prepareSmaa();
		if (mode == AntiAliasing3D::Taa) { features::note(features::Feature::TaaMotionVectors); }
	}
	void setSky(const SkySettings& sky) override
	{
		m_sky = sky;
		m_sky.toonBands = std::clamp(m_sky.toonBands, 0, 8);
		m_sky.metersPerUnit = (m_sky.metersPerUnit > 0.0f) ? m_sky.metersPerUnit : 1.0f;
		m_sky.aerialPerspectiveScale = std::max(m_sky.aerialPerspectiveScale, 0.0f);
		m_skyDriveAltitude = -1.0f;   // 環境光の計算を捨てて、次のフレームで空の設定から求め直す
	}

private:
	bool createStaticObjects();
	[[nodiscard]] bool supportsMsaaColor(wgpu::TextureFormat format) const;
	[[nodiscard]] wgpu::TextureFormat chooseHdrFormat() const;
	void recreateTargets(int width, int height);
	void addDraw(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const sgc::Colorf& diffuse, const float* add,
	             const DawnMaterialMaps* maps = nullptr);
	void addSkinnedDraw(const DawnDrawItem& item, const sgc::Colorf& diffuse, const float* add);
	/// box は包含箱。mesh と skin のどちらか一方を渡し、描くと決まってから GPU の頂点を用意する
	void addDrawFrom(const Mesh::AABB& box, const Mesh* mesh, const DawnDrawItem* skin, const sgc::Mat4f& world, const Material& material,
	                 const sgc::Colorf& diffuse, const float* add, const DawnMaterialMaps* maps);
	/// 骨の行列は直前に変形の入口を呼んだ DawnModelStore::palette() にある。GPU で変形できなければ CPU の変形に回る
	[[nodiscard]] GpuMesh* acquireSkinned(const DawnDrawItem& item);
	[[nodiscard]] Camera3D shaken(const Camera3D& requested) const;
	void writeFrameCB();
	void updateParticles();
	void fillFog(FrameCB& cb) const;
	void encodeFog(wgpu::CommandEncoder& encoder);
	void ensureDrawBuffer(std::size_t bytes);
	/// 描画の定数と同じ提出順の約束で、まとめ描きの InstanceData を 1 回の WriteBuffer で GPU へ渡す
	void uploadInstances();
	/// 直前の描画と同じ見た目なら、新しい定数を積まずに instance を足す。足したら true
	[[nodiscard]] bool extendRun(const float* world, const float* diffuse, const DrawRecord& rec);
	/// 定数の共通の欄 (world と diffuse 以外) を、前の描画と材質・状態が同じなら使い回し、違えば作り直す
	void refreshCbBase(const Material& material, const float* add, const DawnMaterialMaps* maps);
	[[nodiscard]] sgc::Vec3f casterCentroid() const noexcept;
	void drawItems(const std::vector<DawnDrawItem>& items, const DrawTint& tint)
	{
		const auto palette = m_models.palette();
		m_paletteBase = palette.empty() || !m_skinning->ready() || m_cpuSkinForced ? DawnSkinning::kNoPalette : m_skinning->appendPalette(palette);
		for (const DawnDrawItem& item : items)
		{
			if (item.skinned()) { addSkinnedDraw(item, tinted(item.material->diffuse, tint.mul), tint.add); continue; }
			addDraw(*item.mesh, item.world, *item.material, tinted(item.material->diffuse, tint.mul), tint.add, item.maps);
		}
	}
	void encodeScene(wgpu::CommandEncoder& encoder, bool keepLineTargets);
	void encodeMainFrame();
	void renderViews();
	void renderView(DawnViewSlot& view);
	void encodeViewComposites();
	void swapViewState(DawnViewSlot& view) noexcept;
	void retireViewSlot(DawnViewSlot& view);
	[[nodiscard]] bool createViewTargets(DawnViewSlot& view, const View3DPod& pod);
	[[nodiscard]] static bool validViewSlot(int slot) noexcept { return slot >= 0 && slot < kMaxView3DSlots; }
	void recordDraws(wgpu::RenderPassEncoder& pass, std::size_t first, std::size_t last, const wgpu::RenderPipeline* only = nullptr);
	void encodeOitAccumulate(wgpu::CommandEncoder& encoder, std::size_t first);
	[[nodiscard]] bool silhouettesWanted() const noexcept;
	void encodeSilhouettes(wgpu::CommandEncoder& encoder, bool resolve);
	void recordSilhouetteDraws(wgpu::RenderPassEncoder& pass);
	void fillOccluderFade(FrameCB& cb) const;
	void encodeTonemap(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, const wgpu::BindGroup& postGroup, bool scaled);
	void registerBindGroupTables();
	void refreshBindGroups(RefreshPhase phase) { (void)m_tables.refresh(phase); }
	void writeSceneBindGroup(const DescriptorSources& sources);
	void writePostBindGroup(const DescriptorSources& sources);
	void writeFogBindGroup(const DescriptorSources& sources);
	/// 主ビューの描き込み先。副ビューを描く間は m_t が副ビューの物と入れ替わるので、組が写す物はこちらから引く
	[[nodiscard]] DawnSceneTargets& mainTargets() noexcept { return *m_mainTargets; }
	void updateSkyDrive();

	gfx::DawnDevice& m_device;
	gfx::DawnContext& m_ctx;
	bool m_initialized = false;
	bool m_frameActive = false;
	bool m_frameEnded = false;
	int m_width = 0;    ///< 出力 (最終色) の大きさ
	int m_height = 0;
	int m_sceneWidth = 0;    ///< 内部解像度。renderScale が 1 より小さいと出力より小さい
	int m_sceneHeight = 0;
	float m_renderScale = 1.0f;
	bool m_scaleDirty = false;
	std::uint64_t m_frameCounter = 0;
	bool m_wboit = false;   ///< 半透明のメッシュを WBOIT で混ぜるか。互換レベルは描画先ごとに合成を変えられないので、遠い順の混ぜのまま
	bool m_wboitCapable = false;   ///< この device が WBOIT を描けるか。新体制で oit を宣言していなければ capable でも m_wboit は false
	features::RenderNeeds m_needs;   ///< 旧体制 (declaredMode が false) は今までどおり、半透明が初めて出たフレームで蓄積先を作る
	void prepareOitNow();
	/// SMAA を選んだとき、表を持つビルドなら 1 回だけ作る。作れなければ false で、主ビューも FXAA のまま
	[[nodiscard]] bool prepareSmaa();
	float m_hdrScale = 1.0f;   ///< 中間の色が 1 で頭打ちの形式 (互換レベル) のとき、場面の色に掛けて持つ倍率。トーンマップで戻す
	wgpu::TextureFormat m_hdrFormat = wgpu::TextureFormat::RGBA16Float;   ///< 4x MSAA で描ける中間の色の形式。互換レベルでは 16bit float が使えないことがある


	float m_shakeX = 0.0f;   ///< setCameraShake の画面幅に対する割合。finalizeFrame で 0 に戻る
	float m_shakeY = 0.0f;
	Camera3D m_camera;
	Frustum m_frustum;
	sgc::Mat4f m_view;
	sgc::Mat4f m_viewProj;
	bool m_cameraSet = false;

	bool m_hasStyle = false;
	ToonRampGpu m_styleRamp{};
	ToonAccentGpu m_styleAccent{};
	ToonMetalGpu m_styleMetal{};

	DawnScenePipelines m_pipelines;
	DawnUserMaterials m_userMaterials{m_ctx, m_pipelines};
	wgpu::Buffer m_frameBuffer;
	wgpu::Buffer m_toneBuffer;
	wgpu::Buffer m_drawBuffer;
	std::uint64_t m_drawBufferBytes = 0;
	wgpu::Buffer m_instBuffer;
	std::uint64_t m_instBufferBytes = 0;
	std::unique_ptr<DawnShadowPass> m_shadowPass;
	std::unique_ptr<DawnLinePass> m_linePass;
	std::unique_ptr<DawnBloomPass> m_bloomPass;
	std::unique_ptr<DawnSkyPass> m_skyPass;
	std::unique_ptr<DawnLightingBake> m_bake;
	std::unique_ptr<DawnParticles> m_particles;
	std::unique_ptr<DawnVfxTextures> m_vfx;
	std::unique_ptr<DawnDecals> m_decals;
	std::unique_ptr<DawnLocalLights> m_localLights;
	GlobalIlluminationSettings m_gi;   ///< ゲームの頼み (flags 0 は host の既定 = 焼いた光を使う)
	std::string m_bakeWanted;
	std::unique_ptr<DawnFxaaPass> m_fxaaPass;
	std::unique_ptr<DawnSmaaPass> m_smaaPass;
	std::unique_ptr<DawnImpactPass> m_impactPass;
	std::unique_ptr<DawnColorFinishPass> m_colorFinish;   ///< 宣言 (color_looks.json) があるゲームだけ
	int m_colorLookNumber = 0;
	std::uint32_t m_colorFinishSeed = 0;
	float m_wetness = 0.0f;
	float m_cloudCover = 0.0f;
	OccluderFade m_occluderFade{};
	bool m_fadeTargetDraw = false;
	ImpactFrame m_impactFrame{};
	bool m_fxaaOn = true;   ///< 既定は DX12 と同じ MSAA + FXAA
	bool m_smaaOn = false;  ///< 主ビューを SMAA にする (表が無いビルドは立たず、FXAA のまま)
	bool m_smaaFailed = false;   ///< 作れなかった。同じ頼みが毎フレーム来ても作り直さない
	SkySettings m_sky{};
	VolumetricFogSettings m_fog{};
	wgpu::BindGroup m_fogGroup;
	SkyView m_skyView;
	sgc::Vec3f m_skyDriveSun{0.0f, 0.0f, 0.0f};    // 環境光を最後に計算した太陽の向き
	float m_skyDriveAltitude = -1.0f;              // 同じく、カメラの高度 (km)
	ToonSlotTable m_toonSlots;
	std::vector<ShadowCaster> m_casters;
	ShadowFrame m_shadowFrame;
	wgpu::BindGroup m_sceneBindGroup;
	wgpu::BindGroup m_postBindGroup;
	wgpu::Texture m_bloomOff;   ///< 副ビューのトーンマップは bloom の強さ 0 で足すので、束の欄を埋めるだけの 1 画素。主ビューの bloom の絵は結ばない
	wgpu::TextureView m_bloomOffView;
	DescriptorTableRegistry m_tables;
	DawnSceneTargets m_t;
	DawnSceneTargets* m_mainTargets = &m_t;

	DawnMeshCache m_meshCache;
	std::unique_ptr<DawnSkinning> m_skinning;
	bool m_cpuSkinForced = false;   ///< MITIRU_DAWN_CPU_SKIN=1。GPU の変形と絵を比べる試験が使う
	std::uint32_t m_paletteBase = DawnSkinning::kNoPalette;
	std::deque<Mesh> m_cpuSkinMeshes;   ///< GPU で変形できなかった体の頂点。1 体ごとに 1 つ
	std::size_t m_cpuSkinCursor = 0;
	std::vector<sgc::Mat4f> m_cpuIdentity;
	std::vector<sgc::Mat4f> m_cpuPose;
	DawnMaterialPool m_materials{m_ctx};
	DawnModelStore m_models{m_materials};
	GameMeshStore m_gameMeshes;
	std::array<DawnViewSlot, kMaxView3DSlots> m_viewSlots;
	std::unique_ptr<DawnViewBlit> m_viewBlit;
	struct ViewComposite
	{
		int slot = 0;
		float u = 0.0f, v = 0.0f, w = 0.0f, h = 0.0f;
	};
	std::vector<ViewComposite> m_viewComposites;
	std::vector<std::shared_ptr<DawnMaterialMaps>> m_viewMapsGraveyard;   ///< 手放した副ビューの材質の束。このフレームの描画の記録が指すので、次の beginFrame まで生かす
	int m_activeView = -1;           ///< beginViewSlot から endViewSlot の間だけ、描く先の slot
	bool m_renderingView = false;    ///< renderView の間 (副ビューの絵を作っている)
	bool m_viewShadows = true;       ///< いま描いている副ビューが影を受け、描いた物を影の caster に入れるか
	std::vector<DrawRecord> m_draws;
	std::vector<std::uint8_t> m_drawBytes;
	// 定数の共通の欄の使い回し。キーが前の描画と同じなら 400 byte を組み直さず、world と diffuse だけを書く
	struct DrawKey
	{
		const void* maps = nullptr;
		std::uint64_t styleVersion = 0;
		float spec[4] = {};
		float add[3] = {};
		std::uint32_t flags = 0;
		std::uint64_t userVersion = 0;   ///< 自作の材質の値が変わるたびに増える
		float wet = 0.0f;                ///< 濡れの量。トゥーンのつやの 2 値が変わる
		float reserved = 0.0f;
	};
	static_assert(sizeof(DrawKey) == 64, "DrawKey は padding なしで bit ごとに比べる");
	DrawKey m_cbKey{};
	DrawCB m_cbBase{};
	bool m_cbBaseValid = false;
	std::uint32_t m_cbTemplateId = 0;
	std::uint64_t m_styleVersion = 0;
	LinearColorTable<sgc::Colorf, 256> m_linDiffuse;
	bool m_drawMerge = true;   ///< MITIRU_DRAW_MERGE=0 でまとめ描きを切る (まとめた絵と画素を比べる試験と、切り分けに使う)
	std::vector<InstanceData> m_instances;   ///< m_draws のうち instanceCount >= 2 の物が指す列
	int m_drawCalls = 0;
	int m_culled = 0;
};

} // namespace mitiru::render::dawn_detail
