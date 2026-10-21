#include "DawnViewSlots.hpp"

#include <algorithm>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

bool DawnViewBlit::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[2]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 2;
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

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_viewblit(), "mitiru.viewblit");
	wgpu::ColorTargetState target{};
	target.format = m_format;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsView";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsView";
	rp.fragment = &fs;
	m_pipeline = dev.CreateRenderPipeline(&rp);
	return module && m_pipeline && m_sampler;
}

wgpu::BindGroup DawnViewBlit::makeGroup(const wgpu::TextureView& source) const
{
	wgpu::BindGroupEntry be[2]{};
	be[0].binding = 0;
	be[0].textureView = source;
	be[1].binding = 1;
	be[1].sampler = m_sampler;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 2;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

void DawnViewBlit::encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, int targetWidth, int targetHeight,
                          const std::vector<Placement>& placements) const
{
	if (placements.empty()) { return; }
	wgpu::RenderPassColorAttachment color{};
	color.view = target;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_pipeline);
	const float maxX = static_cast<float>(targetWidth);
	const float maxY = static_cast<float>(targetHeight);
	for (const Placement& p : placements)
	{
		// viewport は描画先の範囲内に収める。大きく外れた矩形は貼り付けない。
		const float x0 = std::clamp(p.x, 0.0f, maxX);
		const float y0 = std::clamp(p.y, 0.0f, maxY);
		const float x1 = std::clamp(p.x + p.width, 0.0f, maxX);
		const float y1 = std::clamp(p.y + p.height, 0.0f, maxY);
		if (!(x1 - x0 >= 1.0f) || !(y1 - y0 >= 1.0f)) { continue; }
		pass.SetBindGroup(0, p.group);
		pass.SetViewport(x0, y0, x1 - x0, y1 - y0, 0.0f, 1.0f);
		pass.Draw(3);
	}
	pass.End();
}

} // namespace mitiru::render::dawn_detail
