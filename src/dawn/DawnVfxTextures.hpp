#pragma once

// デカールと粒が共有するテクスチャの配列 (一辺 kVfxLayerSize、kVfxMaxLayers 層)。DX12 の DX12PipelineStates_VfxTextures.inl に相当する。
// 互換レベルは同じ画像を別の形式 (sRGB と素の値) の view で読めないので、色用 (sRGB) とデータ用 (法線) の 2 枚を持ち、
// 登録した層をどちらにも書く。

#include <webgpu/webgpu_cpp.h>

#include <string>
#include <unordered_map>

#include <mitiru/render/VfxTextures.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnVfxTextures
{
public:
	explicit DawnVfxTextures(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	[[nodiscard]] bool create();

	/// 画像ファイル (PNG / JPEG) を層に登録する。同じ path と種類は同じ番号。読めなければ、尽きれば -1
	int registerFile(const char* path, VfxTextureKind kind);
	int registerRgba(const std::uint8_t* rgba, int width, int height, VfxTextureKind kind);

	[[nodiscard]] const wgpu::TextureView& colorView() const noexcept { return m_colorView; }
	[[nodiscard]] const wgpu::TextureView& dataView() const noexcept { return m_dataView; }
	[[nodiscard]] const wgpu::Sampler& sampler() const noexcept { return m_sampler; }

private:
	void writeLayer(const wgpu::Texture& texture, int layer, const std::vector<std::vector<std::uint8_t>>& mips) const;

	gfx::DawnContext& m_ctx;
	wgpu::Texture m_color;
	wgpu::Texture m_data;
	wgpu::TextureView m_colorView;
	wgpu::TextureView m_dataView;
	wgpu::Sampler m_sampler;
	std::unordered_map<std::string, int> m_fileLayers;
	int m_layerCount = 0;
};

} // namespace mitiru::render::dawn_detail
