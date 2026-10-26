#include "DawnParticles.hpp"
#include <mitiru/render/RenderFeatureNote.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <mitiru/render/ColorSpace.hpp>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint64_t kCbBytes = 80;   // viewProj (64 byte) と params (16 byte)
constexpr float kStepDt = 1.0f / static_cast<float>(kParticleStepHz);

wgpu::VertexBufferLayout vertexLayout()
{
	static const wgpu::VertexAttribute attrs[6] = {
		{nullptr, wgpu::VertexFormat::Float32x3, 0, 0},
		{nullptr, wgpu::VertexFormat::Float32x2, 12, 1},
		{nullptr, wgpu::VertexFormat::Float32x4, 20, 2},
		{nullptr, wgpu::VertexFormat::Float32x4, 36, 3},
		{nullptr, wgpu::VertexFormat::Float32x3, 52, 4},
		{nullptr, wgpu::VertexFormat::Float32x4, 64, 5},
	};
	wgpu::VertexBufferLayout vb{};
	vb.arrayStride = sizeof(ParticleVertex);
	vb.attributeCount = 6;
	vb.attributes = attrs;
	return vb;
}

float curve(float a, float b, float c, float mid, float t)
{
	return (t < mid) ? a + (b - a) * (t / std::max(mid, 1e-4f)) : b + (c - b) * ((t - mid) / std::max(1.0f - mid, 1e-4f));
}

float curve(const float c[3][4], int k, float mid, float t) { return curve(c[0][k], c[1][k], c[2][k], mid, t); }

float length3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

float spawnOffsetUnit(const ParticleEmitterDesc& d, std::uint32_t index, float out[3])
{
	const float z = particleRandom(d.seed, index, 4) * 2.0f - 1.0f;
	const float phi = 6.28318530718f * particleRandom(d.seed, index, 5);
	const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
	const float rad = d.spawnRadius * std::cbrt(particleRandom(d.seed, index, 6));
	out[0] = r * std::cos(phi) * rad;
	out[1] = r * std::sin(phi) * rad;
	out[2] = z * rad;
	return rad;
}

} // namespace

