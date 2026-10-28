#include <mitiru/render/Renderer3D_Dawn.hpp>

#include <cstddef>
#include <cstdlib>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/BootTrace.hpp>
#include <mitiru/render/Skinning.hpp>

#include "DawnProfile.hpp"
#include "DawnRenderer3D.hpp"

namespace mitiru::render::dawn_detail
{

void DawnRenderer3D::setToonStyle(const ToonStyle* style)
{
	m_hasStyle = style != nullptr;
	if (!m_hasStyle) { return; }
	const int slot = m_toonSlots.slotFor(*style);
	if (m_toonSlots.overflowed())
	{
		debug::warnOnce("dawn.toon.slots", "線の指定は 1 フレームに 7 個までなので、超えた分は場面の線で描きます");
	}
	const ToonRampGpu ramp = makeToonRampGpu(*style, slot);
	const ToonAccentGpu accent = makeToonAccentGpu(*style);
	const ToonMetalGpu metal = makeToonMetalGpu(*style);
	// 前と同じ線なら定数の共通の欄の使い回しを切らない (描画ごとに同じ線を指定するゲームが多い)
	if (std::memcmp(&ramp, &m_styleRamp, sizeof(ramp)) != 0 || std::memcmp(&accent, &m_styleAccent, sizeof(accent)) != 0 ||
	    std::memcmp(&metal, &m_styleMetal, sizeof(metal)) != 0)
	{
		m_styleRamp = ramp;
		m_styleAccent = accent;
		m_styleMetal = metal;
		++m_styleVersion;
	}
}

void DawnRenderer3D::setLight(const Light& light)
{
	m_look.light = light;
}

// 画質設定が指定する内部解像度倍率。0 は利用者設定の等倍。倍率が変わったときは次の beginFrame で作り直す
void DawnRenderer3D::setRequestedRenderScale(float scale)
{
	if (const std::string forced = env::value("MITIRU_RENDER_SCALE"); !forced.empty())
	{
		const float value = static_cast<float>(std::atof(forced.c_str()));
		scale = value >= 1.0f ? 0.0f : value;
	}
	const float requested = (scale > 0.0f) ? std::clamp(scale, 0.25f, 1.0f) : 1.0f;
	if (requested == m_renderScale) { return; }
	m_renderScale = requested;
	m_scaleDirty = true;
}

// 目と注視点を画面の右と上へ平行移動する。DX12 の setCamera と同じ。注視点より手前は大きく、奥は小さく動く
Camera3D DawnRenderer3D::shaken(const Camera3D& requested) const
{
	if (m_shakeX == 0.0f && m_shakeY == 0.0f) { return requested; }
	const sgc::Vec3f eye = requested.position();
	const sgc::Vec3f target = requested.target();
	const sgc::Vec3f forward = (target - eye).normalized();
	const sgc::Vec3f right = forward.cross(requested.up()).normalized();
	const sgc::Vec3f up = right.cross(forward);
	const float viewH = 2.0f * (target - eye).length() * std::tan(requested.fov() * 0.5f);
	const float viewW = viewH * requested.aspectRatio();
	const sgc::Vec3f shift = right * (-m_shakeX * viewW) + up * (m_shakeY * viewH);
	return Camera3D(eye + shift, target + shift, requested.up(), requested.fov(), requested.aspectRatio(), requested.nearClip(),
	                requested.farClip());
}

void DawnRenderer3D::setCamera(const Camera3D& requested)
{
	const Camera3D camera = shaken(requested);
	m_camera = camera;
	m_view = camera.viewMatrix();
	m_viewProj = camera.viewProjectionMatrixZO();
	m_frustum.extractFromCamera(camera);
	m_cameraSet = true;
	m_models.setView(camera.position(), camera.fov());
}

// ── 静的な GPU の物 ──

// 検証エラーの有無で判定する。互換レベルの D3D11 は RGBA16Float の MSAA に対応しないことがある
bool DawnRenderer3D::supportsMsaaColor(wgpu::TextureFormat format) const
{
	const auto& dev = m_ctx.device;
	dev.PushErrorScope(wgpu::ErrorFilter::Validation);
	wgpu::TextureDescriptor td{};
	td.size = {16, 16, 1};
	td.sampleCount = kSamples;
	td.format = format;
	td.usage = wgpu::TextureUsage::RenderAttachment;
	wgpu::Texture msaa = dev.CreateTexture(&td);
	td.sampleCount = 1;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	wgpu::Texture resolved = dev.CreateTexture(&td);
	bool ok = false;
	bool done = false;
	dev.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
		[&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView) {
			ok = type == wgpu::ErrorType::NoError;
			done = true;
		});
	return m_ctx.pump(done, 5000) && ok;
}

wgpu::TextureFormat DawnRenderer3D::chooseHdrFormat() const
{
	for (const wgpu::TextureFormat candidate : {wgpu::TextureFormat::RGBA16Float, wgpu::TextureFormat::RG11B10Ufloat,
	                                            wgpu::TextureFormat::RGB10A2Unorm})
	{
		if (supportsMsaaColor(candidate)) { return candidate; }
	}
	debug::verboseOnce("dawn.hdr.fallback", "Dawn: HDR の中間の色を 4x MSAA で描ける形式が無いので、8bit の色で描きます (明るい所は白飛びします)。");
	return wgpu::TextureFormat::RGBA8Unorm;
}

