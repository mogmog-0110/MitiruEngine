#include "DawnScenePipelines.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

#include <mitiru/core/Env.hpp>

#include <mitiru/render/Vertex3D.hpp>

#include "DawnRenderTypes.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::int32_t kSilhouetteDepthBias = 4;

wgpu::VertexBufferLayout sceneVertexLayout()
{
	static const wgpu::VertexAttribute attrs[4] = {
		{nullptr, wgpu::VertexFormat::Float32x3, 0, 0},
		{nullptr, wgpu::VertexFormat::Float32x3, 12, 1},
		{nullptr, wgpu::VertexFormat::Float32x2, 24, 2},
		{nullptr, wgpu::VertexFormat::Float32x4, 32, 3},
	};
	wgpu::VertexBufferLayout vb{};
	vb.arrayStride = sizeof(Vertex3D);
	vb.attributeCount = 4;
	vb.attributes = attrs;
	return vb;
}

// まとめ描き用。頂点 (slot 0) は同じで、slot 1 の InstanceData を instance 単位で読む。
// @location は forward.slang の vsMainInst で Slang が付けた番号 (world の 4 列が 1〜4、色が 5、NORMAL・UV・頂点色が 6〜8)
struct SceneInstancedLayout
{
	wgpu::VertexAttribute vertexAttrs[4] = {
		{nullptr, wgpu::VertexFormat::Float32x3, 0, 0},
		{nullptr, wgpu::VertexFormat::Float32x3, 12, 6},
		{nullptr, wgpu::VertexFormat::Float32x2, 24, 7},
		{nullptr, wgpu::VertexFormat::Float32x4, 32, 8},
	};
	wgpu::VertexAttribute instanceAttrs[5] = {
		{nullptr, wgpu::VertexFormat::Float32x4, 0, 1},
		{nullptr, wgpu::VertexFormat::Float32x4, 16, 2},
		{nullptr, wgpu::VertexFormat::Float32x4, 32, 3},
		{nullptr, wgpu::VertexFormat::Float32x4, 48, 4},
		{nullptr, wgpu::VertexFormat::Float32x4, 64, 5},
	};
	wgpu::VertexBufferLayout buffers[2]{};

	SceneInstancedLayout()
	{
		buffers[0].arrayStride = sizeof(Vertex3D);
		buffers[0].attributeCount = 4;
		buffers[0].attributes = vertexAttrs;
		buffers[1].arrayStride = sizeof(InstanceData);
		buffers[1].stepMode = wgpu::VertexStepMode::Instance;
		buffers[1].attributeCount = 5;
		buffers[1].attributes = instanceAttrs;
	}
};

} // namespace

bool DawnScenePipelines::create(wgpu::TextureFormat hdrFormat, wgpu::TextureFormat outputFormat,
                                const wgpu::BindGroupLayout& materialLayout)
{
	const auto& dev = m_ctx.device;
	m_hdrFormat = hdrFormat;
	m_forwardModule = m_ctx.makeShader(dawnShaderSource_forward(), "mitiru.forward");

	wgpu::BindGroupLayoutEntry entries[14]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[0].buffer.minBindingSize = sizeof(FrameCB);
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
	entries[1].buffer.hasDynamicOffset = true;
	entries[1].buffer.minBindingSize = sizeof(DrawCB);
	entries[2].binding = 2;
	entries[2].visibility = wgpu::ShaderStage::Fragment;
	entries[2].texture.sampleType = wgpu::TextureSampleType::Depth;
	entries[3].binding = 3;
	entries[3].visibility = wgpu::ShaderStage::Fragment;
	entries[3].texture.sampleType = wgpu::TextureSampleType::Depth;
	entries[4].binding = 4;
	entries[4].visibility = wgpu::ShaderStage::Fragment;
	entries[4].sampler.type = wgpu::SamplerBindingType::Comparison;
	// 焼いた間接光。プローブ (RGBA32Float、textureLoad で読む)、距離の地図 (RG16Float、双線形)、その標本化
	entries[5].binding = 5;
	entries[5].visibility = wgpu::ShaderStage::Fragment;
	entries[5].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	entries[6].binding = 6;
	entries[6].visibility = wgpu::ShaderStage::Fragment;
	entries[6].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[7].binding = 7;
	entries[7].visibility = wgpu::ShaderStage::Fragment;
	entries[7].sampler.type = wgpu::SamplerBindingType::Filtering;
	// デカールの画像 (RGBA32Float、textureLoad で読む)、VFX テクスチャの色とデータの配列、その標本化
	entries[8].binding = 8;
	entries[8].visibility = wgpu::ShaderStage::Fragment;
	entries[8].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	entries[9].binding = 9;
	entries[9].visibility = wgpu::ShaderStage::Fragment;
	entries[9].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[9].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
	entries[10].binding = 10;
	entries[10].visibility = wgpu::ShaderStage::Fragment;
	entries[10].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[10].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
	entries[11].binding = 11;
	entries[11].visibility = wgpu::ShaderStage::Fragment;
	entries[11].sampler.type = wgpu::SamplerBindingType::Filtering;
	// 局所光の画像 (RGBA32Float、textureLoad で読む)
	entries[12].binding = 12;
	entries[12].visibility = wgpu::ShaderStage::Fragment;
	entries[12].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	// 局所光の格子 (R32Uint、textureLoad で読む)
	entries[13].binding = 13;
	entries[13].visibility = wgpu::ShaderStage::Fragment;
	entries[13].texture.sampleType = wgpu::TextureSampleType::Uint;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 14;
	bgl.entries = entries;
	m_sceneLayout = dev.CreateBindGroupLayout(&bgl);
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_sceneLayout;
	m_scenePipelineLayout = dev.CreatePipelineLayout(&pl);
	wgpu::BindGroupLayout both[2] = {m_sceneLayout, materialLayout};
	pl.bindGroupLayoutCount = 2;
	pl.bindGroupLayouts = both;
	m_materialPipelineLayout = dev.CreatePipelineLayout(&pl);
	return m_forwardModule && createPost(outputFormat);
}

