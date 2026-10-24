#include "DawnSkyPass.hpp"

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr wgpu::TextureFormat kLutFormat = wgpu::TextureFormat::RGBA16Float;
constexpr std::uint32_t kTransmittanceW = 256;
constexpr std::uint32_t kTransmittanceH = 64;
constexpr std::uint32_t kMultiSize = 32;
constexpr std::uint32_t kSkyViewW = 192;
constexpr std::uint32_t kSkyViewH = 108;

void set4(float out[4], float x, float y, float z, float w) noexcept
{
	out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

} // namespace

wgpu::Texture DawnSkyPass::makeLut(std::uint32_t width, std::uint32_t height) const
{
	wgpu::TextureDescriptor td{};
	td.size = {width, height, 1};
	td.format = kLutFormat;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	return m_ctx.device.CreateTexture(&td);
}

wgpu::BindGroup DawnSkyPass::makeGroup(const wgpu::TextureView& transmittance, const wgpu::TextureView& multi,
                                       const wgpu::TextureView& skyView) const
{
	wgpu::BindGroupEntry be[5]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = sizeof(SkyCB);
	be[1].binding = 1;
	be[1].sampler = m_sampler;
	be[2].binding = 2;
	be[2].textureView = transmittance;
	be[3].binding = 3;
	be[3].textureView = multi;
	be[4].binding = 4;
	be[4].textureView = skyView;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 5;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

void DawnSkyPass::startLutPipeline(gfx::AsyncRenderPipeline& out, const char* entry) const
{
	wgpu::ColorTargetState target{};
	target.format = kLutFormat;
	wgpu::FragmentState fs{};
	fs.module = m_module;
	fs.entryPoint = entry;
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_pipelineLayout;
	rp.vertex.module = m_module;
	rp.vertex.entryPoint = "vsFull";
	rp.fragment = &fs;
	out.start(m_ctx, rp, entry);
}

bool DawnSkyPass::create()
{
	const auto& dev = m_ctx.device;
	m_module = m_ctx.makeShader(dawnShaderSource_sky(), "mitiru.sky");
	wgpu::BindGroupLayoutEntry entries[5]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(SkyCB);
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
	for (std::uint32_t i = 2; i <= 4; ++i)
	{
		entries[i].binding = i;
		entries[i].visibility = wgpu::ShaderStage::Fragment;
		entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
	}
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	m_pipelineLayout = dev.CreatePipelineLayout(&pl);

	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	m_sampler = dev.CreateSampler(&sd);
	wgpu::BufferDescriptor bd{};
	bd.size = 256;
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	startLutPipeline(m_transmittancePipeline, "fsTransmittance");
	startLutPipeline(m_multiPipeline, "fsMultiScatter");
	startLutPipeline(m_viewPipeline, "fsSkyView");

	// 場面の 3 枚の MSAA (色、法線と印、距離)へ、深度を読み書きせずに描く
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::False;
	ds.depthCompare = wgpu::CompareFunction::Always;
	wgpu::ColorTargetState targets[3]{};
	targets[0].format = m_hdrFormat;
	targets[1].format = kLineGBufferFormat;
	targets[2].format = kLineGBufferFormat;
	wgpu::FragmentState fs{};
	fs.module = m_module;
	fs.entryPoint = "fsSkyDraw";
	fs.targetCount = 3;
	fs.targets = targets;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_pipelineLayout;
	rp.vertex.module = m_module;
	rp.vertex.entryPoint = "vsFull";
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.fragment = &fs;
	m_drawPipeline.start(m_ctx, rp, "sky.draw");

	// 空気遠近は散乱と透過率を書き、色 × 透過率 + 散乱で合成する
	wgpu::BindGroupLayoutEntry ae[6]{};
	for (int i = 0; i < 5; ++i) { ae[i] = entries[i]; }
	ae[5].binding = 5;
	ae[5].visibility = wgpu::ShaderStage::Fragment;
	ae[5].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	ae[5].texture.multisampled = true;
	wgpu::BindGroupLayoutDescriptor abgl{};
	abgl.entryCount = 6;
	abgl.entries = ae;
	m_aerialLayout = dev.CreateBindGroupLayout(&abgl);
	wgpu::PipelineLayoutDescriptor apl{};
	apl.bindGroupLayoutCount = 1;
	apl.bindGroupLayouts = &m_aerialLayout;
	wgpu::BlendState compose{};
	compose.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::SrcAlpha};
	compose.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One};
	wgpu::ColorTargetState ct{};
	ct.format = m_hdrFormat;
	ct.blend = &compose;
	wgpu::FragmentState afs{};
	afs.module = m_module;
	afs.entryPoint = "fsAerial";
	afs.targetCount = 1;
	afs.targets = &ct;
	wgpu::RenderPipelineDescriptor arp{};
	arp.layout = dev.CreatePipelineLayout(&apl);
	arp.vertex.module = m_module;
	arp.vertex.entryPoint = "vsFull";
	arp.fragment = &afs;
	m_aerialPipeline.start(m_ctx, arp, "sky.aerial");

	m_transmittance = makeLut(kTransmittanceW, kTransmittanceH);
	m_multi = makeLut(kMultiSize, kMultiSize);
	m_skyView = makeLut(kSkyViewW, kSkyViewH);
	m_dummy = makeLut(1, 1);
	m_transmittanceView = m_transmittance.CreateView();
	m_multiView = m_multi.CreateView();
	m_skyViewView = m_skyView.CreateView();
	const wgpu::TextureView dummy = m_dummy.CreateView();
	m_dummyView = dummy;
	// LUT を描くパスでは、まだ描いていない LUT の欄に空の絵を結び、描き込み先を同じパスで読まないようにする
	m_groupTransmittance = makeGroup(dummy, dummy, dummy);
	m_groupMulti = makeGroup(m_transmittanceView, dummy, dummy);
	m_groupView = makeGroup(m_transmittanceView, m_multiView, dummy);
	m_groupDraw = makeGroup(m_transmittanceView, m_multiView, m_skyViewView);
	return m_module && m_transmittancePipeline && m_multiPipeline && m_viewPipeline && m_drawPipeline && m_aerialPipeline;
}

