#pragma once

// 投影デカール (Screen::decals3D)。DX12 の DX12PipelineStates_Decals.inl に相当する。見えるものを選んで描く順に並べ、
// 128 byte の DecalGpu を横 8 画素 (RGBA32Float) × 縦 1 枚ずつの画像に詰める。互換レベルは読み取り専用の
// storage buffer を使えないので画像で渡し、前方描画の fragment が全部を順に当てる (froxel の絞りは無い)。

#include <webgpu/webgpu_cpp.h>

#include <utility>
#include <vector>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Decals.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnDecals
{
public:
	explicit DawnDecals(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	[[nodiscard]] bool create();

	void beginFrame() { m_submitted.clear(); }
	void submit(const DecalDesc* decals, int count);

	/// 見えるものを選んで画像へ書く。fragment が読む数は count()
	void update(const Camera3D& camera);

	[[nodiscard]] int count() const noexcept { return static_cast<int>(m_gpu.size()); }
	[[nodiscard]] const wgpu::TextureView& view() const noexcept { return m_view; }

private:
	gfx::DawnContext& m_ctx;
	wgpu::Texture m_texture;
	wgpu::TextureView m_view;
	std::vector<DecalDesc> m_submitted;
	std::vector<std::pair<float, int>> m_scratch;
	std::vector<DecalGpu> m_gpu;
	std::vector<LocalLightGpu> m_bounds;
};

} // namespace mitiru::render::dawn_detail
