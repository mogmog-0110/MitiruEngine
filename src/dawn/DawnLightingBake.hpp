#pragma once

// 事前計算した間接光 (*.lighting.bin)を GPU 画像として扱う宣言 (DX12LightingProbes.hpp に相当)。
// 放射照度プローブと距離マップは、前方描画から textureLoad または双線形補間で読む。
// 互換レベルでは読み取り専用の storage buffer を使えないため、プローブを画像に詰める。
// 反射のプローブ、動く GI (DDGI)、画面の反射はまだ無い。

#include <webgpu/webgpu_cpp.h>

#include <mitiru/render/IndirectLighting.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

class DawnLightingBake
{
public:
	explicit DawnLightingBake(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// 事前計算した光がないときに使う空の画像を作る
	[[nodiscard]] bool create();

	/// 放射照度プローブを読み込む。格子がない場合は読み込まず false を返す
	bool load(const gi::LightingBake& bake);
	void clear();

	[[nodiscard]] bool hasVolume() const noexcept { return m_hasVolume; }
	[[nodiscard]] const wgpu::TextureView& probesView() const noexcept { return m_hasVolume ? m_probesView : m_dummyProbesView; }
	[[nodiscard]] const wgpu::TextureView& visibilityView() const noexcept { return m_hasVolume ? m_visibilityView : m_dummyVisibilityView; }
	[[nodiscard]] const wgpu::Sampler& sampler() const noexcept { return m_sampler; }

	/// FrameCB の gi* を埋める。使わないときは何も書かず、w = 0 のままにする
	void fill(FrameCB& cb, const GlobalIlluminationSettings& settings) const noexcept;

private:
	gfx::DawnContext& m_ctx;
	bool m_hasVolume = false;
	gi::ProbeGridDesc m_grid;
	std::uint32_t m_tilesPerRow = 1;
	std::uint32_t m_atlasWidth = 1;
	std::uint32_t m_atlasHeight = 1;
	wgpu::Texture m_probes;
	wgpu::Texture m_visibility;
	wgpu::TextureView m_probesView;
	wgpu::TextureView m_visibilityView;
	wgpu::Texture m_dummyProbes;
	wgpu::Texture m_dummyVisibility;
	wgpu::TextureView m_dummyProbesView;
	wgpu::TextureView m_dummyVisibilityView;
	wgpu::Sampler m_sampler;
};

} // namespace mitiru::render::dawn_detail
