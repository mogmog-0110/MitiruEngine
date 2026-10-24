#pragma once

// 止め絵の宣言。DX12ToonStyle.hpp の drawImpactFramePass に相当する。最終色を読み、場面の法線の印で色を残す部分を除いて 2 色化または反転し、最終色へ書き戻す。FXAA の後、2D を重ねる前に呼ぶ。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

#include <mitiru/render/ImpactFrame.hpp>

#include "DawnBindGroupTables.hpp"
#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnImpactPass
{
public:
	DawnImpactPass(gfx::DawnContext& ctx, wgpu::TextureFormat format) : m_ctx(ctx), m_format(format) {}

	[[nodiscard]] bool create();

	/// 最終色か場面の大きさが変わったとき、最終色のコピーを作り直す。束は bind が結ぶ
	void resize(int outWidth, int outHeight, int sceneWidth, int sceneHeight);
	/// コピーと場面の法線 (MSAA) を読む束を作る。登録簿が、法線かコピーが作り替わったときに呼ぶ
	void bind(const wgpu::TextureView& sceneNormal);
	/// bind が結ぶ自分の絵
	void collectSources(DescriptorSources& out) const { out.add("impact.copy", m_copy); }

	/// target に止め絵を掛ける。target は CopySrc と RenderAttachment を持つ最終色。keepMask は色を残す印の集合
	void encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView,
	            const ImpactFrame& frame, std::uint32_t keepMask);

private:
	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	gfx::AsyncRenderPipeline m_pipeline;
	wgpu::Buffer m_cb;
	wgpu::Texture m_copy;
	wgpu::TextureView m_copyView;
	wgpu::BindGroup m_group;
	int m_width = 0;
	int m_height = 0;
	int m_sceneWidth = 1;
	int m_sceneHeight = 1;
};

} // namespace mitiru::render::dawn_detail
