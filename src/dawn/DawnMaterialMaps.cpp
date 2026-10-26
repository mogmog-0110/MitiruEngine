#include "DawnMaterialMaps.hpp"

#include <algorithm>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/TextureMips.hpp>

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr int kMaxTextureSize = 4096;   // 互換レベルの上限に収める。超える画像は半分に縮めて使う

wgpu::BindGroupLayoutEntry textureEntry(std::uint32_t binding)
{
	wgpu::BindGroupLayoutEntry e{};
	e.binding = binding;
	e.visibility = wgpu::ShaderStage::Fragment;
	e.texture.sampleType = wgpu::TextureSampleType::Float;
	return e;
}

wgpu::BindGroupLayoutEntry samplerEntry(std::uint32_t binding)
{
	wgpu::BindGroupLayoutEntry e{};
	e.binding = binding;
	e.visibility = wgpu::ShaderStage::Fragment;
	e.sampler.type = wgpu::SamplerBindingType::Filtering;
	return e;
}

wgpu::Sampler makeSampler(const gfx::DawnContext& ctx, wgpu::FilterMode filter)
{
	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::Repeat;
	sd.magFilter = filter;
	sd.minFilter = filter;
	sd.mipmapFilter = (filter == wgpu::FilterMode::Linear) ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
	return ctx.device.CreateSampler(&sd);
}

} // namespace

bool DawnMaterialPool::create()
{
	// 束縛番号は 1 から。0 は体積フォグのパスが輪郭線の距離に使う (forward.slang の fogLineDepth)
	wgpu::BindGroupLayoutEntry entries[5] = {textureEntry(1), textureEntry(2), textureEntry(3), samplerEntry(4), samplerEntry(5)};
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = entries;
	m_layout = m_ctx.device.CreateBindGroupLayout(&bgl);
	m_linear = makeSampler(m_ctx, wgpu::FilterMode::Linear);
	m_nearest = makeSampler(m_ctx, wgpu::FilterMode::Nearest);
	const std::uint8_t white[4] = {255, 255, 255, 255};
	const std::uint8_t flat[4] = {128, 128, 255, 255};
	const std::uint8_t black[4] = {0, 0, 0, 255};
	m_white = onePixel(white, true);
	m_flatNormal = onePixel(flat, false);
	m_black = onePixel(black, true);
	if (!m_layout || !m_linear || !m_nearest || !m_white || !m_flatNormal || !m_black) { return false; }
	m_defaultGroup = makeGroup(m_white, m_flatNormal, m_black);
	return static_cast<bool>(m_defaultGroup);
}

wgpu::TextureView DawnMaterialPool::onePixel(const std::uint8_t rgba[4], bool srgb) const
{
	wgpu::TextureDescriptor td{};
	td.size = {1, 1, 1};
	td.format = srgb ? wgpu::TextureFormat::RGBA8UnormSrgb : wgpu::TextureFormat::RGBA8Unorm;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	wgpu::Texture texture = m_ctx.device.CreateTexture(&td);
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = 4;
	layout.rowsPerImage = 1;
	const wgpu::Extent3D extent{1, 1, 1};
	m_ctx.queue.WriteTexture(&dst, rgba, 4, &layout, &extent);
	return texture.CreateView();
}