bool DawnScenePipelines::createPost(wgpu::TextureFormat outputFormat)
{
	const auto& dev = m_ctx.device;
	wgpu::ShaderModule post = m_ctx.makeShader(dawnShaderSource_post(), "mitiru.post");
	wgpu::BindGroupLayoutEntry pe[4]{};
	pe[0].binding = 0;
	pe[0].visibility = wgpu::ShaderStage::Fragment;
	pe[0].buffer.type = wgpu::BufferBindingType::Uniform;
	pe[1].binding = 1;
	pe[1].visibility = wgpu::ShaderStage::Fragment;
	pe[1].texture.sampleType = wgpu::TextureSampleType::Float;
	pe[2].binding = 2;
	pe[2].visibility = wgpu::ShaderStage::Fragment;
	pe[2].sampler.type = wgpu::SamplerBindingType::Filtering;
	pe[3].binding = 3;
	pe[3].visibility = wgpu::ShaderStage::Fragment;
	pe[3].texture.sampleType = wgpu::TextureSampleType::Float;
	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	m_postSampler = dev.CreateSampler(&sd);
	wgpu::BindGroupLayoutDescriptor pbgl{};
	pbgl.entryCount = 4;
	pbgl.entries = pe;
	m_postLayout = dev.CreateBindGroupLayout(&pbgl);
	wgpu::PipelineLayoutDescriptor ppl{};
	ppl.bindGroupLayoutCount = 1;
	ppl.bindGroupLayouts = &m_postLayout;
	wgpu::PipelineLayout postLayout = dev.CreatePipelineLayout(&ppl);

	wgpu::ColorTargetState target{};
	target.format = outputFormat;
	wgpu::FragmentState fs{};
	fs.module = post;
	fs.entryPoint = "fsTonemap";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = postLayout;
	rp.vertex.module = post;
	rp.vertex.entryPoint = "vsPost";
	rp.fragment = &fs;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	m_postPipeline = dev.CreateRenderPipeline(&rp);
	fs.entryPoint = "fsTonemapScaled";
	m_postScaledPipeline = dev.CreateRenderPipeline(&rp);
	return m_postPipeline && m_postScaledPipeline;
}

std::size_t DawnScenePipelines::sceneIndex(bool blend, bool cullNone, bool instanced) noexcept
{
	// 半透明はまとめない (遠い順に並べ替えるので) ため、まとめ描きの組は不透明の 2 通りだけ
	return instanced ? 4u + (cullNone ? 1u : 0u) : (blend ? 2u : 0u) + (cullNone ? 1u : 0u);
}