bool DawnRenderer3D::createStaticObjects()
{
	debug::boottrace::mark("dawn_r3d_begin");
	m_hdrFormat = chooseHdrFormat();
	debug::boottrace::mark("dawn_r3d_hdr_format");
	const bool clamped = m_hdrFormat == wgpu::TextureFormat::RGB10A2Unorm || m_hdrFormat == wgpu::TextureFormat::RGBA8Unorm;
	m_hdrScale = clamped ? kClampedHdrScale : 1.0f;
	console::verbosef("Dawn: 3D の中間の色の形式 %u (4x MSAA)", static_cast<unsigned>(m_hdrFormat));
	// 蓄積の reveal (R16Float) は中間の色と別に 4x MSAA で描けるかを確かめる。MITIRU_DAWN_WBOIT=0 は遠い順の混ぜに戻す (差の測定用)
	m_wboitCapable = !m_ctx.compat && m_hdrFormat == kOitAccumFormat && supportsMsaaColor(kOitRevealFormat) && env::value("MITIRU_DAWN_WBOIT") != "0";
	// 新体制で oit を宣言していなければ、蓄積先 (4x MSAA の 2 枚) を持たず、遠い順の混ぜで描く
	m_wboit = m_wboitCapable && m_needs.wants(features::Feature::Oit);
	console::verbosef("Dawn: 半透明のメッシュは %s", m_wboit ? "WBOIT" : "遠い順の混ぜ");
	m_cpuSkinForced = env::value("MITIRU_DAWN_CPU_SKIN") == "1";
	m_drawMerge = env::value("MITIRU_DRAW_MERGE") != "0";
	m_skinning = std::make_unique<DawnSkinning>(m_ctx);
	if (!m_cpuSkinForced && !m_skinning->create())
	{
		debug::warnOnce("dawn.skin.compute", "Dawn: compute シェーダーを使えないので、スキンの頂点は CPU で変形します (遅くなります)");
	}
	// モーフを持つモデルを読み終えたときに、モーフ用の compute を作る (描くときには作らない)
	m_models.setOnMorphModel([this] { (void)m_skinning->createMorphPipeline(); });
	debug::boottrace::mark("dawn_r3d_skinning");
	if (!m_materials.create()) { return false; }
	if (!m_pipelines.create(m_hdrFormat, m_device.target().format, m_materials.layout())) { return false; }
	// 場面のパイプラインは Dawn の作業スレッドで作り始め、残りの起動の仕事 (他のパスの作成) と並べる
	m_pipelines.prewarmScene();

	debug::boottrace::mark("dawn_r3d_scene_pipelines");
	m_shadowPass = std::make_unique<DawnShadowPass>(m_ctx);
	const DirectionalShadowConfig shadowDefaults;
	if (!m_shadowPass->create(shadowDefaults.mapSize, shadowDefaults.farMapSize)) { return false; }
	debug::boottrace::mark("dawn_r3d_shadow");
	m_linePass = std::make_unique<DawnLinePass>(m_ctx, m_device.target().format);
	if (!m_linePass->create()) { return false; }
	m_bloomPass = std::make_unique<DawnBloomPass>(m_ctx, m_hdrFormat);
	if (!m_bloomPass->create()) { return false; }
	m_bake = std::make_unique<DawnLightingBake>(m_ctx);
	if (!m_bake->create()) { return false; }
	m_vfx = std::make_unique<DawnVfxTextures>(m_ctx);
	if (!m_vfx->create()) { return false; }
	m_decals = std::make_unique<DawnDecals>(m_ctx);
	if (!m_decals->create()) { return false; }
	m_localLights = std::make_unique<DawnLocalLights>(m_ctx);
	if (!m_localLights->create()) { return false; }
	debug::boottrace::mark("dawn_r3d_passes_a");
	m_particles = std::make_unique<DawnParticles>(m_ctx, m_hdrFormat);
	if (!m_particles->create()) { return false; }
	m_skyPass = std::make_unique<DawnSkyPass>(m_ctx, m_hdrFormat);
	if (!m_skyPass->create()) { return false; }
	m_fxaaPass = std::make_unique<DawnFxaaPass>(m_ctx, m_device.target().format);
	if (!m_fxaaPass->create()) { return false; }
	m_impactPass = std::make_unique<DawnImpactPass>(m_ctx, m_device.target().format);
	if (!m_impactPass->create()) { return false; }
	m_viewBlit = std::make_unique<DawnViewBlit>(m_ctx, m_device.target().format);
	if (!m_viewBlit->create()) { return false; }

	debug::boottrace::mark("dawn_r3d_passes_b");
	wgpu::BufferDescriptor fb{};
	fb.size = sizeof(FrameCB);
	fb.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_frameBuffer = m_ctx.device.CreateBuffer(&fb);
	wgpu::BufferDescriptor tb{};
	tb.size = 256;
	tb.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_toneBuffer = m_ctx.device.CreateBuffer(&tb);
	wgpu::TextureDescriptor off{};
	off.label = "mitiru.bloom.off";
	off.size = {1, 1, 1};
	off.format = m_hdrFormat;
	off.usage = wgpu::TextureUsage::TextureBinding;
	m_bloomOff = m_ctx.device.CreateTexture(&off);
	m_bloomOffView = m_bloomOff.CreateView();
	debug::boottrace::mark("dawn_r3d_ready");
	return static_cast<bool>(m_bloomOffView);
}

bool DawnRenderer3D::prepareSmaa()
{
	if (m_smaaPass) { return true; }
	// SceneLook は毎フレーム同じ方式を当てるので、作れないと分かったら確保もしない (ADR 0096 の確保の関所)
	if (m_smaaFailed) { return false; }
	auto pass = std::make_unique<DawnSmaaPass>(m_ctx, m_device.target().format);
	if (!pass->create())
	{
		m_smaaFailed = true;
		debug::verboseOnce("dawn.smaa.unavailable", "Dawn: SMAA の表がこのビルドに無いので、FXAA で描きます。");
		return false;
	}
	pass->resize(m_width, m_height);
	m_smaaPass = std::move(pass);
	return true;
}

void DawnRenderer3D::prepareOitNow()
{
	m_t.ensureOit(m_ctx, m_pipelines.oitLayout());
	(void)m_pipelines.oitAccumulate();
	(void)m_pipelines.oitComposite();
}

void DawnRenderer3D::recreateTargets(int width, int height)
{
	if (width <= 0 || height <= 0) { return; }
	(void)m_ctx.waitIdle();
	m_width = width;
	m_height = height;
	m_sceneWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * m_renderScale)));
	m_sceneHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * m_renderScale)));
	m_t.create(m_ctx, m_hdrFormat, static_cast<std::uint32_t>(m_sceneWidth), static_cast<std::uint32_t>(m_sceneHeight));
	// 宣言したゲームには、半透明が初めて出るフレームの山を持ち込まない。旧体制はこれまでどおり遅延で作る
	if (m_needs.declaredMode && m_wboit) { prepareOitNow(); }
	// 各パスは自分の絵だけを作り直す。それを読む bind group は登録簿が写す物の違いから作り直す
	m_fxaaPass->resize(m_width, m_height);
	if (m_smaaPass) { m_smaaPass->resize(m_width, m_height); }
	m_impactPass->resize(m_width, m_height, m_sceneWidth, m_sceneHeight);
	if (m_colorFinish) { m_colorFinish->resize(m_width, m_height); }
	m_linePass->resize(m_sceneWidth, m_sceneHeight);
	m_bloomPass->resize(m_sceneWidth, m_sceneHeight);
	refreshBindGroups(RefreshPhase::Lazy);
}

// ── フレーム ──

