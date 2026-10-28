#pragma once

/// @file RenderFeatures.hpp
/// @brief 3D 描画の機能の表。backend ごとの対応、区分、必要な GPU の機能とビルドの条件を 1 か所に持つ (ADR 0090)。
/// @details 行を足すと、全段の対応と、対応が完全でない段の理由を書くまでコンパイルが通らない。
///          `entries` / `screenEntries` は IRenderer3D / ISceneFx / Screen の入口の名前で、tools/check_render_features.py が実際のヘッダーと突き合わせる。
///          tools/render_features_lib.py は行の書式 (1 行 1 要素) に頼って表を読み、gen_render_features_doc.py が render_features.json を作る。mitiru-cli はその JSON を読むので、ヘッダーの書き方は変えてよいが、書式を変えたら lib を直す。

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/gfx/GpuCompat.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>

namespace mitiru::render::features
{

enum class Support : std::uint8_t
{
	Unset,        ///< 書き忘れ。static_assert が弾く
	Full,         ///< 同じ手法で描く。VRT の許容内で同じ絵になる
	Alternative,  ///< 手法が違う。絵は許容内
	Reduced,      ///< 描くが見た目が落ちる
	None          ///< 描かない
};

/// 基本は常に作る。宣言は、ゲームが宣言したときだけ作る
enum class Region : std::uint8_t
{
	Core,
	Declared
};

/// 宣言の行が宣言されていないときの描き方
enum class Undeclared : std::uint8_t
{
	NotApplicable,  ///< 基本の行
	Fallback,       ///< 代わりの描き方で続ける
	Skip            ///< 描かない
};

/// ビルドで組み込む外部の部品。MITIRU_HAS_* に対応する
enum class BuildFlag : std::uint8_t
{
	None,
	Effekseer,
	Onnx,
	DirectMl,
	Fsr3
};

constexpr std::uint32_t capBit(gfx::compat::GpuCap cap) noexcept
{
	return std::uint32_t{1} << static_cast<unsigned>(cap);
}

struct FeatureRow
{
	Feature id;
	const char* key;            ///< mitiru.toml の needs と --render-features の名前
	const char* label;          ///< 端末の文に出す短い日本語
	Region region;
	Undeclared whenUndeclared;
	std::array<Support, kTierCount> support;
	const char* note;           ///< Full 以外の段があるときは空にできない。端末、診断、文書が同じ文字列を読む
	std::uint32_t gpuCaps;      ///< 要る GPU の機能 (GpuCap の bit)
	BuildFlag build;
	const char* implies;        ///< 先に作る内部の部品 (key のカンマ区切り)
	const char* entries;        ///< IRenderer3D / ISceneFx / IExperimentalRenderer3D の入口の名前 (カンマ区切り)
	const char* screenEntries;  ///< Screen の入口の名前 (カンマ区切り)
};

namespace table_def
{

using gfx::compat::GpuCap;
using enum Support;

// 1 行 1 要素。tools/render_features_lib.py が `FeatureRow{` から行末の `},` までを読む
inline constexpr std::array<FeatureRow, kFeatureCount> kRows = {{
	FeatureRow{Feature::MeshDraw, "mesh_draw", "メッシュ", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"drawMesh,drawMeshInstanced,drawMeshInstances,drawMeshTinted,drawMeshInstancesTinted,drawMeshPieces,registerGameMesh,releaseGameMesh,findGameMesh,updateGameMesh,updateGameMeshTopology,setCameraShake",
		"camera3D,light3D,drawMesh,drawMeshInstanced,drawMeshTinted,drawMeshInstancedTinted,drawMeshPieces,registerMesh3D,releaseMesh3D,hasMesh3D,updateMesh3D"},
	FeatureRow{Feature::ModelGltf, "model_gltf", "モデル", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"金属・粗さの画像は読まない。スキンは CPU (絵は許容内)",
		0, BuildFlag::None, "",
		"drawSkinnedModel,drawModelRot,drawModelPosed,drawModelInstances,drawModelAnimPose,drawModelNodeMatrices,setSkinnedLodLook,reloadModel",
		"drawModel,drawModelBlend,drawModelPose,drawModelNodeMatrices,drawModelInstanced"},
	FeatureRow{Feature::ModelRelease, "model_release", "モデルの解放", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って false を返し、手放さない (絵は変わらず、メモリが増える)",
		0, BuildFlag::None, "",
		"releaseModel",
		"releaseModel"},
	FeatureRow{Feature::ToonStyle, "toon_style", "トゥーン", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"setToonShadowTint,setToonRamp,setToonSpecular,setHemisphereAmbient,setRimLight,setToonStyle",
		"toon3D,toonStyle3D"},
	FeatureRow{Feature::ShadingModels, "shading_models", "陰影の種類", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙ってトゥーンで描く (陰影の指定と環境 cubemap を無視)",
		0, BuildFlag::None, "",
		"setShaderMode,setEnvironment",
		"environment3D"},
	FeatureRow{Feature::SunShadow, "sun_shadow", "太陽の影", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"setShadowEnabled,setShadowDirection,setShadowCaster,setCascadedShadowEnabled,isCascadedShadowEnabled,setCascadedShadowAutoFit,setShadowCascadeCount,setShadowSoftness,setShadowBias",
		"shadowCaster3D"},
	FeatureRow{Feature::Outline, "outline", "輪郭線", Region::Core, Undeclared::NotApplicable, {Full, Full, Alternative},
		"互換は線形の深度を色に書いて線を作る。方式ごとの一致は未確認",
		0, BuildFlag::None, "",
		"setOutlineEnabled,isOutlineEnabled,setOutlineMode,outlineMode,setOutlineParams,setOutlineCaster,setOutlineFade,setOutlineDarken",
		"outline3D,outlineCaster3D"},
	FeatureRow{Feature::Bloom, "bloom", "bloom", Region::Core, Undeclared::NotApplicable, {Full, Full, Reduced},
		"互換は HDR が 1 で頭打ちなので、しきい値 1 では何も拾わない",
		0, BuildFlag::None, "",
		"setBloom",
		""},
	FeatureRow{Feature::AntiAlias, "anti_alias", "アンチエイリアス", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"SMAA (aaMode 4、--aa smaa) は主ビューだけで、副ビューは FXAA。TAAU の間は掛けない。SMAA の表を入れていないビルドは FXAA",
		0, BuildFlag::None, "",
		"setAntiAliasing",
		""},
	FeatureRow{Feature::TonemapGrade, "tonemap_grade", "トーンマップと色調", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"setTonemapExposure,tonemapExposure,setTonemapGamma,tonemapGamma,setColorGrade,setFog,setAmbientColor,ambientColor",
		"fog3D,sceneLook3D"},
	FeatureRow{Feature::ColorLut, "color_lut", "色の仕上げ", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"applySceneLookExtras",
		""},
	FeatureRow{Feature::WetSurface, "wet_surface", "濡れた面", Region::Core, Undeclared::NotApplicable, {Full, Reduced, Reduced},
		"Dawn は粗さを持たないので、暗くすることと鏡面のつやを強めることだけ (PBR の粗さは下げない)。トゥーンのつやは場面の toonSpecular (既定 0) を上げたときだけ出て、既定では暗くなるだけ。DDGI が使う面の色は変わらない。地形 (OutdoorWorld) の地面は濡れない",
		0, BuildFlag::None, "",
		"applySceneLookExtras",
		""},
	FeatureRow{Feature::CloudLayer, "cloud_layer", "雲の層", Region::Core, Undeclared::NotApplicable, {Full, Alternative, Alternative},
		"物理ベースの空 (sky_atmosphere を宣言して SkyLook::enabled) の上にだけ描く。cubemap の空と、単色・グラデーションの空には描かない。Dawn の空は手法が違う (sky_atmosphere と同じ)",
		0, BuildFlag::None, "",
		"applySceneLookExtras",
		""},
	FeatureRow{Feature::HdrOutput, "hdr_output", "HDR 出力", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"固定した Dawn は色管理 (SurfaceColorManagement) を受け付けず、SDR のまま出す。頼まれたときだけ数える",
		0, BuildFlag::None, "",
		"",
		""},
	FeatureRow{Feature::SkyAtmosphere, "sky_atmosphere", "空と空気遠近", Region::Declared, Undeclared::Skip, {Full, Alternative, Alternative},
		"空気遠近は画素ごとの 10 歩の積分 (DX12 は 32^3 の froxel)。setSkyboxEnabled(false) は効かない",
		capBit(GpuCap::TypedUavStore), BuildFlag::None, "",
		"setSky,setSkybox,setSkyboxEnabled,isSkyboxEnabled",
		"skybox3D"},
	FeatureRow{Feature::VolumetricFog, "volumetric_fog", "体積フォグ", Region::Declared, Undeclared::Skip, {Full, Alternative, Alternative},
		"画素ごとの 24 歩の積分。時間方向の混ぜと局所光の散乱は無い",
		0, BuildFlag::None, "sky_atmosphere",
		"setVolumetricFog",
		""},
	FeatureRow{Feature::BakedGi, "baked_gi", "焼いた間接光", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"requestLightingBake,setIndirectLightLook",
		"lightingBake3D"},
	FeatureRow{Feature::ReflectionProbes, "reflection_probes", "反射のプローブ", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って無視 (IndirectLightLook の flag)",
		0, BuildFlag::None, "",
		"setIndirectLightLook",
		""},
	FeatureRow{Feature::Ssr, "ssr", "画面の反射", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って無視 (IndirectLightLook の flag)",
		capBit(GpuCap::TypedUavStore), BuildFlag::None, "",
		"setIndirectLightLook",
		""},
	FeatureRow{Feature::Ddgi, "ddgi", "動く間接光", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って焼いた光のまま (IndirectLightLook の flag)",
		capBit(GpuCap::Dxr11) | capBit(GpuCap::ShaderModel66) | capBit(GpuCap::BindingTier3) | capBit(GpuCap::TypedUavLoad),
		BuildFlag::None, "",
		"setIndirectLightLook",
		""},
	FeatureRow{Feature::LocalLights, "local_lights", "局所光", Region::Core, Undeclared::NotApplicable, {Full, Reduced, Reduced},
		"全灯を画素ごとに回す (256 灯まで)。スポットの影は無い",
		0, BuildFlag::None, "",
		"submitLocalLights",
		"localLights3D,pointLight3D,spotLight3D"},
	FeatureRow{Feature::MultiLight, "multi_light", "複数の光", Region::Core, Undeclared::NotApplicable, {Full, Reduced, Reduced},
		"先頭の 1 灯だけを使う (インターフェースの注記どおり。setUseMultiLight は何もしない)",
		0, BuildFlag::None, "",
		"setLights,setUseMultiLight,useMultiLight",
		""},
	FeatureRow{Feature::PointShadow, "point_shadow", "点光源の影", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って影なし (castShadow を付けた点光源の影を描かない。Dawn は局所光が先頭 1 灯だけ)",
		0, BuildFlag::None, "",
		"",
		""},
	FeatureRow{Feature::AmbientOcclusion, "ambient_occlusion", "AO", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って無視",
		0, BuildFlag::None, "",
		"setAmbientOcclusion,setAmbientOcclusionMethod",
		""},
	FeatureRow{Feature::DepthOfField, "depth_of_field", "被写界深度", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って無視",
		0, BuildFlag::None, "",
		"setDepthOfField",
		""},
	FeatureRow{Feature::AutoExposure, "auto_exposure", "自動露出", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って固定の露出 (SceneLook::autoExposureSpeed を無視)",
		0, BuildFlag::None, "",
		"applySceneLookExtras",
		""},
	FeatureRow{Feature::ContactShadow, "contact_shadow", "接触影", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って影なし (SceneLook::contactShadow を無視)",
		0, BuildFlag::None, "ambient_occlusion",
		"applySceneLookExtras",
		""},
	FeatureRow{Feature::TaaMotionVectors, "taa_motion_vectors", "TAA と動きベクトル", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って MSAA だけで描く (aaMode に TAA を頼んでも)",
		0, BuildFlag::None, "",
		"setAntiAliasing,setMotionKey,setMotionVectorCaster,resetTemporalHistory",
		"setMotionKey,setMotionVectorCaster"},
	FeatureRow{Feature::MotionBlur, "motion_blur", "動きのぼけ", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って無視",
		0, BuildFlag::None, "taa_motion_vectors",
		"setMotionBlur",
		""},
	FeatureRow{Feature::RenderScale, "render_scale", "内部解像度", Region::Declared, Undeclared::Fallback, {Full, Reduced, Reduced},
		"倍率は双線形 + FXAA で引き伸ばす。影の大きさの依頼は黙って無視",
		0, BuildFlag::None, "taa_motion_vectors",
		"setRequestedRenderScale,setRequestedShadowMapSize",
		""},
	FeatureRow{Feature::Fsr3, "fsr3", "FSR 3.1", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って双線形 + FXAA",
		0, BuildFlag::Fsr3, "render_scale",
		"",
		""},
	FeatureRow{Feature::Oit, "oit", "半透明の WBOIT", Region::Declared, Undeclared::Fallback, {Full, Full, Reduced},
		"互換は遠い順の混ぜ",
		0, BuildFlag::None, "",
		"",
		""},
	FeatureRow{Feature::Decals, "decals", "デカール", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"froxel の絞りが無く、全部を画素ごとに回す",
		0, BuildFlag::None, "",
		"submitDecals",
		"decals3D"},
	FeatureRow{Feature::GpuParticles, "gpu_particles", "粒", Region::Core, Undeclared::NotApplicable, {Full, Reduced, Reduced},
		"CPU で進める板ポリ。深度との衝突は無い",
		0, BuildFlag::None, "",
		"submitParticles,submitParticlesShaded",
		"particles3D"},
	FeatureRow{Feature::VfxTextures, "vfx_textures", "VFX テクスチャ", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"registerVfxTextureFile",
		"vfxTexture3D"},
	FeatureRow{Feature::HitFeel, "hit_feel", "ヒットの演出", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って何もしない",
		0, BuildFlag::None, "",
		"setHitFeel",
		"hitFeel3D"},
	FeatureRow{Feature::ImpactFrame, "impact_frame", "止め絵", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"FXAA の後に掛ける (Dawn に TAAU が無い。絵は許容内、ADR 0087 §20)",
		0, BuildFlag::None, "",
		"setImpactFrame",
		"impactFrame3D"},
	FeatureRow{Feature::OccluderFade, "occluder_fade", "透かし", Region::Core, Undeclared::NotApplicable, {Full, Full, Alternative},
		"互換は標本ごとの比べができず、4 標本の距離の割合で濃さを決める (影絵の縁が少し滑らか、ADR 0087 §20)",
		0, BuildFlag::None, "",
		"setOccluderFade,setFadeTarget",
		"fadeOccluders3D,fadeTarget3D"},
	FeatureRow{Feature::Trails, "trails", "剣筋", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って何も描かない",
		0, BuildFlag::None, "",
		"drawTrail",
		"drawTrail"},
	FeatureRow{Feature::ScreenProjection, "screen_projection", "3D の点の画面投影", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って false を返す (投影が出ない。3D の debug draw が出ない)",
		0, BuildFlag::None, "",
		"worldToScreen",
		"projectToScreen"},
	FeatureRow{Feature::OutdoorWorld, "outdoor_world", "屋外の地形・草・水", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って何も描かない (地形・草・水が出ない)",
		0, BuildFlag::None, "",
		"drawOutdoor",
		"drawOutdoor"},
	FeatureRow{Feature::Clod, "clod", "仮想ジオメトリ", Region::Declared, Undeclared::Skip, {Full, None, None},
		".clod / .obj のモデルを描かない (drawModel も、drawSkinnedModel に渡した .clod も)",
		capBit(GpuCap::MeshShader) | capBit(GpuCap::Int64Ops) | capBit(GpuCap::ShaderModel66), BuildFlag::None, "",
		"drawModel",
		"drawModel"},
	FeatureRow{Feature::ViewSlots, "view_slots", "副ビュー", Region::Core, Undeclared::NotApplicable, {Full, Reduced, Reduced},
		"粒、デカール、局所光、フォグ、物理ベースの空、bloom、輪郭線が副ビューに出ない",
		0, BuildFlag::None, "",
		"setViewSlot,beginViewSlot,endViewSlot,compositeViewSlot,drawMeshWithViewSlot,releaseViewSlot",
		"view3D,beginView3D,endView3D,compositeView3D,drawMeshWithView3D,releaseView3D"},
	FeatureRow{Feature::Effekseer, "effekseer", "Effekseer", Region::Declared, Undeclared::Skip, {Full, None, None},
		".efkefc のエフェクトを描かない",
		0, BuildFlag::Effekseer, "",
		"",
		""},
	FeatureRow{Feature::NeuralFx, "neural_fx", "ニューラルの後処理", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って何もしない (experimental() が空)",
		0, BuildFlag::DirectMl, "",
		"enableNeuralFx,enableRelight",
		"enableNeuralFx,enableRelight"},
	FeatureRow{Feature::NeuralStyleDepth, "neural_style_depth", "ONNX の後処理", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って何もしない (experimental() が空)",
		0, BuildFlag::Onnx, "",
		"setRelightDepthModel,requestDevelop,tickDevelop,clearDevelop,styleReady,styleImageData,styleImageW,styleImageH,setStyleStrength",
		"setRelightDepthModel,developToStyle,styleReady,clearStyle,drawStyle"},
	FeatureRow{Feature::SplatLive2d, "splat_live2d", "スプラットと Live2D", Region::Declared, Undeclared::Skip, {Full, None, None},
		"黙って何もしない (experimental() が空)",
		0, BuildFlag::None, "",
		"loadSplatScene,drawSplats,splatBounds,drawLive2D,live2dLookAt,live2dTap,live2dStage,drawSolid,bakeStyleToSplats,resetSplatColors,bakedFraction,captureTargetFromStyle,setShowTarget,hasTarget,matchScore",
		"loadSplatScene,splatBounds,drawSplats,drawLive2D,live2dLookAt,live2dTap,live2dStage,drawSolid,bakeStyle,resetSplats,bakedFraction,captureTarget,showTarget,hasTarget,matchScore"},
	FeatureRow{Feature::UserMaterial, "user_material", "自作の材質", Region::Declared, Undeclared::Skip, {Full, Full, Reduced},
		"Dawn はトゥーンの陰影だけ (場面が Phong・PBR でも)で、tools/bake_materials.py で焼いた .wgsl を読む。半透明の描画には効かない。alpha を色に混ぜるのは DX12 と Dawn 通常で、互換は不透明のまま描く",
		0, BuildFlag::None, "",
		"registerMaterial3D,setUserMaterial",
		"registerMaterial3D,material3D"},
	FeatureRow{Feature::MaterialLayers, "material_layers", "PBR の層", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"黙って層なし (glTF のクリアコート・異方性・シーンを読んでも描かない。Dawn に PBR の陰影が無い)",
		0, BuildFlag::None, "",
		"",
		""},
	FeatureRow{Feature::Light2d, "light_2d", "2D ライト", Region::Declared, Undeclared::Skip, {Full, Full, Full},
		"光は描く順に効き (呼ぶ前に描いた物には当たらない)、法線マップは sprite の id に _n を付けた同じ大きさの画像。クリアの色、SDF の角丸・円、3D と同じフレームの 2D、CPU 描画には当たらない。DX11 は対象外。Dawn は光の間だけ画像を GPU で描く (光なしは今までどおり)",
		0, BuildFlag::None, "",
		"",
		"light2D,ambient2D"},
	FeatureRow{Feature::MorphTargets, "morph_targets", "モーフターゲット", Region::Core, Undeclared::NotApplicable, {Full, Full, Full},
		"",
		0, BuildFlag::None, "model_gltf",
		"drawModelMorph",
		""},
	FeatureRow{Feature::HdrEnvironment, "hdr_environment", "HDR の環境", Region::Core, Undeclared::NotApplicable, {Full, None, None},
		"今の環境光と空のまま描き、1 度だけ知らせる (Dawn に PBR の陰影と環境の cubemap が無い)。DX12 は RGBA16F の cubemap に写し、1 を超える明るさを空と環境光に保つ",
		0, BuildFlag::None, "shading_models",
		"setEnvironmentHdr",
		"environmentHdr3D"},
	FeatureRow{Feature::No3d, "no_3d", "3D を描かない", Region::Declared, Undeclared::Fallback, {Full, Full, Full},
		"",
		0, BuildFlag::None, "",
		"",
		""},
}};

}  // namespace table_def

inline constexpr const std::array<FeatureRow, kFeatureCount>& kFeatureTable = table_def::kRows;

[[nodiscard]] constexpr const FeatureRow& row(Feature f) noexcept
{
	return kFeatureTable[static_cast<std::size_t>(f)];
}

[[nodiscard]] constexpr const char* key(Feature f) noexcept { return row(f).key; }

[[nodiscard]] constexpr std::optional<Feature> findFeature(std::string_view k) noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		if (k == r.key) { return r.id; }
	}
	return std::nullopt;
}