wgpu::RenderPipeline DawnScenePipelines::scene(bool blend, bool cullNone, bool instanced)
{
	SceneSlot& slot = m_scenePipelines[sceneIndex(blend, cullNone, instanced)];
	if (slot.pipeline) { return slot.pipeline; }
	if (slot.pending)
	{
		static const bool log = env::flag("MITIRU_DAWN_PSO_LOG");
		const auto began = std::chrono::steady_clock::now();
		(void)m_ctx.pump(slot.pending->done, 60000);
		if (log)
		{
			std::fprintf(stderr, "pso\tscene[%d%d%d]\twaited_ms=%.1f\n", blend ? 1 : 0, cullNone ? 1 : 0, instanced ? 1 : 0,
			             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
		}
		slot.pipeline = slot.pending->pipeline;
		slot.pending.reset();
	}
	// 始めていなかった、または非同期が失敗したときは、ここで作る (誤りは Dawn の検証が出す)
	if (!slot.pipeline) { slot.pipeline = buildScene(blend, cullNone, instanced, nullptr); }
	return slot.pipeline;
}

void DawnScenePipelines::startScene(bool blend, bool cullNone, bool instanced)
{
	SceneSlot& slot = m_scenePipelines[sceneIndex(blend, cullNone, instanced)];
	if (slot.pipeline || slot.pending) { return; }
	static const bool syncPso = env::flag("MITIRU_DAWN_SYNC_PSO");
	if (syncPso)
	{
		slot.pipeline = buildScene(blend, cullNone, instanced, nullptr);
		return;
	}
	slot.pending = std::make_shared<PendingPipeline>();
	(void)buildScene(blend, cullNone, instanced, slot.pending);
}

void DawnScenePipelines::prewarmScene()
{
	// 1 本が 1 秒前後かかるので、最初のフレームで使う確率が高い 2 本 (背面を除く、普通とまとめ描き) だけを先に始める
	startScene(false, false, false);
	startScene(false, false, true);
}

void DawnScenePipelines::prewarmRest()
{
	startScene(false, true, false);
	startScene(false, true, true);
	startScene(true, false, false);
	startScene(true, true, false);
}

wgpu::RenderPipeline DawnScenePipelines::sky()
{
	if (!m_skyPipeline) { m_skyPipeline = buildSky(); }
	return m_skyPipeline;
}

wgpu::RenderPipeline DawnScenePipelines::fog()
{
	if (!m_fogPipeline) { m_fogPipeline = buildFog(); }
	return m_fogPipeline;
}

wgpu::RenderPipeline DawnScenePipelines::buildFog()
{
	const auto& dev = m_ctx.device;
	wgpu::BindGroupLayoutEntry le{};
	le.binding = 0;
	le.visibility = wgpu::ShaderStage::Fragment;
	le.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
	le.texture.multisampled = true;
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 1;
	bgl.entries = &le;
	m_fogLayout = dev.CreateBindGroupLayout(&bgl);
	wgpu::BindGroupLayout layouts[2] = {m_sceneLayout, m_fogLayout};
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 2;
	pl.bindGroupLayouts = layouts;
	// 色 × 透過率 + 散乱
	wgpu::BlendState compose{};
	compose.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::SrcAlpha};
	compose.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One};
	wgpu::ColorTargetState target{};
	target.format = m_hdrFormat;
	target.blend = &compose;
	wgpu::FragmentState fs{};
	fs.module = m_forwardModule;
	fs.entryPoint = "fsFog";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = dev.CreatePipelineLayout(&pl);
	rp.vertex.module = m_forwardModule;
	rp.vertex.entryPoint = "vsSky";
	rp.fragment = &fs;
	return dev.CreateRenderPipeline(&rp);
}

