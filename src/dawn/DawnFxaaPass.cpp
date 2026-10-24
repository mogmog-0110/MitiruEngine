#include "DawnFxaaPass.hpp"

#include <algorithm>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

// DX12 の既定値に合わせる。
constexpr float kSubpixQuality = 0.75f;
constexpr float kEdgeThreshold = 0.166f;
constexpr float kEdgeThresholdMin = 0.0833f;

} // namespace

bool DawnFxaaPass::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[3]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[2].binding = 2;
	entries[2].visibility = wgpu::ShaderStage::Fragment;
	entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 3;
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
	wgpu::BufferDescriptor bd{};
	bd.size = 256;
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_fxaa(), "mitiru.fxaa");
	wgpu::ColorTargetState target{};
	target.format = m_format;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsFxaa";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsFxaa";
	rp.fragment = &fs;
	m_pipeline.start(m_ctx, rp, "fxaa");
	return module && m_pipeline;
}

void DawnFxaaPass::resize(int width, int height)
{
	m_width = std::max(width, 1);
	m_height = std::max(height, 1);
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.fxaa.copy";
	td.size = {static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	td.format = m_format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_copy = m_ctx.device.CreateTexture(&td);
	const float cb[8] = {1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), kSubpixQuality, kEdgeThreshold,
	                     kEdgeThresholdMin, 0.0f, 0.0f, 0.0f};
	m_ctx.queue.WriteBuffer(m_cb, 0, cb, sizeof(cb));
	wgpu::BindGroupEntry be[3]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = sizeof(cb);
	be[1].binding = 1;
	be[1].textureView = m_copy.CreateView();
	be[2].binding = 2;
	be[2].sampler = m_sampler;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 3;
	bg.entries = be;
	m_group = m_ctx.device.CreateBindGroup(&bg);
}

void DawnFxaaPass::encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView)
{
	wgpu::TexelCopyTextureInfo src{};
	src.texture = target;
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_copy;
	const wgpu::Extent3D size{static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	encoder.CopyTextureToTexture(&src, &dst, &size);

	wgpu::RenderPassColorAttachment color{};
	color.view = targetView;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_pipeline);
	pass.SetBindGroup(0, m_group);
	pass.Draw(3);
	pass.End();
}

} // namespace mitiru::render::dawn_detail
