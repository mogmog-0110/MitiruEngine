#pragma once

// bloom (DX12 の DX12BloomShaders.hpp と同じ式)。HDR の絵を 1/2 → 1/4 に縮めて戻し、1/2 の絵を作る。
// トーンマップ (post.slang) がこれを露出の前に HDR へ足す。

#include <webgpu/webgpu_cpp.h>

#include "DawnBindGroupTables.hpp"
#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

class DawnBloomPass
{
public:
	/// format は HDR の中間の色の形式 (線形に引き伸ばせる物)
	DawnBloomPass(gfx::DawnContext& ctx, wgpu::TextureFormat format) : m_ctx(ctx), m_format(format) {}

	[[nodiscard]] bool create();

	/// 場面の絵の大きさが変わったとき、自分の絵を作り直す。束は bind が結ぶ
	void resize(int width, int height);
	/// 場面の絵 (resolved) と自分の絵を読む束を作る。登録簿が、resolved か自分の絵が作り替わったときに呼ぶ
	void bind(const wgpu::TextureView& resolved);
	/// bind が結ぶ自分の絵
	void collectSources(DescriptorSources& out) const;

	/// 結果の絵 (1/2)。bloom が無効の間も、黒のままの有効な絵を返す
	[[nodiscard]] const wgpu::TextureView& resultView() const noexcept { return m_halfBView; }

	void encode(wgpu::CommandEncoder& encoder, float threshold);

private:
	void runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::BindGroup& group,
	             const wgpu::TextureView& target);
	wgpu::BindGroup makeGroup(const wgpu::Buffer& cb, const wgpu::TextureView& src, const wgpu::TextureView& add);

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	wgpu::RenderPipeline m_downPipeline;
	wgpu::RenderPipeline m_upPipeline;
	wgpu::Sampler m_sampler;
	wgpu::Buffer m_cb[3];
	wgpu::Texture m_halfA;
	wgpu::Texture m_quarter;
	wgpu::Texture m_halfB;
	wgpu::TextureView m_halfAView;
	wgpu::TextureView m_quarterView;
	wgpu::TextureView m_halfBView;
	wgpu::BindGroup m_groups[3];
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::render::dawn_detail
