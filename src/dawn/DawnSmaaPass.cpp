#include "DawnSmaaPass.hpp"

#include <algorithm>

#include <mitiru/render/SmaaTables.hpp>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kBindingCount = 8;

wgpu::Texture makeTexture(const wgpu::Device& dev, const char* label, wgpu::TextureFormat format, int w, int h,
                          wgpu::TextureUsage usage)
{
	wgpu::TextureDescriptor td{};
	td.label = label;
	td.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
	td.format = format;
	td.usage = usage;
	return dev.CreateTexture(&td);
}

void writeTexels(const gfx::DawnContext& ctx, const wgpu::Texture& texture, const std::uint8_t* data, int w, int h,
                 int bytesPerTexel)
{
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = static_cast<std::uint32_t>(w * bytesPerTexel);
	layout.rowsPerImage = static_cast<std::uint32_t>(h);
	const wgpu::Extent3D extent{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
	ctx.queue.WriteTexture(&dst, data, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(bytesPerTexel),
	                       &layout, &extent);
}

} // namespace

bool DawnSmaaPass::create()
{
	const SmaaTables tables = smaaTables();
	if (!tables.available()) { return false; }
	const auto& dev = m_ctx.device;

	wgpu::BindGroupLayoutEntry entries[kBindingCount]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	for (std::uint32_t i = 1; i <= 5; ++i)
	{
		entries[i].binding = i;
		entries[i].visibility = wgpu::ShaderStage::Fragment;
		entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
	}
	entries[6].binding = 6;
	entries[6].visibility = wgpu::ShaderStage::Fragment;
	entries[6].sampler.type = wgpu::SamplerBindingType::Filtering;
	entries[7].binding = 7;
	entries[7].visibility = wgpu::ShaderStage::Fragment;
	entries[7].sampler.type = wgpu::SamplerBindingType::NonFiltering;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = kBindingCount;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	m_linear = dev.CreateSampler(&sd);
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Nearest;
	m_point = dev.CreateSampler(&sd);
	wgpu::BufferDescriptor bd{};
	bd.size = 256;
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	const auto sampled = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_area = makeTexture(dev, "mitiru.smaa.area", wgpu::TextureFormat::RG8Unorm, kSmaaAreaWidth, kSmaaAreaHeight, sampled);
	m_search = makeTexture(dev, "mitiru.smaa.search", wgpu::TextureFormat::R8Unorm, kSmaaSearchWidth, kSmaaSearchHeight, sampled);
	writeTexels(m_ctx, m_area, tables.area, kSmaaAreaWidth, kSmaaAreaHeight, 2);
	writeTexels(m_ctx, m_search, tables.search, kSmaaSearchWidth, kSmaaSearchHeight, 1);
	m_areaView = m_area.CreateView();
	m_searchView = m_search.CreateView();

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_smaa(), "mitiru.smaa");
	const auto start = [&](gfx::AsyncRenderPipeline& out, const char* fragment, wgpu::TextureFormat format, const char* label) {
		wgpu::ColorTargetState target{};
		target.format = format;
		wgpu::FragmentState fs{};
		fs.module = module;
		fs.entryPoint = fragment;
		fs.targetCount = 1;
		fs.targets = &target;
		wgpu::RenderPipelineDescriptor rp{};
		rp.layout = layout;
		rp.vertex.module = module;
		rp.vertex.entryPoint = "vsSmaa";
		rp.fragment = &fs;
		out.start(m_ctx, rp, label);
	};
	start(m_edgePipeline, "fsSmaaEdge", wgpu::TextureFormat::RG8Unorm, "smaa.edge");
	start(m_weightPipeline, "fsSmaaWeight", wgpu::TextureFormat::RGBA8Unorm, "smaa.weight");
	start(m_blendPipeline, "fsSmaaBlend", m_format, "smaa.blend");
	return static_cast<bool>(module);
}

wgpu::BindGroup DawnSmaaPass::makeGroup(const wgpu::TextureView& color, const wgpu::TextureView& edges,
                                        const wgpu::TextureView& blend) const
{
	wgpu::BindGroupEntry be[kBindingCount]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = 16;
	be[1].binding = 1;
	be[1].textureView = color;
	be[2].binding = 2;
	be[2].textureView = edges;
	be[3].binding = 3;
	be[3].textureView = blend;
	be[4].binding = 4;
	be[4].textureView = m_areaView;
	be[5].binding = 5;
	be[5].textureView = m_searchView;
	be[6].binding = 6;
	be[6].sampler = m_linear;
	be[7].binding = 7;
	be[7].sampler = m_point;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = kBindingCount;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

void DawnSmaaPass::resize(int width, int height)
{
	m_width = std::max(width, 1);
	m_height = std::max(height, 1);
	const auto target = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
	m_copy = makeTexture(m_ctx.device, "mitiru.smaa.copy", m_format, m_width, m_height,
	                     wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);
	m_edges = makeTexture(m_ctx.device, "mitiru.smaa.edges", wgpu::TextureFormat::RG8Unorm, m_width, m_height, target);
	m_weights = makeTexture(m_ctx.device, "mitiru.smaa.weights", wgpu::TextureFormat::RGBA8Unorm, m_width, m_height, target);
	m_copyView = m_copy.CreateView();
	m_edgesView = m_edges.CreateView();
	m_weightsView = m_weights.CreateView();
	const float w = static_cast<float>(m_width);
	const float h = static_cast<float>(m_height);
	const float cb[4] = {1.0f / w, 1.0f / h, w, h};
	m_ctx.queue.WriteBuffer(m_cb, 0, cb, sizeof(cb));
	// 同じパスで描画先にもなる絵は束ねられないので、そのパスが読まない枠には表の view (読むだけ) を詰める
	m_edgeGroup = makeGroup(m_copyView, m_areaView, m_areaView);
	m_weightGroup = makeGroup(m_areaView, m_edgesView, m_areaView);
	m_blendGroup = makeGroup(m_copyView, m_areaView, m_weightsView);
}

void DawnSmaaPass::pass(wgpu::CommandEncoder& encoder, const wgpu::TextureView& view, const gfx::AsyncRenderPipeline& pipeline,
                        const wgpu::BindGroup& group, bool clear) const
{
	wgpu::RenderPassColorAttachment color{};
	color.view = view;
	color.loadOp = clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	color.clearValue = {0.0, 0.0, 0.0, 0.0};
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder p = encoder.BeginRenderPass(&rp);
	p.SetPipeline(pipeline);
	p.SetBindGroup(0, group);
	p.Draw(3);
	p.End();
}

void DawnSmaaPass::encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView)
{
	wgpu::TexelCopyTextureInfo src{};
	src.texture = target;
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_copy;
	const wgpu::Extent3D size{static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	encoder.CopyTextureToTexture(&src, &dst, &size);
	pass(encoder, m_edgesView, m_edgePipeline, m_edgeGroup, true);
	pass(encoder, m_weightsView, m_weightPipeline, m_weightGroup, true);
	pass(encoder, targetView, m_blendPipeline, m_blendGroup, false);
}

} // namespace mitiru::render::dawn_detail
