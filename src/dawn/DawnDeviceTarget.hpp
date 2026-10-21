#pragma once

#include <webgpu/webgpu_cpp.h>

#include <mitiru/gfx/dawn/DawnDevice.hpp>

namespace mitiru::gfx
{

/// 最終の色。レンダラーのトーンマップと UI が書き、readPixels が読む。窓つきのときは、これを surface へ写す
struct DawnDevice::Target
{
	wgpu::Texture texture;
	wgpu::TextureView view;
	wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;

	wgpu::Surface surface;
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::BGRA8Unorm;
	wgpu::RenderPipeline blitPipeline;
	wgpu::BindGroupLayout blitLayout;
	wgpu::BindGroup blitGroup;
};

} // namespace mitiru::gfx