[[nodiscard]] constexpr Support supportAt(Feature f, Tier t) noexcept
{
	return row(f).support[static_cast<std::size_t>(t)];
}

/// @brief 宣言が「初期化で作るか」を実際に決める行か。表の区分が宣言でも、作る時期の付け替えを済ませた行だけが true になる。
/// @details 付け替え前の宣言の行は宣言しても作る時期が変わらないので、宣言していなくても未宣言の知らせを出さない。
///          `render_scale` は自分の資源を持たないが、TAAU が TAA の動きベクトルと履歴を要るので、宣言が TAA の部品を引き込む (implies)。
///          Dawn の互換は WBOIT を持たず (遠い順の混ぜ)、宣言で作る物は 2D ライトだけ。
[[nodiscard]] constexpr bool creationGated(Feature f, Tier t) noexcept
{
	switch (t)
	{
	case Tier::Dx12:
		return f == Feature::Oit || f == Feature::Clod || f == Feature::TaaMotionVectors || f == Feature::MotionBlur ||
		       f == Feature::AmbientOcclusion || f == Feature::Effekseer || f == Feature::RenderScale || f == Feature::PointShadow ||
		       f == Feature::Light2d || f == Feature::AutoExposure || f == Feature::ContactShadow;
	case Tier::DawnCore:
		return f == Feature::Oit || f == Feature::Light2d;
	default:
		return f == Feature::Light2d;
	}
}

