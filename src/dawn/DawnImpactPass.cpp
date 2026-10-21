#include "DawnImpactPass.hpp"

#include <algorithm>

#include "DawnRenderTypes.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

struct ImpactCB
{
	float lightColor[4];
	float darkColor[4];
	float params[4];
	float keep[2][4];
};

} // namespace

bool DawnImpactPass::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[3]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(ImpactCB);
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	entries[2].binding = 2;
	entries[2].visibility = wgpu::ShaderStage::Fragment;
	entries[2].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	entries[2].texture.multisampled = true;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 3;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::BufferDescriptor bd{};
	bd.size = sizeof(ImpactCB);
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_impact(), "mitiru.impact");
	wgpu::ColorTargetState target{};
	target.format = m_format;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsImpact";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsImpact";
	rp.fragment = &fs;
	m_pipeline = dev.CreateRenderPipeline(&rp);
	return module && m_pipeline;
}

void DawnImpactPass::resize(int outWidth, int outHeight, int sceneWidth, int sceneHeight)
{
	m_width = std::max(outWidth, 1);
	m_height = std::max(outHeight, 1);
	m_sceneWidth = std::max(sceneWidth, 1);
	m_sceneHeight = std::max(sceneHeight, 1);
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.impact.copy";
	td.size = {static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	td.format = m_format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_copy = m_ctx.device.CreateTexture(&td);
	m_copyView = m_copy.CreateView();
}

void DawnImpactPass::bind(const wgpu::TextureView& sceneNormal)
{
	wgpu::BindGroupEntry be[3]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = sizeof(ImpactCB);
	be[1].binding = 1;
	be[1].textureView = m_copyView;
	be[2].binding = 2;
	be[2].textureView = sceneNormal;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 3;
	bg.entries = be;
	m_group = m_ctx.device.CreateBindGroup(&bg);
}

void DawnImpactPass::encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView,
                            const ImpactFrame& frame, std::uint32_t keepMask)
{
	if (!m_group) { return; }
	ImpactCB cb{};
	for (int i = 0; i < 3; ++i)
	{
		cb.lightColor[i] = frame.lightColor[i];
		cb.darkColor[i] = frame.darkColor[i];
	}
	cb.lightColor[3] = frame.threshold;
	cb.darkColor[3] = frame.amount;
	cb.params[0] = static_cast<float>(frame.mode);
	cb.params[1] = static_cast<float>(m_sceneWidth);
	cb.params[2] = static_cast<float>(m_sceneHeight);
	for (int i = 0; i < 8; ++i) { cb.keep[i / 4][i % 4] = ((keepMask >> i) & 1u) ? 1.0f : 0.0f; }
	m_ctx.queue.WriteBuffer(m_cb, 0, &cb, sizeof(cb));

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