bool DawnParticles::create()
{
	const auto& dev = m_ctx.device;
	m_module = m_ctx.makeShader(dawnShaderSource_particle(), "mitiru.particle");

	wgpu::BindGroupLayoutEntry le[4]{};
	le[0].binding = 0;
	le[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	le[0].buffer.type = wgpu::BufferBindingType::Uniform;
	le[0].buffer.minBindingSize = kCbBytes;
	le[1].binding = 1;
	le[1].visibility = wgpu::ShaderStage::Fragment;
	le[1].texture.sampleType = wgpu::TextureSampleType::Float;
	le[1].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
	le[2].binding = 2;
	le[2].visibility = wgpu::ShaderStage::Fragment;
	le[2].sampler.type = wgpu::SamplerBindingType::Filtering;
	le[3].binding = 3;
	le[3].visibility = wgpu::ShaderStage::Fragment;
	le[3].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	le[3].texture.multisampled = true;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 4;
	bgl.entries = le;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	bgl.entryCount = 3;
	m_opaqueLayout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	m_pipelineLayout = dev.CreatePipelineLayout(&pl);
	pl.bindGroupLayouts = &m_opaqueLayout;
	m_opaquePipelineLayout = dev.CreatePipelineLayout(&pl);

	wgpu::BufferDescriptor bd{};
	bd.size = kCbBytes;
	bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_cb = dev.CreateBuffer(&bd);

	m_additivePipeline = makePipeline(true);
	m_alphaPipeline = makePipeline(false);
	// 互換レベルが alpha to coverage を受けないときは、形の半分で切る
	m_opaquePipeline = makeOpaquePipeline(true);
	if (!m_opaquePipeline) { m_opaquePipeline = makeOpaquePipeline(false); }
	return m_module && m_additivePipeline && m_alphaPipeline && m_opaquePipeline;
}

void DawnParticles::bind(const wgpu::TextureView& vfxColor, const wgpu::Sampler& vfxSampler, const wgpu::TextureView& sceneDepth)
{
	wgpu::BindGroupEntry be[4]{};
	be[0].binding = 0;
	be[0].buffer = m_cb;
	be[0].size = kCbBytes;
	be[1].binding = 1;
	be[1].textureView = vfxColor;
	be[2].binding = 2;
	be[2].sampler = vfxSampler;
	be[3].binding = 3;
	be[3].textureView = sceneDepth;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_opaqueLayout;
	bg.entryCount = 3;
	bg.entries = be;
	m_opaqueGroup = m_ctx.device.CreateBindGroup(&bg);
	bg.layout = m_layout;
	bg.entryCount = 4;
	m_group = m_ctx.device.CreateBindGroup(&bg);
}

wgpu::RenderPipeline DawnParticles::makePipeline(bool additive) const
{
	wgpu::VertexBufferLayout vb = vertexLayout();
	wgpu::BlendState bs{};
	if (additive)
	{
		bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
		bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
	}
	else
	{
		bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
		bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
	}
	wgpu::ColorTargetState target{};
	target.format = m_hdrFormat;
	target.blend = &bs;
	wgpu::FragmentState fs{};
	fs.module = m_module;
	fs.entryPoint = additive ? "fsAdditive" : "fsAlpha";
	fs.targetCount = 1;
	fs.targets = &target;
	// 深度は読み取りだけにし、場面の不透明物に隠れるようにする
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::False;
	ds.depthCompare = wgpu::CompareFunction::LessEqual;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_pipelineLayout;
	rp.vertex.module = m_module;
	rp.vertex.entryPoint = "vsParticle";
	rp.vertex.bufferCount = 1;
	rp.vertex.buffers = &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.cullMode = wgpu::CullMode::None;
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.fragment = &fs;
	::mitiru::gfx::notePipelineCreated();
	return m_ctx.device.CreateRenderPipeline(&rp);
}

// 場面の不透明のパスで、色・輪郭線の下地 (法線と印、距離)・深度を書く。縁は alpha to coverage
wgpu::RenderPipeline DawnParticles::makeOpaquePipeline(bool alphaToCoverage) const
{
	const auto& dev = m_ctx.device;
	wgpu::VertexBufferLayout vb = vertexLayout();
	wgpu::ColorTargetState targets[3]{};
	targets[0].format = m_hdrFormat;
	targets[1].format = kLineGBufferFormat;
	targets[2].format = kLineGBufferFormat;
	wgpu::FragmentState fs{};
	fs.module = m_module;
	fs.entryPoint = alphaToCoverage ? "fsOpaque" : "fsOpaqueCut";
	fs.targetCount = 3;
	fs.targets = targets;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::True;
	ds.depthCompare = wgpu::CompareFunction::Less;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_opaquePipelineLayout;
	rp.vertex.module = m_module;
	rp.vertex.entryPoint = "vsParticle";
	rp.vertex.bufferCount = 1;
	rp.vertex.buffers = &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.cullMode = wgpu::CullMode::None;
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.multisample.alphaToCoverageEnabled = alphaToCoverage;
	rp.fragment = &fs;
	dev.PushErrorScope(wgpu::ErrorFilter::Validation);
	::mitiru::gfx::notePipelineCreated();
	wgpu::RenderPipeline pipeline = dev.CreateRenderPipeline(&rp);
	bool ok = false;
	bool done = false;
	dev.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView) {
		ok = type == wgpu::ErrorType::NoError;
		done = true;
	});
	if (!m_ctx.pump(done, 5000) || !ok) { return {}; }
	return pipeline;
}

void DawnParticles::submit(const ParticleEmitterDesc* emitters, const ParticleFlatShade* shades, int count)
{
	if (emitters == nullptr) { return; }
	for (int i = 0; i < count && static_cast<int>(m_submitted.size()) < kMaxParticleEmitters; ++i)
	{
		Submitted s;
		s.desc = emitters[i];
		if (s.desc.collision != ParticleCollision::None) { features::note(features::Feature::GpuParticles); }
		s.hasShade = shades != nullptr;
		if (shades != nullptr) { s.shade = shades[i]; }
		m_submitted.push_back(s);
	}
}