/// カンマ区切りの語を 1 つずつ渡す。語の前後の空白は落とす
template <class Fn>
constexpr void forEachWord(std::string_view list, Fn&& fn)
{
	std::size_t pos = 0;
	while (pos <= list.size())
	{
		const std::size_t comma = list.find(',', pos);
		std::string_view word = list.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
		pos = (comma == std::string_view::npos) ? list.size() + 1 : comma + 1;
		while (!word.empty() && (word.front() == ' ' || word.front() == '\t')) { word.remove_prefix(1); }
		while (!word.empty() && (word.back() == ' ' || word.back() == '\t')) { word.remove_suffix(1); }
		if (!word.empty()) { fn(word); }
	}
}

namespace detail
{

constexpr bool nonEmpty(const char* s) noexcept { return s != nullptr && s[0] != '\0'; }

constexpr bool idsMatchIndex() noexcept
{
	for (std::size_t i = 0; i < kFeatureCount; ++i)
	{
		if (static_cast<std::size_t>(kFeatureTable[i].id) != i) { return false; }
	}
	return true;
}

constexpr bool everyRowIsNamed() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		if (!nonEmpty(r.key) || !nonEmpty(r.label)) { return false; }
	}
	return true;
}

constexpr bool keysAreUnique() noexcept
{
	for (std::size_t i = 0; i < kFeatureCount; ++i)
	{
		for (std::size_t j = i + 1; j < kFeatureCount; ++j)
		{
			if (std::string_view{kFeatureTable[i].key} == kFeatureTable[j].key) { return false; }
		}
	}
	return true;
}

