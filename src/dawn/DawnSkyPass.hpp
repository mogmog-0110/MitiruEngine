#pragma once

// 物理ベースの空の宣言 (DX12 の DX12PipelineStates_Atmosphere.inl に相当)。
// 透過率、多重散乱、空の見え方の LUT を描画先へ描き、場面の背景へ空と太陽の円盤を描く。

#include <webgpu/webgpu_cpp.h>

#include <cstring>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Atmosphere.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// sky.slang の SkyCB と同じ並び
struct SkyCB
{
	float cameraKm[4];
	float sunDir[4];
	float rayleigh[4];
	float mie[4];
	float ozone[4];
	float ground[4];
	float radii[4];
	float camForward[4];
	float camRight[4];
	float camUp[4];
};

/// 空を描くカメラの向き。right と up は tan で伸ばし、画面の端までの長さで持つ
struct SkyView
{
	float altitudeKm = 0.0005f;
	float forward[3]{};
	float right[3]{};
	float up[3]{};
	float farClip = 1.0f;   ///< 輪郭線の下地が距離を詰めた範囲
};

class DawnSkyPass
{
public:
	DawnSkyPass(gfx::DawnContext& ctx, wgpu::TextureFormat hdrFormat)
		: m_ctx(ctx), m_hdrFormat(hdrFormat),
		  m_hdrScale((hdrFormat == wgpu::TextureFormat::RGB10A2Unorm || hdrFormat == wgpu::TextureFormat::RGBA8Unorm) ? kClampedHdrScale : 0.0f)
	{
	}

	[[nodiscard]] bool create();

	/// 組成が変わったときだけ透過率と多重散乱の LUT を作り直す。空の見え方の LUT は毎フレーム作る
	void encodeLuts(wgpu::CommandEncoder& encoder, const SkySettings& sky, const sgc::Vec3f& sunDir, const SkyView& view);

	/// encodeLuts の後、場面のパスの中で空と太陽を全面に描く
	void draw(wgpu::RenderPassEncoder& pass) const;

	/// 空気遠近の束を作る。輪郭線の下地の距離 (MSAA の色) が作り替わったとき、登録簿が呼ぶ
	void bindSceneDepth(const wgpu::TextureView& lineDepth);

	/// resolve した HDR の場面に散乱を足し、透過率を掛ける。encodeLuts と resolve の後に呼ぶ
	void encodeAerial(wgpu::CommandEncoder& encoder, const wgpu::TextureView& resolved) const;

private:
	[[nodiscard]] wgpu::Texture makeLut(std::uint32_t width, std::uint32_t height) const;
	[[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& transmittance, const wgpu::TextureView& multi,
	                                        const wgpu::TextureView& skyView) const;
	[[nodiscard]] wgpu::RenderPipeline makeLutPipeline(const char* entry) const;
	void runLut(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, const wgpu::RenderPipeline& pipeline,
	            const wgpu::BindGroup& group) const;

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_hdrFormat;
	float m_hdrScale;   ///< 色が 1 で頭打ちの形式 (互換レベルで 16bit float が使えなかったとき) の倍率。0 は掛けない
	wgpu::ShaderModule m_module;
	wgpu::BindGroupLayout m_layout;
	wgpu::PipelineLayout m_pipelineLayout;
	wgpu::Sampler m_sampler;
	wgpu::Buffer m_cb;
	wgpu::RenderPipeline m_transmittancePipeline;
	wgpu::RenderPipeline m_multiPipeline;
	wgpu::RenderPipeline m_viewPipeline;
	wgpu::RenderPipeline m_drawPipeline;
	wgpu::RenderPipeline m_aerialPipeline;
	wgpu::BindGroupLayout m_aerialLayout;
	wgpu::BindGroup m_groupAerial;
	wgpu::TextureView m_dummyView;
	wgpu::Texture m_transmittance;
	wgpu::Texture m_multi;
	wgpu::Texture m_skyView;
	wgpu::Texture m_dummy;
	wgpu::TextureView m_transmittanceView;
	wgpu::TextureView m_multiView;
	wgpu::TextureView m_skyViewView;
	wgpu::BindGroup m_groupTransmittance;
	wgpu::BindGroup m_groupMulti;
	wgpu::BindGroup m_groupView;
	wgpu::BindGroup m_groupDraw;
	AtmosphereParams m_lutParams{};
	bool m_lutValid = false;
};

} // namespace mitiru::render::dawn_detail
