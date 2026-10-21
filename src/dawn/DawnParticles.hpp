#pragma once

// Screen::particles3D の粒を Dawn で描くための宣言。
// DX12 と同じ刻みの計画、乱数、GpuParticles.hpp の式を使い、CPU で粒を進める。
// 生きている粒だけを板ポリの頂点にする。
// 加算と半透明の粒は並べ替えずに足し込み、深度は読むだけにして、輪郭線の下地の距離から面との柔らかい交差 (softDistance) を作る。
// 不透明の粒は場面の描画の中で深度と下地を書き、縁は alpha to coverage にする。VFX テクスチャの層 (textureLayer) も出る。
// まだ無い物: 深度との衝突 (跳ね返り・消える。CPU で進めるので、深度を読み戻す必要があり決定性が崩れる)、局所光の受け、WBOIT。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/GpuParticles.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// 板ポリの 1 頂点は 80 byte。particle.slang の VsIn と同じ並び。
struct ParticleVertex
{
	float position[3];
	float uv[2];
	float color[4];
	float info[4];     // x = 形 (0 円 1 火花 2 縁の硬い円) y = 影の色を持つか z, w = 明るい円をずらす量
	float shadow[3];
	float extra[4];    // x = VFX テクスチャの層 (-1 は形) y = 面と交わる所を消していく距離
};

static_assert(sizeof(ParticleVertex) == 80);

/// 粒を描くカメラと線形空間の場面光。
struct ParticleView
{
	sgc::Vec3f eye;
	sgc::Vec3f right;
	sgc::Vec3f up;
	sgc::Vec3f lightDir;        ///< 光が進む向き
	sgc::Colorf lightColor;     ///< 強度込み
	sgc::Colorf ambient;
	float viewProj[16];         ///< 列優先
	float farClip = 1.0f;       ///< 輪郭線の下地の距離を詰めた範囲
	float hdrScale = 1.0f;      ///< 中間の色に掛ける倍率
};

class DawnParticles
{
public:
	DawnParticles(gfx::DawnContext& ctx, wgpu::TextureFormat hdrFormat) : m_ctx(ctx), m_hdrFormat(hdrFormat) {}

	[[nodiscard]] bool create();
	/// VFX テクスチャの配列 (色の層) と輪郭線の下地の距離 (MSAA) を読む束を作る。どちらかが作り替わったとき、登録簿が呼ぶ
	void bind(const wgpu::TextureView& vfxColor, const wgpu::Sampler& vfxSampler, const wgpu::TextureView& sceneDepth);

	/// フレームの最初に呼ぶ。
	void beginFrame() { m_submitted.clear(); }
	void submit(const ParticleEmitterDesc* emitters, const ParticleFlatShade* shades, int count);

	void update(const ParticleView& view);

	/// 加算か半透明の粒があるか (場面の半透明のパスで描く)
	[[nodiscard]] bool hasDraws() const noexcept { return m_additiveIndices > 0 || m_alphaIndices > 0; }
	[[nodiscard]] bool hasOpaqueDraws() const noexcept { return m_opaqueIndices > 0; }
	/// 面との柔らかい交差があるので、輪郭線の下地の距離を残してほしいか
	[[nodiscard]] bool wantsSceneDepth() const noexcept { return m_wantsSceneDepth; }

	/// 場面の半透明パス内で呼ぶ。深度は読み取り専用。
	void draw(wgpu::RenderPassEncoder& pass) const;
	/// 場面の不透明のパス内で呼ぶ。色・下地・深度を書く。
	void drawOpaque(wgpu::RenderPassEncoder& pass) const;

private:
	struct Seat
	{
		float position[3] = {};
		float age = 0.0f;
		float velocity[3] = {};
		std::uint32_t index = kParticleNoIndex;
		float lifetime = 0.0f;
		float random = 0.0f;
	};

	struct Emitter
	{
		ParticleEmitterDesc desc;
		ParticleFlatShade shade;
		bool hasShade = false;
		std::vector<Seat> seats;
		bool hasState = false;
		std::uint32_t doneStep = 0;
		std::uint32_t shapeHash = 0;
		std::uint64_t lastFrame = 0;
	};

	void advance(Emitter& e);
	void runStep(Emitter& e, std::uint32_t step) const;
	void appendQuads(const Emitter& e, const ParticleView& view);
	void upload(wgpu::Buffer& buffer, std::uint64_t& capacity, wgpu::BufferUsage usage, const void* data, std::size_t bytes) const;
	[[nodiscard]] wgpu::RenderPipeline makePipeline(bool additive) const;
	[[nodiscard]] wgpu::RenderPipeline makeOpaquePipeline(bool alphaToCoverage) const;

	gfx::DawnContext& m_ctx;
	wgpu::TextureFormat m_hdrFormat;
	wgpu::ShaderModule m_module;
	wgpu::BindGroupLayout m_layout;          ///< 加算と半透明: 定数、VFX の色、標本化、輪郭線の下地の距離
	wgpu::BindGroupLayout m_opaqueLayout;    ///< 不透明: 輪郭線の下地を書くので、距離は読まない
	wgpu::PipelineLayout m_pipelineLayout;
	wgpu::PipelineLayout m_opaquePipelineLayout;
	wgpu::RenderPipeline m_additivePipeline;
	wgpu::RenderPipeline m_alphaPipeline;
	wgpu::RenderPipeline m_opaquePipeline;
	wgpu::Buffer m_cb;
	wgpu::BindGroup m_group;
	wgpu::BindGroup m_opaqueGroup;
	wgpu::Buffer m_vertices;
	std::uint64_t m_vertexCapacity = 0;
	wgpu::Buffer m_indices;
	std::uint64_t m_indexCapacity = 0;
	std::uint32_t m_additiveIndices = 0;   ///< 索引バッファの先頭から
	std::uint32_t m_alphaIndices = 0;      ///< 加算の続き
	std::uint32_t m_opaqueIndices = 0;     ///< 半透明の続き
	bool m_wantsSceneDepth = false;

	struct Submitted
	{
		ParticleEmitterDesc desc;
		ParticleFlatShade shade;
		bool hasShade = false;
	};
	std::vector<Submitted> m_submitted;
	std::unordered_map<std::uint32_t, Emitter> m_emitters;
	std::uint64_t m_frame = 0;
	std::vector<ParticleVertex> m_additiveVerts;
	std::vector<ParticleVertex> m_alphaVerts;
	std::vector<ParticleVertex> m_opaqueVerts;
};

} // namespace mitiru::render::dawn_detail
