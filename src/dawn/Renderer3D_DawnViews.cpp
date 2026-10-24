// Dawn の副ビュー実装（view slot、ABI v49）。DX12 の DX12Views.hpp と DX12ViewSlots.hpp に相当する。
// slot ごとに描画先と、描画の記録、影の caster、カメラを持つ。
// beginViewSlot 中は主ビューと状態を入れ替え、addDraw を共用する。
// finalizeFrame で主ビューの後に副ビューを 1 枚ずつ作り、compositeViewSlot の矩形へまとめて貼る。
// 対象はメッシュ、モデル、太陽の影、縦のグラデーションの空、トーンマップ、FXAA。
// 粒、デカール、局所光、体積フォグ、物理ベースの空、bloom、輪郭線は主ビューだけで扱う。

#include <cstring>

#include <mitiru/render/Renderer3D_Dawn.hpp>

#include "DawnRenderer3D.hpp"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr int kMaxViewExtent = 8192;   // DX12 の kMaxViewExtent と同じ

} // namespace

const DawnViewSlot* DawnRenderer3D::viewSlot(int slot) const noexcept
{
	return (validViewSlot(slot) && m_viewSlots[static_cast<std::size_t>(slot)].created) ? &m_viewSlots[static_cast<std::size_t>(slot)] : nullptr;
}

// DrawRecord が指す材質の束は、描画後もフレームが終わるまで残す。
void DawnRenderer3D::retireViewSlot(DawnViewSlot& view)
{
	if (view.maps) { m_viewMapsGraveyard.push_back(std::move(view.maps)); }
	view = DawnViewSlot{};
}

bool DawnRenderer3D::createViewTargets(DawnViewSlot& view, const View3DPod& pod)
{
	const auto w = static_cast<std::uint32_t>(pod.width);
	const auto h = static_cast<std::uint32_t>(pod.height);
	view.targets.create(m_ctx, m_hdrFormat, w, h);
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.view.output";
	td.size = {w, h, 1};
	td.format = m_device.target().format;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
	view.output = m_ctx.device.CreateTexture(&td);
	if (!view.output) { return false; }
	view.outputView = view.output.CreateView();

	wgpu::BufferDescriptor tb{};
	tb.size = 256;
	tb.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	view.toneBuffer = m_ctx.device.CreateBuffer(&tb);
	wgpu::BindGroupEntry be[4]{};
	be[0].binding = 0;
	be[0].buffer = view.toneBuffer;
	be[0].size = sizeof(ToneCB);
	be[1].binding = 1;
	be[1].textureView = view.targets.resolvedView;
	be[2].binding = 2;
	be[2].sampler = m_pipelines.postSampler();
	be[3].binding = 3;
	be[3].textureView = m_bloomOffView;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_pipelines.postLayout();
	bg.entryCount = 4;
	bg.entries = be;
	view.postGroup = m_ctx.device.CreateBindGroup(&bg);
	view.blitGroup = m_viewBlit->makeGroup(view.outputView);

	// 出力は sRGB 値をそのまま 8 bit で持つため、基本色に結ぶときは線形へ戻す印（mapParams.z）を立てる。
	view.maps = std::make_shared<DawnMaterialMaps>();
	view.maps->group = m_materials.makeBaseGroup(view.outputView);
	view.maps->mapFlags[0] = 1.0f;
	view.maps->mapParams[2] = 1.0f;
	return view.postGroup && view.blitGroup && view.maps->group;
}

bool DawnRenderer3D::setViewSlot(int slot, const View3DPod& pod)
{
	if (!m_initialized || !validViewSlot(slot) || m_activeView >= 0) { return false; }
	if (pod.width <= 0 || pod.height <= 0 || pod.width > kMaxViewExtent || pod.height > kMaxViewExtent) { return false; }
	DawnViewSlot& view = m_viewSlots[static_cast<std::size_t>(slot)];
	if (view.created && std::memcmp(&view.pod, &pod, sizeof(View3DPod)) == 0) { return true; }
	retireViewSlot(view);
	view.pod = pod;
	view.width = pod.width;
	view.height = pod.height;
	if (!createViewTargets(view, pod))
	{
		debug::verboseOnce("dawn.view.create", "副ビューの描画先を作れなかったので、この副ビューは使えません。");
		retireViewSlot(view);
		return false;
	}
	view.created = true;
	return true;
}