wgpu::BindGroup DawnMaterialPool::makeGroup(const wgpu::TextureView& base, const wgpu::TextureView& normal,
                                            const wgpu::TextureView& emissive) const
{
	wgpu::BindGroupEntry be[5]{};
	be[0].binding = 1;
	be[0].textureView = base;
	be[1].binding = 2;
	be[1].textureView = normal;
	be[2].binding = 3;
	be[2].textureView = emissive;
	be[3].binding = 4;
	be[3].sampler = m_linear;
	be[4].binding = 5;
	be[4].sampler = m_nearest;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 5;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

DawnImageData DawnMaterialPool::buildImage(const std::uint8_t* rgba, int width, int height, Role role)
{
	const MipFilter filter = (role == Role::Normal) ? MipFilter::NormalMap : MipFilter::Srgb;
	std::vector<std::uint8_t> level0(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
	while (std::max(width, height) > kMaxTextureSize)
	{
		const int nw = std::max(width / 2, 1);
		const int nh = std::max(height / 2, 1);
		level0 = downsampleHalf(level0.data(), width, height, nw, nh, filter);
		width = nw;
		height = nh;
	}
	DawnImageData image;
	image.width = width;
	image.height = height;
	image.mips = buildMipChain(level0.data(), width, height, filter);
	return image;
}

wgpu::TextureView DawnMaterialPool::makeView(const DawnImageData& image, Role role) const
{
	const std::uint32_t width = static_cast<std::uint32_t>(image.width);
	const std::uint32_t height = static_cast<std::uint32_t>(image.height);
	wgpu::TextureDescriptor td{};
	td.size = {width, height, 1};
	td.mipLevelCount = static_cast<std::uint32_t>(image.mips.size());
	td.format = (role == Role::Normal) ? wgpu::TextureFormat::RGBA8Unorm : wgpu::TextureFormat::RGBA8UnormSrgb;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	wgpu::Texture texture = m_ctx.device.CreateTexture(&td);
	for (std::uint32_t level = 0; level < image.mips.size(); ++level)
	{
		const std::uint32_t w = std::max<std::uint32_t>(width >> level, 1);
		const std::uint32_t h = std::max<std::uint32_t>(height >> level, 1);
		wgpu::TexelCopyTextureInfo dst{};
		dst.texture = texture;
		dst.mipLevel = level;
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = w * 4;
		layout.rowsPerImage = h;
		const wgpu::Extent3D extent{w, h, 1};
		m_ctx.queue.WriteTexture(&dst, image.mips[level].data(), image.mips[level].size(), &layout, &extent);
	}
	return texture.CreateView();
}

// 埋め込みの画像を優先し、外部の URI はモデルのディレクトリからの相対で読む。読めなければ空の画像
DawnImageData DawnMaterialPool::imageFor(const CpuTexture& decoded, const std::string& uri, Role role, const std::string& modelPath)
{
	if (decoded.valid()) { return buildImage(decoded.rgba.data(), decoded.width, decoded.height, role); }
	if (uri.empty()) { return {}; }
	const auto dirEnd = modelPath.find_last_of("/\\");
	const std::string path = ((dirEnd == std::string::npos) ? std::string() : modelPath.substr(0, dirEnd + 1)) + uri;
	if (const auto tex = Texture::fromFile(path); tex && tex->valid())
	{
		return buildImage(tex->pixels().data(), tex->width(), tex->height(), role);
	}
	debug::warnOnce("dawn.model.tex." + path, "glTF のテクスチャ " + path + " を読めないので、.gltf からの相対パスを確かめてください");
	return {};
}

// 同じ鍵の画像は 1 度だけ上げる (読めなかった画像は空の view を覚える)
wgpu::TextureView DawnMaterialPool::viewFor(const DawnImageData& image, Role role, const std::string& key)
{
	if (const auto it = m_views.find(key); it != m_views.end()) { return it->second; }
	wgpu::TextureView view;
	if (image.valid()) { view = makeView(image, role); }
	m_views.emplace(key, view);
	return view;
}

DawnPreparedMaterial DawnMaterialPool::prepare(const GltfMaterialData& m, const std::string& modelPath)
{
	DawnPreparedMaterial out;
	out.base = imageFor(m.baseColorTexture, m.baseColorTexturePath, Role::Base, modelPath);
	out.normal = imageFor(m.normalTexture, m.normalTexturePath, Role::Normal, modelPath);
	out.emissive = imageFor(m.emissiveTexture, m.emissiveTexturePath, Role::Emissive, modelPath);
	out.mask = m.alphaMode == GltfAlphaMode::Mask;
	out.alphaCutoff = m.alphaCutoff;
	out.nearestFilter = m.nearestFilter;
	out.normalScale = m.normalScale;
	for (int k = 0; k < 3; ++k) { out.emissiveFactor[k] = m.emissiveFactor[k]; }
	// Dawn に PBR の陰影は無い。層を持つ材質を読んだことを数える
	if (m.clearcoat > 0.0f || m.anisotropyStrength > 0.0f || m.sheenColor[0] > 0.0f || m.sheenColor[1] > 0.0f || m.sheenColor[2] > 0.0f)
	{
		features::use(features::Feature::MaterialLayers);
		features::note(features::Feature::MaterialLayers);
	}
	return out;
}

std::shared_ptr<const DawnMaterialMaps> DawnMaterialPool::make(const DawnPreparedMaterial& m, const std::string& modelPath,
                                                               std::size_t materialIndex)
{
	const std::string prefix = modelPath + "#" + std::to_string(materialIndex) + ":";
	const wgpu::TextureView base = viewFor(m.base, Role::Base, prefix + "base");
	const wgpu::TextureView normal = viewFor(m.normal, Role::Normal, prefix + "normal");
	const wgpu::TextureView emissive = viewFor(m.emissive, Role::Emissive, prefix + "emissive");
	const bool hasEmissiveFactor = m.emissiveFactor[0] > 0.0f || m.emissiveFactor[1] > 0.0f || m.emissiveFactor[2] > 0.0f;
	if (!base && !normal && !emissive && !m.mask && !m.nearestFilter && !hasEmissiveFactor) { return nullptr; }

	auto maps = std::make_shared<DawnMaterialMaps>();
	maps->group = makeGroup(base ? base : m_white, normal ? normal : m_flatNormal, emissive ? emissive : m_black);
	maps->mapFlags[0] = base ? 1.0f : 0.0f;
	maps->mapFlags[1] = normal ? 1.0f : 0.0f;
	maps->mapFlags[2] = emissive ? 1.0f : 0.0f;
	maps->mapFlags[3] = m.mask ? std::max(m.alphaCutoff, 1e-4f) : 0.0f;
	maps->mapParams[0] = m.normalScale;
	maps->mapParams[1] = m.nearestFilter ? 1.0f : 0.0f;
	for (int k = 0; k < 3; ++k) { maps->emissive[k] = m.emissiveFactor[k]; }
	return maps;
}

} // namespace mitiru::render::dawn_detail
