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

	// beginFrame が付ける「背景を塗る約束」。塗るのは最終色へ描く側が必要としたときまで遅らせる (flushBackground)。
	// 3D の最終パスは全面を書き直すので、そのフレームは塗りの提出を 1 回省ける (skipBackground)
	bool backgroundPending = false;
	float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

/// 背景がまだ塗られていなければ塗る (clear の色で、空の描画パスを 1 回提出する)。最終色へ Load で描く前に呼ぶ
void flushBackground(const DawnDevice& device);

/// 背景を塗る約束を捨てる。最終色の全面を Clear で書く描画の前に呼ぶ
inline void skipBackground(const DawnDevice& device) noexcept { device.target().backgroundPending = false; }

} // namespace mitiru::gfx
