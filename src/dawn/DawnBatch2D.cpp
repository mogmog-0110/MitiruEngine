#include <mitiru/render/Batch2D_Dawn.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

#include <mitiru/gfx/dawn/DawnDevice.hpp>

#include "DawnContext.hpp"
#include "DawnDeviceTarget.hpp"
#include "DawnProfile.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render
{

namespace
{

constexpr std::uint64_t kRingBytes = 8ull * 1024 * 1024;

// 論理画面 → クリップ空間の列優先行列。RenderPipeline2D の OrthoMatrix と同じ値
void orthoMatrix(float width, float height, float out[16])
{
	std::fill(out, out + 16, 0.0f);
	out[0] = 2.0f / width;
	out[5] = -2.0f / height;
	out[10] = 1.0f;
	out[12] = -1.0f;
	out[13] = 1.0f;
	out[15] = 1.0f;
}

class DawnBatch2D final : public IBatch2DSink
{
public:
	DawnBatch2D(gfx::DawnDevice& device, float width, float height) : m_device(device), m_ctx(*device.context())
	{
		resize(width, height);
	}

	bool initialize()
	{
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
		wgpu::BindGroupLayout layoutBgl = dev.CreateBindGroupLayout(&bgl);
		wgpu::BindGroupEntry be{};
		be.binding = 0;
		be.buffer = m_uniform;
		be.size = 64;
		wgpu::BindGroupDescriptor bg{};
		bg.layout = layoutBgl;
		bg.entryCount = 1;
		bg.entries = &be;
		m_bindGroup = dev.CreateBindGroup(&bg);
		wgpu::PipelineLayoutDescriptor pl{};
		pl.bindGroupLayoutCount = 1;
		pl.bindGroupLayouts = &layoutBgl;
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
		wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_batch2d(), "mitiru.batch2d");
		wgpu::FragmentState fs{};
		fs.module = module;
		fs.entryPoint = "fsBatch";
		fs.targetCount = 1;
		fs.targets = &target;
		wgpu::RenderPipelineDescriptor rp{};
		rp.layout = layout;
		rp.vertex.module = module;
		rp.vertex.entryPoint = "vsBatch";
		rp.vertex.bufferCount = 1;
		rp.vertex.buffers = &vb;
		rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		rp.primitive.cullMode = wgpu::CullMode::None;
		rp.fragment = &fs;
		m_pipeline.start(m_ctx, rp, "batch2d");
		return static_cast<bool>(m_pipeline);
	}

	void resize(float width, float height) override
	{
		float m[16];
		orthoMatrix(std::max(width, 1.0f), std::max(height, 1.0f), m);
		std::memcpy(m_ortho, m, sizeof(m));
		m_orthoDirty = true;
	}

	void submit(const std::vector<Vertex2D>& vertices, const std::vector<std::uint32_t>& indices) override
	{
		if (vertices.empty() || indices.empty() || m_ctx.lost) { return; }
		MITIRU_DAWN_ZONE("batch2d.submit");
		MITIRU_DAWN_COUNT("batch2d.vertices", vertices.size());
		MITIRU_DAWN_COUNT("batch2d.indices", indices.size());
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
		pass.SetPipeline(m_pipeline);
		pass.SetBindGroup(0, m_bindGroup);
		pass.SetVertexBuffer(0, m_ring, vOffset, vBytes);
		pass.SetIndexBuffer(m_ring, wgpu::IndexFormat::Uint32, iOffset, indices.size() * sizeof(std::uint32_t));
		pass.DrawIndexed(static_cast<std::uint32_t>(indices.size()));
		pass.End();
		wgpu::CommandBuffer commands = encoder.Finish();
		m_ctx.queue.Submit(1, &commands);
	}

private:
	gfx::DawnDevice& m_device;
	gfx::DawnContext& m_ctx;
	wgpu::Buffer m_uniform;
	wgpu::Buffer m_ring;
	wgpu::BindGroup m_bindGroup;
	gfx::AsyncRenderPipeline m_pipeline;
	float m_ortho[16] = {};
	bool m_orthoDirty = true;
	std::uint64_t m_cursor = 0;
};

} // namespace

std::shared_ptr<IBatch2DSink> createDawnBatch2D(gfx::IDevice* device, float screenWidth, float screenHeight)
{
	auto* dawn = dynamic_cast<gfx::DawnDevice*>(device);
	if (dawn == nullptr) { return nullptr; }
	auto batch = std::make_shared<DawnBatch2D>(*dawn, screenWidth, screenHeight);
	if (!batch->initialize()) { return nullptr; }
	return batch;
}

} // namespace mitiru::render