constexpr bool everyTierIsFilled() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		for (const Support s : r.support)
		{
			if (s == Support::Unset) { return false; }
		}
	}
	return true;
}

constexpr bool dx12IsFull() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		if (r.support[static_cast<std::size_t>(Tier::Dx12)] != Support::Full) { return false; }
	}
	return true;
}

/// Alternative / Reduced / None の段を持つ行は理由を書く。書かないと利用者に何も伝わらない
constexpr bool gapsHaveReason() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		for (const Support s : r.support)
		{
			if (s != Support::Full && !nonEmpty(r.note)) { return false; }
		}
	}
	return true;
}

constexpr bool policyMatchesRegion() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		const bool declared = r.region == Region::Declared;
		if (declared == (r.whenUndeclared == Undeclared::NotApplicable)) { return false; }
	}
	return true;
}

/// 外部の部品を要る行は宣言の行にする。基本の行が、ビルドに無いだけで作れなくなるのを避ける
constexpr bool buildRowsAreDeclared() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		if (r.build != BuildFlag::None && r.region != Region::Declared) { return false; }
	}
	return true;
}

/// implies の語が、自分以外の実在する行を指すか
constexpr bool impliesResolve() noexcept
{
	bool ok = true;
	for (const FeatureRow& r : kFeatureTable)
	{
		forEachWord(r.implies == nullptr ? std::string_view{} : std::string_view{r.implies}, [&](std::string_view w) {
			const std::optional<Feature> f = findFeature(w);
			if (!f || *f == r.id) { ok = false; }
		});
	}
	return ok;
}

