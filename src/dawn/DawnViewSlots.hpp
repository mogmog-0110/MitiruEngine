#pragma once

// 副ビュー（view slot、ABI v49）1 枚分の描画先と、そのフレームに積む物。
// 主ビューと同じ前方描画を使う。内部は slot と同じ大きさの MSAA と resolve 後の HDR、出力は主ビューの最終色と同じ形式の 8 bit（sRGB の値）。
// DX12 の View3D（DX12Views.hpp）に相当する。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/FrustumCulling.hpp>
#include <mitiru/render/View3DPod.hpp>

#include "DawnContext.hpp"
#include "DawnFxaaPass.hpp"
#include "DawnMaterialMaps.hpp"
#include "DawnRenderTypes.hpp"
#include "DawnSceneTargets.hpp"

namespace mitiru::render::dawn_detail
{

/// 副ビューの出力を画面内の矩形へそのまま貼るパイプライン。slot ごとの束を作る layout も持つ。
class DawnViewBlit
{
public:
	DawnViewBlit(gfx::DawnContext& ctx, wgpu::TextureFormat targetFormat) : m_ctx(ctx), m_format(targetFormat) {}

	[[nodiscard]] bool create();
	[[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& source) const;

	/// target 内の画素単位の矩形へ貼る。rects は 1 つの描画パスにまとめて積む。
	struct Placement
	{
		wgpu::BindGroup group;
		float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
	};
	void encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, int targetWidth, int targetHeight,
	            const std::vector<Placement>& placements) const;

private:
	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_format;
	wgpu::BindGroupLayout m_layout;
	gfx::AsyncRenderPipeline m_pipeline;
	wgpu::Sampler m_sampler;
};

struct DawnViewSlot
{
	bool created = false;
	View3DPod pod{};
	int width = 0;
	int height = 0;
	DawnSceneTargets targets;
	wgpu::Texture output;           ///< 最終色の形式 (sRGB の値)。UI の画像と、材質の基本色に使う
	wgpu::TextureView outputView;
	wgpu::Buffer toneBuffer;
	wgpu::BindGroup postGroup;      ///< トーンマップ (post.slang) が resolve した HDR を読む束
	wgpu::BindGroup blitGroup;      ///< compositeViewSlot が output を読む束
	std::unique_ptr<DawnFxaaPass> fxaa;   ///< pod が FXAA を止めていないときだけ
	std::shared_ptr<DawnMaterialMaps> maps;   ///< drawMeshWithViewSlot が結ぶ材質の束 (基本色 = output)
	bool hasOutput = false;         ///< 1 回でも描いたか。描く前の output は中身が決まっていない

	// そのフレームに積む物。beginViewSlot の間は、主ビューの同じ項目と入れ替わる。
	std::uint64_t drawnFrame = 0;
	Camera3D camera;
	Frustum frustum;
	sgc::Mat4f view;
	sgc::Mat4f viewProj;
	bool cameraSet = false;
	std::vector<DrawRecord> draws;
	std::vector<std::uint8_t> drawBytes;
	std::vector<InstanceData> instances;
	std::vector<ShadowCaster> casters;
};

} // namespace mitiru::render::dawn_detail
