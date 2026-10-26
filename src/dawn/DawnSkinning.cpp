#include "DawnSkinning.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/MorphTargets.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint64_t kParamStride = 256;          // uniform の dynamic offset の境
constexpr std::uint64_t kParamBytes = 16;
constexpr std::uint64_t kMorphParamBytes = 32;       // モーフ用の uniform: 8 word
constexpr std::uint32_t kWeightUnit = 16;            // 重みはパレットの行列と同じ 16 float を 1 単位として積む
constexpr std::uint32_t kSourceWords = 20;           // 頂点 1 つの源: float4 x 3 + uint4 + float4
constexpr std::uint64_t kOutStride = sizeof(Vertex3D);
constexpr std::uint32_t kThreads = 64;
constexpr std::uint32_t kMaxGroupsY = 65535;         // maxComputeWorkgroupsPerDimension の既定
constexpr std::uint64_t kMinBufferBytes = 64 * 1024;

static_assert(sizeof(sgc::Mat4f) == 64, "パレットは 4x4 の float をそのまま並べる");
static_assert(kOutStride == 48, "出力は Vertex3D の 48 byte で、skin.slang の float4 x 3 と合わせている");

std::uint32_t bits(float v) noexcept { return std::bit_cast<std::uint32_t>(v); }

} // namespace

bool DawnSkinning::create()
{
	wgpu::Limits limits{};
	if (m_ctx.device.GetLimits(&limits) != wgpu::Status::Success) { return false; }
	m_maxBufferBytes = std::min<std::uint64_t>(limits.maxStorageBufferBindingSize, limits.maxBufferSize);
	if (limits.maxStorageBuffersPerShaderStage < 4 || limits.maxComputeWorkgroupSizeX < kThreads ||
	    limits.maxComputeInvocationsPerWorkgroup < kThreads || limits.maxComputeWorkgroupsPerDimension < kMaxGroupsY)
	{
		return false;
	}
	if (!createPipeline()) { return false; }
	ensureShared(kMinBufferBytes, kMinBufferBytes, kMinBufferBytes, kParamStride * 64);
	m_ready = m_palette && m_instances && m_out && m_params;
	return m_ready;
}

bool DawnSkinning::createPipeline()
{
	const auto& dev = m_ctx.device;
	dev.PushErrorScope(wgpu::ErrorFilter::Validation);
	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_skin(), "mitiru.skin");
	wgpu::BindGroupLayoutEntry entries[5]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Compute;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.hasDynamicOffset = true;
	entries[0].buffer.minBindingSize = kParamBytes;
	for (std::uint32_t b = 1; b <= 3; ++b)
	{
		entries[b].binding = b;
		entries[b].visibility = wgpu::ShaderStage::Compute;
		entries[b].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	}
	entries[4].binding = 4;
	entries[4].visibility = wgpu::ShaderStage::Compute;
	entries[4].buffer.type = wgpu::BufferBindingType::Storage;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = entries;
	m_layout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);
	wgpu::ComputePipelineDescriptor cpd{};
	cpd.layout = layout;
	cpd.compute.module = module;
	cpd.compute.entryPoint = "csSkin";
	::mitiru::gfx::notePipelineCreated();
	m_pipeline = dev.CreateComputePipeline(&cpd);
	bool valid = false;
	bool done = false;
	dev.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
		[&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView) {
			valid = type == wgpu::ErrorType::NoError;
			done = true;
		});
	return m_ctx.pump(done, 5000) && valid && module && m_pipeline;
}

bool DawnSkinning::createMorphPipeline()
{
	if (m_morphPipeline) { return true; }
	if (!m_ready) { return false; }
	const auto& dev = m_ctx.device;
	dev.PushErrorScope(wgpu::ErrorFilter::Validation);
	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_skin_morph(), "mitiru.skin_morph");
	wgpu::BindGroupLayoutEntry entries[5]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Compute;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.hasDynamicOffset = true;
	entries[0].buffer.minBindingSize = kMorphParamBytes;
	for (std::uint32_t b = 1; b <= 3; ++b)
	{
		entries[b].binding = b;
		entries[b].visibility = wgpu::ShaderStage::Compute;
		entries[b].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
	}
	entries[4].binding = 4;
	entries[4].visibility = wgpu::ShaderStage::Compute;
	entries[4].buffer.type = wgpu::BufferBindingType::Storage;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = entries;
	m_morphLayout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_morphLayout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);
	wgpu::ComputePipelineDescriptor cpd{};
	cpd.layout = layout;
	cpd.compute.module = module;
	cpd.compute.entryPoint = "csSkinMorph";
	::mitiru::gfx::notePipelineCreated();
	m_morphPipeline = dev.CreateComputePipeline(&cpd);
	bool valid = false;
	bool done = false;
	dev.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
		[&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView) {
			valid = type == wgpu::ErrorType::NoError;
			done = true;
		});
	if (!(m_ctx.pump(done, 5000) && valid && module && m_morphPipeline))
	{
		m_morphPipeline = nullptr;
		debug::warnOnce("dawn.skin.morph.pipeline", "モーフの compute を作れなかったので、モーフは CPU で足します (遅くなります)");
		return false;
	}
	return true;
}

