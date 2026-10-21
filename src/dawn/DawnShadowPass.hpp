#pragma once

// 太陽の影の深度パス (DX12 の DX12PipelineStates_Shadow.inl の drawShadowCascades に相当)。
// 近いカスケードは 1 枚の影マップ、遠い 2 つは 2 列のアトラスへ描く。深度だけを書き、色は出さない。
// 影を引く側は forward.slang。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// 1 フレームの影の枠。cascades が 0 なら影なし (深度パスも走らせない)
struct ShadowFrame
{
	int cascades = 0;
	sgc::Mat4f view[3];
	sgc::Mat4f proj[3];
};

class DawnShadowPass
{
public:
	explicit DawnShadowPass(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// 影マップとパイプラインを作る。mapSize はカスケード 0、farMapSize はアトラスの 1 列の一辺。作れなければ false
	[[nodiscard]] bool create(int mapSize, int farMapSize);

	[[nodiscard]] const wgpu::TextureView& nearView() const noexcept { return m_nearView; }
	[[nodiscard]] const wgpu::TextureView& farView() const noexcept { return m_farView; }
	[[nodiscard]] const wgpu::Sampler& sampler() const noexcept { return m_sampler; }
	[[nodiscard]] int mapSize() const noexcept { return m_mapSize; }
	[[nodiscard]] int farMapSize() const noexcept { return m_farMapSize; }

	/// 影マップを描く。encoder に深度パスを積む
	void encode(wgpu::CommandEncoder& encoder, const ShadowFrame& frame, const std::vector<ShadowCaster>& casters);

private:
	struct Draw
	{
		const GpuMesh* mesh;
		std::uint32_t offset;
	};

	void collect(const ShadowFrame& frame, int cascade, const std::vector<ShadowCaster>& casters, std::vector<Draw>& out);
	void ensureDrawBuffer(std::size_t bytes);
	void recordCascade(wgpu::RenderPassEncoder& pass, const std::vector<Draw>& draws) const;

	gfx::DawnContext& m_ctx;
	int m_mapSize = 0;
	int m_farMapSize = 0;
	wgpu::Texture m_near;
	wgpu::Texture m_far;
	wgpu::TextureView m_nearView;
	wgpu::TextureView m_farView;
	wgpu::Sampler m_sampler;
	wgpu::BindGroupLayout m_layout;
	wgpu::RenderPipeline m_pipeline;
	wgpu::Buffer m_drawBuffer;
	wgpu::BindGroup m_bindGroup;
	std::uint64_t m_drawBufferBytes = 0;
	std::vector<std::uint8_t> m_bytes;
	std::vector<Draw> m_drawsNear;
	std::vector<Draw> m_drawsMid;
	std::vector<Draw> m_drawsFar;
};

} // namespace mitiru::render::dawn_detail
