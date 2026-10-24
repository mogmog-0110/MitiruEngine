#pragma once

// 局所光 (Screen::localLights3D、pointLight3D)。DX12 の DX12ClusteredLights.hpp に相当する。見える光を近い順に選び、
// 64 byte の LocalLightGpu を横 4 画素 (RGBA32Float) × 縦 1 灯ずつの画像に詰める。互換レベルは読み取り専用の
// storage buffer を使えないので画像で渡す。
// 光が届く球の画面上の範囲を 16 画素 (既定) の格子に割り付け、格子ごとの光の番号の一覧を R32Uint の画像に詰める (ADR 0095)。fragment は自分の格子の光だけを当てる。
// 格子は偶数の画素で揃えるので、微分を取る 2x2 の画素は同じ格子を読む。ループの回数は 1 フレームで 1 つの値 (格子の最大の光の数) に
// 揃え、足りない分は何も足さない光 (shape = 0) で通す。D3D11 の FXC は、画素ごとに回数が違うループの中の微分を通さないため。
// まだ無い物: スポットの影 (castShadow)。

#include <webgpu/webgpu_cpp.h>

#include <utility>
#include <vector>

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/LocalLights.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

class DawnLocalLights
{
public:
	explicit DawnLocalLights(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// 格子の一辺の既定 (画素)。偶数であること (2x2 の微分の組が格子をまたがない)。MITIRU_DAWN_LIGHT_TILE_SIZE=8|16|32 で替えられる
	static constexpr int kTileSize = 16;
	/// 格子の画像の大きさ。上の段は格子ごとの (一覧の先頭 << 9 | 光の数)、その下は光の番号の一覧
	static constexpr std::uint32_t kTileTexWidth = 1024;
	static constexpr std::uint32_t kTileTexHeight = 1024;

	[[nodiscard]] bool create();

	void beginFrame() { m_submitted.clear(); }
	void submit(const LocalLight* lights, int count);

	/// 見える光を選んで画像へ書く。fragment が読む数は count()。viewProj と場面の大きさ (画素) で、光ごとの格子を割り付ける
	void update(const Camera3D& camera, const sgc::Mat4f& viewProj, int sceneWidth, int sceneHeight);

	[[nodiscard]] int count() const noexcept { return static_cast<int>(m_gpu.size()); }
	[[nodiscard]] const wgpu::TextureView& view() const noexcept { return m_view; }
	[[nodiscard]] const wgpu::TextureView& tileView() const noexcept { return m_tileView; }
	/// 格子の一覧を使うか (使わないときは fragment が全部の光を順に当てる)
	[[nodiscard]] bool tilesActive() const noexcept { return m_tilesActive; }
	[[nodiscard]] int tileSize() const noexcept { return m_tileSize; }
	/// 格子の一覧を使うときのループの回数 (どの格子の光の数よりも大きい 1 つの値)
	[[nodiscard]] int tileLoopCount() const noexcept { return m_tileLoop; }
	/// 一覧の画像の、格子ごとの欄の行数 (その下から光の番号の一覧)
	[[nodiscard]] int tileHeaderRows() const noexcept { return m_tileRows; }

private:
	/// 光ごとの画面上の範囲から、格子の一覧を作って画像へ書く。収まらなければ false (全部を当てる形に戻る)
	[[nodiscard]] bool buildTiles(const Camera3D& camera, const sgc::Mat4f& viewProj, int sceneWidth, int sceneHeight);

	gfx::DawnContext& m_ctx;
	wgpu::Texture m_texture;
	wgpu::TextureView m_view;
	wgpu::Texture m_tileTexture;
	wgpu::TextureView m_tileView;
	int m_tileSize = kTileSize;
	bool m_tilesWanted = true;   ///< MITIRU_DAWN_LIGHT_TILES=0 で切る (一覧を使う絵と全部を当てる絵を同じビルドで比べる)
	bool m_tilesActive = false;
	int m_tileLoop = 0;
	int m_tileRows = 0;
	std::vector<std::uint32_t> m_tileHeader;
	std::vector<std::uint32_t> m_tileList;
	std::vector<std::uint32_t> m_tileCursor;
	std::vector<LocalLight> m_submitted;
	std::vector<std::pair<float, int>> m_scratch;
	std::vector<LocalLightGpu> m_gpu;
};

} // namespace mitiru::render::dawn_detail