// もう一度呼ぶと、主ビューと副ビューの状態が元に戻る。
void DawnRenderer3D::swapViewState(DawnViewSlot& view) noexcept
{
	std::swap(m_camera, view.camera);
	std::swap(m_frustum, view.frustum);
	std::swap(m_view, view.view);
	std::swap(m_viewProj, view.viewProj);
	std::swap(m_cameraSet, view.cameraSet);
	std::swap(m_draws, view.draws);
	std::swap(m_drawBytes, view.drawBytes);
	std::swap(m_instances, view.instances);
	std::swap(m_casters, view.casters);
	std::swap(m_t, view.targets);
	m_mainTargets = (m_mainTargets == &m_t) ? &view.targets : &m_t;
}

// カメラの縦横比は使わず、副ビューの大きさから決める。DLL は副ビューの大きさを持たない。
bool DawnRenderer3D::beginViewSlot(int slot, const Camera3D& camera)
{
	if (!m_initialized || !m_frameActive || m_activeView >= 0 || !validViewSlot(slot)) { return false; }
	DawnViewSlot& view = m_viewSlots[static_cast<std::size_t>(slot)];
	if (!view.created) { return false; }
	if (view.drawnFrame != m_frameCounter)
	{
		view.draws.clear();
		view.drawBytes.clear();
		view.instances.clear();
		view.casters.clear();
		view.drawnFrame = m_frameCounter;
	}
	const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
	const Camera3D cam(camera.position(), camera.target(), camera.up(), camera.fov(), aspect, camera.nearClip(), camera.farClip());
	view.camera = cam;
	view.frustum.extractFromCamera(cam);
	view.view = cam.viewMatrix();
	view.viewProj = cam.viewProjectionMatrixZO();
	view.cameraSet = true;
	swapViewState(view);
	m_models.setView(cam.position(), cam.fov());
	m_activeView = slot;
	m_viewShadows = (view.pod.flags & kView3DShadows) != 0;
	return true;
}

void DawnRenderer3D::endViewSlot()
{
	if (m_activeView < 0) { return; }
	swapViewState(m_viewSlots[static_cast<std::size_t>(m_activeView)]);
	m_activeView = -1;
	m_viewShadows = true;
	if (m_cameraSet) { m_models.setView(m_camera.position(), m_camera.fov()); }
}

void DawnRenderer3D::compositeViewSlot(int slot, float u, float v, float w, float h)
{
	if (!validViewSlot(slot) || !m_viewSlots[static_cast<std::size_t>(slot)].created || !m_frameActive || !(w > 0.0f) || !(h > 0.0f)) { return; }
	m_viewComposites.push_back({slot, u, v, w, h});
}

// 出力には前のフレームの絵を使う。描画中または出力のない副ビューは描かない。
void DawnRenderer3D::drawMeshWithViewSlot(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, int slot)
{
	if (!validViewSlot(slot) || slot == m_activeView) { return; }
	const DawnViewSlot& view = m_viewSlots[static_cast<std::size_t>(slot)];
	if (!view.created || !view.hasOutput) { return; }
	addDraw(mesh, world, material, material.diffuse, nullptr, view.maps.get());
}

void DawnRenderer3D::releaseViewSlot(int slot)
{
	if (!validViewSlot(slot) || slot == m_activeView) { return; }
	std::erase_if(m_viewComposites, [slot](const ViewComposite& c) { return c.slot == slot; });
	retireViewSlot(m_viewSlots[static_cast<std::size_t>(slot)]);
}

void DawnRenderer3D::renderViews()
{
	for (DawnViewSlot& view : m_viewSlots)
	{
		if (view.created && view.drawnFrame == m_frameCounter && m_activeView < 0) { renderView(view); }
	}
}