wgpu::RenderPipeline DawnScenePipelines::buildScene(bool blend, bool cullNone, bool instanced,
                                                    const std::shared_ptr<PendingPipeline>& asyncOut) const
{
	const wgpu::VertexBufferLayout vb = sceneVertexLayout();
	const SceneInstancedLayout instancedLayout;

	wgpu::BlendState bs{};
	bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
	bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
	// 不透明は色と輪郭線の下地 (法線と印、距離)を同じ描画で書く。半透明は色だけを別のパスで書く
	// (互換レベルでは出力ごとに書き込みや合成を変えられない)
	wgpu::ColorTargetState targets[3]{};
	targets[0].format = m_hdrFormat;
	if (blend) { targets[0].blend = &bs; }
	targets[1].format = kLineGBufferFormat;
	targets[2].format = kLineGBufferFormat;
	wgpu::FragmentState fs{};
	fs.module = m_forwardModule;
	fs.entryPoint = blend ? "fsMainColor" : "fsMain";
	fs.targetCount = blend ? 1 : 3;
	fs.targets = targets;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = blend ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
	ds.depthCompare = wgpu::CompareFunction::Less;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_materialPipelineLayout;
	rp.vertex.module = m_forwardModule;
	rp.vertex.entryPoint = instanced ? "vsMainInst" : "vsMain";
	rp.vertex.bufferCount = instanced ? 2 : 1;
	rp.vertex.buffers = instanced ? instancedLayout.buffers : &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.frontFace = wgpu::FrontFace::CCW;
	rp.primitive.cullMode = cullNone ? wgpu::CullMode::None : wgpu::CullMode::Back;
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.fragment = &fs;
	if (asyncOut != nullptr)
	{
		m_ctx.device.CreateRenderPipelineAsync(&rp, wgpu::CallbackMode::AllowProcessEvents,
			[state = asyncOut](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView) {
				if (status == wgpu::CreatePipelineAsyncStatus::Success) { state->pipeline = std::move(pipeline); }
				state->done = true;
			});
		return {};
	}
	return m_ctx.device.CreateRenderPipeline(&rp);
}

const wgpu::BindGroupLayout& DawnScenePipelines::oitLayout()
{
	if (!m_oitLayout)
	{
		wgpu::BindGroupLayoutEntry le[2]{};
		for (std::uint32_t k = 0; k < 2; ++k)
		{
			le[k].binding = k;
			le[k].visibility = wgpu::ShaderStage::Fragment;
			le[k].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
		}
		wgpu::BindGroupLayoutDescriptor bgl{};
		bgl.entryCount = 2;
		bgl.entries = le;
		m_oitLayout = m_ctx.device.CreateBindGroupLayout(&bgl);
	}
	return m_oitLayout;
}

wgpu::RenderPipeline DawnScenePipelines::oitAccumulate()
{
	if (!m_oitAccumulatePipeline) { m_oitAccumulatePipeline = buildOitAccumulate(); }
	return m_oitAccumulatePipeline;
}

wgpu::RenderPipeline DawnScenePipelines::oitComposite()
{
	if (!m_oitCompositePipeline) { m_oitCompositePipeline = buildOitComposite(); }
	return m_oitCompositePipeline;
}

// DX12 の WeightedBlendedOIT::fillAccumulateBlend と同じ合成。accum は加算、reveal は dst * (1 - src)。
// 深度は読むだけで、透明な面を両側から見せるため背面を除かない。
wgpu::RenderPipeline DawnScenePipelines::buildOitAccumulate() const
{
	const wgpu::VertexBufferLayout vb = sceneVertexLayout();
	wgpu::BlendState accumBlend{};
	accumBlend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
	accumBlend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
	wgpu::BlendState revealBlend{};
	revealBlend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::OneMinusSrc};
	revealBlend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::OneMinusSrcAlpha};
	wgpu::ColorTargetState targets[2]{};
	targets[0].format = kOitAccumFormat;
	targets[0].blend = &accumBlend;
	targets[1].format = kOitRevealFormat;
	targets[1].blend = &revealBlend;
	wgpu::FragmentState fs{};
	fs.module = m_forwardModule;
	fs.entryPoint = "fsMainOit";
	fs.targetCount = 2;
	fs.targets = targets;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::False;
	ds.depthCompare = wgpu::CompareFunction::Less;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = m_materialPipelineLayout;
	rp.vertex.module = m_forwardModule;
	rp.vertex.entryPoint = "vsMain";
	rp.vertex.bufferCount = 1;
	rp.vertex.buffers = &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.frontFace = wgpu::FrontFace::CCW;
	rp.primitive.cullMode = wgpu::CullMode::None;
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.fragment = &fs;
	return m_ctx.device.CreateRenderPipeline(&rp);
}

// 場面の色 (MSAA) の全サンプルへ同じ値を重ねる。合成後も同じ描画パスで粒を描くため、深度の形式は合わせるが、深度テストは常に通す。
wgpu::RenderPipeline DawnScenePipelines::buildOitComposite()
{
	const auto& dev = m_ctx.device;
	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_oit(), "mitiru.oit");
	wgpu::BindGroupLayout layout = oitLayout();
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &layout;
	wgpu::BlendState over{};
	over.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
	over.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
	wgpu::ColorTargetState target{};
	target.format = m_hdrFormat;
	target.blend = &over;
	wgpu::FragmentState fs{};
	fs.module = module;
	fs.entryPoint = "fsOitComposite";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::False;
	ds.depthCompare = wgpu::CompareFunction::Always;
	wgpu::RenderPipelineDescriptor rp{};
	rp.layout = dev.CreatePipelineLayout(&pl);
	rp.vertex.module = module;
	rp.vertex.entryPoint = "vsOitComposite";
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	rp.fragment = &fs;
	return dev.CreateRenderPipeline(&rp);
}