void DawnRenderer3D::beginFrame(const sgc::Colorf& clearColor)
{
	MITIRU_DAWN_ZONE("beginFrame");
	m_frameActive = true;
	m_frameEnded = false;
	m_look.clear = clearColor;
	m_draws.clear();
	m_models.beginFrame();
	m_particles->beginFrame();
	m_decals->beginFrame();
	m_localLights->beginFrame();
	m_drawBytes.clear();
	m_instances.clear();
	m_casters.clear();
	m_viewComposites.clear();
	m_viewMapsGraveyard.clear();
	m_toonSlots.reset();
	m_userMaterials.beginFrame();
	m_shadowCaster = true;
	m_outlineCaster = true;
	m_drawCalls = 0;
	m_culled = 0;
	m_hasStyle = false;
	m_occluderFade = OccluderFade{};
	m_fadeTargetDraw = false;
	m_impactFrame = ImpactFrame{};
	++m_frameCounter;
	// 半透明の組は、起動の数フレームが済んでから作り始める (最初のフレームの山にしない)
	if (m_frameCounter == 12) { m_pipelines.prewarmRest(); }
	m_skinning->beginFrame(m_frameCounter);
	m_cpuSkinCursor = 0;
	if (m_scaleDirty || m_device.width() != m_width || m_device.height() != m_height)
	{
		m_scaleDirty = false;
		recreateTargets(m_device.width(), m_device.height());
	}
	refreshBindGroups(RefreshPhase::FrameHead);
}

void DawnRenderer3D::addDraw(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, const sgc::Colorf& diffuse,
                             const float* add, const DawnMaterialMaps* maps)
{
	addDrawFrom(mesh.localAABB(), &mesh, nullptr, world, material, diffuse, add, maps);
}

void DawnRenderer3D::addSkinnedDraw(const DawnDrawItem& item, const sgc::Colorf& diffuse, const float* add)
{
	addDrawFrom(item.skinBounds, nullptr, &item, item.world, *item.material, diffuse, add, item.maps);
}

// 変形は描くと決まってから頼む。画面外で影も落とさない体は、頂点を用意しない
GpuMesh* DawnRenderer3D::acquireSkinned(const DawnDrawItem& item)
{
	const DawnSkinMorph morph{item.morphWeights, item.morphSlotBase};
	const DawnSkinMorph* morphArg = item.morphActive ? &morph : nullptr;
	if (item.rigidMorph)
	{
		// 骨の無い体はパレットを読まない
		if (GpuMesh* gpu = m_skinning->request(item.skin, 0, 0, morphArg)) { return gpu; }
	}
	else if (m_paletteBase != DawnSkinning::kNoPalette)
	{
		if (GpuMesh* gpu = m_skinning->request(item.skin, m_paletteBase + item.paletteFirst, item.jointCount, morphArg)) { return gpu; }
		debug::warnOnce("dawn.skin.full", "スキンの頂点が GPU の buffer に収まらないので、入らない分は CPU で変形します (遅くなります)");
	}
	std::vector<Vertex3D> skinned;
	if (item.rigidMorph)
	{
		skinned = morphVertices(*item.skin.base, *item.skin.morph, item.morphWeights, item.morphSlotBase);
	}
	else
	{
		const auto palette = m_models.palette().subspan(item.paletteFirst, item.jointCount);
		m_cpuIdentity.assign(palette.size(), sgc::Mat4f::identity());
		m_cpuPose.assign(palette.begin(), palette.end());
		skinned = item.morphActive
			? morphSkinVertices(*item.skin.base, *item.skin.binding, *item.skin.morph, item.morphWeights, item.morphSlotBase, m_cpuIdentity, m_cpuPose)
			: skinVertices(*item.skin.base, *item.skin.binding, m_cpuIdentity, m_cpuPose);
	}
	if (m_cpuSkinCursor >= m_cpuSkinMeshes.size()) { m_cpuSkinMeshes.emplace_back(); }
	Mesh& pool = m_cpuSkinMeshes[m_cpuSkinCursor++];
	const std::vector<std::uint32_t>& indices = *item.skin.indices;
	if (!pool.overwrite(skinned.data(), skinned.size(), indices.data(), indices.size()))
	{
		pool.setVertices(skinned);
		pool.setIndices(indices);
	}
	return m_meshCache.get(pool, m_frameCounter);
}

void DawnRenderer3D::addDrawFrom(const Mesh::AABB& box, const Mesh* mesh, const DawnDrawItem* skin, const sgc::Mat4f& world,
                                 const Material& materialIn, const sgc::Colorf& diffuseIn, const float* add, const DawnMaterialMaps* maps)
{
	// 濡れは材質と拡散色へ描く直前に掛ける (WetSurface.hpp)。0 のときは引数をそのまま使う
	const bool wet = m_wetness > 0.0f;
	const Material wetMat = wet ? wetMaterial(materialIn, m_wetness) : Material{};
	const Material& material = wet ? wetMat : materialIn;
	const sgc::Colorf diffuse = wet ? wetDiffuse(diffuseIn, m_wetness) : diffuseIn;
	const bool doubleSided = material.doubleSided;
	const bool blend = material.alphaMode == Material::AlphaMode::Blend;
	if (!m_initialized || !m_cameraSet) { return; }
	const bool visible = m_frustum.isMeshVisible(box, world);
	// 画面外の壁も光を遮り、画面内へ影を落とす。半透明は影を落とさない
	const bool casts = m_shadowCaster && (m_activeView < 0 || m_viewShadows) && !blend && diffuse.a >= 0.999f;
	if (!visible && !casts) { ++m_culled; return; }
	GpuMesh* gpu = skin != nullptr ? acquireSkinned(*skin) : m_meshCache.get(*mesh, m_frameCounter);
	if (gpu == nullptr) { return; }
	if (casts) { m_casters.push_back({gpu, world, box}); }
	if (!visible) { ++m_culled; return; }

	float worldCm[16];
	toColumnMajor(world, worldCm);
	const sgc::Colorf lin = m_linDiffuse(diffuse);
	const float diffuse4[4] = {lin.r, lin.g, lin.b, diffuse.a};
	refreshCbBase(material, add, maps);

	DrawRecord rec;
	rec.mesh = gpu;
	rec.maps = maps;
	rec.blend = blend || diffuse.a < 0.999f;
	rec.cullNone = doubleSided || rec.blend;   // DX12 の半透明は両面を見せる (透明は CullMode NONE)
	rec.fadeTarget = m_fadeTargetDraw && !rec.blend && m_activeView < 0;
	rec.templateId = m_cbTemplateId;
	if (m_userMaterials.active() > 0)
	{
		if (rec.blend) { debug::verboseOnce("dawn.usermat.blend", "半透明の描画には自作の材質が効かないので、場面の陰影で描きます。"); }
		else { rec.userMaterial = static_cast<std::uint16_t>(m_userMaterials.active()); }
	}
	++m_drawCalls;
	if (extendRun(worldCm, diffuse4, rec)) { return; }
	if (rec.blend)
	{
		// 遠い順に並べ替えるのは半透明だけ
		const sgc::Vec3f c = world.transformPoint(box.center());
		rec.viewDepth = (c - m_camera.position()).length();
	}
	rec.offset = static_cast<std::uint32_t>(m_drawBytes.size());
	m_drawBytes.resize(m_drawBytes.size() + kDrawStride, 0);
	std::uint8_t* dst = m_drawBytes.data() + rec.offset;
	std::memcpy(dst, &m_cbBase, sizeof(DrawCB));
	std::memcpy(dst + offsetof(DrawCB, world), worldCm, sizeof(worldCm));
	std::memcpy(dst + offsetof(DrawCB, diffuse), diffuse4, sizeof(diffuse4));
	m_draws.push_back(rec);
}