/// implies をたどって自分に戻る行が無いか。行数を超える回数たどれば、戻らない鎖は尽きている
constexpr bool impliesHaveNoCycle() noexcept
{
	for (const FeatureRow& start : kFeatureTable)
	{
		std::uint64_t reach = 0;
		std::uint64_t frontier = std::uint64_t{1} << static_cast<unsigned>(start.id);
		for (std::size_t step = 0; step < kFeatureCount && frontier != 0; ++step)
		{
			std::uint64_t next = 0;
			for (const FeatureRow& r : kFeatureTable)
			{
				if ((frontier & (std::uint64_t{1} << static_cast<unsigned>(r.id))) == 0) { continue; }
				forEachWord(r.implies == nullptr ? std::string_view{} : std::string_view{r.implies}, [&](std::string_view w) {
					if (const std::optional<Feature> f = findFeature(w)) { next |= std::uint64_t{1} << static_cast<unsigned>(*f); }
				});
			}
			if ((next & (std::uint64_t{1} << static_cast<unsigned>(start.id))) != 0) { return false; }
			frontier = next & ~reach;
			reach |= next;
		}
	}
	return true;
}

}  // namespace detail

static_assert(kFeatureTable.size() == kFeatureCount, "表の行数は Feature の数と同じ");
static_assert(detail::idsMatchIndex(), "表の行の並びが Feature の並びと違う");
static_assert(detail::everyRowIsNamed(), "key か label が空の行がある");
static_assert(detail::keysAreUnique(), "key が重なっている行がある");
static_assert(detail::everyTierIsFilled(), "backend の段の対応が書かれていない行がある (Support::Unset)");
static_assert(detail::dx12IsFull(), "DX12 は全部の行を対応にする (新しい機能は DX12 から出す)");
static_assert(detail::gapsHaveReason(), "対応が完全でない段のある行に note が無い");
static_assert(detail::policyMatchesRegion(), "宣言の行は whenUndeclared を決め、基本の行は NotApplicable にする");
static_assert(detail::buildRowsAreDeclared(), "ビルドの条件を持つ行は宣言の行にする");
static_assert(detail::impliesResolve(), "implies が存在しない key か自分自身を指している");
static_assert(detail::impliesHaveNoCycle(), "implies が循環している");