wgpu::RenderPipeline DawnScenePipelines::silhouetteNearest()
{
	if (!m_silhouetteNearestPipeline) { m_silhouetteNearestPipeline = buildSilhouette(false); }
	return m_silhouetteNearestPipeline;
}

wgpu::RenderPipeline DawnScenePipelines::silhouettePaint()
{
	if (!m_silhouettePaintPipeline) { m_silhouettePaintPipeline = buildSilhouette(true); }
	return m_silhouettePaintPipeline;
}

// DX12 の影絵は R32 の MIN 混ぜで「的を寄せた深度のいちばん手前」を置くが、R32Float の混ぜは Dawn の既定の機能に無い。
// 代わりに専用の深度へ Less で書き (nearest)、色のパスはその深度を LessEqual で読む (paint)。同じ頂点シェーダーが作る同じ深度なので、
// 最も手前の面だけが通る。色のパスは数 ulp 手前へ寄せて、同じ式の丸めで穴が空くのを防ぐ。
// 背面は DX12 の PSO と同じく除く
wgpu::RenderPipeline DawnScenePipelines::buildSilhouette(bool paint)
{
	const wgpu::VertexBufferLayout vb = sceneVertexLayout();
	wgpu::PipelineLayout layout = m_scenePipelineLayout;
	wgpu::BlendState over{};
	over.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha};
	over.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::One};
	wgpu::ColorTargetState target{};
	target.format = m_hdrFormat;
	target.blend = &over;
	wgpu::FragmentState fs{};
	fs.module = m_forwardModule;
	fs.entryPoint = m_ctx.compat ? "fsSilhouetteCompat" : "fsSilhouette";
	fs.targetCount = 1;
	fs.targets = &target;
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = paint ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
	ds.depthCompare = paint ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Less;
	if (paint) { ds.depthBias = -kSilhouetteDepthBias; }
	wgpu::RenderPipelineDescriptor rp{};
	if (paint)
	{
		(void)fog();   // fogLayout を作る
		wgpu::BindGroupLayout layouts[2] = {m_sceneLayout, m_fogLayout};
		wgpu::PipelineLayoutDescriptor pl{};
		pl.bindGroupLayoutCount = 2;
		pl.bindGroupLayouts = layouts;
		layout = m_ctx.device.CreatePipelineLayout(&pl);
		rp.fragment = &fs;
	}
	rp.layout = layout;
	rp.vertex.module = m_forwardModule;
	rp.vertex.entryPoint = "vsSilhouette";
	rp.vertex.bufferCount = 1;
	rp.vertex.buffers = &vb;
	rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	rp.primitive.frontFace = wgpu::FrontFace::CCW;
	rp.primitive.cullMode = wgpu::CullMode::Back;
	rp.depthStencil = &ds;
	rp.multisample.count = kSamples;
	return m_ctx.device.CreateRenderPipeline(&rp);
}

// 空は色と下地の 3 枚を書く (法線なし、距離は遠方)
wgpu::RenderPipeline DawnScenePipelines::buildSky() const
{
	wgpu::DepthStencilState ds{};
	ds.format = kDepthFormat;
	ds.depthWriteEnabled = wgpu::OptionalBool::False;
	ds.depthCompare = wgpu::CompareFunction::Always;
	wgpu::ColorTargetState targets[3]{};
	targets[0].format = m_hdrFormat;
	targets[1].format = kLineGBufferFormat;
	targets[2].format = kLineGBufferFormat;
	wgpu::FragmentState fs{};
	fs.module = m_forwardModule;
	fs.entryPoint = "fsSky";
	fs.targetCount = 3;
	fs.targets = targets;
	wgpu::RenderPipelineDescriptor sp{};
	sp.layout = m_scenePipelineLayout;
	sp.vertex.module = m_forwardModule;
	sp.vertex.entryPoint = "vsSky";
	sp.depthStencil = &ds;
	sp.multisample.count = kSamples;
	sp.fragment = &fs;
	return m_ctx.device.CreateRenderPipeline(&sp);
}

} // namespace mitiru::render::dawn_detail
