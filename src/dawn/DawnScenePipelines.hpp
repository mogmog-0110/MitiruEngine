#pragma once

// 場面の前方描画 (forward.slang)と最後のトーンマップ (post.slang)の bind group layout とパイプラインの宣言。
// 描く物が増えても、パイプラインの組み立てを Renderer3D_Dawn.cpp から分離する。

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstddef>
#include <memory>

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
	/// instanced はまとめ描き (不透明だけ) の物で、頂点バッファの 2 番目 (InstanceData、instance 単位) から world と色を読む
	[[nodiscard]] wgpu::RenderPipeline scene(bool blend, bool cullNone, bool instanced = false);
	/// 場面の不透明のパイプライン (背面を除く、普通とまとめ描き) を、Dawn の作業スレッドで作り始める。変換は 1 本 1 秒ほどかかるので、
	/// 残りの起動の仕事と並べる。使うとき (scene) に終わっていなければ待つ
	void prewarmScene();
	/// 両面の 2 つと半透明の 2 つも同じように作り始める。最初のフレームの山を避けるため、起動の数フレーム後に呼ぶ
	void prewarmRest();
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
	/// 作りかけ (非同期) のパイプライン。Dawn の作業スレッドが終えると、ProcessEvents の中で done と pipeline が入る
	struct PendingPipeline
	{
		bool done = false;
		wgpu::RenderPipeline pipeline;
	};
	struct SceneSlot
	{
		wgpu::RenderPipeline pipeline;
		std::shared_ptr<PendingPipeline> pending;
	};
	/// asyncOut が空でなければ非同期に作り始めて空を返す。空なら作り終えて返す
	[[nodiscard]] wgpu::RenderPipeline buildScene(bool blend, bool cullNone, bool instanced, const std::shared_ptr<PendingPipeline>& asyncOut) const;
	void startScene(bool blend, bool cullNone, bool instanced);
	[[nodiscard]] static std::size_t sceneIndex(bool blend, bool cullNone, bool instanced) noexcept;
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
	std::array<SceneSlot, 6> m_scenePipelines;
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