// 1 刻み進める。GPU の CSMain と同じ手順で、席ごとに新しい粒が出る番か、今の粒を進めるかを決める。衝突は扱わない
void DawnParticles::runStep(Emitter& e, std::uint32_t step) const
{
	const ParticleEmitterDesc& d = e.desc;
	const std::uint32_t capacity = static_cast<std::uint32_t>(e.seats.size());
	const float t = static_cast<float>(step) / static_cast<float>(kParticleStepHz);
	const std::uint32_t n = particleSpawnedAtStep(d, step);
	for (std::uint32_t s = 0; s < capacity; ++s)
	{
		Seat& seat = e.seats[s];
		if (n > s)
		{
			const std::uint32_t newest = s + capacity * ((n - 1u - s) / capacity);
			if (newest != seat.index)
			{
				const float a = t - particleSpawnTime(d, newest);
				float v0[3];
				particleInitialVelocity(d, newest, v0);
				float off[3] = {};
				if (d.spawnRadius > 0.0f) { spawnOffsetUnit(d, newest, off); }
				seat = Seat{};
				seat.index = newest;
				seat.random = particleRandom(d.seed, newest, 7);
				seat.lifetime = particleLifetime(d, newest);
				for (int k = 0; k < 3; ++k)
				{
					seat.position[k] = d.position[k] + off[k] + v0[k] * a + 0.5f * d.gravity[k] * a * a;
					seat.velocity[k] = v0[k] + d.gravity[k] * a;
				}
				seat.age = a;
				// 進め直しの最初の刻みより前に出た粒は、目標の時刻には寿命が終わっている
				if (a > 1.01f / static_cast<float>(kParticleStepHz) + 1e-5f) { seat.age = 0.0f; seat.lifetime = 0.0f; }
				continue;
			}
		}
		if (seat.index == kParticleNoIndex || seat.age >= seat.lifetime) { continue; }
		const float damp = 1.0f / (1.0f + d.drag * kStepDt);
		for (int k = 0; k < 3; ++k)
		{
			seat.velocity[k] += d.gravity[k] * kStepDt;
			seat.velocity[k] *= damp;
			seat.position[k] += seat.velocity[k] * kStepDt;
		}
		seat.age += kStepDt;
	}
}

void DawnParticles::advance(Emitter& e)
{
	const std::uint32_t target = particleTargetStep(e.desc.age);
	const ParticleStepPlan plan = planParticleSteps(e.desc, e.hasState, e.doneStep, target);
	if (plan.reset) { e.seats.assign(particleCapacity(e.desc), Seat{}); }
	for (std::uint32_t i = 0; i < plan.count; ++i) { runStep(e, plan.first + i); }
	e.hasState = true;
	e.doneStep = plan.first + plan.count - 1;
}

