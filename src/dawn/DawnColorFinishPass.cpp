#include "DawnColorFinishPass.hpp"

#include <algorithm>
#include <utility>

#include "DawnRenderTypes.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

bool DawnColorFinishPass::create()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry entries[3]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(ColorFinishCb);
	for (std::uint32_t i = 1; i < 3; ++i)
	{
		entries[i].binding = i;
		entries[i].visibility = wgpu::ShaderStage::Fragment;
		entries[i].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	}
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 3;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::BufferDescriptor bd{};
	bd.size = sizeof(ColorFinishCb);
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	wgpu::TextureDescriptor nd{};
	nd.label = "mitiru.colorfinish.nolut";
	nd.size = {1, 1, 1};
	nd.format = wgpu::TextureFormat::RGBA8Unorm;
	nd.usage = wgpu::TextureUsage::TextureBinding;
	m_noLut = dev.CreateTexture(&nd);
	m_noLutView = m_noLut.CreateView();

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_colorfinish(), "mitiru.colorfinish");
	wgpu::ColorTargetState target{};
	target.format = m_format;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsColorFinish";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsColorFinish";
	rp.fragment = &fs;
	m_pipeline.start(m_ctx, rp, "colorfinish");
	return module && m_pipeline && m_noLutView;
}

void DawnColorFinishPass::setLooks(ColorLookSet looks)
{
	m_looks = std::move(looks);
	m_lutTextures.assign(m_looks.looks.size(), wgpu::Texture{});
	m_lutViews.assign(m_looks.looks.size(), wgpu::TextureView{});
	for (std::size_t i = 0; i < m_looks.luts.size(); ++i)
	{
		const ColorLutImage& lut = m_looks.luts[i];
		if (!lut.valid()) { continue; }
		const std::uint32_t n = static_cast<std::uint32_t>(lut.size);
		wgpu::TextureDescriptor td{};
		td.label = "mitiru.colorfinish.lut";
		td.size = {n * n, n, 1};
		td.format = wgpu::TextureFormat::RGBA8Unorm;   // sRGB 符号化のままの値を線形として読む
		td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		m_lutTextures[i] = m_ctx.device.CreateTexture(&td);
		wgpu::TexelCopyTextureInfo dst{};
		dst.texture = m_lutTextures[i];
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = n * n * 4;
		layout.rowsPerImage = n;
		const wgpu::Extent3D extent{n * n, n, 1};
		m_ctx.queue.WriteTexture(&dst, lut.rgba.data(), lut.rgba.size(), &layout, &extent);
		m_lutViews[i] = m_lutTextures[i].CreateView();
	}
	rebuildGroups();
}

void DawnColorFinishPass::resize(int outWidth, int outHeight)
{
	m_width = std::max(outWidth, 1);
	m_height = std::max(outHeight, 1);
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.colorfinish.copy";
	td.size = {static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	td.format = m_format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_copy = m_ctx.device.CreateTexture(&td);
	m_copyView = m_copy.CreateView();
	rebuildGroups();
}

void DawnColorFinishPass::rebuildGroups()
{
	m_groups.assign(m_looks.looks.size(), wgpu::BindGroup{});
	if (!m_copyView) { return; }
	for (std::size_t i = 0; i < m_groups.size(); ++i)
	{
		wgpu::BindGroupEntry be[3]{};
		be[0].binding = 0;
		be[0].buffer = m_cb;
		be[0].size = sizeof(ColorFinishCb);
		be[1].binding = 1;
		be[1].textureView = m_copyView;
		be[2].binding = 2;
		be[2].textureView = m_lutViews[i] ? m_lutViews[i] : m_noLutView;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = m_layout;
		bg.entryCount = 3;
		bg.entries = be;
		m_groups[i] = m_ctx.device.CreateBindGroup(&bg);
	}
}

bool DawnColorFinishPass::encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView,
                                 int number, std::uint32_t seed)
{
	const ColorLookSpec* spec = m_looks.find(number);
	if (spec == nullptr) { return false; }
	const std::size_t index = static_cast<std::size_t>(number - 1);
	if (index >= m_groups.size() || !m_groups[index]) { return false; }
	const ColorLutImage* lut = m_lutViews[index] ? &m_looks.luts[index] : nullptr;
	const ColorFinishCb cb = makeColorFinishCb(*spec, lut, m_width, m_height, seed);
	if (!colorFinishActive(cb)) { return false; }
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
	pass.SetBindGroup(0, m_groups[index]);
	pass.Draw(3);
	pass.End();
	return true;
}

} // namespace mitiru::render::dawn_detail