void DawnSkyPass::runLut(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, const wgpu::RenderPipeline& pipeline,
                         const wgpu::BindGroup& group) const
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

void DawnSkyPass::encodeLuts(wgpu::CommandEncoder& encoder, const SkySettings& sky, const sgc::Vec3f& sunDir, const SkyView& view)
{
	const AtmosphereParams& a = sky.atmosphere;
	SkyCB cb{};
	set4(cb.cameraKm, 0.0f, a.bottomRadiusKm + view.altitudeKm, 0.0f, sky.metersPerUnit * 0.001f);
	set4(cb.sunDir, sunDir.x, sunDir.y, sunDir.z, sky.sunIlluminance);
	set4(cb.rayleigh, a.rayleighScattering.x, a.rayleighScattering.y, a.rayleighScattering.z, a.rayleighScaleHeightKm);
	set4(cb.mie, a.mieScattering, a.mieExtinction, a.mieScaleHeightKm, a.mieG);
	set4(cb.ozone, a.ozoneAbsorption.x, a.ozoneAbsorption.y, a.ozoneAbsorption.z, a.ozoneCenterKm);
	set4(cb.ground, a.groundAlbedo.x, a.groundAlbedo.y, a.groundAlbedo.z, a.ozoneHalfWidthKm);
	set4(cb.radii, a.bottomRadiusKm, a.topRadiusKm, sky.sunDiskAngularRadius, static_cast<float>(sky.toonBands));
	set4(cb.camForward, view.forward[0], view.forward[1], view.forward[2], m_hdrScale);
	set4(cb.camRight, view.right[0], view.right[1], view.right[2], sky.aerialPerspectiveScale);
	set4(cb.camUp, view.up[0], view.up[1], view.up[2], view.farClip);
	m_ctx.queue.WriteBuffer(m_cb, 0, &cb, sizeof(cb));

	if (!m_lutValid || std::memcmp(&m_lutParams, &a, sizeof(AtmosphereParams)) != 0)
	{
		runLut(encoder, m_transmittanceView, m_transmittancePipeline, m_groupTransmittance);
		runLut(encoder, m_multiView, m_multiPipeline, m_groupMulti);
		m_lutParams = a;
		m_lutValid = true;
	}
	runLut(encoder, m_skyViewView, m_viewPipeline, m_groupView);
}

void DawnSkyPass::bindSceneDepth(const wgpu::TextureView& lineDepth)
{
	wgpu::BindGroupEntry be[6]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = sizeof(SkyCB);
	be[1].binding = 1;
	be[1].sampler = m_sampler;
	be[2].binding = 2;
	be[2].textureView = m_transmittanceView;
	be[3].binding = 3;
	be[3].textureView = m_multiView;
	be[4].binding = 4;
	be[4].textureView = m_skyViewView;
	be[5].binding = 5;
	be[5].textureView = lineDepth;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_aerialLayout;
	bg.entryCount = 6;
	bg.entries = be;
	m_groupAerial = m_ctx.device.CreateBindGroup(&bg);
}

void DawnSkyPass::encodeAerial(wgpu::CommandEncoder& encoder, const wgpu::TextureView& resolved) const
{
	wgpu::RenderPassColorAttachment color{};
	color.view = resolved;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_aerialPipeline);
	pass.SetBindGroup(0, m_groupAerial);
	pass.Draw(3);
	pass.End();
}

void DawnSkyPass::draw(wgpu::RenderPassEncoder& pass) const
{
	pass.SetPipeline(m_drawPipeline);
	pass.SetBindGroup(0, m_groupDraw);
	pass.Draw(3);
}

} // namespace mitiru::render::dawn_detail
