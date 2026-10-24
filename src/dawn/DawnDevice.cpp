#include <mitiru/gfx/dawn/DawnDevice.hpp>

#include <algorithm>
#include <stdexcept>

#include <cstdlib>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/BootTrace.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>

#include "DawnContext.hpp"
#include "DawnDeviceTarget.hpp"
#include "DawnProfile.hpp"
#include "DawnShaderSources.inc"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mitiru::gfx
{

DawnDevice::DawnDevice(int width, int height, DawnApi api)
	: m_width(std::max(width, 1)), m_height(std::max(height, 1)), m_api(api)
{
	std::string error;
	m_context = DawnContext::create(api, error);
	if (!m_context) { throw std::runtime_error(error); }
	m_description = m_context->description;
	console::verbosef("Dawn: %s で動かします (%s、%s)", std::string(dawnApiName(api)).c_str(), m_description.c_str(),
	                  m_context->compat ? "互換レベル" : "通常のレベル");
	m_target = std::make_unique<Target>();
	createTarget();
	debug::boottrace::mark("dawn_target");
	// device lost の扱いを試すための入口。DX12 の MITIRU_TEST_DX12_SETUP_FAIL と同じく、環境変数でだけ効く
	m_loseAtFrame = std::max(std::atoi(env::value("MITIRU_TEST_DAWN_LOSE_AT_FRAME").c_str()), 0);
}

DawnDevice::DawnDevice(std::uintptr_t nativeWindow, int width, int height, DawnApi api, bool vsync)
	: DawnDevice(width, height, api)
{
	m_windowed = true;
	m_vsync = vsync;

	wgpu::SurfaceSourceWindowsHWND source{};
	source.hinstance = GetModuleHandleW(nullptr);
	source.hwnd = reinterpret_cast<HWND>(nativeWindow);
	wgpu::SurfaceDescriptor descriptor{};
	descriptor.nextInChain = &source;
	m_target->surface = m_context->instance.CreateSurface(&descriptor);
	if (!m_target->surface) { throw std::runtime_error("Dawn の surface を作れない"); }

	wgpu::SurfaceCapabilities caps{};
	m_target->surface.GetCapabilities(m_context->adapter, &caps);
	// 色の並びは BGRA8 を優先する (DXGI の標準)。無ければ RGBA8、それも無ければ surface が先頭に挙げる形式
	m_target->surfaceFormat = caps.formatCount > 0 ? caps.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
	for (std::size_t i = 0; i < caps.formatCount; ++i)
	{
		if (caps.formats[i] == wgpu::TextureFormat::BGRA8Unorm) { m_target->surfaceFormat = caps.formats[i]; break; }
		if (caps.formats[i] == wgpu::TextureFormat::RGBA8Unorm) { m_target->surfaceFormat = caps.formats[i]; }
	}

	// 最終色 -> surface の 1 パス
	wgpu::BindGroupLayoutEntry le{};
	le.binding = 0;
	le.visibility = wgpu::ShaderStage::Fragment;
	le.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 1;
	bgl.entries = &le;
	m_target->blitLayout = m_context->device.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_target->blitLayout;
	wgpu::PipelineLayout layout = m_context->device.CreatePipelineLayout(&pl);
	wgpu::ShaderModule module = m_context->makeShader(dawnShaderSource_blit(), "mitiru.blit");
	wgpu::ColorTargetState target{};
	target.format = m_target->surfaceFormat;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsBlit";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsBlit";
	rp.fragment = &fs;
	m_target->blitPipeline = m_context->device.CreateRenderPipeline(&rp);
	if (!m_target->blitPipeline) { throw std::runtime_error("Dawn の提示のパイプラインを作れない"); }
	createTarget();
	configureSurface();
}

DawnDevice::~DawnDevice()
{
	if (m_context) { (void)m_context->waitIdle(); }
	if (m_target && m_target->surface) { m_target->surface.Unconfigure(); }
	m_target.reset();
}

bool DawnDevice::compatibilityLevel() const noexcept
{
	return m_context && m_context->compat;
}

bool DawnDevice::isDeviceLost() const noexcept
{
	return m_context && m_context->lost;
}

void DawnDevice::createTarget()
{
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.dawn.final";
	td.size = {static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1};
	td.format = m_target->format;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
	           wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
	m_target->texture = m_context->device.CreateTexture(&td);
	m_target->view = m_target->texture.CreateView();
	if (m_target->blitLayout)
	{
		wgpu::BindGroupEntry be{};
		be.binding = 0;
		be.textureView = m_target->view;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = m_target->blitLayout;
		bg.entryCount = 1;
		bg.entries = &be;
		m_target->blitGroup = m_context->device.CreateBindGroup(&bg);
	}
}

void DawnDevice::configureSurface()
{
	if (!m_windowed) { return; }
	wgpu::SurfaceConfiguration config{};
	config.device = m_context->device;
	config.format = m_target->surfaceFormat;
	config.usage = wgpu::TextureUsage::RenderAttachment;
	config.width = static_cast<std::uint32_t>(m_width);
	config.height = static_cast<std::uint32_t>(m_height);
	config.presentMode = m_vsync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate;
	config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
	m_target->surface.Configure(&config);
}

void DawnDevice::setVSync(bool enabled)
{
	if (m_vsync == enabled) { return; }
	m_vsync = enabled;
	if (m_windowed) { configureSurface(); }
}

void DawnDevice::onResize(int w, int h)
{
	if (w <= 0 || h <= 0 || (w == m_width && h == m_height)) { return; }
	(void)m_context->waitIdle();
	m_width = w;
	m_height = h;
	createTarget();
	configureSurface();
}

void DawnDevice::beginFrame()
{
	MITIRU_DAWN_ZONE("device.beginFrame");
	if (m_context->lost) { return; }
	// 3D を使わないフレームの背景。3D を使うフレームではトーンマップが全面を書き直すので、塗るのは最終色へ描く側が必要としたときまで遅らせる
	m_target->backgroundPending = true;
	for (int i = 0; i < 4; ++i) { m_target->clear[i] = m_clearColor[i]; }
}

void flushBackground(const DawnDevice& device)
{
	DawnDevice::Target& target = device.target();
	if (!target.backgroundPending) { return; }
	target.backgroundPending = false;
	const auto& context = *device.context();
	if (context.lost) { return; }
	wgpu::RenderPassColorAttachment color{};
	color.view = target.view;
	color.loadOp = wgpu::LoadOp::Clear;
	color.storeOp = wgpu::StoreOp::Store;
	color.clearValue = {target.clear[0], target.clear[1], target.clear[2], target.clear[3]};
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::CommandEncoder encoder = context.device.CreateCommandEncoder();
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.End();
	wgpu::CommandBuffer commands = encoder.Finish();
	context.queue.Submit(1, &commands);
}

// 最終色を surface の今のテクスチャへ写して提示する。surface が古い・失われた (リサイズ中、最小化) ときは、そのフレームを捨てる
void DawnDevice::present()
{
	MITIRU_DAWN_ZONE("device.present");
	flushBackground(*this);
	if (m_surfaceDirty)
	{
		m_surfaceDirty = false;
		configureSurface();
	}
	wgpu::SurfaceTexture current{};
	m_target->surface.GetCurrentTexture(&current);
	switch (current.status)
	{
	case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
	case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
		break;
	case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
	case wgpu::SurfaceGetCurrentTextureStatus::Lost:
		configureSurface();
		return;
	default:
		// 最小化の間などは取れない。次のフレームで設定し直して戻る
		m_surfaceDirty = true;
		return;
	}
	wgpu::RenderPassColorAttachment color{};
	color.view = current.texture.CreateView();
	color.loadOp = wgpu::LoadOp::Clear;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::CommandEncoder encoder = m_context->device.CreateCommandEncoder();
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_target->blitPipeline);
	pass.SetBindGroup(0, m_target->blitGroup);
	pass.Draw(3);
	pass.End();
	wgpu::CommandBuffer commands = encoder.Finish();
	m_context->queue.Submit(1, &commands);
	m_target->surface.Present();
}

