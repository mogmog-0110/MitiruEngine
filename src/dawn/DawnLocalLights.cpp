#include "DawnLocalLights.hpp"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kTexelsPerLight = sizeof(LocalLightGpu) / 16;   // forward.slang の accumulateLocalLights が先頭の 3 画素を読む
static_assert(kTexelsPerLight == 4);

} // namespace

bool DawnLocalLights::create()
{
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.localLights";
	td.size = {kTexelsPerLight, static_cast<std::uint32_t>(kMaxVisibleLocalLights), 1};
	td.format = wgpu::TextureFormat::RGBA32Float;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_texture = m_ctx.device.CreateTexture(&td);
	if (!m_texture) { return false; }
	m_view = m_texture.CreateView();
	return static_cast<bool>(m_view);
}

void DawnLocalLights::submit(const LocalLight* lights, int count)
{
	if (lights == nullptr) { return; }
	for (int i = 0; i < count && static_cast<int>(m_submitted.size()) < kMaxLocalLights; ++i) { m_submitted.push_back(lights[i]); }
}

void DawnLocalLights::update(const Camera3D& camera)
{
	m_gpu.clear();
	if (m_submitted.empty()) { return; }
	(void)selectVisibleLocalLights(m_submitted, ClusterView::fromCamera(camera), m_scratch, m_gpu);
	if (m_gpu.empty()) { return; }
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = sizeof(LocalLightGpu);
	layout.rowsPerImage = static_cast<std::uint32_t>(m_gpu.size());
	const wgpu::Extent3D extent{kTexelsPerLight, static_cast<std::uint32_t>(m_gpu.size()), 1};
	m_ctx.queue.WriteTexture(&dst, m_gpu.data(), m_gpu.size() * sizeof(LocalLightGpu), &layout, &extent);
}

} // namespace mitiru::render::dawn_detail
