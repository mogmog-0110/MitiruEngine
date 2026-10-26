#pragma once

// 色の仕上げ (LUT・ビネット・グレイン)。DX12 の drawColorFinishPass に相当する。最終色をコピーへ取り、仕上げを掛けて最終色へ書き戻す。
// アンチエイリアスの後、止め絵の前に呼ぶ。宣言 (assets/color_looks.json) が無いゲームは、この pass を作らない。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

#include <mitiru/render/ColorLook.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnColorFinishPass
{
public:
	DawnColorFinishPass(gfx::DawnContext& ctx, wgpu::TextureFormat format) : m_ctx(ctx), m_format(format) {}

	[[nodiscard]] bool create();

	/// 宣言と LUT を受け、LUT を GPU へ上げる。組の数だけバインドグループを作る
	void setLooks(ColorLookSet looks);
	/// 最終色の大きさが変わったとき、コピーを作り直してバインドグループを作り直す
	void resize(int outWidth, int outHeight);

	[[nodiscard]] const ColorLookSet& looks() const noexcept { return m_looks; }

	/// number (1 始まり) の仕上げを target に掛ける。target は CopySrc と RenderAttachment を持つ最終色。掛けたときだけ true
	bool encode(wgpu::CommandEncoder& encoder, const wgpu::Texture& target, const wgpu::TextureView& targetView, int number,
	            std::uint32_t seed);

private:
	void rebuildGroups();

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	gfx::AsyncRenderPipeline m_pipeline;
	wgpu::Buffer m_cb;
	wgpu::Texture m_copy;
	wgpu::TextureView m_copyView;
	wgpu::Texture m_noLut;   ///< LUT のない組に結ぶ 1x1。読まれない
	wgpu::TextureView m_noLutView;
	ColorLookSet m_looks;
	std::vector<wgpu::Texture> m_lutTextures;
	std::vector<wgpu::TextureView> m_lutViews;
	std::vector<wgpu::BindGroup> m_groups;   ///< looks と同じ添字
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::render::dawn_detail