wgpu::Buffer DawnSkinning::makeBuffer(const char* label, std::uint64_t bytes, wgpu::BufferUsage usage) const
{
	wgpu::BufferDescriptor bd{};
	bd.label = label;
	bd.size = bytes;
	bd.usage = usage | wgpu::BufferUsage::CopyDst;
	return m_ctx.device.CreateBuffer(&bd);
}

// 足りない buffer だけ 2 の冪へ広げる。広げたら世代を進め、源ごとの束を使う前に再作成させる
void DawnSkinning::ensureShared(std::uint64_t paletteBytes, std::uint64_t instanceBytes, std::uint64_t outBytes, std::uint64_t paramBytes)
{
	const auto grown = [&](wgpu::Buffer& buffer, std::uint64_t& have, std::uint64_t need, std::uint64_t cap, const char* label,
	                       wgpu::BufferUsage usage) {
		if (buffer && have >= need) { return; }
		std::uint64_t size = std::max<std::uint64_t>(std::bit_ceil(std::max(need, kMinBufferBytes)), kMinBufferBytes);
		size = std::min(size, std::max(cap, need));
		buffer = makeBuffer(label, size, usage);
		have = size;
		++m_generation;
	};
	grown(m_palette, m_paletteBytes, paletteBytes, m_maxBufferBytes, "mitiru.skin.palette", wgpu::BufferUsage::Storage);
	grown(m_instances, m_instanceBytes, instanceBytes, m_maxBufferBytes, "mitiru.skin.instances", wgpu::BufferUsage::Storage);
	grown(m_out, m_outBytes, outBytes, m_maxBufferBytes, "mitiru.skin.out", wgpu::BufferUsage::Storage | wgpu::BufferUsage::Vertex);
	grown(m_params, m_paramBytes, paramBytes, ~std::uint64_t(0), "mitiru.skin.params", wgpu::BufferUsage::Uniform);
}

void DawnSkinning::beginFrame(std::uint64_t frame) noexcept
{
	m_frame = frame;
	m_paletteCpu.clear();
	m_requests.clear();
	m_meshCursor = 0;
	m_outVertices = 0;
	m_stats = {};
}

std::uint32_t DawnSkinning::appendPalette(std::span<const sgc::Mat4f> matrices)
{
	if (!m_ready || matrices.empty()) { return kNoPalette; }
	if ((m_paletteCpu.size() + matrices.size()) * sizeof(sgc::Mat4f) > m_maxBufferBytes) { return kNoPalette; }
	const auto first = static_cast<std::uint32_t>(m_paletteCpu.size());
	m_paletteCpu.insert(m_paletteCpu.end(), matrices.begin(), matrices.end());
	return first;
}

std::uint32_t DawnSkinning::appendWeights(std::span<const float> weights)
{
	if (!m_ready || weights.empty()) { return kNoPalette; }
	const std::size_t units = (weights.size() + kWeightUnit - 1) / kWeightUnit;
	if ((m_paletteCpu.size() + units) * sizeof(sgc::Mat4f) > m_maxBufferBytes) { return kNoPalette; }
	const auto first = static_cast<std::uint32_t>(m_paletteCpu.size());
	m_paletteCpu.resize(m_paletteCpu.size() + units);
	std::memset(&m_paletteCpu[first], 0, units * sizeof(sgc::Mat4f));
	std::memcpy(&m_paletteCpu[first], weights.data(), weights.size() * sizeof(float));
	return first;
}

