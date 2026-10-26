#include "DawnLit2D.hpp"

#include <algorithm>
#include <cstring>

#include <mitiru/debug/WarnOnce.hpp>

#include "DawnDeviceTarget.hpp"
#include "DawnProfile.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint64_t kRingBytes = 8ull * 1024 * 1024;

// シェーダーの LitCB と同じ並び
struct LitCb
{
	float ortho[16];
	float mode[4];
	float map[4];
	float ambient[4];
	float lights[kMaxLights2D * light2d::kVec4PerLight][4];
};
static_assert(sizeof(LitCb) == 64 + 48 + 16 * kMaxLights2D * light2d::kVec4PerLight);

std::uint64_t contentHash(const std::uint8_t* data, std::size_t bytes)
{
	std::uint64_t h = 14695981039346656037ull;
	for (std::size_t i = 0; i < bytes; ++i)
	{
		h ^= data[i];
		h *= 1099511628211ull;
	}
	return h;
}

} // namespace

void orthoMatrix2D(float width, float height, float out[16])
{
	std::fill(out, out + 16, 0.0f);
	out[0] = 2.0f / width;
	out[5] = -2.0f / height;
	out[10] = 1.0f;
	out[12] = -1.0f;
	out[13] = 1.0f;
	out[15] = 1.0f;
}

DawnLit2D::DawnLit2D(gfx::DawnDevice& device) : m_device(device), m_ctx(*device.context()) {}

wgpu::Texture DawnLit2D::makeTexture(int w, int h) const
{
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.lit2d.texture";
	td.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
	td.format = wgpu::TextureFormat::RGBA8Unorm;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	return m_ctx.device.CreateTexture(&td);
}

void DawnLit2D::upload(const wgpu::Texture& texture, int firstRow, int rowCount, int w, const std::uint8_t* rgba) const
{
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = texture;
	dst.origin = {0, static_cast<std::uint32_t>(firstRow), 0};
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = static_cast<std::uint32_t>(w) * 4;
	layout.rowsPerImage = static_cast<std::uint32_t>(rowCount);
	const wgpu::Extent3D extent{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(rowCount), 1};
	const std::size_t offset = static_cast<std::size_t>(firstRow) * static_cast<std::size_t>(w) * 4;
	m_ctx.queue.WriteTexture(&dst, rgba + offset, static_cast<std::size_t>(rowCount) * static_cast<std::size_t>(w) * 4, &layout, &extent);
}

