#include "DawnShadowPass.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include <mitiru/debug/WarnOnce.hpp>

#include "DawnProfile.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

// 箱の 8 隅がすべて光の錐台 (D3D の -w <= x, y <= w、0 <= z <= w) の同じ 1 面の外なら、その物は 1 画素も描かない
bool outsideLightClip(const Mesh::AABB& b, const sgc::Mat4f& mvp)
{
	int outside[6] = {};
	for (int i = 0; i < 8; ++i)
	{
		const float p[4] = {(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z, 1.0f};
		float c[4] = {};
		for (int r = 0; r < 4; ++r)
		{
			c[r] = mvp.m[r][0] * p[0] + mvp.m[r][1] * p[1] + mvp.m[r][2] * p[2] + mvp.m[r][3] * p[3];
		}
		outside[0] += (c[0] > c[3]);
		outside[1] += (c[0] < -c[3]);
		outside[2] += (c[1] > c[3]);
		outside[3] += (c[1] < -c[3]);
		outside[4] += (c[2] > c[3]);
		outside[5] += (c[2] < 0.0f);
	}
	for (const int n : outside) { if (n == 8) { return true; } }
	return false;
}

wgpu::Texture makeDepthTexture(const wgpu::Device& device, std::uint32_t w, std::uint32_t h, const char* label)
{
	wgpu::TextureDescriptor td{};
	td.label = label;
	td.size = {w, h, 1};
	td.format = kDepthFormat;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	return device.CreateTexture(&td);
}

} // namespace

bool DawnShadowPass::create(int mapSize, int farMapSize)
{
	const auto& dev = m_ctx.device;
	m_mapSize = std::max(mapSize, 16);
	m_farMapSize = std::max(farMapSize, 16);
	m_near = makeDepthTexture(dev, static_cast<std::uint32_t>(m_mapSize), static_cast<std::uint32_t>(m_mapSize), "mitiru.shadow.near");
	m_far = makeDepthTexture(dev, static_cast<std::uint32_t>(m_farMapSize) * 2u, static_cast<std::uint32_t>(m_farMapSize), "mitiru.shadow.far");
	m_nearView = m_near.CreateView();
	m_farView = m_far.CreateView();

	// 光の錐台の外 (影マップの縁より外) は影なしとして扱うので、縁の外を引かないよう clamp する
	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	sd.compare = wgpu::CompareFunction::LessEqual;
	m_sampler = dev.CreateSampler(&sd);

	// 光の行列は instance 単位の頂点属性で渡すので、束 (bind group) は無い
	wgpu::PipelineLayoutDescriptor pl{};
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_shadow(), "mitiru.shadow");
	// 頂点バッファは前方の描画と同じ (stride 48) を使い、位置だけを読む。2 本目が光の view * proj * world (列優先の 4 列、instance 単位)
	static const wgpu::VertexAttribute attr = {nullptr, wgpu::VertexFormat::Float32x3, 0, 0};
	static const wgpu::VertexAttribute matrixAttrs[4] = {
		{nullptr, wgpu::VertexFormat::Float32x4, 0, 1},
		{nullptr, wgpu::VertexFormat::Float32x4, 16, 2},
		{nullptr, wgpu::VertexFormat::Float32x4, 32, 3},
		{nullptr, wgpu::VertexFormat::Float32x4, 48, 4},
	};
	wgpu::VertexBufferLayout vb[2]{};
	vb[0].arrayStride = sizeof(Vertex3D);
	vb[0].attributeCount = 1;
	vb[0].attributes = &attr;
	vb[1].arrayStride = sizeof(ShadowDrawCB);
	vb[1].stepMode = wgpu::VertexStepMode::Instance;
	vb[1].attributeCount = 4;
	vb[1].attributes = matrixAttrs;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::True;
	ds.depthCompare = wgpu::CompareFunction::Less;
	// DX12 の影の PSO と同じ余白 (DepthBias 1000、傾き 1)
	ds.depthBias = 1000;
	ds.depthBiasSlopeScale = 1.0f;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = layout;
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsShadow";
	rp.vertex.bufferCount = 2;
	rp.vertex.buffers = vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	// 光の view は左手系で、前面の向きが主カメラと逆になる。向きを取り違えて影が消えないよう、両面を描く
	rp.primitive.cullMode = wgpu::CullMode::None;
	rp.depthStencil = &ds;
	m_pipeline.start(m_ctx, rp, "shadow");
	return module && m_pipeline && m_near && m_far;
}

void DawnShadowPass::collect(const ShadowFrame& frame, int cascade, const std::vector<ShadowCaster>& casters, std::vector<Draw>& out)
{
	out.clear();
	const sgc::Mat4f lightViewProj = frame.proj[cascade] * frame.view[cascade];
	for (const ShadowCaster& c : casters)
	{
		if (c.mesh == nullptr || c.mesh->vertexBuffer == nullptr) { continue; }
		const sgc::Mat4f mvp = lightViewProj * c.world;
		if (outsideLightClip(c.bounds, mvp)) { continue; }
		// 深度だけを書くので、描く順は絵に効かない。同じ物が続くかぎり 1 つの instanced draw にする
		if (out.empty() || out.back().mesh != c.mesh) { out.push_back({c.mesh, static_cast<std::uint32_t>(m_matrices.size()), 0}); }
		ShadowDrawCB cb{};
		toColumnMajor(mvp, cb.mvp);
		m_matrices.push_back(cb);
		++out.back().count;
	}
}