void DawnRenderer3D::refreshCbBase(const Material& material, const float* add, const DawnMaterialMaps* maps)
{
	DrawKey key{};
	key.maps = maps;
	key.styleVersion = m_styleVersion;
	key.wet = m_wetness;
	key.spec[0] = material.specular.r; key.spec[1] = material.specular.g; key.spec[2] = material.specular.b; key.spec[3] = material.shininess;
	if (add != nullptr) { key.add[0] = add[0]; key.add[1] = add[1]; key.add[2] = add[2]; }
	key.flags = (m_hasStyle ? 1u : 0u) | (m_outlineCaster ? 2u : 0u) | (m_fadeTargetDraw ? 4u : 0u) | (add != nullptr ? 8u : 0u);
	key.userVersion = m_userMaterials.version();
	if (m_cbBaseValid && std::memcmp(&key, &m_cbKey, sizeof(key)) == 0) { return; }
	m_cbKey = key;
	m_cbBaseValid = true;
	++m_cbTemplateId;

	DrawCB cb{};
	if (add != nullptr)
	{
		const auto a = linearRgb(add[0], add[1], add[2]);
		cb.tintAdd[0] = a[0]; cb.tintAdd[1] = a[1]; cb.tintAdd[2] = a[2];
	}
	const float toonGain = m_wetness > 0.0f ? wetToonGain(m_wetness) : 1.0f;
	cb.toonMaterial[0] = toonGain; cb.toonMaterial[1] = toonGain; cb.toonMaterial[2] = toonGain;
	cb.toonMaterial[3] = m_wetness > 0.0f ? wetToonSharpness(m_wetness) : 1.0f;
	cb.misc[0] = m_outlineCaster ? 0.0f : 1.0f;
	cb.misc[1] = m_fadeTargetDraw ? 1.0f : 0.0f;
	const sgc::Colorf spec = linearColor(material.specular);
	setRgb(cb.phong, spec, material.shininess);
	if (maps != nullptr)
	{
		std::memcpy(cb.mapFlags, maps->mapFlags, sizeof(cb.mapFlags));
		std::memcpy(cb.mapParams, maps->mapParams, sizeof(cb.mapParams));
		std::memcpy(cb.emissive, maps->emissive, sizeof(cb.emissive));
	}
	if (m_hasStyle)
	{
		cb.ramp = m_styleRamp;
		cb.accent = m_styleAccent;
		cb.metal = m_styleMetal;
	}
	m_userMaterials.fill(cb);
	m_cbBase = cb;
}

// 直前の描画と、メッシュ・材質の束・描き方・定数の共通の欄が同じ不透明な描画は、描く順を変えずに 1 回の instanced draw にまとめる (ADR 0095)。
// 記録は値で持つ (GpuMesh は 1 フレームの間は動かず、Mesh 自体は addDraw のうちに読み終えている) ので、呼び出し側の Mesh の寿命に触れない
bool DawnRenderer3D::extendRun(const float* world, const float* diffuse, const DrawRecord& rec)
{
	if (!m_drawMerge || m_draws.empty() || rec.blend || rec.fadeTarget || rec.userMaterial != 0) { return false; }
	DrawRecord& prev = m_draws.back();
	if (prev.blend || prev.fadeTarget || prev.userMaterial != 0 || prev.mesh != rec.mesh || prev.maps != rec.maps || prev.cullNone != rec.cullNone ||
	    prev.templateId != rec.templateId)
	{
		return false;
	}
	InstanceData row;
	std::memcpy(row.world, world, sizeof(row.world));
	std::memcpy(row.diffuse, diffuse, sizeof(row.diffuse));
	if (prev.instanceCount == 1)
	{
		// 2 個目が来て初めて run になる。最初の 1 個の world と diffuse は、積んである定数から写す
		const std::uint8_t* prevCb = m_drawBytes.data() + prev.offset;
		InstanceData first;
		std::memcpy(first.world, prevCb + offsetof(DrawCB, world), sizeof(first.world));
		std::memcpy(first.diffuse, prevCb + offsetof(DrawCB, diffuse), sizeof(first.diffuse));
		prev.instanceFirst = static_cast<std::uint32_t>(m_instances.size());
		m_instances.push_back(first);
	}
	m_instances.push_back(row);
	++prev.instanceCount;
	return true;
}

void DawnRenderer3D::uploadInstances()
{
	if (m_instances.empty()) { return; }
	const std::uint64_t bytes = m_instances.size() * sizeof(InstanceData);
	const std::uint64_t need = std::max<std::uint64_t>(std::bit_ceil(bytes), sizeof(InstanceData) * 256);
	if (!m_instBuffer || m_instBufferBytes < need)
	{
		wgpu::BufferDescriptor bd{};
		bd.size = need;
		bd.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
		m_instBuffer = m_ctx.device.CreateBuffer(&bd);
		m_instBufferBytes = need;
	}
	m_ctx.queue.WriteBuffer(m_instBuffer, 0, m_instances.data(), bytes);
}

