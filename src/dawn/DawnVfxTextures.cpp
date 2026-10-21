#include "DawnVfxTextures.hpp"

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kMipCount = 9;   // 256 から 1 まで
static_assert((1 << (kMipCount - 1)) == kVfxLayerSize);

wgpu::Texture makeArray(const gfx::DawnContext& ctx, wgpu::TextureFormat format, const char* label)
{
	wgpu::TextureDescriptor td{};
	td.label = label;
	td.size = {static_cast<std::uint32_t>(kVfxLayerSize), static_cast<std::uint32_t>(kVfxLayerSize),
	           static_cast<std::uint32_t>(kVfxMaxLayers)};
	td.mipLevelCount = kMipCount;
	td.format = format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	return ctx.device.CreateTexture(&td);
}

wgpu::TextureView arrayView(const wgpu::Texture& texture)
{
	wgpu::TextureViewDescriptor vd{};
	vd.dimension = wgpu::TextureViewDimension::e2DArray;
	vd.mipLevelCount = kMipCount;
	vd.arrayLayerCount = static_cast<std::uint32_t>(kVfxMaxLayers);
	return texture.CreateView(&vd);
}

} // namespace

bool DawnVfxTextures::create()
{
	m_color = makeArray(m_ctx, wgpu::TextureFormat::RGBA8UnormSrgb, "mitiru.vfx.color");
	m_data = makeArray(m_ctx, wgpu::TextureFormat::RGBA8Unorm, "mitiru.vfx.data");
	if (!m_color || !m_data) { return false; }
	m_colorView = arrayView(m_color);
	m_dataView = arrayView(m_data);
	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	sd.mipmapFilter = wgpu::MipmapFilterMode::Linear;
	m_sampler = m_ctx.device.CreateSampler(&sd);
	return m_colorView && m_dataView && m_sampler;
}

void DawnVfxTextures::writeLayer(const wgpu::Texture& texture, int layer, const std::vector<std::vector<std::uint8_t>>& mips) const
{
	for (std::uint32_t level = 0; level < kMipCount; ++level)
	{
		const std::uint32_t size = static_cast<std::uint32_t>(kVfxLayerSize) >> level;
		wgpu::TexelCopyTextureInfo dst{};
		dst.texture = texture;
		dst.mipLevel = level;
		dst.origin = {0, 0, static_cast<std::uint32_t>(layer)};
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = size * 4;
		layout.rowsPerImage = size;
		const wgpu::Extent3D extent{size, size, 1};
		m_ctx.queue.WriteTexture(&dst, mips[level].data(), mips[level].size(), &layout, &extent);
	}
}

int DawnVfxTextures::registerRgba(const std::uint8_t* rgba, int width, int height, VfxTextureKind kind)
{
	if (m_layerCount >= kVfxMaxLayers)
	{
		debug::warnOnce("dawn.vfx.layers", "VFX テクスチャが上限 " + std::to_string(kVfxMaxLayers) +
		                                       " 枚を超えたので、vfxTexture3D で登録する画像を減らしてください");
		return -1;
	}
	const auto mips = buildVfxLayer(rgba, width, height, kind);
	if (mips.size() < kMipCount) { return -1; }
	const int layer = m_layerCount++;
	writeLayer(m_color, layer, mips);
	writeLayer(m_data, layer, mips);
	return layer;
}

int DawnVfxTextures::registerFile(const char* path, VfxTextureKind kind)
{
	if (path == nullptr) { return -1; }
	const std::string key = std::string(kind == VfxTextureKind::Normal ? "n:" : "c:") + path;
	if (const auto it = m_fileLayers.find(key); it != m_fileLayers.end()) { return it->second; }
	const auto tex = Texture::fromFile(path);
	if (!tex || !tex->valid())
	{
		debug::warnOnce(std::string("dawn.vfx.file.") + path, std::string("VFX テクスチャ ") + path + " を読めないので、パスを確かめてください");
		return -1;
	}
	const int layer = registerRgba(tex->pixels().data(), tex->width(), tex->height(), kind);
	if (layer >= 0) { m_fileLayers.emplace(key, layer); }
	return layer;
}

} // namespace mitiru::render::dawn_detail
