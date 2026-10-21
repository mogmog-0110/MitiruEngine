#include "DawnLightingBake.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kProbesPerRow = 256;   // forward.slang の giProbeTexel と同じ

wgpu::Texture makeTexture(const gfx::DawnContext& ctx, std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format)
{
	wgpu::TextureDescriptor td{};
	td.size = {w, h, 1};
	td.format = format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	return ctx.device.CreateTexture(&td);
}

void writeTexture(const gfx::DawnContext& ctx, const wgpu::Texture& texture, const void* data, std::uint32_t w, std::uint32_t h,
                  std::uint32_t bytesPerTexel)
{
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = w * bytesPerTexel;
	layout.rowsPerImage = h;
	const wgpu::Extent3D extent{w, h, 1};
	ctx.queue.WriteTexture(&dst, data, static_cast<std::size_t>(w) * h * bytesPerTexel, &layout, &extent);
}

} // namespace

bool DawnLightingBake::create()
{
	m_dummyProbes = makeTexture(m_ctx, 1, 1, wgpu::TextureFormat::RGBA32Float);
	m_dummyVisibility = makeTexture(m_ctx, 1, 1, wgpu::TextureFormat::RG16Float);
	const float zeros[4] = {};
	writeTexture(m_ctx, m_dummyProbes, zeros, 1, 1, 16);
	writeTexture(m_ctx, m_dummyVisibility, zeros, 1, 1, 4);
	m_dummyProbesView = m_dummyProbes.CreateView();
	m_dummyVisibilityView = m_dummyVisibility.CreateView();
	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	m_sampler = m_ctx.device.CreateSampler(&sd);
	return m_dummyProbesView && m_dummyVisibilityView && m_sampler;
}

void DawnLightingBake::clear()
{
	m_hasVolume = false;
	m_probes = {};
	m_visibility = {};
	m_probesView = {};
	m_visibilityView = {};
}

bool DawnLightingBake::load(const gi::LightingBake& bake)
{
	clear();
	if (!bake.hasVolume) { return false; }
	const gi::ProbeVolume& vol = bake.volume;
	const std::uint32_t n = vol.grid.count();
	if (n == 0) { return false; }

	// プローブ i の係数 k (float4)は (i % 256 * 7 + k, i / 256)に置く。DX12 と同じ並びで、float4 × 7 の最後の w が有効かどうかを表す。
	const std::uint32_t rows = (n + kProbesPerRow - 1) / kProbesPerRow;
	const std::uint32_t width = kProbesPerRow * gi::kProbeFloat4s;
	std::vector<float> texels(static_cast<std::size_t>(width) * rows * 4, 0.0f);
	for (std::uint32_t i = 0; i < n; ++i)
	{
		float* dst = texels.data() + (static_cast<std::size_t>(i / kProbesPerRow) * width + (i % kProbesPerRow) * gi::kProbeFloat4s) * 4;
		for (int k = 0; k < gi::kShCoefficients; ++k)
		{
			dst[k * 3 + 0] = vol.irradiance[i].c[k].x;
			dst[k * 3 + 1] = vol.irradiance[i].c[k].y;
			dst[k * 3 + 2] = vol.irradiance[i].c[k].z;
		}
		dst[27] = vol.valid[i] != 0 ? 1.0f : 0.0f;
	}
	const std::uint32_t aw = vol.atlasWidth();
	const std::uint32_t ah = vol.atlasHeight();
	std::vector<std::uint16_t> visibility(static_cast<std::size_t>(aw) * ah * 2);
	for (std::size_t i = 0; i < visibility.size(); ++i) { visibility[i] = gi::floatToHalf(vol.visibility[i]); }

	m_probes = makeTexture(m_ctx, width, rows, wgpu::TextureFormat::RGBA32Float);
	m_visibility = makeTexture(m_ctx, aw, ah, wgpu::TextureFormat::RG16Float);
	if (!m_probes || !m_visibility)
	{
		clear();
		return false;
	}
	writeTexture(m_ctx, m_probes, texels.data(), width, rows, 16);
	writeTexture(m_ctx, m_visibility, visibility.data(), aw, ah, 4);
	m_probesView = m_probes.CreateView();
	m_visibilityView = m_visibility.CreateView();
	m_grid = vol.grid;
	m_tilesPerRow = vol.tilesPerRow;
	m_atlasWidth = aw;
	m_atlasHeight = ah;
	m_hasVolume = true;
	return true;
}

void DawnLightingBake::fill(FrameCB& cb, const GlobalIlluminationSettings& settings) const noexcept
{
	if (!m_hasVolume || !settings.enabled) { return; }
	cb.giOrigin[0] = m_grid.origin.x;
	cb.giOrigin[1] = m_grid.origin.y;
	cb.giOrigin[2] = m_grid.origin.z;
	cb.giOrigin[3] = 1.0f;
	cb.giSpacing[0] = m_grid.spacing.x;
	cb.giSpacing[1] = m_grid.spacing.y;
	cb.giSpacing[2] = m_grid.spacing.z;
	cb.giSpacing[3] = settings.intensity;
	for (int a = 0; a < 3; ++a) { cb.giDims[a] = static_cast<float>(m_grid.dims[a]); }
	cb.giDims[3] = static_cast<float>(m_tilesPerRow);
	cb.giParams[0] = settings.normalBias > 0.0f ? settings.normalBias : gi::defaultProbeBias(m_grid);
	cb.giParams[1] = static_cast<float>(settings.toonBands);
	cb.giParams[2] = 1.0f / static_cast<float>(m_atlasWidth);
	cb.giParams[3] = 1.0f / static_cast<float>(m_atlasHeight);
}

} // namespace mitiru::render::dawn_detail