void DawnDevice::endFrame()
{
	MITIRU_DAWN_ZONE("device.endFrame");
	if (m_windowed && !m_context->lost) { present(); }
	if (m_loseAtFrame > 0 && ++m_frames == m_loseAtFrame) { m_context->device.ForceLoss(wgpu::DeviceLostReason::Unknown, "MITIRU_TEST_DAWN_LOSE_AT_FRAME"); }
	{
		MITIRU_DAWN_ZONE("device.processEvents");
		m_context->instance.ProcessEvents();
	}
}

void DawnDevice::waitForGpu()
{
	(void)m_context->waitIdle();
}

std::vector<std::uint8_t> DawnDevice::readPixels(int width, int height) const
{
	flushBackground(*this);
	if (width <= 0 || height <= 0) { return {}; }
	const auto w = static_cast<std::uint32_t>(std::min(width, m_width));
	const auto h = static_cast<std::uint32_t>(std::min(height, m_height));
	if (w == 0 || h == 0) { return {}; }
	// 切り出す場合も、先に全体を読む。headless の撮影は窓と同じ大きさなので、通常は切り出さない
	if (w == static_cast<std::uint32_t>(m_width) && h == static_cast<std::uint32_t>(m_height))
	{
		return m_context->readTexture(m_target->texture, w, h);
	}
	const auto full = m_context->readTexture(m_target->texture, static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height));
	if (full.size() < static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4) { return {}; }
	std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
	for (std::uint32_t y = 0; y < h; ++y)
	{
		std::copy_n(full.begin() + static_cast<std::ptrdiff_t>(y) * m_width * 4, static_cast<std::ptrdiff_t>(w) * 4,
		            out.begin() + static_cast<std::ptrdiff_t>(y) * w * 4);
	}
	return out;
}

} // namespace mitiru::gfx