void DawnShadowPass::uploadMatrices()
{
	const std::uint64_t bytes = m_matrices.size() * sizeof(ShadowDrawCB);
	const std::uint64_t need = std::max<std::uint64_t>(std::bit_ceil(bytes), sizeof(ShadowDrawCB) * 256);
	if (!m_matrixBuffer || m_matrixBufferBytes < need)
	{
		wgpu::BufferDescriptor bd{};
		bd.size = need;
		bd.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
		m_matrixBuffer = m_ctx.device.CreateBuffer(&bd);
		m_matrixBufferBytes = need;
	}
	m_ctx.queue.WriteBuffer(m_matrixBuffer, 0, m_matrices.data(), bytes);
}

void DawnShadowPass::recordCascade(wgpu::RenderPassEncoder& pass, const std::vector<Draw>& draws) const
{
	const GpuMesh* bound = nullptr;
	for (const Draw& d : draws)
	{
		if (d.mesh != bound)
		{
			pass.SetVertexBuffer(0, d.mesh->vertexBuffer, d.mesh->vertexOffset, d.mesh->vertexBytes);
			if (d.mesh->indexCount > 0) { pass.SetIndexBuffer(d.mesh->indexBuffer, wgpu::IndexFormat::Uint32, 0, d.mesh->indexBytes); }
			bound = d.mesh;
		}
		pass.SetVertexBuffer(1, m_matrixBuffer, static_cast<std::uint64_t>(d.first) * sizeof(ShadowDrawCB),
		                     static_cast<std::uint64_t>(d.count) * sizeof(ShadowDrawCB));
		if (d.mesh->indexCount > 0) { pass.DrawIndexed(d.mesh->indexCount, d.count); }
		else { pass.Draw(d.mesh->vertexCount, d.count); }
	}
}

void DawnShadowPass::encode(wgpu::CommandEncoder& encoder, const ShadowFrame& frame, const std::vector<ShadowCaster>& casters)
{
	if (frame.cascades <= 0 || !m_pipeline) { return; }
	m_matrices.clear();
	m_drawsNear.clear();
	m_drawsMid.clear();
	m_drawsFar.clear();
	{
		MITIRU_DAWN_ZONE("shadow.collect");
		collect(frame, 0, casters, m_drawsNear);
		if (frame.cascades >= 2) { collect(frame, 1, casters, m_drawsMid); }
		if (frame.cascades >= 3) { collect(frame, 2, casters, m_drawsFar); }
	}
	MITIRU_DAWN_COUNT("shadow.instances", m_matrices.size());
	MITIRU_DAWN_COUNT("shadow.runs", m_drawsNear.size() + m_drawsMid.size() + m_drawsFar.size());
	if (!m_matrices.empty())
	{
		MITIRU_DAWN_ZONE("shadow.upload");
		uploadMatrices();
	}

	const auto beginPass = [&](const wgpu::TextureView& view) {
		wgpu::RenderPassDepthStencilAttachment depth{};
		depth.view = view;
		depth.depthLoadOp = wgpu::LoadOp::Clear;
		depth.depthStoreOp = wgpu::StoreOp::Store;
		depth.depthClearValue = 1.0f;
		wgpu::RenderPassDescriptor rp{};
		rp.depthStencilAttachment = &depth;
		return encoder.BeginRenderPass(&rp);
	};

	MITIRU_DAWN_ZONE("shadow.record");
	wgpu::RenderPassEncoder nearPass = beginPass(m_nearView);
	if (!m_drawsNear.empty())
	{
		nearPass.SetPipeline(m_pipeline);
		recordCascade(nearPass, m_drawsNear);
	}
	nearPass.End();
	if (frame.cascades < 2) { return; }

	wgpu::RenderPassEncoder farPass = beginPass(m_farView);
	const auto side = static_cast<float>(m_farMapSize);
	if (!m_drawsMid.empty())
	{
		farPass.SetPipeline(m_pipeline);
		farPass.SetViewport(0.0f, 0.0f, side, side, 0.0f, 1.0f);
		farPass.SetScissorRect(0, 0, static_cast<std::uint32_t>(m_farMapSize), static_cast<std::uint32_t>(m_farMapSize));
		recordCascade(farPass, m_drawsMid);
	}
	if (frame.cascades >= 3 && !m_drawsFar.empty())
	{
		farPass.SetPipeline(m_pipeline);
		farPass.SetViewport(side, 0.0f, side, side, 0.0f, 1.0f);
		farPass.SetScissorRect(static_cast<std::uint32_t>(m_farMapSize), 0, static_cast<std::uint32_t>(m_farMapSize), static_cast<std::uint32_t>(m_farMapSize));
		recordCascade(farPass, m_drawsFar);
	}
	farPass.End();
}

} // namespace mitiru::render::dawn_detail