void DawnRenderer3D::writeFrameCB()
{
	FrameCB cb{};
	toColumnMajor(m_viewProj, cb.viewProj);
	const sgc::Vec3f eye = m_camera.position();
	cb.cameraPos[0] = eye.x; cb.cameraPos[1] = eye.y; cb.cameraPos[2] = eye.z;
	updateSkyDrive();
	m_look.fill(cb);
	m_bake->fill(cb, m_gi);
	cb.lightInfo[1] = m_hdrScale;
	// デカール、局所光、体積フォグは主ビューだけで使う。副ビューでは数を 0 のままにする
	if (!m_renderingView)
	{
		fillOccluderFade(cb);
		{
			MITIRU_DAWN_ZONE("frameCB.fogDecals");
			fillFog(cb);
			m_decals->update(m_camera);
		}
		cb.decalInfo[0] = static_cast<float>(m_decals->count());
		{
			MITIRU_DAWN_ZONE("frameCB.lights");
			m_localLights->update(m_camera, m_viewProj, m_sceneWidth, m_sceneHeight);
		}
		cb.lightInfo[0] = static_cast<float>(m_localLights->count());
		if (m_localLights->tilesActive())
		{
			cb.lightTiles[0] = static_cast<float>(m_localLights->tileSize());
			cb.lightTiles[1] = static_cast<float>(m_localLights->tileHeaderRows());
			cb.lightTiles[2] = static_cast<float>(m_localLights->tileLoopCount());
		}
	}

	// 空の光線。画面端の向き = 前 + ndc.x * 右 * tan(fov/2) * aspect + ndc.y * 上 * tan(fov/2)
	const sgc::Vec3f fwd = (m_camera.target() - eye).normalized();
	const sgc::Vec3f right = fwd.cross(m_camera.up()).normalized();
	const sgc::Vec3f up = right.cross(fwd);
	const float tanY = std::tan(m_camera.fov() * 0.5f);
	const float tanX = tanY * m_camera.aspectRatio();
	cb.camRight[0] = right.x * tanX; cb.camRight[1] = right.y * tanX; cb.camRight[2] = right.z * tanX;
	cb.camUp[0] = up.x * tanY; cb.camUp[1] = up.y * tanY; cb.camUp[2] = up.z * tanY;
	cb.camForward[0] = fwd.x; cb.camForward[1] = fwd.y; cb.camForward[2] = fwd.z;
	cb.viewInfo[0] = m_camera.farClip();
	m_skyView.altitudeKm = atmosphere::cameraAltitudeKm(m_sky, eye.y);
	m_skyView.farClip = m_camera.farClip();
	for (int k = 0; k < 3; ++k)
	{
		m_skyView.forward[k] = cb.camForward[k];
		m_skyView.right[k] = cb.camRight[k];
		m_skyView.up[k] = cb.camUp[k];
	}
	m_shadowFrame = m_shadowSettings.build(m_camera, !m_casters.empty(), casterCentroid(), m_shadowPass->mapSize(),
	                                       m_shadowPass->farMapSize(), cb);
	m_ctx.queue.WriteBuffer(m_frameBuffer, 0, &cb, sizeof(cb));
	if (m_renderingView) { return; }   // 副ビューのトーンは renderView が自分の定数に書く

	const ToneCB tone{{m_look.exposure, m_look.gamma, m_look.saturation, m_look.contrast},
	                  {static_cast<float>(m_width), static_cast<float>(m_height), m_bloomOn ? m_bloomStrength : 0.0f, 1.0f / m_hdrScale}};
	m_ctx.queue.WriteBuffer(m_toneBuffer, 0, &tone, sizeof(tone));
}

void DawnRenderer3D::ensureDrawBuffer(std::size_t bytes)
{
	const std::uint64_t need = std::max<std::uint64_t>(std::bit_ceil(std::max<std::size_t>(bytes, 4096)), kDrawStride * 64);
	if (m_drawBuffer && m_drawBufferBytes >= need) { return; }
	wgpu::BufferDescriptor bd{};
	bd.size = need;
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_drawBuffer = m_ctx.device.CreateBuffer(&bd);
	m_drawBufferBytes = need;
	refreshBindGroups(RefreshPhase::Lazy);
}

void DawnRenderer3D::writeSceneBindGroup(const DescriptorSources& sources)
{
	// 描画の定数の buffer は最初の描画の直前まで無い。無い間は束を作らず、できた時に Lazy の見直しが作る
	if (sources.anyMissing())
	{
		m_sceneBindGroup = nullptr;
		return;
	}
	wgpu::BindGroupEntry be[14]{};
	be[0].binding = 0;
	be[0].buffer = m_frameBuffer;
	be[0].size = sizeof(FrameCB);
	be[1].binding = 1;
	be[1].buffer = m_drawBuffer;
	be[1].size = sizeof(DrawCB);
	be[2].binding = 2;
	be[2].textureView = m_shadowPass->nearView();
	be[3].binding = 3;
	be[3].textureView = m_shadowPass->farView();
	be[4].binding = 4;
	be[4].sampler = m_shadowPass->sampler();
	be[5].binding = 5;
	be[5].textureView = m_bake->probesView();
	be[6].binding = 6;
	be[6].textureView = m_bake->visibilityView();
	be[7].binding = 7;
	be[7].sampler = m_bake->sampler();
	be[8].binding = 8;
	be[8].textureView = m_decals->view();
	be[9].binding = 9;
	be[9].textureView = m_vfx->colorView();
	be[10].binding = 10;
	be[10].textureView = m_vfx->dataView();
	be[11].binding = 11;
	be[11].sampler = m_vfx->sampler();
	be[12].binding = 12;
	be[12].textureView = m_localLights->view();
	be[13].binding = 13;
	be[13].textureView = m_localLights->tileView();
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_pipelines.sceneLayout();
	bg.entryCount = 14;
	bg.entries = be;
	m_sceneBindGroup = m_ctx.device.CreateBindGroup(&bg);
}

// 影を落とす物の重心。カスケードを手動で置くときの中心
sgc::Vec3f DawnRenderer3D::casterCentroid() const noexcept
{
	if (m_casters.empty()) { return {0.0f, 0.0f, 0.0f}; }
	sgc::Vec3f sum{0.0f, 0.0f, 0.0f};
	for (const ShadowCaster& c : m_casters)
	{
		sum.x += c.world.m[0][3];
		sum.y += c.world.m[1][3];
		sum.z += c.world.m[2][3];
	}
	const float inv = 1.0f / static_cast<float>(m_casters.size());
	return {sum.x * inv, sum.y * inv, sum.z * inv};
}

