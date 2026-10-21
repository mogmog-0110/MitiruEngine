#pragma once

// 描画ごとの輪郭線 (DX12 の DX12ToonStyle.hpp の drawToonLinePass に相当)。種の 1 パスと線の 1 パスで、
// 場面の MSAA の法線 (a = 印) と線形の距離を読み、トーンマップ後の最終色の上に線を重ねる。

#include <webgpu/webgpu_cpp.h>

#include <mitiru/render/ISceneFx.hpp>
#include <mitiru/render/ToonStyleGpu.hpp>

#include "DawnBindGroupTables.hpp"
#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// 場面の線 (印 0) の設定。ゲームが ISceneFx の setOutline* で渡す。描画ごとの線 (印 1..7) は ToonSlotTable が持つ
struct DawnLineSettings
{
	bool enabled = true;
	OutlineMode mode = OutlineMode::DepthSobel;
	float widthPx = 1.0f;
	float fadeNear = 0.0f;
	float fadeFar = 0.0f;
	float fadeMin = 0.0f;
	float darken = 1.0f;

	/// 今までの線は 1 画素ずらした所と比べるので、太さはずらす幅の 2 倍の画素になる
	[[nodiscard]] ToonLineSlot sceneLine() const noexcept;

	/// 線を引くフレームか。描画ごとの線を 1 つでも使ったか、場面の線が有効
	[[nodiscard]] bool active(const ToonSlotTable& slots) const noexcept
	{
		return slots.count() > 1 || sceneLine().width > 0.0f;
	}

	/// 線のパスの定数。sceneH は場面 (MSAA の絵) の高さで、描画ごとの線の太さ (1080p の画素) をこれに換算する
	[[nodiscard]] LineCB buildCB(const ToonSlotTable& slots, int sceneW, int sceneH, int outW, int outH, float farDepth) const noexcept;
};

class DawnLinePass
{
public:
	/// finalFormat は線を重ねる最終色の形式
	DawnLinePass(gfx::DawnContext& ctx, wgpu::TextureFormat finalFormat) : m_ctx(ctx), m_finalFormat(finalFormat) {}

	[[nodiscard]] bool create();

	/// 場面の大きさが変わったとき、種の絵を作り直す。束は bind が結ぶ
	void resize(int sceneWidth, int sceneHeight);
	/// 場面の法線と距離 (MSAA) と種の絵を読む束を作る。登録簿が、どれかが作り替わったときに呼ぶ
	void bind(const wgpu::TextureView& sceneNormal, const wgpu::TextureView& sceneDepth);
	/// bind が結ぶ自分の絵
	void collectSources(DescriptorSources& out) const { out.add("lines.seed", m_seed); }

	/// finalView の上に線を重ねる。cb は呼び手が詰めた定数
	void encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& finalView, const LineCB& cb);

private:
	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_finalFormat;
	wgpu::BindGroupLayout m_layout;
	wgpu::RenderPipeline m_seedPipeline;
	wgpu::RenderPipeline m_linePipeline;
	wgpu::Buffer m_cbBuffer;
	wgpu::Texture m_seed;
	wgpu::TextureView m_seedView;
	wgpu::Texture m_dummy;          ///< 種のパスの間、種の欄に結ぶ空の絵 (描き込み先と同時に読まない)
	wgpu::TextureView m_dummyView;
	wgpu::BindGroup m_seedGroup;
	wgpu::BindGroup m_lineGroup;
};

} // namespace mitiru::render::dawn_detail
