#pragma once

// FXAA 3.11 の宣言。DX12 の DX12PipelineStates_PostProcess.inl にある drawFXAAPass に相当する。
// 最終色をコピーし、コピーから読み取って最終色へ書き戻す。
// トーンマップと輪郭線の後、2D を重ねる前に呼ぶ。

#include <webgpu/webgpu_cpp.h>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnFxaaPass
{
public:
	DawnFxaaPass(gfx::DawnContext& ctx, wgpu::TextureFormat format) : m_ctx(ctx), m_format(format) {}

	[[nodiscard]] bool create();

	/// 最終色の大きさが変わったときだけ呼ぶ
	void resize(int width, int height);

	/// CopySrc と RenderAttachment を持つ target に FXAA を掛ける
	void encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView);

private:
	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	wgpu::RenderPipeline m_pipeline;
	wgpu::Sampler m_sampler;
	wgpu::Buffer m_cb;
	wgpu::Texture m_copy;
	wgpu::BindGroup m_group;
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::render::dawn_detail