namespace detail
{

/// 作る時期を宣言に結んだ行は宣言の行でなければならない。基本の行は宣言なしで常に作る
constexpr bool gatedRowsAreDeclared() noexcept
{
	for (const FeatureRow& r : kFeatureTable)
	{
		for (std::size_t t = 0; t < kTierCount; ++t)
		{
			if (creationGated(r.id, static_cast<Tier>(t)) && r.region != Region::Declared) { return false; }
		}
	}
	return true;
}

}  // namespace detail

static_assert(detail::gatedRowsAreDeclared(), "作る時期を宣言に結んだ行は、宣言の行にする");

// ── 状態の決め方 (ADR 0090 の 3.3) ──────────────────────────────

enum class FeatureState : std::uint8_t
{
	Active,         ///< 作って描く。Alternative / Reduced でもここ
	Undeclared,     ///< 新体制で、宣言の行が宣言されていない
	NoBuild,        ///< ビルドに部品が入っていない
	NoGpu,          ///< GPU に機能が無い
	Unsupported,    ///< この段が描かない
	QualityCapped   ///< 利用者が選んだ画質の上限が許さない
};

using FeatureSet = std::bitset<kFeatureCount>;

/// 今のビルドが持つ部品。MITIRU_HAS_* と対応する
[[nodiscard]] constexpr bool buildHas(BuildFlag b) noexcept
{
	switch (b)
	{
	case BuildFlag::None: return true;
#ifdef MITIRU_HAS_EFFEKSEER
	case BuildFlag::Effekseer: return true;
#endif
#ifdef MITIRU_HAS_ONNX
	case BuildFlag::Onnx: return true;
#endif
#ifdef MITIRU_HAS_DIRECTML
	case BuildFlag::DirectMl: return true;
#endif
#ifdef MITIRU_HAS_FSR3
	case BuildFlag::Fsr3: return true;
#endif
	default: return false;
	}
}

