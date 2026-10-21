#include "DawnLinePass.hpp"

#include <algorithm>
#include <cmath>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

// 種は (距離, 印)。距離は 3% の差で線の持ち主を見分けるだけなので、16 bit の小数で足りる
constexpr wgpu::TextureFormat kSeedFormat = wgpu::TextureFormat::RG16Float;

wgpu::BindGroupLayoutEntry msTexture(std::uint32_t binding)
{
	wgpu::BindGroupLayoutEntry e{};
	e.binding = binding;
	e.visibility = wgpu::ShaderStage::Fragment;
	e.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	e.texture.multisampled = true;
	return e;
}

constexpr float kLineDepthStep = 0.04f;    // 線にする深さの段差 (割合)
constexpr float kLineCreaseCos = 0.8f;     // 折れ目とみなす面の向きの差 (cos)
constexpr int kLineMaxRadius = 8;

} // namespace

ToonLineSlot DawnLineSettings::sceneLine() const noexcept
{
	ToonLineSlot l;
	if (!enabled || mode == OutlineMode::Fresnel) { return l; }
	l.width = 2.0f * static_cast<float>(std::max(static_cast<int>(widthPx), 1));
	l.farWidth = l.width;
	l.mix = std::clamp(darken, 0.0f, 1.0f);
	l.fadeStart = fadeNear;
	l.fadeEnd = fadeFar;
	l.fadeMin = fadeMin;
	return l;
}

LineCB DawnLineSettings::buildCB(const ToonSlotTable& slots, int sceneW, int sceneH, int outW, int outH, float farDepth) const noexcept
{
	LineCB cb{};
	const float scale = static_cast<float>(sceneH) / 1080.0f;
	float widest = 0.0f;
	for (int i = 0; i < slots.count(); ++i)
	{
		const ToonLineSlot l = (i == 0) ? sceneLine() : slots.slot(i);
		const float k = (i == 0) ? 1.0f : scale;   // 場面の線は今までどおり今の画面の画素で数える
		for (int c = 0; c < 3; ++c) { cb.slotColor[i][c] = l.color[c]; }
		cb.slotColor[i][3] = l.mix;
		cb.slotWidth[i][0] = l.width * k;
		cb.slotWidth[i][1] = l.farWidth * k;
		cb.slotWidth[i][2] = l.farDistance;
		cb.slotFade[i][0] = l.fadeStart;
		cb.slotFade[i][1] = l.fadeEnd;
		cb.slotFade[i][2] = l.fadeMin;
		widest = std::max({widest, cb.slotWidth[i][0], cb.slotWidth[i][1]});
	}
	cb.screen[0] = static_cast<float>(sceneW);
	cb.screen[1] = static_cast<float>(sceneH);
	cb.screen[2] = static_cast<float>(std::max(outW, 1));
	cb.screen[3] = static_cast<float>(std::max(outH, 1));
	cb.depthParams[0] = farDepth;
	cb.depthParams[2] = kLineDepthStep;
	cb.depthParams[3] = kLineCreaseCos;
	cb.extra[0] = static_cast<float>(std::min(static_cast<int>(std::ceil(widest * 0.5f + 0.5f)), kLineMaxRadius));
	return cb;
}

bool DawnLinePass::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[4]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(LineCB);
	entries[1] = msTexture(1);
	entries[2] = msTexture(2);
	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Fragment;
	entries[3].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 4;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::BufferDescriptor bd{};
	bd.size = sizeof(LineCB);
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cbBuffer = dev.CreateBuffer(&bd);

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_lines(), "mitiru.lines");
	wgpu::ColorTargetState seedTarget{};
	seedTarget.format = kSeedFormat;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsSeed";
	fs.targetCount = 1;
	fs.targets = &seedTarget;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsLine";
	rp.fragment = &fs;
	m_seedPipeline = dev.CreateRenderPipeline(&rp);

	// 出力は lerp(下の色, 線の色, a)。a に線の色の割合を掛けてあるので、色を読まずに下の色と混ぜられる
	wgpu::BlendState blend{};
	blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
	blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::Zero};
	wgpu::ColorTargetState lineTarget{};
	lineTarget.format = m_finalFormat;
	lineTarget.blend = &blend;
	fs.entryPoint = "fsLine";
	fs.targets = &lineTarget;
	m_linePipeline = dev.CreateRenderPipeline(&rp);

	wgpu::TextureDescriptor dd{};
	dd.label = "mitiru.lines.dummy";
	dd.size = {1, 1, 1};
	dd.format = kSeedFormat;
	dd.usage = wgpu::TextureUsage::TextureBinding;
	m_dummy = dev.CreateTexture(&dd);
	m_dummyView = m_dummy.CreateView();
	return module && m_seedPipeline && m_linePipeline && m_dummyView;
}

void DawnLinePass::resize(int sceneWidth, int sceneHeight)
{
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.lines.seed";
	td.size = {static_cast<std::uint32_t>(std::max(sceneWidth, 1)), static_cast<std::uint32_t>(std::max(sceneHeight, 1)), 1};
	td.format = kSeedFormat;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	m_seed = m_ctx.device.CreateTexture(&td);
	m_seedView = m_seed.CreateView();
}

void DawnLinePass::bind(const wgpu::TextureView& sceneNormal, const wgpu::TextureView& sceneDepth)
{
	// 種のパスは種の絵へ書くので、同じ絵を読む欄には空の絵を結ぶ。線のパスは本物の種を読む
	const auto makeGroup = [&](const wgpu::TextureView& seedSlot) {
		wgpu::BindGroupEntry be[4]{};
		be[0].binding = 0;
		be[0].buffer = m_cbBuffer;
		be[0].size = sizeof(LineCB);
		be[1].binding = 1;
		be[1].textureView = sceneDepth;
		be[2].binding = 2;
		be[2].textureView = sceneNormal;
		be[3].binding = 3;
		be[3].textureView = seedSlot;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = m_layout;
		bg.entryCount = 4;
		bg.entries = be;
		return m_ctx.device.CreateBindGroup(&bg);
	};
	m_seedGroup = makeGroup(m_dummyView);
	m_lineGroup = makeGroup(m_seedView);
}

void DawnLinePass::encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& finalView, const LineCB& cb)
{
	if (!m_lineGroup) { return; }
	m_ctx.queue.WriteBuffer(m_cbBuffer, 0, &cb, sizeof(cb));

	wgpu::RenderPassColorAttachment seedColor{};
	seedColor.view = m_seedView;
	seedColor.loadOp = wgpu::LoadOp::Clear;
	seedColor.storeOp = wgpu::StoreOp::Store;
	seedColor.clearValue = {0.0, 0.0, 0.0, 0.0};
	wgpu::RenderPassDescriptor seedPass{};
	seedPass.colorAttachmentCount = 1;
	seedPass.colorAttachments = &seedColor;
	wgpu::RenderPassEncoder seed = encoder.BeginRenderPass(&seedPass);
	seed.SetPipeline(m_seedPipeline);
	seed.SetBindGroup(0, m_seedGroup);
	seed.Draw(3);
	seed.End();

	wgpu::RenderPassColorAttachment out{};
	out.view = finalView;
	out.loadOp = wgpu::LoadOp::Load;
	out.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor linePass{};
	linePass.colorAttachmentCount = 1;
	linePass.colorAttachments = &out;
	wgpu::RenderPassEncoder line = encoder.BeginRenderPass(&linePass);
	line.SetPipeline(m_linePipeline);
	line.SetBindGroup(0, m_lineGroup);
	line.Draw(3);
	line.End();
}

} // namespace mitiru::render::dawn_detail