bool DawnLit2D::initialize()
{
	const auto& dev = m_ctx.device;
	wgpu::BufferDescriptor ub{};
	ub.size = sizeof(LitCb);
	ub.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_uniform = dev.CreateBuffer(&ub);
	wgpu::BufferDescriptor rb{};
	rb.size = kRingBytes;
	rb.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
	m_ring = dev.CreateBuffer(&rb);

	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	sd.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
	m_linear = dev.CreateSampler(&sd);
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Nearest;
	m_point = dev.CreateSampler(&sd);

	const std::uint8_t white[4] = {255, 255, 255, 255};
	const std::uint8_t flat[4] = {128, 128, 255, 255};
	m_blank.texture = makeTexture(1, 1);
	m_flatNormal.texture = makeTexture(1, 1);
	if (!m_blank.texture || !m_flatNormal.texture) { return false; }
	upload(m_blank.texture, 0, 1, 1, white);
	upload(m_flatNormal.texture, 0, 1, 1, flat);
	m_blank.view = m_blank.texture.CreateView();
	m_flatNormal.view = m_flatNormal.texture.CreateView();

	wgpu::BindGroupLayoutEntry le[5]{};
	le[0].binding = 0;
	le[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	le[0].buffer.type = wgpu::BufferBindingType::Uniform;
	le[0].buffer.minBindingSize = sizeof(LitCb);
	for (std::uint32_t i = 1; i <= 2; ++i)
	{
		le[i].binding = i;
		le[i].visibility = wgpu::ShaderStage::Fragment;
		le[i].texture.sampleType = wgpu::TextureSampleType::Float;
		le[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	}
	for (std::uint32_t i = 3; i <= 4; ++i)
	{
		le[i].binding = i;
		le[i].visibility = wgpu::ShaderStage::Fragment;
		le[i].sampler.type = wgpu::SamplerBindingType::Filtering;
	}
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = le;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	static const wgpu::VertexAttribute attrs[3] = {
		{nullptr, wgpu::VertexFormat::Float32x2, offsetof(Vertex2D, position), 0},
		{nullptr, wgpu::VertexFormat::Float32x2, offsetof(Vertex2D, texCoord), 1},
		{nullptr, wgpu::VertexFormat::Float32x4, offsetof(Vertex2D, color), 2},
	};
	wgpu::VertexBufferLayout vb{};
	vb.arrayStride = sizeof(Vertex2D);
	vb.attributeCount = 3;
	vb.attributes = attrs;
	wgpu::BlendState bs{};
	bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
	bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
	wgpu::ColorTargetState target{};
	target.format = m_device.target().format;
	target.blend = &bs;
	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_batch2dlit(), "mitiru.batch2dlit");
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsLit";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsLit";
	rp.vertex.bufferCount = 1;
	rp.vertex.buffers = &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.cullMode = wgpu::CullMode::None;
	rp.fragment = &fs;
	m_pipeline.start(m_ctx, rp, "batch2dlit");
	return static_cast<bool>(m_pipeline);
}

void DawnLit2D::resize(float logicalWidth, float logicalHeight)
{
	m_logicalW = std::max(logicalWidth, 1.0f);
	m_logicalH = std::max(logicalHeight, 1.0f);
	orthoMatrix2D(m_logicalW, m_logicalH, m_ortho);
}

void DawnLit2D::setLighting(const Lighting2D* lighting)
{
	if (lighting == nullptr)
	{
		m_active = false;
		m_lighting = Lighting2D{};
		return;
	}
	m_lighting = *lighting;
	for (std::uint32_t i = 0; i < m_lighting.count; ++i) { m_packed[i] = light2d::pack(m_lighting.lights[i]); }
	m_active = m_lighting.active();
}

std::uint32_t DawnLit2D::ensureTexture(const void* key, int w, int h, const std::uint8_t* rgba, bool contentMayChange)
{
	if (key == nullptr || w <= 0 || h <= 0 || rgba == nullptr || m_ctx.lost) { return 0; }
	const std::uint64_t hash = contentMayChange ? contentHash(rgba, static_cast<std::size_t>(w) * h * 4) : 0;
	const auto found = m_lookup.find(key);
	if (found != m_lookup.end())
	{
		Texture& t = m_textures[found->second];
		const std::uint32_t handle = found->second + 1;
		if (t.w == w && t.h == h)
		{
			if (contentMayChange ? t.hash == hash : t.src == rgba) { return handle; }
			upload(t.texture, 0, h, w, rgba);
			t.src = rgba;
			t.hash = hash;
			return handle;
		}
		t.texture = makeTexture(w, h);
		t.view = t.texture.CreateView();
		t.w = w;
		t.h = h;
		t.src = rgba;
		t.hash = hash;
		++t.version;
		upload(t.texture, 0, h, w, rgba);
		return handle;
	}
	Texture t;
	t.texture = makeTexture(w, h);
	if (!t.texture) { return 0; }
	t.view = t.texture.CreateView();
	t.w = w;
	t.h = h;
	t.key = key;
	t.src = rgba;
	t.hash = hash;
	if (const auto range = m_ranges.find(key); range != m_ranges.end()) { t.distanceRange = range->second; }
	upload(t.texture, 0, h, w, rgba);
	m_textures.push_back(std::move(t));
	const auto index = static_cast<std::uint32_t>(m_textures.size() - 1);
	m_lookup.emplace(key, index);
	return index + 1;
}

void DawnLit2D::setDistanceRange(const void* key, float range)
{
	m_ranges[key] = range;   // 画像より先に登録されることがあるので、作るときにも読む
	if (const auto it = m_lookup.find(key); it != m_lookup.end()) { m_textures[it->second].distanceRange = range; }
}

void DawnLit2D::updateRows(const void* key, int firstRow, int rowCount, const std::uint8_t* rgba)
{
	const auto it = m_lookup.find(key);
	if (it == m_lookup.end() || rgba == nullptr || rowCount <= 0) { return; }
	const Texture& t = m_textures[it->second];
	if (firstRow < 0 || firstRow + rowCount > t.h) { return; }
	upload(t.texture, firstRow, rowCount, t.w, rgba);
}

void DawnLit2D::setNormalMap(const void* colorKey, const void* normalKey, int w, int h, const std::uint8_t* rgba)
{
	if (colorKey == nullptr) { return; }
	m_normals[colorKey] = NormalRef{normalKey, w, h, rgba};
}

const wgpu::TextureView& DawnLit2D::viewOf(std::uint32_t handle) const
{
	return handle == 0 ? m_blank.view : m_textures[handle - 1].view;
}

const wgpu::BindGroup* DawnLit2D::groupFor(std::uint32_t colorHandle, std::uint32_t normalHandle)
{
	const std::uint32_t colorVersion = colorHandle == 0 ? 0 : m_textures[colorHandle - 1].version;
	const std::uint32_t normalVersion = normalHandle == 0 ? 0 : m_textures[normalHandle - 1].version;
	const std::uint64_t key = (static_cast<std::uint64_t>(colorHandle) << 32) | normalHandle;
	GroupEntry& entry = m_groups[key];
	if (!entry.group || entry.colorVersion != colorVersion || entry.normalVersion != normalVersion)
	{
		wgpu::BindGroupEntry be[5]{};
		be[0].binding = 0;
		be[0].buffer = m_uniform;
		be[0].size = sizeof(LitCb);
		be[1].binding = 1;
		be[1].textureView = viewOf(colorHandle);
		be[2].binding = 2;
		be[2].textureView = normalHandle == 0 ? m_flatNormal.view : m_textures[normalHandle - 1].view;
		be[3].binding = 3;
		be[3].sampler = m_linear;
		be[4].binding = 4;
		be[4].sampler = m_point;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = m_layout;
		bg.entryCount = 5;
		bg.entries = be;
		entry.group = m_ctx.device.CreateBindGroup(&bg);
		entry.colorVersion = colorVersion;
		entry.normalVersion = normalVersion;
	}
	return &entry.group;
}

void DawnLit2D::submit(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices, std::uint32_t texHandle)
{
	if (!m_active || vertices.empty() || indices.empty() || m_ctx.lost || !m_pipeline) { return; }
	MITIRU_DAWN_ZONE("lit2d.submit");
	if (texHandle > m_textures.size()) { return; }

	std::uint32_t normalHandle = 0;
	float range = 0.0f;
	if (texHandle != 0)
	{
		const void* colorKey = m_textures[texHandle - 1].key;
		const int colorW = m_textures[texHandle - 1].w;
		const int colorH = m_textures[texHandle - 1].h;
		range = m_textures[texHandle - 1].distanceRange;
		if (range <= 0.0f)
		{
			if (const auto ref = m_normals.find(colorKey); ref != m_normals.end() && ref->second.rgba != nullptr)
			{
				const NormalRef nref = ref->second;
				if (nref.w == colorW && nref.h == colorH) { normalHandle = ensureTexture(nref.key, nref.w, nref.h, nref.rgba, false); }
				else
				{
					debug::warnOnce("dawn.lit2d.normal_size", "the normal map size differs from its sprite; drawing it without a normal map");
				}
			}
		}
	}

	LitCb cb{};
	std::memcpy(cb.ortho, m_ortho, sizeof(m_ortho));
	cb.mode[0] = texHandle == 0 ? 0.0f : (range > 0.0f ? 2.0f : 1.0f);
	cb.mode[1] = range;
	cb.mode[2] = normalHandle != 0 ? 1.0f : 0.0f;
	cb.mode[3] = (texHandle != 0 && range <= 0.0f) ? 1.0f : 0.0f;
	cb.map[0] = m_logicalW / static_cast<float>(std::max(m_device.width(), 1));
	cb.map[1] = m_logicalH / static_cast<float>(std::max(m_device.height(), 1));
	cb.ambient[0] = m_lighting.ambient[0];
	cb.ambient[1] = m_lighting.ambient[1];
	cb.ambient[2] = m_lighting.ambient[2];
	cb.ambient[3] = static_cast<float>(m_lighting.count);
	for (std::uint32_t i = 0; i < m_lighting.count; ++i)
	{
		std::memcpy(cb.lights[i * 3 + 0], m_packed[i].posHeightRadius, sizeof(float) * 4);
		std::memcpy(cb.lights[i * 3 + 1], m_packed[i].colorFalloff, sizeof(float) * 4);
		std::memcpy(cb.lights[i * 3 + 2], m_packed[i].coneDir, sizeof(float) * 4);
	}

	const std::uint64_t vBytes = vertices.size() * sizeof(Vertex2D);
	const std::uint64_t iBytes = (indices.size() * sizeof(std::uint32_t) + 3) & ~std::uint64_t(3);
	const std::uint64_t need = ((vBytes + 3) & ~std::uint64_t(3)) + iBytes;
	if (need > kRingBytes) { return; }
	// 同じ範囲を後で書き直しても、キューの順で前の描画後になるため安全
	if (m_cursor + need > kRingBytes) { m_cursor = 0; }
	const std::uint64_t vOffset = m_cursor;
	const std::uint64_t iOffset = vOffset + ((vBytes + 3) & ~std::uint64_t(3));
	m_ctx.queue.WriteBuffer(m_ring, vOffset, vertices.data(), vBytes);
	m_ctx.queue.WriteBuffer(m_ring, iOffset, indices.data(), indices.size() * sizeof(std::uint32_t));
	m_cursor += need;
	m_ctx.queue.WriteBuffer(m_uniform, 0, &cb, sizeof(cb));

	const wgpu::BindGroup* group = groupFor(texHandle, normalHandle);
	gfx::flushBackground(m_device);
	wgpu::RenderPassColorAttachment color{};
	color.view = m_device.target().view;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	wgpu::CommandEncoder encoder = m_ctx.device.CreateCommandEncoder();
	wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
	pass.SetPipeline(m_pipeline);
	pass.SetBindGroup(0, *group);
	pass.SetVertexBuffer(0, m_ring, vOffset, vBytes);
	pass.SetIndexBuffer(m_ring, wgpu::IndexFormat::Uint32, iOffset, indices.size() * sizeof(std::uint32_t));
	pass.DrawIndexed(static_cast<std::uint32_t>(indices.size()));
	pass.End();
	wgpu::CommandBuffer commands = encoder.Finish();
	m_ctx.queue.Submit(1, &commands);
}

} // namespace mitiru::render::dawn_detail
