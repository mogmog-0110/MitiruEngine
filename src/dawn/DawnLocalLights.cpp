#include "DawnLocalLights.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <cmath>

#include <mitiru/core/Env.hpp>

#include "DawnProfile.hpp"

namespace mitiru::render::dawn_detail
{

namespace
{

constexpr std::uint32_t kTexelsPerLight = sizeof(LocalLightGpu) / 16;   // forward.slang の accumulateLocalLights が先頭の 3 画素を読む
static_assert(kTexelsPerLight == 4);
static_assert(DawnLocalLights::kTileSize % 2 == 0);
static_assert(kMaxVisibleLocalLights <= 511, "格子の欄は光の数を 9 bit に詰める");

struct TileRect
{
	int x0 = 0, y0 = 0, x1 = -1, y1 = -1;   // 含む。x1 < x0 は空
};

// 光が届く球 (ビュー空間の中心と半径) を包む軸平行の箱の 8 隅を画面へ写し、覆う格子の範囲を返す。
// 隅が 1 つでもカメラの後ろ (w が小さい) なら画面全体。外側へ 1 画素広げて丸めの取りこぼしを防ぐ
TileRect tileRectOf(const LocalLightGpu& g, const ClusterView& cv, const sgc::Mat4f& viewProj, int tilesX, int tilesY, int width, int height, int tileSize)
{
	const sgc::Vec3f centerVs{g.boundCenterVS[0], g.boundCenterVS[1], g.boundCenterVS[2]};
	const sgc::Vec3f center = cv.eye + cv.right * centerVs.x + cv.up * centerVs.y + cv.forward * centerVs.z;
	const float r = g.boundRadius;
	float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
	for (int i = 0; i < 8; ++i)
	{
		const float p[4] = {center.x + ((i & 1) ? r : -r), center.y + ((i & 2) ? r : -r), center.z + ((i & 4) ? r : -r), 1.0f};
		float c[4] = {};
		for (int row = 0; row < 4; ++row)
		{
			c[row] = viewProj.m[row][0] * p[0] + viewProj.m[row][1] * p[1] + viewProj.m[row][2] * p[2] + viewProj.m[row][3] * p[3];
		}
		if (!(c[3] > 1e-4f)) { return {0, 0, tilesX - 1, tilesY - 1}; }
		const float px = (c[0] / c[3] * 0.5f + 0.5f) * static_cast<float>(width);
		const float py = (0.5f - c[1] / c[3] * 0.5f) * static_cast<float>(height);
		minX = std::min(minX, px); maxX = std::max(maxX, px);
		minY = std::min(minY, py); maxY = std::max(maxY, py);
	}
	const auto tile = [tileSize](float v) { return static_cast<int>(std::floor(v / static_cast<float>(tileSize))); };
	TileRect t{std::max(tile(minX - 1.0f), 0), std::max(tile(minY - 1.0f), 0), std::min(tile(maxX + 1.0f), tilesX - 1),
	           std::min(tile(maxY + 1.0f), tilesY - 1)};
	return t;
}

} // namespace

bool DawnLocalLights::create()
{
	m_tilesWanted = env::value("MITIRU_DAWN_LIGHT_TILES") != "0";
	const std::string tileEnv = env::value("MITIRU_DAWN_LIGHT_TILE_SIZE");
	if (tileEnv == "8" || tileEnv == "16" || tileEnv == "32") { m_tileSize = std::stoi(tileEnv); }
	wgpu::TextureDescriptor td{};
	td.label = "mitiru.localLights";
	td.size = {kTexelsPerLight, static_cast<std::uint32_t>(kMaxVisibleLocalLights), 1};
	td.format = wgpu::TextureFormat::RGBA32Float;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_texture = m_ctx.device.CreateTexture(&td);
	if (!m_texture) { return false; }
	m_view = m_texture.CreateView();
	wgpu::TextureDescriptor tt{};
	tt.label = "mitiru.localLightTiles";
	tt.size = {kTileTexWidth, kTileTexHeight, 1};
	tt.format = wgpu::TextureFormat::R32Uint;
	tt.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	m_tileTexture = m_ctx.device.CreateTexture(&tt);
	if (!m_tileTexture) { return false; }
	m_tileView = m_tileTexture.CreateView();
	return static_cast<bool>(m_view) && static_cast<bool>(m_tileView);
}

void DawnLocalLights::submit(const LocalLight* lights, int count)
{
	if (lights == nullptr) { return; }
	for (int i = 0; i < count && static_cast<int>(m_submitted.size()) < kMaxLocalLights; ++i) { m_submitted.push_back(lights[i]); }
}

void DawnLocalLights::update(const Camera3D& camera, const sgc::Mat4f& viewProj, int sceneWidth, int sceneHeight)
{
	m_gpu.clear();
	m_tilesActive = false;
	m_tileLoop = 0;
	if (m_submitted.empty()) { return; }
	(void)selectVisibleLocalLights(m_submitted, ClusterView::fromCamera(camera), m_scratch, m_gpu);
	if (m_gpu.empty()) { return; }
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_texture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = sizeof(LocalLightGpu);
	layout.rowsPerImage = static_cast<std::uint32_t>(m_gpu.size());
	const wgpu::Extent3D extent{kTexelsPerLight, static_cast<std::uint32_t>(m_gpu.size()), 1};
	m_ctx.queue.WriteTexture(&dst, m_gpu.data(), m_gpu.size() * sizeof(LocalLightGpu), &layout, &extent);
	if (m_tilesWanted) { m_tilesActive = buildTiles(camera, viewProj, sceneWidth, sceneHeight); }
}

bool DawnLocalLights::buildTiles(const Camera3D& camera, const sgc::Mat4f& viewProj, int sceneWidth, int sceneHeight)
{
	const int tilesX = (sceneWidth + m_tileSize - 1) / m_tileSize;
	const int tilesY = (sceneHeight + m_tileSize - 1) / m_tileSize;
	if (tilesX <= 0 || tilesY <= 0 || tilesX > static_cast<int>(kTileTexWidth) || tilesY >= static_cast<int>(kTileTexHeight) / 2) { return false; }
	const ClusterView cv = ClusterView::fromCamera(camera);
	const std::size_t tileCount = static_cast<std::size_t>(tilesX) * static_cast<std::size_t>(tilesY);
	std::array<TileRect, kMaxVisibleLocalLights> rects{};
	m_tileHeader.assign(tileCount, 0u);
	m_tileCursor.assign(tileCount, 0u);
	for (std::size_t i = 0; i < m_gpu.size(); ++i)
	{
		rects[i] = tileRectOf(m_gpu[i], cv, viewProj, tilesX, tilesY, sceneWidth, sceneHeight, m_tileSize);
		for (int y = rects[i].y0; y <= rects[i].y1; ++y)
		{
			for (int x = rects[i].x0; x <= rects[i].x1; ++x) { ++m_tileHeader[static_cast<std::size_t>(y) * tilesX + x]; }
		}
	}
	// 数えた光の数から、一覧の先頭と、ループの回数 (最大の光の数) を決める
	std::size_t total = 0;
	std::uint32_t maxCount = 0;
	for (std::size_t t = 0; t < tileCount; ++t)
	{
		const std::uint32_t n = m_tileHeader[t];
		m_tileCursor[t] = static_cast<std::uint32_t>(total);
		maxCount = std::max(maxCount, n);
		total += n;
	}
	const std::size_t listRows = (total + kTileTexWidth - 1) / kTileTexWidth;
	const std::size_t headerRows = static_cast<std::size_t>(tilesY);
	if (headerRows + listRows > kTileTexHeight || total >= (std::size_t{1} << 23)) { return false; }
	m_tileList.assign(std::max<std::size_t>(listRows, 1) * kTileTexWidth, 0u);
	// 光の番号の小さい順に積む。足す順が全部を当てる形と同じになり、和の丸めも変わらない
	for (std::size_t i = 0; i < m_gpu.size(); ++i)
	{
		for (int y = rects[i].y0; y <= rects[i].y1; ++y)
		{
			for (int x = rects[i].x0; x <= rects[i].x1; ++x)
			{
				const std::size_t t = static_cast<std::size_t>(y) * tilesX + x;
				m_tileList[m_tileCursor[t]++] = static_cast<std::uint32_t>(i);
			}
		}
	}
	// 欄は (一覧の先頭 << 9 | 光の数)。cursor は積み終えて「先頭 + 数」になっているので、数を引いて先頭へ戻す
	for (std::size_t t = 0; t < tileCount; ++t)
	{
		const std::uint32_t n = m_tileHeader[t];
		m_tileHeader[t] = ((m_tileCursor[t] - n) << 9) | n;
	}
	m_tileRows = tilesY;
	m_tileLoop = static_cast<int>(maxCount);
	MITIRU_DAWN_COUNT("lights.visible", m_gpu.size());
	MITIRU_DAWN_COUNT("lights.tileLoop", maxCount);
	MITIRU_DAWN_COUNT("lights.tileEntries", total);
	MITIRU_DAWN_COUNT("lights.tiles", tileCount);

	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = m_tileTexture;
	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = static_cast<std::uint32_t>(tilesX * sizeof(std::uint32_t));
	layout.rowsPerImage = static_cast<std::uint32_t>(tilesY);
	wgpu::Extent3D extent{static_cast<std::uint32_t>(tilesX), static_cast<std::uint32_t>(tilesY), 1};
	m_ctx.queue.WriteTexture(&dst, m_tileHeader.data(), tileCount * sizeof(std::uint32_t), &layout, &extent);
	if (listRows > 0)
	{
		dst.origin = {0, static_cast<std::uint32_t>(headerRows), 0};
		layout.bytesPerRow = kTileTexWidth * sizeof(std::uint32_t);
		layout.rowsPerImage = static_cast<std::uint32_t>(listRows);
		extent = {kTileTexWidth, static_cast<std::uint32_t>(listRows), 1};
		m_ctx.queue.WriteTexture(&dst, m_tileList.data(), listRows * kTileTexWidth * sizeof(std::uint32_t), &layout, &extent);
	}
	return true;
}

} // namespace mitiru::render::dawn_detail
