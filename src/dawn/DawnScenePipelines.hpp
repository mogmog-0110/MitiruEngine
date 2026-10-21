#pragma once

// 場面の前方描画 (forward.slang)と最後のトーンマップ (post.slang)の bind group layout とパイプラインの宣言。
// 描く物が増えても、パイプラインの組み立てを Renderer3D_Dawn.cpp から分離する。

#include <webgpu/webgpu_cpp.h>

#include <array>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnScenePipelines
{
public:
	explicit DawnScenePipelines(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// hdrFormat は 4x MSAA で描画できる中間色の形式、outputFormat は最終色の形式。作成できなければ false を返す。
	/// materialLayout は材質の画像の束 (DawnMaterialPool)。場面の描画は 1 番目の束にこれを結ぶ。
	[[nodiscard]] bool create(wgpu::TextureFormat hdrFormat, wgpu::TextureFormat outputFormat, const wgpu::BindGroupLayout& materialLayout);

	[[nodiscard]] const wgpu::BindGroupLayout& sceneLayout() const noexcept { return m_sceneLayout; }
	[[nodiscard]] const wgpu::BindGroupLayout& postLayout() const noexcept { return m_postLayout; }
	[[nodiscard]] const wgpu::Sampler& postSampler() const noexcept { return m_postSampler; }
	[[nodiscard]] const wgpu::RenderPipeline& tonemap(bool scaled) const noexcept
	{
		return scaled ? m_postScaledPipeline : m_postPipeline;
	}

	/// 不透明か半透明か、背面を除くかで 4 通り。最初に使うときに作る。
	[[nodiscard]] wgpu::RenderPipeline scene(bool blend, bool cullNone);
	[[nodiscard]] wgpu::RenderPipeline sky();
	/// 体積フォグ。解決後の HDR に (散乱、透過率)を重ねる。bind group は 0 が場面と同じ、1 が輪郭線の下地の距離 (fogLayout)。
	[[nodiscard]] wgpu::RenderPipeline fog();
	[[nodiscard]] const wgpu::BindGroupLayout& fogLayout() const noexcept { return m_fogLayout; }
	/// 半透明の WBOIT (通常のレベルだけ)。蓄積は accum と reveal の 2 枚に、描画先ごとに違う合成で書く (互換レベルでは作れない)。
	/// 合成は resolve した 2 枚を読んで場面の色へ over で重ねる。bind group は 0 が accum、1 が reveal (oitLayout)。
	[[nodiscard]] wgpu::RenderPipeline oitAccumulate();
	[[nodiscard]] wgpu::RenderPipeline oitComposite();
	[[nodiscard]] const wgpu::BindGroupLayout& oitLayout();
	/// 隠れた的の影絵 (透かし)。nearest は的を寄せた深度だけを書く 1 つ目のパス、paint はその深度が最も手前の面に色を重ねる 2 つ目のパス。
	/// paint の bind group は 0 が場面と同じ、1 が輪郭線の下地の距離 (fogLayout)
	[[nodiscard]] wgpu::RenderPipeline silhouetteNearest();
	[[nodiscard]] wgpu::RenderPipeline silhouettePaint();

private:
	[[nodiscard]] wgpu::RenderPipeline buildScene(bool blend, bool cullNone) const;
	[[nodiscard]] wgpu::RenderPipeline buildSky() const;
	[[nodiscard]] wgpu::RenderPipeline buildFog();
	[[nodiscard]] wgpu::RenderPipeline buildOitAccumulate() const;
	[[nodiscard]] wgpu::RenderPipeline buildOitComposite();
	[[nodiscard]] wgpu::RenderPipeline buildSilhouette(bool paint);
	[[nodiscard]] bool createPost(wgpu::TextureFormat outputFormat);

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_hdrFormat = wgpu::TextureFormat::RGBA16Float;
	wgpu::BindGroupLayout m_sceneLayout;
	wgpu::PipelineLayout m_scenePipelineLayout;      ///< 空・フォグ用 (束は場面だけ)
	wgpu::PipelineLayout m_materialPipelineLayout;   ///< モデルを含む場面の描画用 (場面と材質の画像)
	wgpu::ShaderModule m_forwardModule;
	std::array<wgpu::RenderPipeline, 4> m_scenePipelines;
	wgpu::RenderPipeline m_skyPipeline;
	wgpu::RenderPipeline m_fogPipeline;
	wgpu::BindGroupLayout m_fogLayout;
	wgpu::RenderPipeline m_oitAccumulatePipeline;
	wgpu::RenderPipeline m_oitCompositePipeline;
	wgpu::RenderPipeline m_silhouetteNearestPipeline;
	wgpu::RenderPipeline m_silhouettePaintPipeline;
	wgpu::BindGroupLayout m_oitLayout;
	wgpu::BindGroupLayout m_postLayout;
	wgpu::RenderPipeline m_postPipeline;
	wgpu::RenderPipeline m_postScaledPipeline;
	wgpu::Sampler m_postSampler;
};

} // namespace mitiru::render::dawn_detail