struct StateContext
{
	Tier tier = Tier::Dx12;
	bool declaredMode = false;          ///< 宣言を出した DLL (新体制)。段階 A では常に false
	FeatureSet declared;                ///< declaredMode のときだけ読む
	std::uint32_t haveGpuCaps = ~std::uint32_t{0};
	bool (*hasBuild)(BuildFlag) = nullptr;   ///< nullptr なら buildHas
	FeatureSet qualityCapped;
};

[[nodiscard]] constexpr FeatureState resolveState(Feature f, const StateContext& ctx) noexcept
{
	const FeatureRow& r = row(f);
	if (ctx.declaredMode && creationGated(f, ctx.tier) && !ctx.declared.test(static_cast<std::size_t>(f))) { return FeatureState::Undeclared; }
	const bool built = ctx.hasBuild != nullptr ? ctx.hasBuild(r.build) : buildHas(r.build);
	if (!built) { return FeatureState::NoBuild; }
	if ((r.gpuCaps & ~ctx.haveGpuCaps) != 0) { return FeatureState::NoGpu; }
	if (supportAt(f, ctx.tier) == Support::None) { return FeatureState::Unsupported; }
	if (ctx.qualityCapped.test(static_cast<std::size_t>(f))) { return FeatureState::QualityCapped; }
	return FeatureState::Active;
}

// ── needs の文字列 ─────────────────────────────────────────────

struct NeedsParse
{
	FeatureSet declared;
	std::vector<std::string> unknown;        ///< 表に無い名前。綴りの誤りを黙って無視しない
	std::vector<std::string> notDeclarable;  ///< 基本の行の名前。宣言しなくても常に作る
};

[[nodiscard]] inline NeedsParse parseNeeds(std::string_view text)
{
	NeedsParse out;
	forEachWord(text, [&](std::string_view w) {
		const std::optional<Feature> f = findFeature(w);
		if (!f) { out.unknown.emplace_back(w); return; }
		if (row(*f).region != Region::Declared) { out.notDeclarable.emplace_back(w); return; }
		out.declared.set(static_cast<std::size_t>(*f));
	});
	return out;
}

/// @brief 宣言した行が先に作る部品 (implies) を、鎖の先まで足す。作る順は表の並びが決めるので、ここでは集合だけを返す
[[nodiscard]] constexpr FeatureSet withImplied(FeatureSet declared) noexcept
{
	for (std::size_t round = 0; round < kFeatureCount; ++round)
	{
		FeatureSet next = declared;
		for (const FeatureRow& r : kFeatureTable)
		{
			if (!declared.test(static_cast<std::size_t>(r.id))) { continue; }
			forEachWord(r.implies == nullptr ? std::string_view{} : std::string_view{r.implies}, [&](std::string_view w) {
				if (const std::optional<Feature> f = findFeature(w)) { next.set(static_cast<std::size_t>(*f)); }
			});
		}
		if (next == declared) { break; }
		declared = next;
	}
	return declared;
}

