#pragma once

// SMAA 1x。DX12 の DX12PipelineStates_Smaa.inl に相当する。最終色をコピーし、辺 → 重み → 近傍の混ぜの 3 パスで最終色へ書き戻す。
// トーンマップと輪郭線の後、2D を重ねる前に呼ぶ。

#include <webgpu/webgpu_cpp.h>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnSmaaPass
{
public:
	DawnSmaaPass(gfx::DawnContext& ctx, wgpu::TextureFormat format) : m_ctx(ctx), m_format(format) {}

	/// 面積と探索の表が無いビルドは false
	[[nodiscard]] bool create();

	/// 最終色の大きさが変わったときだけ呼ぶ
	void resize(int width, int height);

	/// CopySrc と RenderAttachment を持つ target に SMAA を掛ける
	void encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView);

private:
	[[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& color, const wgpu::TextureView& edges,
	                                        const wgpu::TextureView& blend) const;
	void pass(wgpu::CommandEncoder& encoder, const wgpu::TextureView& view, const gfx::AsyncRenderPipeline& pipeline,
	          const wgpu::BindGroup& group, bool clear) const;

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	gfx::AsyncRenderPipeline m_edgePipeline;
	gfx::AsyncRenderPipeline m_weightPipeline;
	gfx::AsyncRenderPipeline m_blendPipeline;
	wgpu::Sampler m_linear;
	wgpu::Sampler m_point;
	wgpu::Buffer m_cb;
	wgpu::Texture m_area;
	wgpu::Texture m_search;
	wgpu::TextureView m_areaView;
	wgpu::TextureView m_searchView;
	wgpu::Texture m_copy;
	wgpu::Texture m_edges;
	wgpu::Texture m_weights;
	wgpu::TextureView m_copyView;
	wgpu::TextureView m_edgesView;
	wgpu::TextureView m_weightsView;
	wgpu::BindGroup m_edgeGroup;
	wgpu::BindGroup m_weightGroup;
	wgpu::BindGroup m_blendGroup;
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::render::dawn_detail