// 生きている粒を板ポリにする。DX12 の VSMain と同じ式で、大きさと色の 3 点の曲線、近くで薄くする処理、速度の向きへの引き伸ばしを行う
void DawnParticles::appendQuads(const Emitter& e, const ParticleView& view)
{
	const ParticleEmitterDesc& d = e.desc;
	std::vector<ParticleVertex>& out = d.blend == ParticleBlend::Additive ? m_additiveVerts
	                                  : (d.blend == ParticleBlend::Opaque ? m_opaqueVerts : m_alphaVerts);
	const bool opaque = d.blend == ParticleBlend::Opaque;
	if (!opaque && d.softDistance > 0.0f) { m_wantsSceneDepth = true; }
	for (const Seat& s : e.seats)
	{
		if (s.index == kParticleNoIndex || s.age >= s.lifetime) { continue; }
		const float t = std::clamp(s.age / std::max(s.lifetime, 1e-4f), 0.0f, 1.0f);
		float size = curve(d.size[0], d.size[1], d.size[2], d.midPoint, t);
		float color[4] = {curve(d.color, 0, d.midPoint, t), curve(d.color, 1, d.midPoint, t), curve(d.color, 2, d.midPoint, t),
		                  curve(d.color, 3, d.midPoint, t)};
		const float toEyeRaw[3] = {view.eye.x - s.position[0], view.eye.y - s.position[1], view.eye.z - s.position[2]};
		const float dist = length3(toEyeRaw);
		if (d.nearFadeEnd > d.nearFadeStart)
		{
			const float nearFade = std::clamp((dist - d.nearFadeStart) / (d.nearFadeEnd - d.nearFadeStart), 0.0f, 1.0f);
			if (nearFade <= 0.0f) { continue; }
			if (d.blend == ParticleBlend::Opaque) { size *= nearFade; } else { color[3] *= nearFade; }
		}
		const float inv = 1.0f / std::max(dist, 1e-6f);
		const sgc::Vec3f toEye{toEyeRaw[0] * inv, toEyeRaw[1] * inv, toEyeRaw[2] * inv};
		const sgc::Vec3f vel{s.velocity[0], s.velocity[1], s.velocity[2]};
		const sgc::Vec3f v = vel - toEye * vel.dot(toEye);
		const float speed = v.length();

		// 場面の光を受ける割合。局所光は扱わない
		float light[3] = {1.0f, 1.0f, 1.0f};
		if (d.lit > 0.0f)
		{
			const float sun = 0.35f + 0.65f * std::clamp(toEye.dot(view.lightDir * -1.0f), 0.0f, 1.0f);
			const float scene[3] = {view.ambient.r + view.lightColor.r * sun, view.ambient.g + view.lightColor.g * sun,
			                        view.ambient.b + view.lightColor.b * sun};
			for (int k = 0; k < 3; ++k) { light[k] = 1.0f + (scene[k] - 1.0f) * d.lit; }
		}

		sgc::Vec3f corners[4];
		const float cx[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
		const float cy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
		const sgc::Vec3f pos{s.position[0], s.position[1], s.position[2]};
		if (d.stretch > 0.0f && speed > 1e-4f)
		{
			const sgc::Vec3f axis = v * (1.0f / speed);
			const sgc::Vec3f side = axis.cross(toEye).normalized();
			for (int c = 0; c < 4; ++c)
			{
				corners[c] = pos + side * (cx[c] * size * 0.5f) + axis * (cy[c] * (size * 0.5f + speed * d.stretch * 0.5f));
			}
		}
		else
		{
			// 縁の硬い円は回さず、影の三日月を常に画面の同じ側に出す
			const float ang = (d.shape == ParticleShape::HardDisc) ? 0.0f : s.random * 6.28318530718f;
			const float ca = std::cos(ang);
			const float sa = std::sin(ang);
			for (int c = 0; c < 4; ++c)
			{
				const float x = cx[c] * ca - cy[c] * sa;
				const float y = cx[c] * sa + cy[c] * ca;
				corners[c] = pos + (view.right * x + view.up * y) * (size * 0.5f);
			}
		}
		for (int c = 0; c < 4; ++c)
		{
			ParticleVertex v4{};
			v4.position[0] = corners[c].x;
			v4.position[1] = corners[c].y;
			v4.position[2] = corners[c].z;
			v4.uv[0] = cx[c] * 0.5f + 0.5f;
			v4.uv[1] = cy[c] * -0.5f + 0.5f;
			v4.color[0] = color[0] * light[0];
			v4.color[1] = color[1] * light[1];
			v4.color[2] = color[2] * light[2];
			v4.color[3] = color[3];
			v4.info[0] = static_cast<float>(d.shape);
			v4.extra[0] = static_cast<float>(d.textureLayer);
			v4.extra[1] = opaque ? 0.0f : d.softDistance;
			if (e.hasShade && d.shape == ParticleShape::HardDisc)
			{
				const sgc::Colorf sh = linearColor(sgc::Colorf{e.shade.shadowColor[0], e.shade.shadowColor[1], e.shade.shadowColor[2], 1.0f});
				v4.info[1] = 1.0f;
				v4.info[2] = e.shade.litOffset[0];
				v4.info[3] = e.shade.litOffset[1];
				v4.shadow[0] = sh.r * light[0];
				v4.shadow[1] = sh.g * light[1];
				v4.shadow[2] = sh.b * light[2];
			}
			out.push_back(v4);
		}
	}
}

void DawnParticles::upload(wgpu::Buffer& buffer, std::uint64_t& capacity, wgpu::BufferUsage usage, const void* data,
                           std::size_t bytes) const
{
	const std::uint64_t need = (static_cast<std::uint64_t>(bytes) + 3) & ~std::uint64_t(3);
	if (!buffer || capacity < need)
	{
		capacity = std::max<std::uint64_t>(need * 2, 4096);
		wgpu::BufferDescriptor bd{};
		bd.size = capacity;
		bd.usage = usage | wgpu::BufferUsage::CopyDst;
		buffer = m_ctx.device.CreateBuffer(&bd);
	}
	m_ctx.queue.WriteBuffer(buffer, 0, data, need);
}

void DawnParticles::update(const ParticleView& view)
{
	++m_frame;
	// 渡されなくなったエミッターの粒は消える
	for (auto it = m_emitters.begin(); it != m_emitters.end();)
	{
		it = (it->second.lastFrame + 1 < m_frame) ? m_emitters.erase(it) : std::next(it);
	}
	m_additiveVerts.clear();
	m_alphaVerts.clear();
	m_opaqueVerts.clear();
	m_wantsSceneDepth = false;
	for (const Submitted& s : m_submitted)
	{
		Emitter& e = m_emitters[s.desc.key];
		const std::uint32_t hash = particleShapeHash(s.desc);
		if (e.hasState && e.shapeHash != hash) { e.hasState = false; }   // 形が変わったら作り直す
		e.desc = s.desc;
		e.shade = s.shade;
		e.hasShade = s.hasShade;
		e.shapeHash = hash;
		e.lastFrame = m_frame;
		advance(e);
		appendQuads(e, view);
	}
	m_additiveIndices = static_cast<std::uint32_t>(m_additiveVerts.size() / 4 * 6);
	m_alphaIndices = static_cast<std::uint32_t>(m_alphaVerts.size() / 4 * 6);
	m_opaqueIndices = static_cast<std::uint32_t>(m_opaqueVerts.size() / 4 * 6);
	if (!hasDraws() && !hasOpaqueDraws()) { return; }

	std::vector<ParticleVertex> verts = m_additiveVerts;
	verts.insert(verts.end(), m_alphaVerts.begin(), m_alphaVerts.end());
	verts.insert(verts.end(), m_opaqueVerts.begin(), m_opaqueVerts.end());
	std::vector<std::uint32_t> indices;
	indices.reserve(m_additiveIndices + m_alphaIndices + m_opaqueIndices);
	for (std::uint32_t q = 0; q < verts.size() / 4; ++q)
	{
		const std::uint32_t b = q * 4;
		for (const std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u}) { indices.push_back(b + k); }
	}
	upload(m_vertices, m_vertexCapacity, wgpu::BufferUsage::Vertex, verts.data(), verts.size() * sizeof(ParticleVertex));
	upload(m_indices, m_indexCapacity, wgpu::BufferUsage::Index, indices.data(), indices.size() * sizeof(std::uint32_t));
	float cb[20];
	std::memcpy(cb, view.viewProj, sizeof(view.viewProj));
	cb[16] = view.farClip;
	cb[17] = view.hdrScale;
	cb[18] = cb[19] = 0.0f;
	m_ctx.queue.WriteBuffer(m_cb, 0, cb, sizeof(cb));
}

