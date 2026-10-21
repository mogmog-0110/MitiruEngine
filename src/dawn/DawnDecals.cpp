#include "DawnDecals.hpp"

#include <algorithm>

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kTexelsPerDecal = sizeof(DecalGpu) / 16;   // forward.slang の applyDecals が 8 画素を読む
static_assert(kTexelsPerDecal == 8);

} // namespace

bool DawnDecals::create()
{
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.decals";
	td.size = {kTexelsPerDecal, static_cast<std::uint32_t>(kMaxVisibleDecals), 1};
	td.format = wgpu::TextureFormat::RGBA32Float;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_texture = m_ctx.device.CreateTexture(&td);
	if (!m_texture) { return false; }
	m_view = m_texture.CreateView();
	return static_cast<bool>(m_view);
}

void DawnDecals::submit(const DecalDesc* decals, int count)
{
	if (decals == nullptr) { return; }
	for (int i = 0; i < count && static_cast<int>(m_submitted.size()) < kMaxDecals; ++i) { m_submitted.push_back(decals[i]); }
}

void DawnDecals::update(const Camera3D& camera)
{
	m_gpu.clear();
	if (m_submitted.empty()) { return; }
	const ClusterView view = ClusterView::fromCamera(camera);
	(void)selectVisibleDecals(m_submitted, view, m_scratch, m_gpu, m_bounds);
	if (m_gpu.empty()) { return; }
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = sizeof(DecalGpu);
	layout.rowsPerImage = static_cast<std::uint32_t>(m_gpu.size());
	const wgpu::Extent3D extent{kTexelsPerDecal, static_cast<std::uint32_t>(m_gpu.size()), 1};
	m_ctx.queue.WriteTexture(&dst, m_gpu.data(), m_gpu.size() * sizeof(DecalGpu), &layout, &extent);
}

} // namespace mitiru::render::dawn_detail
