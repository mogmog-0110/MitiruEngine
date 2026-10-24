#include "DawnBloomPass.hpp"

#include <algorithm>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

wgpu::Texture makeTarget(const wgpu::Device& device, wgpu::TextureFormat format, int w, int h, const char* label)
{
	wgpu::TextureDescriptor td{};
	td.label = label;
	td.size = {static_cast<std::uint32_t>(std::max(w, 1)), static_cast<std::uint32_t>(std::max(h, 1)), 1};
	td.format = format;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	return device.CreateTexture(&td);
}

} // namespace

bool DawnBloomPass::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[4]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(BloomCB);
	for (std::uint32_t i = 1; i <= 2; ++i)
	{
		entries[i].binding = i;
		entries[i].visibility = wgpu::ShaderStage::Fragment;
		entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
	}
	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Fragment;
	entries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 4;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	m_sampler = dev.CreateSampler(&sd);

	for (wgpu::Buffer& b : m_cb)
	{
		wgpu::BufferDescriptor bd{};
		bd.size = 256;
		bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		b = dev.CreateBuffer(&bd);
	}

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_bloom(), "mitiru.bloom");
	wgpu::ColorTargetState target{};
	target.format = m_format;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsBloom";
	rp.fragment = &fs;
	fs.entryPoint = "fsBloomDown";
	m_downPipeline.start(m_ctx, rp, "bloom.down");
	fs.entryPoint = "fsBloomUp";
	m_upPipeline.start(m_ctx, rp, "bloom.up");
	return module && m_downPipeline && m_upPipeline;
}

wgpu::BindGroup DawnBloomPass::makeGroup(const wgpu::Buffer& cb, const wgpu::TextureView& src, const wgpu::TextureView& add)
{
	wgpu::BindGroupEntry be[4]{};
	be[0].binding = 0;
	be[0].buffer = cb;
	be[0].size = sizeof(BloomCB);
	be[1].binding = 1;
	be[1].textureView = src;
	be[2].binding = 2;
	be[2].textureView = add;
	be[3].binding = 3;
	be[3].sampler = m_sampler;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 4;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

void DawnBloomPass::collectSources(DescriptorSources& out) const
{
	out.add("bloom.half", m_halfA);
	out.add("bloom.quarter", m_quarter);
}

void DawnBloomPass::bind(const wgpu::TextureView& resolved)
{
	// 縮小は resolved → 1/2 (しきい値) → 1/4、戻しは 1/4 を広げて 1/2 を足す
	m_groups[0] = makeGroup(m_cb[0], resolved, resolved);
	m_groups[1] = makeGroup(m_cb[1], m_halfAView, m_halfAView);
	m_groups[2] = makeGroup(m_cb[2], m_quarterView, m_halfAView);
}

void DawnBloomPass::resize(int width, int height)
{
	m_width = std::max(width, 1);
	m_height = std::max(height, 1);
	const int hw = std::max(m_width / 2, 1);
	const int hh = std::max(m_height / 2, 1);
	const int qw = std::max(hw / 2, 1);
	const int qh = std::max(hh / 2, 1);
	m_halfA = makeTarget(m_ctx.device, m_format, hw, hh, "mitiru.bloom.half");
	m_quarter = makeTarget(m_ctx.device, m_format, qw, qh, "mitiru.bloom.quarter");
	m_halfB = makeTarget(m_ctx.device, m_format, hw, hh, "mitiru.bloom.result");
	m_halfAView = m_halfA.CreateView();
	m_quarterView = m_quarter.CreateView();
	m_halfBView = m_halfB.CreateView();
	const auto write = [&](int i, float tx, float ty, float threshold, float apply) {
		const BloomCB cb{{tx, ty, threshold, apply}};
		m_ctx.queue.WriteBuffer(m_cb[i], 0, &cb, sizeof(cb));
	};
	write(0, 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 1.0f, 1.0f);
	write(1, 1.0f / static_cast<float>(hw), 1.0f / static_cast<float>(hh), 0.0f, 0.0f);
	write(2, 1.0f / static_cast<float>(qw), 1.0f / static_cast<float>(qh), 0.0f, 0.0f);
}

void DawnBloomPass::runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::BindGroup& group,
                            const wgpu::TextureView& target)
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
	pass.SetPipeline(pipeline);
	pass.SetBindGroup(0, group);
	pass.Draw(3);
	pass.End();
}

void DawnBloomPass::encode(wgpu::CommandEncoder& encoder, float threshold)
{
	if (!m_groups[0]) { return; }
	const BloomCB first{{1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), threshold, 1.0f}};
	m_ctx.queue.WriteBuffer(m_cb[0], 0, &first, sizeof(first));
	runPass(encoder, m_downPipeline, m_groups[0], m_halfAView);
	runPass(encoder, m_downPipeline, m_groups[1], m_quarterView);
	runPass(encoder, m_upPipeline, m_groups[2], m_halfBView);
}

} // namespace mitiru::render::dawn_detail