void DawnParticles::draw(wgpu::RenderPassEncoder& pass) const
{
	if (!hasDraws()) { return; }
	pass.SetBindGroup(0, m_group);
	pass.SetVertexBuffer(0, m_vertices);
	pass.SetIndexBuffer(m_indices, wgpu::IndexFormat::Uint32);
	if (m_additiveIndices > 0)
	{
		pass.SetPipeline(m_additivePipeline);
		pass.DrawIndexed(m_additiveIndices, 1, 0, 0, 0);
	}
	if (m_alphaIndices > 0)
	{
		pass.SetPipeline(m_alphaPipeline);
		pass.DrawIndexed(m_alphaIndices, 1, m_additiveIndices, 0, 0);
	}
}

void DawnParticles::drawOpaque(wgpu::RenderPassEncoder& pass) const
{
	if (!hasOpaqueDraws()) { return; }
	pass.SetPipeline(m_opaquePipeline);
	pass.SetBindGroup(0, m_opaqueGroup);
	pass.SetVertexBuffer(0, m_vertices);
	pass.SetIndexBuffer(m_indices, wgpu::IndexFormat::Uint32);
	pass.DrawIndexed(m_opaqueIndices, 1, m_additiveIndices + m_alphaIndices, 0, 0);
}

} // namespace mitiru::render::dawn_detail