DawnSkinning::Source* DawnSkinning::findOrUpload(const DawnSkinGeometry& g, std::uint32_t jointCount)
{
	if (const auto it = m_sources.find(g.id); it != m_sources.end())
	{
		it->second.lastUsed = m_frame;
		return it->second.jointCount == jointCount ? &it->second : nullptr;
	}
	if (g.base == nullptr || g.binding == nullptr || g.indices == nullptr || g.base->empty() || g.binding->size() != g.base->size())
	{
		return nullptr;
	}
	const std::uint64_t vertexBytes = std::uint64_t{g.base->size()} * kSourceWords * sizeof(std::uint32_t);
	if (vertexBytes > m_maxBufferBytes) { return nullptr; }
	// モーフの差分は、頂点の並びの後ろに行 (頂点数 + 1 word) と要素 (kMorphEntryWords word ずつ) として詰める
	const bool withMorph = g.morph != nullptr && !g.morph->empty() && g.morph->rowStart.size() == g.base->size() + 1;
	const std::uint64_t morphWords = withMorph ? g.morph->rowStart.size() + g.morph->entries.size() * kMorphEntryWords : 0;
	const std::uint64_t totalBytes = vertexBytes + morphWords * sizeof(std::uint32_t);
	const bool morphFits = withMorph && totalBytes <= m_maxBufferBytes;
	Source source;
	source.vertexCount = static_cast<std::uint32_t>(g.base->size());
	source.indexCount = static_cast<std::uint32_t>(g.indices->size());
	source.jointCount = jointCount;
	source.lastUsed = m_frame;
	m_sourceScratch.resize(g.base->size() * kSourceWords + (morphFits ? morphWords : 0));
	for (std::size_t i = 0; i < g.base->size(); ++i)
	{
		const Vertex3D& v = (*g.base)[i];
		const SkinVertexBinding& b = (*g.binding)[i];
		std::uint32_t* w = m_sourceScratch.data() + i * kSourceWords;
		const float head[12] = {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z,
		                        v.texCoord.x, v.texCoord.y, v.color.r, v.color.g, v.color.b, v.color.a};
		for (int k = 0; k < 12; ++k) { w[k] = bits(head[k]); }
		for (int k = 0; k < 4; ++k) { w[12 + k] = b.joints[k]; }
		for (int k = 0; k < 4; ++k) { w[16 + k] = bits(b.weights[k]); }
	}
	std::uint64_t bufferBytes = vertexBytes;
	if (morphFits)
	{
		source.rowsWord = static_cast<std::uint32_t>(g.base->size() * kSourceWords);
		source.entriesWord = source.rowsWord + static_cast<std::uint32_t>(g.morph->rowStart.size());
		std::memcpy(m_sourceScratch.data() + source.rowsWord, g.morph->rowStart.data(), g.morph->rowStart.size() * sizeof(std::uint32_t));
		const std::vector<std::uint32_t> packed = packMorphEntries(*g.morph);
		std::memcpy(m_sourceScratch.data() + source.entriesWord, packed.data(), packed.size() * sizeof(std::uint32_t));
		source.morphBytes = totalBytes;
		bufferBytes = totalBytes;
	}
	source.vertices = makeBuffer("mitiru.skin.source", bufferBytes, wgpu::BufferUsage::Storage);
	m_ctx.queue.WriteBuffer(source.vertices, 0, m_sourceScratch.data(), bufferBytes);
	if (source.indexCount > 0)
	{
		const std::uint64_t indexBytes = std::uint64_t{source.indexCount} * sizeof(std::uint32_t);
		source.indices = makeBuffer("mitiru.skin.indices", indexBytes, wgpu::BufferUsage::Index);
		m_ctx.queue.WriteBuffer(source.indices, 0, g.indices->data(), indexBytes);
	}
	return &m_sources.emplace(g.id, std::move(source)).first->second;
}