/// @brief 新体制の宣言。未知の名前と基本の行の名前は parseNeeds が返すので、呼び出し側が知らせる
[[nodiscard]] inline RenderNeeds makeRenderNeeds(const NeedsParse& parsed)
{
	RenderNeeds out;
	out.declaredMode = true;
	out.declared = withImplied(parsed.declared);
	return out;
}

// ── 知らせの決め方 (ADR 0090 の 3.4) ───────────────────────────

enum class NoticeLevel : std::uint8_t
{
	None,     ///< 端末に出さない
	Detail,   ///< 詳細を出す実行でだけ出す
	Notice    ///< 常に出す
};

struct NoticeInput
{
	FeatureState state = FeatureState::Active;
	Support support = Support::Full;   ///< 状態を決めた段での対応
	bool playerRun = false;            ///< 遊ぶ人の実行 (窓つきで、台本もリプレイも使わない)
};

struct NoticeDecision
{
	NoticeLevel console = NoticeLevel::None;
	bool report = false;   ///< gpu_report.txt に書く
};

/// 遊ぶ人は欠けを直せないので、端末には出さず診断ファイルに残す。作る人には直す手がかりとして 1 行出す
[[nodiscard]] constexpr NoticeDecision decideFeatureNotice(const NoticeInput& in) noexcept
{
	switch (in.state)
	{
	case FeatureState::Active:
		if (in.support == Support::Alternative || in.support == Support::Reduced) { return {NoticeLevel::Detail, true}; }
		return {NoticeLevel::None, false};
	case FeatureState::QualityCapped:
		return {NoticeLevel::None, true};
	case FeatureState::Undeclared:
	case FeatureState::NoBuild:
	case FeatureState::NoGpu:
	case FeatureState::Unsupported:
		return {in.playerRun ? NoticeLevel::Detail : NoticeLevel::Notice, true};
	}
	return {};
}

[[nodiscard]] constexpr const char* tierName(Tier t) noexcept
{
	switch (t)
	{
	case Tier::Dx12: return "DX12";
	case Tier::DawnCore: return "Dawn";
	case Tier::DawnCompat: return "Dawn (互換)";
	default: return "?";
	}
}

[[nodiscard]] constexpr const char* tierJsonName(Tier t) noexcept
{
	switch (t)
	{
	case Tier::Dx12: return "dx12";
	case Tier::DawnCore: return "dawn";
	case Tier::DawnCompat: return "dawn_compat";
	default: return "?";
	}
}

[[nodiscard]] constexpr const char* supportLabel(Support s) noexcept
{
	switch (s)
	{
	case Support::Full: return "対応";
	case Support::Alternative: return "別手法";
	case Support::Reduced: return "弱い";
	case Support::None: return "なし";
	default: return "?";
	}
}

[[nodiscard]] constexpr const char* supportJsonName(Support s) noexcept
{
	switch (s)
	{
	case Support::Full: return "full";
	case Support::Alternative: return "alternative";
	case Support::Reduced: return "reduced";
	case Support::None: return "none";
	default: return "unset";
	}
}

/// 端末と診断ファイルの 1 文。docs/CONSOLE_OUTPUT.md の書き方 (1 文、「。」なし) に合わせる
[[nodiscard]] inline std::string featureNoticeText(Feature f, FeatureState state, Tier tier)
{
	const FeatureRow& r = row(f);
	std::string out;
	switch (state)
	{
	case FeatureState::Undeclared:
		out = std::string(r.key) + " を [render] needs に足すまで、" + r.label +
		      (r.whenUndeclared == Undeclared::Fallback ? "は代わりの描き方で続けます" : "は描きません");
		break;
	case FeatureState::NoBuild:
		out = std::string(r.key) + " を組み込んでいないので、" + r.label + "を描けません";
		break;
	case FeatureState::NoGpu:
		out = std::string("この GPU では ") + r.label + " (" + r.key + ") を描けません";
		break;
	case FeatureState::Unsupported:
		out = std::string(tier == Tier::Dx12 ? "DX12" : "Dawn") + " では " + r.key + " (" + r.label + ") を描けません";
		break;
	case FeatureState::QualityCapped:
		out = std::string("画質の上限で ") + r.label + " (" + r.key + ") を切っています";
		break;
	case FeatureState::Active:
		out = std::string(tierName(tier)) + " の " + r.label + " (" + r.key + ") は " + supportLabel(supportAt(f, tier)) + ": " + r.note;
		break;
	}
	return out;
}

}  // namespace mitiru::render::features
