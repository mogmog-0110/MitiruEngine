#pragma once

// 局所光 (Screen::localLights3D、pointLight3D)。DX12 の DX12ClusteredLights.hpp に相当する。見える光を近い順に選び、
// 64 byte の LocalLightGpu を横 4 画素 (RGBA32Float) × 縦 1 灯ずつの画像に詰める。互換レベルは読み取り専用の
// storage buffer を使えないので画像で渡し、前方描画の fragment が全部の灯を順に当てる (froxel の絞りは無い)。
// まだ無い物: スポットの影 (castShadow)。

#include <webgpu/webgpu_cpp.h>

#include <utility>
#include <vector>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/LocalLights.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnLocalLights
{
public:
	explicit DawnLocalLights(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	[[nodiscard]] bool create();

	void beginFrame() { m_submitted.clear(); }
	void submit(const LocalLight* lights, int count);

	/// 見える光を選んで画像へ書く。fragment が読む数は count()
	void update(const Camera3D& camera);

	[[nodiscard]] int count() const noexcept { return static_cast<int>(m_gpu.size()); }
	[[nodiscard]] const wgpu::TextureView& view() const noexcept { return m_view; }

private:
	gfx::DawnContext& m_ctx;
	wgpu::Texture m_texture;
	wgpu::TextureView m_view;
	std::vector<LocalLight> m_submitted;
	std::vector<std::pair<float, int>> m_scratch;
	std::vector<LocalLightGpu> m_gpu;
};

} // namespace mitiru::render::dawn_detail