GpuMesh* DawnSkinning::request(const DawnSkinGeometry& geometry, std::uint32_t paletteFirst, std::uint32_t jointCount,
                               const DawnSkinMorph* morph)
{
	// スキンの無いモーフの体 (jointCount 0) はパレットを読まない。それ以外は骨の行列がパレットの中にあること
	const bool noJoints = jointCount == 0;
	if (!m_ready || geometry.id == 0 || (noJoints && morph == nullptr) ||
	    (!noJoints && (paletteFirst == kNoPalette || std::uint64_t{paletteFirst} + jointCount > m_paletteCpu.size())))
	{
		return nullptr;
	}
	Source* source = findOrUpload(geometry, jointCount);
	if (source == nullptr) { return nullptr; }
	if ((m_outVertices + source->vertexCount) * kOutStride > m_maxBufferBytes) { return nullptr; }
	Request request{source, noJoints ? 0u : paletteFirst, static_cast<std::uint32_t>(m_outVertices)};
	if (morph != nullptr)
	{
		// モーフを足せない依頼は nullptr を返し、呼び出し側が CPU のパスで足す
		if (source->rowsWord == 0 || !m_morphPipeline || geometry.morph == nullptr) { return nullptr; }
		// prim のモーフ 0 番から始まる重みを積む (シェーダーは target の番号で引く)
		const std::size_t from = std::min<std::size_t>(morph->slotBase, morph->weights.size());
		const std::size_t count = std::min<std::size_t>(geometry.morph->targetCount, morph->weights.size() - from);
		const std::uint32_t weightsFirst = appendWeights(morph->weights.subspan(from, count));
		if (weightsFirst == kNoPalette) { return nullptr; }
		request.morph = true;
		request.weightsFirst = weightsFirst;
		request.weightCount = static_cast<std::uint32_t>(count);
	}
	if (m_meshCursor >= m_meshes.size()) { m_meshes.emplace_back(); }
	GpuMesh& mesh = m_meshes[m_meshCursor++];
	mesh.vertexBuffer = nullptr;   // encode が出力の buffer を入れる
	mesh.indexBuffer = source->indices;
	mesh.vertexOffset = m_outVertices * kOutStride;
	mesh.vertexBytes = std::uint64_t{source->vertexCount} * kOutStride;
	mesh.indexBytes = std::uint64_t{source->indexCount} * sizeof(std::uint32_t);
	mesh.indexCount = source->indexCount;
	mesh.vertexCount = source->vertexCount;
	mesh.revision = 0;
	mesh.lastUsed = m_frame;
	m_requests.push_back(request);
	m_outVertices += source->vertexCount;
	return &mesh;
}

void DawnSkinning::makeGroup(Source& source)
{
	wgpu::BindGroupEntry be[5]{};
	be[0].binding = 0;
	be[0].buffer = m_params;
	be[0].size = kParamBytes;
	be[1].binding = 1;
	be[1].buffer = source.vertices;
	be[1].size = std::uint64_t{source.vertexCount} * kSourceWords * sizeof(std::uint32_t);
	be[2].binding = 2;
	be[2].buffer = m_palette;
	be[2].size = m_paletteBytes;
	be[3].binding = 3;
	be[3].buffer = m_instances;
	be[3].size = m_instanceBytes;
	be[4].binding = 4;
	be[4].buffer = m_out;
	be[4].size = m_outBytes;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 5;
	bg.entries = be;
	source.group = m_ctx.device.CreateBindGroup(&bg);
	source.groupGeneration = m_generation;
}

void DawnSkinning::makeMorphGroup(Source& source)
{
	wgpu::BindGroupEntry be[5]{};
	be[0].binding = 0;
	be[0].buffer = m_params;
	be[0].size = kMorphParamBytes;
	be[1].binding = 1;
	be[1].buffer = source.vertices;
	be[1].size = source.morphBytes;
	be[2].binding = 2;
	be[2].buffer = m_palette;
	be[2].size = m_paletteBytes;
	be[3].binding = 3;
	be[3].buffer = m_instances;
	be[3].size = m_instanceBytes;
	be[4].binding = 4;
	be[4].buffer = m_out;
	be[4].size = m_outBytes;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_morphLayout;
	bg.entryCount = 5;
	bg.entries = be;
	source.morphGroup = m_ctx.device.CreateBindGroup(&bg);
	source.morphGroupGeneration = m_generation;
}