void DawnRenderer3D::encodeScene(wgpu::CommandEncoder& encoder, bool keepLineTargets)
{
	const sgc::Colorf clear = linearColor(m_look.clear);
	// 不透明 → 半透明 (遠い順)
	{
		MITIRU_DAWN_ZONE("scene.sort");
		std::stable_sort(m_draws.begin(), m_draws.end(), [](const DrawRecord& a, const DrawRecord& b) {
			if (a.blend != b.blend) { return !a.blend; }
			return a.blend ? a.viewDepth > b.viewDepth : false;
		});
	}
	// 半透明があるときは不透明パスで色を resolve せず、半透明パスで続けて resolve する
	const std::size_t firstBlend = static_cast<std::size_t>(std::find_if(m_draws.begin(), m_draws.end(),
		[](const DrawRecord& d) { return d.blend; }) - m_draws.begin());
	const bool withParticles = !m_renderingView && m_particles->hasDraws();
	const bool hasBlendMesh = firstBlend < m_draws.size();
	const bool hasBlend = hasBlendMesh || withParticles;
	const bool oit = m_wboit && hasBlendMesh;
	if (hasBlendMesh) { features::use(features::Feature::Oit); }
	if (m_wboitCapable && !m_wboit && hasBlendMesh) { features::note(features::Feature::Oit); }   // 新体制で oit を宣言していない
	const bool silhouettes = silhouettesWanted();
	const bool later = hasBlend || silhouettes;   // 不透明のパスの後に、色を続けて描くパスがあるか
	wgpu::RenderPassColorAttachment colors[3]{};
	wgpu::RenderPassColorAttachment& color = colors[0];
	color.view = m_t.colorMsaaView;
	if (!later) { color.resolveTarget = m_t.resolvedView; }
	color.loadOp = wgpu::LoadOp::Clear;
	color.storeOp = later ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
	color.clearValue = {clear.r * m_hdrScale, clear.g * m_hdrScale, clear.b * m_hdrScale, clear.a};
	// 下地 2 枚の初期値は、法線なし (0.5, 0.5, 0.5, 印 0) と、遠方の距離 (24 bit がすべて 1)
	colors[1].view = m_t.normalMsaaView;
	colors[1].loadOp = wgpu::LoadOp::Clear;
	colors[1].storeOp = keepLineTargets ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
	colors[1].clearValue = {0.5, 0.5, 0.5, 0.0};
	colors[2].view = m_t.lineDepthMsaaView;
	colors[2].loadOp = wgpu::LoadOp::Clear;
	colors[2].storeOp = (keepLineTargets || silhouettes) ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
	colors[2].clearValue = {1.0, 1.0, 1.0, 1.0};
	wgpu::RenderPassDepthStencilAttachment depth{};
	depth.view = m_t.depthMsaaView;
	depth.depthLoadOp = wgpu::LoadOp::Clear;
	depth.depthStoreOp = hasBlend ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
	depth.depthClearValue = 1.0f;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 3;
	rp.colorAttachments = colors;
	rp.depthStencilAttachment = &depth;
	wgpu::RenderPassEncoder pass = [&] {
		MITIRU_DAWN_ZONE("scene.beginPass");
		return encoder.BeginRenderPass(&rp);
	}();

	std::uint32_t zero = 0;
	// 物理ベースの空の向きは主ビューのカメラで作るため、副ビューには縦のグラデーションだけを張る
	if (m_sky.enabled && !m_renderingView)
	{
		MITIRU_DAWN_ZONE("scene.sky");
		m_skyPass->draw(pass);
	}
	else if (m_look.skyOn)
	{
		MITIRU_DAWN_ZONE("scene.skyGradient");
		pass.SetPipeline(m_pipelines.sky());
		pass.SetBindGroup(0, m_sceneBindGroup, 1, &zero);
		pass.Draw(3);
	}

	recordDraws(pass, 0, firstBlend);
	if (!m_renderingView) { m_particles->drawOpaque(pass); }
	{
		MITIRU_DAWN_ZONE("scene.endPass");
		pass.End();
	}
	if (silhouettes) { encodeSilhouettes(encoder, !hasBlend); }
	if (!hasBlend) { return; }

	if (oit)
	{
		m_t.ensureOit(m_ctx, m_pipelines.oitLayout());
		encodeOitAccumulate(encoder, firstBlend);
	}
	wgpu::RenderPassColorAttachment blendColor{};
	blendColor.view = m_t.colorMsaaView;
	blendColor.resolveTarget = m_t.resolvedView;
	blendColor.loadOp = wgpu::LoadOp::Load;
	blendColor.storeOp = wgpu::StoreOp::Discard;
	wgpu::RenderPassDepthStencilAttachment blendDepth{};
	blendDepth.view = m_t.depthMsaaView;
	blendDepth.depthLoadOp = wgpu::LoadOp::Load;
	blendDepth.depthStoreOp = wgpu::StoreOp::Discard;
	wgpu::RenderPassDescriptor blendRp{};
	blendRp.colorAttachmentCount = 1;
	blendRp.colorAttachments = &blendColor;
	blendRp.depthStencilAttachment = &blendDepth;
	wgpu::RenderPassEncoder blendPass = encoder.BeginRenderPass(&blendRp);
	if (oit)
	{
		blendPass.SetPipeline(m_pipelines.oitComposite());
		blendPass.SetBindGroup(0, m_t.oit.compositeGroup);
		blendPass.Draw(3);
	}
	else
	{
		recordDraws(blendPass, firstBlend, m_draws.size());
	}
	if (!m_renderingView) { m_particles->draw(blendPass); }
	blendPass.End();
}

// 透かしの定数。目と画角は shaken 後のカメラ。Dawn には TAA が無いので、網点の閾値のずらしは 0 のまま
void DawnRenderer3D::fillOccluderFade(FrameCB& cb) const
{
	const OccluderFadeGpu gpu = makeOccluderFadeGpu(m_occluderFade, m_camera.position(), m_camera.fov(), 0.0f);
	std::memcpy(cb.fadeCamera, gpu.camera, sizeof(gpu.camera));
	std::memcpy(cb.fadeParams, gpu.params, sizeof(gpu.params));
	std::memcpy(cb.fadeTargets, gpu.targets, sizeof(gpu.targets));
	std::memcpy(cb.fadeCones, gpu.cones, sizeof(gpu.cones));
	const float* s = m_occluderFade.silhouette;
	const sgc::Colorf c = linearColor(sgc::Colorf{s[0], s[1], s[2], 1.0f});
	cb.fadeSilColor[0] = c.r; cb.fadeSilColor[1] = c.g; cb.fadeSilColor[2] = c.b;
	cb.fadeSilColor[3] = std::clamp(s[3], 0.0f, 1.0f);
	const int n = std::clamp(m_occluderFade.targetCount, 0, OccluderFade::kMaxTargets);
	for (int i = 0; i < n; ++i) { cb.fadeSilParams[0] = std::max(cb.fadeSilParams[0], m_occluderFade.targets[i].radius); }
}

// 隠れた的の影絵を描くか。的として描いた不透明の物があり、影絵の濃さが 0 より大きいフレームの主ビューだけ
bool DawnRenderer3D::silhouettesWanted() const noexcept
{
	if (m_renderingView || !(m_occluderFade.silhouette[3] > 0.0f) || m_occluderFade.targetCount <= 0) { return false; }
	return std::any_of(m_draws.begin(), m_draws.end(), [](const DrawRecord& d) { return d.fadeTarget; });
}

