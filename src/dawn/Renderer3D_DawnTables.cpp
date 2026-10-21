// 主ビューの bind group の組を管理する実装 (ADR 0091)。
// 資源を作り直す recreateTargets、ensureDrawBuffer、requestLightingBake は bind group を扱わず、参照先が変わるとこの一覧から作り直す。
// 場面や影などの画像を bind group に結ぶ効果は、この一覧に組を追加する。
// FXAA、材質、2D、UI、副ビューの出力は、資源と bind group を同じ関数で作るため含めない。

#include <mitiru/render/Renderer3D_Dawn.hpp>

#include "DawnRenderer3D.hpp"

namespace mitiru::render::dawn_detail
{

void DawnRenderer3D::registerBindGroupTables()
{
	m_tables.add("scene",
	             [this](DescriptorSources& s) {
		             s.add("frame", m_frameBuffer);
		             s.add("draw", m_drawBuffer);
		             s.add("shadow.near", m_shadowPass->nearView());
		             s.add("shadow.far", m_shadowPass->farView());
		             s.add("shadow.sampler", m_shadowPass->sampler());
		             s.add("bake.probes", m_bake->probesView());
		             s.add("bake.visibility", m_bake->visibilityView());
		             s.add("bake.sampler", m_bake->sampler());
		             s.add("decals", m_decals->view());
		             s.add("vfx.color", m_vfx->colorView());
		             s.add("vfx.data", m_vfx->dataView());
		             s.add("vfx.sampler", m_vfx->sampler());
		             s.add("localLights", m_localLights->view());
	             },
	             [this](const DescriptorSources& s) { writeSceneBindGroup(s); });
	m_tables.add("post",
	             [this](DescriptorSources& s) {
		             s.add("tone", m_toneBuffer);
		             s.add("resolved", mainTargets().resolvedView);
		             s.add("sampler", m_pipelines.postSampler());
		             s.add("bloom.result", m_bloomPass->resultView());
	             },
	             [this](const DescriptorSources& s) { writePostBindGroup(s); });
	m_tables.add("fog",
	             [this](DescriptorSources& s) { s.add("lineDepth", mainTargets().lineDepthMsaaView); },
	             [this](const DescriptorSources& s) { writeFogBindGroup(s); });
	m_tables.add("sky.aerial",
	             [this](DescriptorSources& s) { s.add("lineDepth", mainTargets().lineDepthMsaaView); },
	             [this](const DescriptorSources& s) {
		             if (!s.anyMissing()) { m_skyPass->bindSceneDepth(mainTargets().lineDepthMsaaView); }
	             });
	m_tables.add("particles",
	             [this](DescriptorSources& s) {
		             s.add("vfx.color", m_vfx->colorView());
		             s.add("vfx.sampler", m_vfx->sampler());
		             s.add("lineDepth", mainTargets().lineDepthMsaaView);
	             },
	             [this](const DescriptorSources& s) {
		             if (!s.anyMissing()) { m_particles->bind(m_vfx->colorView(), m_vfx->sampler(), mainTargets().lineDepthMsaaView); }
	             });
	m_tables.add("lines",
	             [this](DescriptorSources& s) {
		             s.add("normal", mainTargets().normalMsaaView);
		             s.add("lineDepth", mainTargets().lineDepthMsaaView);
		             m_linePass->collectSources(s);
	             },
	             [this](const DescriptorSources& s) {
		             if (!s.anyMissing()) { m_linePass->bind(mainTargets().normalMsaaView, mainTargets().lineDepthMsaaView); }
	             });
	m_tables.add("bloom",
	             [this](DescriptorSources& s) {
		             s.add("resolved", mainTargets().resolvedView);
		             m_bloomPass->collectSources(s);
	             },
	             [this](const DescriptorSources& s) {
		             if (!s.anyMissing()) { m_bloomPass->bind(mainTargets().resolvedView); }
	             });
	m_tables.add("impact",
	             [this](DescriptorSources& s) {
		             s.add("normal", mainTargets().normalMsaaView);
		             m_impactPass->collectSources(s);
	             },
	             [this](const DescriptorSources& s) {
		             if (!s.anyMissing()) { m_impactPass->bind(mainTargets().normalMsaaView); }
	             });
}

void DawnRenderer3D::writePostBindGroup(const DescriptorSources& sources)
{
	if (sources.anyMissing())
	{
		m_postBindGroup = nullptr;
		return;
	}
	wgpu::BindGroupEntry be[4]{};
	be[0].binding = 0;
	be[0].buffer = m_toneBuffer;
	be[0].size = sizeof(ToneCB);
	be[1].binding = 1;
	be[1].textureView = mainTargets().resolvedView;
	be[2].binding = 2;
	be[2].sampler = m_pipelines.postSampler();
	be[3].binding = 3;
	be[3].textureView = m_bloomPass->resultView();
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_pipelines.postLayout();
	bg.entryCount = 4;
	bg.entries = be;
	m_postBindGroup = m_ctx.device.CreateBindGroup(&bg);
}

void DawnRenderer3D::writeFogBindGroup(const DescriptorSources& sources)
{
	if (sources.anyMissing())
	{
		m_fogGroup = nullptr;
		return;
	}
	(void)m_pipelines.fog();   // fogLayout は体積フォグのパイプラインを作ったときにできる
	wgpu::BindGroupEntry fe{};
	fe.binding = 0;
	fe.textureView = mainTargets().lineDepthMsaaView;
	wgpu::BindGroupDescriptor fbg{};
	fbg.layout = m_pipelines.fogLayout();
	fbg.entryCount = 1;
	fbg.entries = &fe;
	m_fogGroup = m_ctx.device.CreateBindGroup(&fbg);
}

} // namespace mitiru::render::dawn_detail

namespace mitiru::render
{

DawnBindGroupStats dawnBindGroupStats(const IRenderer3D& renderer)
{
	const auto* dawn = dynamic_cast<const dawn_detail::DawnRenderer3D*>(&renderer);
	if (dawn == nullptr) { return {}; }
	const auto& stats = dawn->bindGroupTables().stats();
	return {stats.sweeps, stats.rewrites, stats.lateRewrites};
}

DawnSkinFrameStats dawnSkinStats(const IRenderer3D& renderer)
{
	const auto* dawn = dynamic_cast<const dawn_detail::DawnRenderer3D*>(&renderer);
	if (dawn == nullptr) { return {}; }
	const auto& stats = dawn->skinStats();
	return {stats.requests, stats.dispatches, stats.vertices, dawn->cpuSkinBodies(), dawn->skinComputeReady()};
}

std::uint64_t dawnBindGroupRewrites(const IRenderer3D& renderer, const char* table)
{
	const auto* dawn = dynamic_cast<const dawn_detail::DawnRenderer3D*>(&renderer);
	return dawn != nullptr && table != nullptr ? dawn->bindGroupTables().rewritesOf(table) : 0;
}

} // namespace mitiru::render
