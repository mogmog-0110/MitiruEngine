#pragma once

// スキンの頂点を compute で変形する。DX12 の Dx12SkinningCompute に当たる。
// 描く側が request で出力の場所を受け取り、finalizeFrame の最初に encode が 1 回の提出で全部を変形する。
// 出力は 1 本の大きなバッファを分け合い (GpuMesh の vertexOffset)、頂点バッファとして既存のパイプラインがそのまま読む。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"
#include "DawnSkinTypes.hpp"

namespace mitiru::render::dawn_detail
{

/// モーフを足してから変形する依頼の重み。weights はモデルのスロット表の並びで、slotBase は prim のモーフ 0 番のスロット
struct DawnSkinMorph
{
	std::span<const float> weights;
	std::uint32_t slotBase = 0;
};

struct DawnSkinStats
{
	std::uint32_t requests = 0;     ///< このフレームに GPU で変形した 1 体 1 prim の数
	std::uint32_t dispatches = 0;   ///< 同じ源の個体をまとめた dispatch の数
	std::uint32_t morphRequests = 0;   ///< requests のうち、モーフを足したもの
	std::uint32_t vertices = 0;     ///< 変形した頂点の延べ数
};

class DawnSkinning
{
public:
	static constexpr std::uint32_t kNoPalette = ~std::uint32_t(0);

	explicit DawnSkinning(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// 作れなければ false。以後 ready() は false のままで、描く側が CPU の変形に回る
	[[nodiscard]] bool create();
	[[nodiscard]] bool ready() const noexcept { return m_ready; }

	/// モーフ用の pipeline を作る。モーフを持つモデルを読み終えたときに呼び、描くときには作らない。何度呼んでもよい
	[[nodiscard]] bool createMorphPipeline();
	[[nodiscard]] bool morphReady() const noexcept { return m_morphPipeline != nullptr; }

	void beginFrame(std::uint64_t frame) noexcept;

	/// 骨の行列 (姿勢 * 逆バインド) をこのフレームのパレットの末尾へ足し、先頭の行列の位置を返す。入らなければ kNoPalette
	[[nodiscard]] std::uint32_t appendPalette(std::span<const sgc::Mat4f> matrices);

	/// 1 体ぶんの変形を頼み、出力を指す GpuMesh を返す (頂点バッファは encode が埋める)。出力が大きすぎるなどで頼めなければ nullptr。
	/// morph があって源がモーフの差分を持てば、足してから変形する。スキンの無い体は jointCount 0 (パレットは読まない)
	[[nodiscard]] GpuMesh* request(const DawnSkinGeometry& geometry, std::uint32_t paletteFirst, std::uint32_t jointCount,
	                               const DawnSkinMorph* morph = nullptr);

	/// 頼まれた変形を提出する。描画の記録を作る前に呼ぶ
	void encode();

	void evictIdle(std::uint64_t frame);

	[[nodiscard]] const DawnSkinStats& stats() const noexcept { return m_stats; }

private:
	struct Source
	{
		wgpu::Buffer vertices;   ///< 源の頂点 (storage)
		wgpu::Buffer indices;
		wgpu::BindGroup group;
		std::uint64_t groupGeneration = 0;
		std::uint64_t lastUsed = 0;
		std::uint32_t vertexCount = 0;
		std::uint32_t indexCount = 0;
		std::uint32_t jointCount = 0;
		std::uint32_t rowsWord = 0;      ///< 源の buffer の中の、モーフの行の先頭の word (0 ならモーフの差分を持たない)
		std::uint32_t entriesWord = 0;
		std::uint64_t morphBytes = 0;    ///< モーフ用がバインドする buffer の大きさ (頂点 + 行 + 要素)
		wgpu::BindGroup morphGroup;
		std::uint64_t morphGroupGeneration = 0;
	};

	struct Request
	{
		Source* source = nullptr;
		std::uint32_t paletteFirst = 0;
		std::uint32_t outFirst = 0;
		bool morph = false;
		std::uint32_t weightsFirst = 0;
		std::uint32_t weightCount = 0;
	};

	struct Run
	{
		Source* source = nullptr;
		bool morph = false;
		std::uint32_t first = 0;   ///< 個体の表の、この dispatch の先頭の要素 (uint2 の単位)
		std::uint32_t count = 0;
	};

	[[nodiscard]] Source* findOrUpload(const DawnSkinGeometry& geometry, std::uint32_t jointCount);
	[[nodiscard]] bool createPipeline();
	void ensureShared(std::uint64_t paletteBytes, std::uint64_t instanceBytes, std::uint64_t outBytes, std::uint64_t paramBytes);
	[[nodiscard]] wgpu::Buffer makeBuffer(const char* label, std::uint64_t bytes, wgpu::BufferUsage usage) const;
	void makeGroup(Source& source);
	void makeMorphGroup(Source& source);
	/// 重みをパレットの buffer の末尾へ積み、先頭の位置 (16 float を 1 単位) を返す。入らなければ kNoPalette
	[[nodiscard]] std::uint32_t appendWeights(std::span<const float> weights);

	gfx::DawnContext& m_ctx;
	bool m_ready = false;
	std::uint64_t m_frame = 0;
	std::uint64_t m_maxBufferBytes = 0;   ///< 1 本の storage buffer の束縛に入る上限
	wgpu::ComputePipeline m_pipeline;
	wgpu::BindGroupLayout m_layout;
	wgpu::ComputePipeline m_morphPipeline;
	wgpu::BindGroupLayout m_morphLayout;

	wgpu::Buffer m_palette;
	wgpu::Buffer m_instances;
	wgpu::Buffer m_out;
	wgpu::Buffer m_params;
	std::uint64_t m_paletteBytes = 0;
	std::uint64_t m_instanceBytes = 0;
	std::uint64_t m_outBytes = 0;
	std::uint64_t m_paramBytes = 0;
	std::uint64_t m_generation = 1;   ///< 共有の buffer を作り直すたびに進め、源ごとの束を作り直させる

	std::unordered_map<std::uint64_t, Source> m_sources;
	std::vector<sgc::Mat4f> m_paletteCpu;
	std::vector<Request> m_requests;
	std::vector<Run> m_runs;
	std::vector<std::uint32_t> m_instanceTable;   ///< 個体ごとに {パレットの先頭, 出力の先頭}
	std::vector<std::uint8_t> m_paramCpu;
	std::vector<std::uint32_t> m_sourceScratch;   ///< 源の頂点を詰める (80 byte = 20 u32 / 頂点)
	std::deque<GpuMesh> m_meshes;                 ///< 1 体ごとに 1 つ。deque なのでアドレスは変わらない
	std::size_t m_meshCursor = 0;
	std::uint64_t m_outVertices = 0;
	DawnSkinStats m_stats;
};

} // namespace mitiru::render::dawn_detail