void DawnSkinning::encode()
{
	if (m_requests.empty()) { return; }
	// 同じ源の個体を並べて 1 回の dispatch (y が個体) にまとめる。モーフ用は pipeline が違うので別の run。出力の場所は頼んだ時に決まっているので、並べ替えても動かない
	std::sort(m_requests.begin(), m_requests.end(), [](const Request& a, const Request& b) {
		if (a.source != b.source) { return std::less<Source*>{}(a.source, b.source); }
		return a.morph != b.morph ? a.morph < b.morph : a.outFirst < b.outFirst;
	});
	m_runs.clear();
	m_instanceTable.clear();
	m_instanceTable.reserve(m_requests.size() * 4);
	std::uint32_t morphRequests = 0;
	for (const Request& r : m_requests)
	{
		if (m_runs.empty() || m_runs.back().source != r.source || m_runs.back().morph != r.morph || m_runs.back().count >= kMaxGroupsY)
		{
			m_runs.push_back({r.source, r.morph, static_cast<std::uint32_t>(m_instanceTable.size() / 2), 0});
		}
		// 個体の表は 1 個体につき {パレットの先頭, 出力の先頭}。モーフ用はその後ろに {重みの先頭, 重みの数} が続く
		m_instanceTable.push_back(r.paletteFirst);
		m_instanceTable.push_back(r.outFirst);
		if (r.morph)
		{
			m_instanceTable.push_back(r.weightsFirst);
			m_instanceTable.push_back(r.weightCount);
			++morphRequests;
		}
		++m_runs.back().count;
	}
	m_paramCpu.assign(m_runs.size() * kParamStride, 0);
	for (std::size_t i = 0; i < m_runs.size(); ++i)
	{
		const Source& src = *m_runs[i].source;
		// 通常は先頭の 4 word (頂点数、骨の数、個体の表の先頭、0) だけを読み、モーフ用は 8 word を読む
		const std::uint32_t params[8] = {src.vertexCount, src.jointCount, m_runs[i].first, m_runs[i].morph ? src.rowsWord : 0u,
		                                 m_runs[i].morph ? src.entriesWord : 0u, 0, 0, 0};
		std::memcpy(m_paramCpu.data() + i * kParamStride, params, sizeof(params));
	}
	const std::uint64_t paletteBytes = m_paletteCpu.size() * sizeof(sgc::Mat4f);
	const std::uint64_t instanceBytes = m_instanceTable.size() * sizeof(std::uint32_t);
	ensureShared(paletteBytes, instanceBytes, m_outVertices * kOutStride, m_paramCpu.size());
	m_ctx.queue.WriteBuffer(m_palette, 0, m_paletteCpu.data(), paletteBytes);
	m_ctx.queue.WriteBuffer(m_instances, 0, m_instanceTable.data(), instanceBytes);
	m_ctx.queue.WriteBuffer(m_params, 0, m_paramCpu.data(), m_paramCpu.size());
	for (std::size_t i = 0; i < m_meshCursor; ++i) { m_meshes[i].vertexBuffer = m_out; }

	wgpu::CommandEncoder encoder = m_ctx.device.CreateCommandEncoder();
	wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
	bool morphBound = false;
	pass.SetPipeline(m_pipeline);
	for (std::size_t i = 0; i < m_runs.size(); ++i)
	{
		Source& source = *m_runs[i].source;
		const auto offset = static_cast<std::uint32_t>(i * kParamStride);
		if (m_runs[i].morph)
		{
			if (!morphBound) { pass.SetPipeline(m_morphPipeline); morphBound = true; }
			if (!source.morphGroup || source.morphGroupGeneration != m_generation) { makeMorphGroup(source); }
			pass.SetBindGroup(0, source.morphGroup, 1, &offset);
		}
		else
		{
			if (morphBound) { pass.SetPipeline(m_pipeline); morphBound = false; }
			if (!source.group || source.groupGeneration != m_generation) { makeGroup(source); }
			pass.SetBindGroup(0, source.group, 1, &offset);
		}
		pass.DispatchWorkgroups((source.vertexCount + kThreads - 1) / kThreads, m_runs[i].count, 1);
	}
	pass.End();
	wgpu::CommandBuffer commands = encoder.Finish();
	m_ctx.queue.Submit(1, &commands);
	m_stats.requests = static_cast<std::uint32_t>(m_requests.size());
	m_stats.morphRequests = morphRequests;
	m_stats.dispatches = static_cast<std::uint32_t>(m_runs.size());
	m_stats.vertices = static_cast<std::uint32_t>(m_outVertices);
}

void DawnSkinning::evictIdle(std::uint64_t frame)
{
	for (auto it = m_sources.begin(); it != m_sources.end();)
	{
		it = (frame - it->second.lastUsed > kMeshIdleFrames) ? m_sources.erase(it) : std::next(it);
	}
}

} // namespace mitiru::render::dawn_detail
