#pragma once

// 場面の描き込み先。4x MSAA の色と深度、MSAA のまま残す輪郭線の下地 2 枚、resolve した HDR の色。
// 半透明の WBOIT の蓄積先は、半透明の描画が初めて出たときに作る (4x MSAA の 2 枚で大きいので、半透明の無い場面に持たせない)。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// WBOIT の蓄積先。MSAA のまま蓄積して resolve し (DX12 と同じく 4 標本の平均)、resolve した 2 枚を合成の束で読む。
struct DawnOitTargets
{
	wgpu::Texture accumMsaa;
	wgpu::Texture revealMsaa;
	wgpu::Texture accum;
	wgpu::Texture reveal;
	wgpu::TextureView accumMsaaView;
	wgpu::TextureView revealMsaaView;
	wgpu::TextureView accumView;
	wgpu::TextureView revealView;
	wgpu::BindGroup compositeGroup;

	[[nodiscard]] bool ready() const noexcept { return static_cast<bool>(compositeGroup); }

	void create(const gfx::DawnContext& ctx, const wgpu::BindGroupLayout& compositeLayout, std::uint32_t width, std::uint32_t height)
	{
		wgpu::TextureDescriptor td{};
		td.size = {width, height, 1};
		td.sampleCount = kSamples;
		td.format = kOitAccumFormat;
		td.usage = wgpu::TextureUsage::RenderAttachment;
		accumMsaa = ctx.device.CreateTexture(&td);
		td.format = kOitRevealFormat;
		revealMsaa = ctx.device.CreateTexture(&td);
		td.sampleCount = 1;
		td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
		td.format = kOitAccumFormat;
		accum = ctx.device.CreateTexture(&td);
		td.format = kOitRevealFormat;
		reveal = ctx.device.CreateTexture(&td);
		accumMsaaView = accumMsaa.CreateView();
		revealMsaaView = revealMsaa.CreateView();
		accumView = accum.CreateView();
		revealView = reveal.CreateView();
		wgpu::BindGroupEntry be[2]{};
		be[0].binding = 0;
		be[0].textureView = accumView;
		be[1].binding = 1;
		be[1].textureView = revealView;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = compositeLayout;
		bg.entryCount = 2;
		bg.entries = be;
		compositeGroup = ctx.device.CreateBindGroup(&bg);
	}
};

struct DawnSceneTargets
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	DawnOitTargets oit;
	wgpu::Texture silhouetteDepth;          ///< 透かしの影絵: 的を寄せた深度のいちばん手前 (4x MSAA)。影絵を描くフレームで初めて作る
	wgpu::TextureView silhouetteDepthView;
	wgpu::Texture colorMsaa;
	wgpu::Texture depthMsaa;
	wgpu::Texture normalMsaa;       ///< 輪郭線の下地: 法線 (rgb) と描画の印 (a)
	wgpu::Texture lineDepthMsaa;    ///< 輪郭線の下地: 線形の距離を 24 bit に詰めた色
	wgpu::Texture resolved;
	wgpu::TextureView colorMsaaView;
	wgpu::TextureView depthMsaaView;
	wgpu::TextureView normalMsaaView;
	wgpu::TextureView lineDepthMsaaView;
	wgpu::TextureView resolvedView;

	void create(const gfx::DawnContext& ctx, wgpu::TextureFormat hdrFormat, std::uint32_t w, std::uint32_t h)
	{
		width = w;
		height = h;
		oit = DawnOitTargets{};
		silhouetteDepth = nullptr;
		silhouetteDepthView = nullptr;
		wgpu::TextureDescriptor td{};
		td.size = {width, height, 1};
		td.sampleCount = kSamples;
		td.format = hdrFormat;
		td.usage = wgpu::TextureUsage::RenderAttachment;
		colorMsaa = ctx.device.CreateTexture(&td);
		td.format = kDepthFormat;
		depthMsaa = ctx.device.CreateTexture(&td);
		// 輪郭線の下地はあとで読むため、resolve せず MSAA のまま残す。サンプル 0 だけを読む。
		td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
		td.format = kLineGBufferFormat;
		normalMsaa = ctx.device.CreateTexture(&td);
		lineDepthMsaa = ctx.device.CreateTexture(&td);
		td.sampleCount = 1;
		td.format = hdrFormat;
		resolved = ctx.device.CreateTexture(&td);
		colorMsaaView = colorMsaa.CreateView();
		depthMsaaView = depthMsaa.CreateView();
		normalMsaaView = normalMsaa.CreateView();
		lineDepthMsaaView = lineDepthMsaa.CreateView();
		resolvedView = resolved.CreateView();
	}

	void ensureSilhouetteDepth(const gfx::DawnContext& ctx)
	{
		if (silhouetteDepth) { return; }
		wgpu::TextureDescriptor td{};
		td.label = "mitiru.silhouette.depth";
		td.size = {width, height, 1};
		td.sampleCount = kSamples;
		td.format = kDepthFormat;
		td.usage = wgpu::TextureUsage::RenderAttachment;
		silhouetteDepth = ctx.device.CreateTexture(&td);
		silhouetteDepthView = silhouetteDepth.CreateView();
	}

	void ensureOit(const gfx::DawnContext& ctx, const wgpu::BindGroupLayout& compositeLayout)
	{
		if (!oit.ready()) { oit.create(ctx, compositeLayout, width, height); }
	}
};

} // namespace mitiru::render::dawn_detail