void DawnRenderer3D::recordSilhouetteDraws(wgpu::RenderPassEncoder& pass)
{
	const GpuMesh* boundMesh = nullptr;
	for (const DrawRecord& d : m_draws)
	{
		if (!d.fadeTarget) { continue; }
		if (d.mesh != boundMesh)
		{
			pass.SetVertexBuffer(0, d.mesh->vertexBuffer, d.mesh->vertexOffset, d.mesh->vertexBytes);
			if (d.mesh->indexCount > 0) { pass.SetIndexBuffer(d.mesh->indexBuffer, wgpu::IndexFormat::Uint32, 0, d.mesh->indexBytes); }
			boundMesh = d.mesh;
		}
		pass.SetBindGroup(0, m_sceneBindGroup, 1, &d.offset);
		if (d.mesh->indexCount > 0) { pass.DrawIndexed(d.mesh->indexCount); }
		else { pass.Draw(d.mesh->vertexCount); }
	}
}

// 不透明の後、半透明の前。的を寄せた深度のいちばん手前を専用の深度に置き (1 つ目)、その面のうち手前に場面の別の面がある標本へ
// 影絵の色を重ねる (2 つ目)。resolve が最後なら、色をここで resolve する
void DawnRenderer3D::encodeSilhouettes(wgpu::CommandEncoder& encoder, bool resolve)
{
	m_t.ensureSilhouetteDepth(m_ctx);
	wgpu::RenderPassDepthStencilAttachment nearestDepth{};
	nearestDepth.view = m_t.silhouetteDepthView;
	nearestDepth.depthLoadOp = wgpu::LoadOp::Clear;
	nearestDepth.depthStoreOp = wgpu::StoreOp::Store;
	nearestDepth.depthClearValue = 1.0f;
	wgpu::RenderPassDescriptor nearestRp{};
	nearestRp.depthStencilAttachment = &nearestDepth;
	wgpu::RenderPassEncoder nearest = encoder.BeginRenderPass(&nearestRp);
	nearest.SetPipeline(m_pipelines.silhouetteNearest());
	recordSilhouetteDraws(nearest);
	nearest.End();

	wgpu::RenderPassColorAttachment color{};
	color.view = m_t.colorMsaaView;
	if (resolve) { color.resolveTarget = m_t.resolvedView; }
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = resolve ? wgpu::StoreOp::Discard : wgpu::StoreOp::Store;
	wgpu::RenderPassDepthStencilAttachment paintDepth{};
	paintDepth.view = m_t.silhouetteDepthView;
	paintDepth.depthReadOnly = true;
	wgpu::RenderPassDescriptor paintRp{};
	paintRp.colorAttachmentCount = 1;
	paintRp.colorAttachments = &color;
	paintRp.depthStencilAttachment = &paintDepth;
	wgpu::RenderPassEncoder paint = encoder.BeginRenderPass(&paintRp);
	paint.SetPipeline(m_pipelines.silhouettePaint());
	paint.SetBindGroup(1, m_fogGroup);
	recordSilhouetteDraws(paint);
	paint.End();
}

// 半透明のメッシュ [first, 末尾) を accum と reveal へ蓄積する。深度は不透明の物を読むだけ。
// 色は 4 標本の平均へ resolve する (DX12 の ResolveSubresource と同じ)。
void DawnRenderer3D::encodeOitAccumulate(wgpu::CommandEncoder& encoder, std::size_t first)
{
	wgpu::RenderPassColorAttachment colors[2]{};
	colors[0].view = m_t.oit.accumMsaaView;
	colors[0].resolveTarget = m_t.oit.accumView;
	colors[0].loadOp = wgpu::LoadOp::Clear;
	colors[0].storeOp = wgpu::StoreOp::Discard;
	colors[0].clearValue = {0.0, 0.0, 0.0, 0.0};
	colors[1].view = m_t.oit.revealMsaaView;
	colors[1].resolveTarget = m_t.oit.revealView;
	colors[1].loadOp = wgpu::LoadOp::Clear;
	colors[1].storeOp = wgpu::StoreOp::Discard;
	colors[1].clearValue = {1.0, 1.0, 1.0, 1.0};
	wgpu::RenderPassDepthStencilAttachment depth{};
	depth.view = m_t.depthMsaaView;
	depth.depthReadOnly = true;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 2;
	rp.colorAttachments = colors;
	rp.depthStencilAttachment = &depth;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	const wgpu::RenderPipeline pipeline = m_pipelines.oitAccumulate();
	recordDraws(pass, first, m_draws.size(), &pipeline);
	pass.End();
}

// m_draws の [first, last) を積む。パイプラインと頂点バッファは変わったときだけ張り直す。only があれば全部をそのパイプラインで描く
void DawnRenderer3D::recordDraws(wgpu::RenderPassEncoder& pass, std::size_t first, std::size_t last, const wgpu::RenderPipeline* only)
{
	MITIRU_DAWN_ZONE("recordDraws");
	wgpu::RenderPipeline bound;
	const GpuMesh* boundMesh = nullptr;
	const DawnMaterialMaps* boundMaps = nullptr;
	bool materialBound = false;
	std::size_t runCount = 0;
	std::size_t runInstances = 0;
	for (std::size_t i = first; i < last; ++i)
	{
		const DrawRecord& d = m_draws[i];
		if (!materialBound || d.maps != boundMaps)
		{
			pass.SetBindGroup(1, d.maps != nullptr ? d.maps->group : m_materials.defaultGroup());
			boundMaps = d.maps;
			materialBound = true;
		}
		const bool run = only == nullptr && d.instanceCount > 1;
		runCount += run ? 1u : 0u;
		runInstances += run ? d.instanceCount : 0u;
		wgpu::RenderPipeline pipe;
		if (only != nullptr) { pipe = *only; }
		else if (d.userMaterial != 0) { pipe = m_userMaterials.pipeline(d.userMaterial, d.cullNone); }
		if (!pipe) { pipe = m_pipelines.scene(d.blend, d.cullNone, run); }   // 材質のパイプラインを作れなかったときも場面の陰影で描く
		if (pipe.Get() != bound.Get())
		{
			pass.SetPipeline(pipe);
			bound = pipe;
		}
		if (d.mesh != boundMesh)
		{
			pass.SetVertexBuffer(0, d.mesh->vertexBuffer, d.mesh->vertexOffset, d.mesh->vertexBytes);
			if (d.mesh->indexCount > 0) { pass.SetIndexBuffer(d.mesh->indexBuffer, wgpu::IndexFormat::Uint32, 0, d.mesh->indexBytes); }
			boundMesh = d.mesh;
		}
		pass.SetBindGroup(0, m_sceneBindGroup, 1, &d.offset);
		if (run)
		{
			pass.SetVertexBuffer(1, m_instBuffer, static_cast<std::uint64_t>(d.instanceFirst) * sizeof(InstanceData),
			                     static_cast<std::uint64_t>(d.instanceCount) * sizeof(InstanceData));
		}
		const std::uint32_t instances = run ? d.instanceCount : 1;
		if (d.mesh->indexCount > 0) { pass.DrawIndexed(d.mesh->indexCount, instances); }
		else { pass.Draw(d.mesh->vertexCount, instances); }
	}
	MITIRU_DAWN_COUNT("rec.records", last - first);
	MITIRU_DAWN_COUNT("rec.runs", runCount);
	MITIRU_DAWN_COUNT("rec.runInstances", runInstances);
}

