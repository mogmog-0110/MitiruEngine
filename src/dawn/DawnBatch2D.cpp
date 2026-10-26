#include <mitiru/render/Batch2D_Dawn.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

#include <mitiru/gfx/dawn/DawnDevice.hpp>

#include "DawnContext.hpp"
#include "DawnDeviceTarget.hpp"
#include "DawnLit2D.hpp"
#include "DawnProfile.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render
{

namespace
{

constexpr std::uint64_t kRingBytes = 8ull * 1024 * 1024;

class DawnBatch2D final : public IBatch2DSink
{
public:
	DawnBatch2D(gfx::DawnDevice& device, float width, float height, bool withLighting) : m_device(device), m_ctx(*device.context())
	{
		if (withLighting) { m_lit = std::make_unique<dawn_detail::DawnLit2D>(device); }
		resize(width, height);
	}

	bool initialize()
	{
		// 光が作れなくても、光を使わない 2D は描ける
		if (m_lit && !m_lit->initialize()) { m_lit.reset(); }
		const auto& dev = m_ctx.device;
		wgpu::BufferDescriptor ub{};
		ub.size = 256;
		ub.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		m_uniform = dev.CreateBuffer(&ub);
		wgpu::BufferDescriptor rb{};
		rb.size = kRingBytes;
		rb.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
		m_ring = dev.CreateBuffer(&rb);

		wgpu::BindGroupLayoutEntry le{};
		le.binding = 0;
		le.visibility = wgpu::ShaderStage::Vertex;
		le.buffer.type = wgpu::BufferBindingType::Uniform;
		le.buffer.minBindingSize = 64;
		wgpu::BindGroupLayoutDescriptor bgl{};
		bgl.entryCount = 1;
		bgl.entries = &le;
		m_uniformLayout = dev.CreateBindGroupLayout(&bgl);
		wgpu::BindGroupEntry be{};
		be.binding = 0;
		be.buffer = m_uniform;
		be.size = 64;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = m_uniformLayout;
		bg.entryCount = 1;
		bg.entries = &be;
		m_bindGroup = dev.CreateBindGroup(&bg);
		wgpu::PipelineLayoutDescriptor pl{};
		pl.bindGroupLayoutCount = 1;
		pl.bindGroupLayouts = &m_uniformLayout;
		wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

		m_module = m_ctx.makeShader(dawnShaderSource_batch2d(), "mitiru.batch2d");
		startPipeline(m_pipeline, layout, "vsBatch", "fsBatch", "batch2d");
		return static_cast<bool>(m_pipeline);
	}

	void resize(float width, float height) override
	{
		float m[16];
		dawn_detail::orthoMatrix2D(std::max(width, 1.0f), std::max(height, 1.0f), m);
		std::memcpy(m_ortho, m, sizeof(m));
		m_orthoDirty = true;
		if (m_lit) { m_lit->resize(width, height); }
	}

	[[nodiscard]] bool supportsLighting2D() const override { return m_lit != nullptr; }
	void setLighting2D(const Lighting2D* lighting) override { if (m_lit) { m_lit->setLighting(lighting); } }
	[[nodiscard]] bool supportsTextured() const override { return m_lit && m_lit->active(); }
	[[nodiscard]] std::uint32_t ensureTexture(const void* key, int w, int h, const std::uint8_t* rgba, bool contentMayChange) override
	{
		return m_lit ? m_lit->ensureTexture(key, w, h, rgba, contentMayChange) : 0;
	}
	void setDistanceRange(const void* key, float pixelRange) override { if (m_lit) { m_lit->setDistanceRange(key, pixelRange); } }
	void updateTextureRows(const void* key, int firstRow, int rowCount, const std::uint8_t* rgba) override
	{
		if (m_lit) { m_lit->updateRows(key, firstRow, rowCount, rgba); }
	}
	void setNormalMap(const void* colorKey, const void* normalKey, int w, int h, const std::uint8_t* rgba) override
	{
		if (m_lit) { m_lit->setNormalMap(colorKey, normalKey, w, h, rgba); }
	}
	void submitTextured(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices, std::uint32_t texHandle) override
	{
		if (m_lit) { m_lit->submit(vertices, indices, texHandle); }
	}

	void submit(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices) override
	{
		if (vertices.empty() || indices.empty() || m_ctx.lost) { return; }
		if (m_lit && m_lit->active())
		{
			m_lit->submit(vertices, indices, 0);   // 光が有効な間は、頂点色の描画にも光を当てる
			return;
		}
		MITIRU_DAWN_ZONE("batch2d.submit");
		MITIRU_DAWN_COUNT("batch2d.vertices", vertices.size());
		MITIRU_DAWN_COUNT("batch2d.indices", indices.size());
		draw(m_pipeline, nullptr, vertices.data(), vertices.size(), indices.data(), indices.size());
	}

	bool submitImage(float x, float y, float w, float h, const std::uint8_t* rgba, int pixelWidth, int pixelHeight) override
	{
		if (rgba == nullptr || pixelWidth <= 0 || pixelHeight <= 0 || m_ctx.lost) { return false; }
		MITIRU_DAWN_ZONE("batch2d.image");
		if (!ensureImageResources(pixelWidth, pixelHeight)) { return false; }
		// WriteTexture は queue の順に効く。直前の描画が前の絵を読み終えてから、この絵に替わる
		wgpu::TexelCopyTextureInfo dst{};
		dst.texture = m_imageTex;
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = static_cast<std::uint32_t>(pixelWidth) * 4u;
		layout.rowsPerImage = static_cast<std::uint32_t>(pixelHeight);
		const wgpu::Extent3D extent{static_cast<std::uint32_t>(pixelWidth), static_cast<std::uint32_t>(pixelHeight), 1};
		m_ctx.queue.WriteTexture(&dst, rgba, static_cast<std::size_t>(pixelWidth) * static_cast<std::size_t>(pixelHeight) * 4u, &layout, &extent);

		const sgc::Colorf white{1.0f, 1.0f, 1.0f, 1.0f};
		const Vertex2D quad[4] = {
			{{x, y}, {0.0f, 0.0f}, white},
			{{x + w, y}, {1.0f, 0.0f}, white},
			{{x + w, y + h}, {1.0f, 1.0f}, white},
			{{x, y + h}, {0.0f, 1.0f}, white},
		};
		static constexpr std::uint32_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};
		draw(m_imagePipeline, &m_imageBindGroup, quad, 4, kQuadIndices, 6);
		return true;
	}

private:
	void startPipeline(gfx::AsyncRenderPipeline& out, const wgpu::PipelineLayout& layout, const char* vs, const char* fs, const char* label)
	{
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
		wgpu::FragmentState fragment{};
		fragment.module = m_module;
		fragment.entryPoint = fs;
		fragment.targetCount = 1;
		fragment.targets = &target;
		wgpu::RenderPipelineDescriptor rp{};
		rp.layout = layout;
		rp.vertex.module = m_module;
		rp.vertex.entryPoint = vs;
		rp.vertex.bufferCount = 1;
		rp.vertex.buffers = &vb;
		rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		rp.primitive.cullMode = wgpu::CullMode::None;
		rp.fragment = &fragment;
		out.start(m_ctx, rp, label);
	}

	// 画像用の部品は最初の画像を描くときに作る。動画を使わないゲームには何も作らない
	bool ensureImageResources(int pixelWidth, int pixelHeight)
	{
		const auto& dev = m_ctx.device;
		if (!m_imagePipeline)
		{
			wgpu::BindGroupLayoutEntry entries[2]{};
			entries[0].binding = 0;
			entries[0].visibility = wgpu::ShaderStage::Fragment;
			entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
			entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
			entries[1].binding = 1;
			entries[1].visibility = wgpu::ShaderStage::Fragment;
			entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
			wgpu::BindGroupLayoutDescriptor bgl{};
			bgl.entryCount = 2;
			bgl.entries = entries;
			m_imageLayout = dev.CreateBindGroupLayout(&bgl);
			const wgpu::BindGroupLayout layouts[2] = {m_uniformLayout, m_imageLayout};
			wgpu::PipelineLayoutDescriptor pl{};
			pl.bindGroupLayoutCount = 2;
			pl.bindGroupLayouts = layouts;
			wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);
			startPipeline(m_imagePipeline, layout, "vsImage", "fsImage", "batch2d.image");
			wgpu::SamplerDescriptor sd{};
			sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
			sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
			m_imageSampler = dev.CreateSampler(&sd);
		}
		if (!m_imagePipeline || !m_imageSampler) { return false; }
		if (!m_imageTex || m_imageW != pixelWidth || m_imageH != pixelHeight)
		{
			wgpu::TextureDescriptor td{};
			td.label = "mitiru.batch2d.image";
			td.size = {static_cast<std::uint32_t>(pixelWidth), static_cast<std::uint32_t>(pixelHeight), 1};
			td.format = wgpu::TextureFormat::RGBA8Unorm;   // 頂点色のバッチと同じく、値を変換せずに通す
			td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
			m_imageTex = dev.CreateTexture(&td);
			if (!m_imageTex) { return false; }
			wgpu::BindGroupEntry be[2]{};
			be[0].binding = 0;
			be[0].textureView = m_imageTex.CreateView();
			be[1].binding = 1;
			be[1].sampler = m_imageSampler;
			wgpu::BindGroupDescriptor bg{};
			bg.layout = m_imageLayout;
			bg.entryCount = 2;
			bg.entries = be;
			m_imageBindGroup = dev.CreateBindGroup(&bg);
			m_imageW = pixelWidth;
			m_imageH = pixelHeight;
		}
		return static_cast<bool>(m_imageBindGroup);
	}

	void draw(const gfx::AsyncRenderPipeline& pipeline, const wgpu::BindGroup* extra, const Vertex2D* vertices, std::size_t vertexCount,
	          const std::uint32_t* indices, std::size_t indexCount)
	{
		const std::uint64_t vBytes = vertexCount * sizeof(Vertex2D);
		const std::uint64_t iBytes = (indexCount * sizeof(std::uint32_t) + 3) & ~std::uint64_t(3);
		const std::uint64_t need = ((vBytes + 3) & ~std::uint64_t(3)) + iBytes;
		if (need > kRingBytes) { return; }
		// 同じ範囲を後で書き直しても、キューの順で前の描画後になるため安全
		if (m_cursor + need > kRingBytes) { m_cursor = 0; }
		const std::uint64_t vOffset = m_cursor;
		const std::uint64_t iOffset = vOffset + ((vBytes + 3) & ~std::uint64_t(3));
		m_ctx.queue.WriteBuffer(m_ring, vOffset, vertices, vBytes);
		m_ctx.queue.WriteBuffer(m_ring, iOffset, indices, indexCount * sizeof(std::uint32_t));
		m_cursor += need;
		if (m_orthoDirty)
		{
			m_ctx.queue.WriteBuffer(m_uniform, 0, m_ortho, sizeof(m_ortho));
			m_orthoDirty = false;
		}

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
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, m_bindGroup);
		if (extra != nullptr) { pass.SetBindGroup(1, *extra); }
		pass.SetVertexBuffer(0, m_ring, vOffset, vBytes);
		pass.SetIndexBuffer(m_ring, wgpu::IndexFormat::Uint32, iOffset, indexCount * sizeof(std::uint32_t));
		pass.DrawIndexed(static_cast<std::uint32_t>(indexCount));
		pass.End();
		wgpu::CommandBuffer commands = encoder.Finish();
		m_ctx.queue.Submit(1, &commands);
	}

	gfx::DawnDevice& m_device;
	gfx::DawnContext& m_ctx;
	wgpu::Buffer m_uniform;
	wgpu::Buffer m_ring;
	wgpu::BindGroupLayout m_uniformLayout;
	wgpu::BindGroup m_bindGroup;
	wgpu::ShaderModule m_module;
	gfx::AsyncRenderPipeline m_pipeline;
	std::unique_ptr<dawn_detail::DawnLit2D> m_lit;
	// 画像 (動画の 1 コマ)。最初の submitImage で作る
	gfx::AsyncRenderPipeline m_imagePipeline;
	wgpu::BindGroupLayout m_imageLayout;
	wgpu::Sampler m_imageSampler;
	wgpu::Texture m_imageTex;
	wgpu::BindGroup m_imageBindGroup;
	int m_imageW = 0;
	int m_imageH = 0;
	float m_ortho[16] = {};
	bool m_orthoDirty = true;
	std::uint64_t m_cursor = 0;
};

} // namespace

std::shared_ptr<IBatch2DSink> createDawnBatch2D(gfx::IDevice* device, float screenWidth, float screenHeight, bool withLighting)
{
	auto* dawn = dynamic_cast<gfx::DawnDevice*>(device);
	if (dawn == nullptr) { return nullptr; }
	auto batch = std::make_shared<DawnBatch2D>(*dawn, screenWidth, screenHeight, withLighting);
	if (!batch->initialize()) { return nullptr; }
	return batch;
}

} // namespace mitiru::render