// 定数と描画記録は提出順に反映されるため、主ビューや別の副ビューと同じ buffer を使い回す。影マップも提出順に使い回す。
void DawnRenderer3D::renderView(DawnViewSlot& view)
{
	// 副ビューに出ない物 (主ビューが使っている粒、デカール、局所光、フォグ、物理ベースの空、bloom、輪郭線) を数える
	const bool wantsSky = (view.pod.flags & kView3DSky) != 0 && (m_sky.enabled || m_fog.enabled);
	if (wantsSky || m_particles->hasDraws() || m_particles->hasOpaqueDraws() || m_decals->count() > 0 || m_localLights->count() > 0 ||
	    (m_bloomOn && (view.pod.flags & kView3DNoBloom) == 0) || m_lineSettings.enabled)
	{
		features::note(features::Feature::ViewSlots);
	}
	swapViewState(view);
	const sgc::Colorf savedClear = m_look.clear;
	const bool savedSky = m_look.skyOn;
	m_look.clear = sgc::Colorf{view.pod.clear[0], view.pod.clear[1], view.pod.clear[2], 1.0f};
	m_look.skyOn = savedSky && (view.pod.flags & kView3DSky) != 0;
	m_renderingView = true;

	ensureDrawBuffer(m_drawBytes.size());
	writeFrameCB();
	if (!m_drawBytes.empty()) { m_ctx.queue.WriteBuffer(m_drawBuffer, 0, m_drawBytes.data(), m_drawBytes.size()); }
	uploadInstances();
	const ToneCB tone{{m_look.exposure, m_look.gamma, m_look.saturation, m_look.contrast},
	                  {static_cast<float>(view.width), static_cast<float>(view.height), 0.0f, 1.0f / m_hdrScale}};
	m_ctx.queue.WriteBuffer(view.toneBuffer, 0, &tone, sizeof(tone));

	if (!view.fxaa && m_fxaaOn && (view.pod.flags & kView3DNoAntiAlias) == 0)
	{
		view.fxaa = std::make_unique<DawnFxaaPass>(m_ctx, m_device.target().format);
		if (view.fxaa->create()) { view.fxaa->resize(view.width, view.height); }
		else { view.fxaa.reset(); }
	}
	wgpu::CommandEncoder encoder = m_ctx.device.CreateCommandEncoder();
	m_shadowPass->encode(encoder, m_shadowFrame, m_casters);
	encodeScene(encoder, false);
	encodeTonemap(encoder, view.outputView, view.postGroup, false);
	if (view.fxaa && m_fxaaOn) { view.fxaa->encode(encoder, view.output, view.outputView); }
	wgpu::CommandBuffer commands = encoder.Finish();
	m_ctx.queue.Submit(1, &commands);
	view.hasOutput = true;

	m_renderingView = false;
	m_look.clear = savedClear;
	m_look.skyOn = savedSky;
	swapViewState(view);
}

// 副ビューの出力を、主ビューの後、2D の前に画面の矩形へ貼る。矩形は出力の幅と高さに対する割合で表す。
void DawnRenderer3D::encodeViewComposites()
{
	std::vector<DawnViewBlit::Placement> placements;
	const auto width = static_cast<float>(m_width);
	const auto height = static_cast<float>(m_height);
	for (const ViewComposite& c : m_viewComposites)
	{
		const DawnViewSlot& view = m_viewSlots[static_cast<std::size_t>(c.slot)];
		if (!view.created || !view.hasOutput) { continue; }
		placements.push_back({view.blitGroup, c.u * width, c.v * height, c.w * width, c.h * height});
	}
	if (placements.empty()) { return; }
	gfx::flushBackground(m_device);
	wgpu::CommandEncoder encoder = m_ctx.device.CreateCommandEncoder();
	m_viewBlit->encode(encoder, m_device.target().view, m_width, m_height, placements);
	wgpu::CommandBuffer commands = encoder.Finish();
	m_ctx.queue.Submit(1, &commands);
}

} // namespace mitiru::render::dawn_detail

namespace mitiru::render
{

DawnViewImage dawnViewSlotImage(const IRenderer3D& renderer, int slot)
{
	const auto* dawn = dynamic_cast<const dawn_detail::DawnRenderer3D*>(&renderer);
	const dawn_detail::DawnViewSlot* view = dawn != nullptr ? dawn->viewSlot(slot) : nullptr;
	if (view == nullptr) { return {}; }
	return {view->outputView.Get(), view->output.Get(), view->width, view->height};
}

} // namespace mitiru::render