void DawnRenderer3D::encodeTonemap(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, const wgpu::BindGroup& postGroup,
                                   bool scaled)
{
	wgpu::RenderPassColorAttachment color{};
	color.view = target;
	color.loadOp = wgpu::LoadOp::Clear;
	color.storeOp = wgpu::StoreOp::Store;
	color.clearValue = {0.0, 0.0, 0.0, 1.0};
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_pipelines.tonemap(scaled));
	pass.SetBindGroup(0, postGroup);
	pass.Draw(3);
	pass.End();
}

void DawnRenderer3D::finalizeFrame()
{
	MITIRU_DAWN_ZONE("finalizeFrame");
	struct FrameTick { ~FrameTick() { prof::frameDone(); } } frameTick;
	m_shakeX = 0.0f;
	m_shakeY = 0.0f;
	if (!m_initialized || !m_frameActive || m_ctx.lost) { return; }
	refreshBindGroups(RefreshPhase::PreSubmit);
	m_skinning->encode();
	if (m_cameraSet) { encodeMainFrame(); }
	renderViews();
	encodeViewComposites();
	if (m_frameCounter % 60 == 0)
	{
		m_meshCache.evictIdle(m_frameCounter);
		m_skinning->evictIdle(m_frameCounter);
	}
}

void DawnRenderer3D::encodeMainFrame()
{
	MITIRU_DAWN_ZONE("encodeMainFrame");
	ensureDrawBuffer(m_drawBytes.size());
	{
		MITIRU_DAWN_ZONE("encode.frameCB");
		writeFrameCB();
	}
	updateParticles();
	{
		MITIRU_DAWN_ZONE("encode.upload");
		if (!m_drawBytes.empty()) { m_ctx.queue.WriteBuffer(m_drawBuffer, 0, m_drawBytes.data(), m_drawBytes.size()); }
		uploadInstances();
	}
	const bool lines = m_lineSettings.active(m_toonSlots);
	wgpu::CommandEncoder encoder = [&] {
		MITIRU_DAWN_ZONE("encode.createEncoder");
		return m_ctx.device.CreateCommandEncoder();
	}();
	{
		MITIRU_DAWN_ZONE("encode.shadow");
		m_shadowPass->encode(encoder, m_shadowFrame, m_casters);
	}
	m_skyView.cloudCover = m_cloudCover;
	m_skyView.cloudTime = static_cast<float>(m_colorFinishSeed) * (1.0f / 60.0f);
	if (m_sky.enabled) { m_skyPass->encodeLuts(encoder, m_sky, (m_look.light.direction * -1.0f).normalized(), m_skyView); }
	const bool impact = impactFrameActive(m_impactFrame);
	{
		MITIRU_DAWN_ZONE("encode.scene");
		encodeScene(encoder, lines || impact || m_fog.enabled || (m_sky.enabled && m_sky.aerialPerspectiveScale > 0.0f) || m_particles->wantsSceneDepth());
	}
	if (m_sky.enabled && m_sky.aerialPerspectiveScale > 0.0f) { m_skyPass->encodeAerial(encoder, m_t.resolvedView); }
	if (m_fog.enabled) { encodeFog(encoder); }
	if (m_bloomOn && m_bloomStrength > 0.0f) { m_bloomPass->encode(encoder, m_bloomThreshold * m_hdrScale); }
	encodeTonemap(encoder, m_device.target().view, m_postBindGroup, m_sceneWidth != m_width || m_sceneHeight != m_height);
	gfx::skipBackground(m_device);   // トーンマップが最終色の全面を書き直す
	if (lines)
	{
		m_linePass->encode(encoder, m_device.target().view,
		                   m_lineSettings.buildCB(m_toonSlots, m_sceneWidth, m_sceneHeight, m_width, m_height, m_camera.farClip()));
	}
	if (m_smaaOn) { m_smaaPass->encode(encoder, m_device.target().texture, m_device.target().view); }
	else if (m_fxaaOn) { m_fxaaPass->encode(encoder, m_device.target().texture, m_device.target().view); }
	// 色の仕上げ (LUT・ビネット・グレイン) は DX12 と同じく、アンチエイリアスの後・止め絵の前。粒を AA がならして消さない
	if (m_colorLookNumber != 0 && m_colorFinish)
	{
		(void)m_colorFinish->encode(encoder, m_device.target().texture, m_device.target().view, m_colorLookNumber, m_colorFinishSeed);
	}
	m_colorLookNumber = 0;
	// 止め絵は出力の大きさの絵に掛ける (DX12 と同じく、アップスケールとアンチエイリアスの後)
	if (impact) { m_impactPass->encode(encoder, m_device.target().texture, m_device.target().view, m_impactFrame, m_toonSlots.keepMask()); }
	wgpu::CommandBuffer commands = [&] {
		MITIRU_DAWN_ZONE("encode.finish");
		return encoder.Finish();
	}();
	{
		MITIRU_DAWN_ZONE("encode.submit");
		m_ctx.queue.Submit(1, &commands);
	}
}

} // namespace mitiru::render::dawn_detail

namespace mitiru::render
{

std::unique_ptr<IRenderer3D> createDawnRenderer3D(gfx::IDevice* device, const features::RenderNeeds* needs)
{
	auto* dawn = dynamic_cast<gfx::DawnDevice*>(device);
	if (dawn == nullptr) { return nullptr; }
	auto renderer = std::make_unique<dawn_detail::DawnRenderer3D>(*dawn, needs);
	if (!renderer->isInitialized())
	{
		debug::warnOnce("dawn.renderer3d.init", "Dawn の 3D レンダラーを作れなかったので、3D は出ません");
		return nullptr;
	}
	return renderer;
}

void dawnDeclareFeatures(IRenderer3D& renderer, const features::RenderNeeds& needs)
{
	if (auto* dawn = dynamic_cast<dawn_detail::DawnRenderer3D*>(&renderer)) { dawn->declareFeatures(needs); }
}

bool dawnWboitReady(const IRenderer3D& renderer)
{
	const auto* dawn = dynamic_cast<const dawn_detail::DawnRenderer3D*>(&renderer);
	return dawn != nullptr && dawn->wboitReady();
}

} // namespace mitiru::render
