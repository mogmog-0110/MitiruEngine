#pragma once

/// @file TilemapRenderer_impl.hpp
/// @brief TilemapRenderer のオートタイル・CPU描画パス実装本体（TilemapRenderer.hpp から機械的分割）

#include <mitiru/render/TilemapRenderer.hpp>

namespace mitiru::render
{

/// @brief レイヤーにオートタイルを適用する
inline void TilemapRenderer::applyAutoTile(TilemapLayer& layer, int terrainTileId) const
{
	const auto it = m_autoTileRules.find(terrainTileId);
	if (it == m_autoTileRules.end())
	{
		return;
	}
	const auto& rules = it->second;

	for (int y = 0; y < layer.height; ++y)
	{
		for (int x = 0; x < layer.width; ++x)
		{
			const int current = layer.getTile(x, y);
			if (current != terrainTileId)
			{
				continue;
			}
			const auto mask = computeNeighborMask(layer, x, y, terrainTileId);
			const int resolved = rules.resolve(mask);
			layer.setTile(x, y, resolved);
		}
	}
}

/// @brief 単一レイヤーをCPU描画する（ビューポートカリング付き）
inline void TilemapRenderer::drawLayer(Screen& screen,
                                       const TilemapLayer& layer,
                                       const TilesetConfig& tileset,
                                       float cameraX, float cameraY,
                                       float viewportW, float viewportH) const
{
	if (!layer.visible || layer.opacity <= 0.0f)
	{
		return;
	}
	if (tileset.tileWidth <= 0 || tileset.tileHeight <= 0)
	{
		return;
	}

	/// パララックスオフセットを適用する
	const float effCameraX = cameraX * layer.parallaxX - layer.offsetX;
	const float effCameraY = cameraY * layer.parallaxY - layer.offsetY;

	/// ビューポート内の可視タイル範囲を計算する
	const int startX = std::max(0,
		static_cast<int>(std::floor(effCameraX / tileset.tileWidth)));
	const int startY = std::max(0,
		static_cast<int>(std::floor(effCameraY / tileset.tileHeight)));
	const int endX = std::min(layer.width,
		static_cast<int>(std::ceil((effCameraX + viewportW) / tileset.tileWidth)) + 1);
	const int endY = std::min(layer.height,
		static_cast<int>(std::ceil((effCameraY + viewportH) / tileset.tileHeight)) + 1);

	const sgc::Colorf tint{1.0f, 1.0f, 1.0f, layer.opacity};

	for (int ty = startY; ty < endY; ++ty)
	{
		for (int tx = startX; tx < endX; ++tx)
		{
			int tileId = layer.getTile(tx, ty);
			if (tileId <= 0)
			{
				continue;
			}

			/// アニメーション置換
			tileId = resolveAnimatedTile(tileId);

			/// タイルセットのfirstGidを引いてローカルIDにする
			const int localId = tileId - tileset.firstGid;
			if (localId < 0)
			{
				continue;
			}

			/// スクリーン座標を計算する
			const float screenX =
				static_cast<float>(tx * tileset.tileWidth) - effCameraX;
			const float screenY =
				static_cast<float>(ty * tileset.tileHeight) - effCameraY;

			/// 反転フラグの取得
			const TileFlip flip = layer.getFlip(tx, ty);

			/// CPU描画: SpriteBatch経由でクワッドを描画する
			drawTileCpu(screen, tileset, localId,
			            screenX, screenY, flip, tint);
		}
	}
}

/// @brief タイルマップ全体をCPU描画する
inline void TilemapRenderer::drawTilemap(Screen& screen,
                                         const Tilemap& tilemap,
                                         float cameraX, float cameraY) const
{
	/// スクリーンサイズをビューポートとして使用する
	const float vpW = static_cast<float>(getScreenWidth(screen));
	const float vpH = static_cast<float>(getScreenHeight(screen));

	for (const auto& layer : tilemap.layers)
	{
		if (!layer.visible)
		{
			continue;
		}

		/// レイヤー内のタイルIDに対応するタイルセットを決定する
		/// 簡略化: 全タイルに最初のタイルセットを使用する
		/// （複数タイルセット対応はタイルIDのGID範囲でルーティングする）
		const TilesetConfig* ts = nullptr;
		if (!tilemap.tilesets.empty())
		{
			ts = &tilemap.tilesets.front();
		}

		if (ts)
		{
			drawLayer(screen, layer, *ts, cameraX, cameraY, vpW, vpH);
		}
	}
}

/// @brief タイルマップ全体をビューポート指定でCPU描画する
inline void TilemapRenderer::drawTilemap(Screen& screen,
                                         const Tilemap& tilemap,
                                         float cameraX, float cameraY,
                                         float viewportW, float viewportH) const
{
	for (const auto& layer : tilemap.layers)
	{
		if (!layer.visible)
		{
			continue;
		}
		const TilesetConfig* ts = nullptr;
		if (!tilemap.tilesets.empty())
		{
			ts = &tilemap.tilesets.front();
		}
		if (ts)
		{
			drawLayer(screen, layer, *ts, cameraX, cameraY,
			          viewportW, viewportH);
		}
	}
}

/// @brief CPUパスで1タイルを描画する
inline void TilemapRenderer::drawTileCpu(Screen& screen,
                                         const TilesetConfig& tileset,
                                         int localTileId,
                                         float screenX, float screenY,
                                         TileFlip flip,
                                         const sgc::Colorf& tint) const
{
	/// テクスチャが有効ならスプライト描画する
	/// テクスチャが無い場合は色付き矩形でフォールバックする
	const auto srcRect = tileset.getTileRect(localTileId);
	const sgc::Rectf dstRect{
		screenX, screenY,
		static_cast<float>(tileset.tileWidth),
		static_cast<float>(tileset.tileHeight)
	};

	if (tileset.texture.valid())
	{
		/// タイルのピクセルカラーをサンプリングして矩形描画（CPU路線）
		drawTileFromTexture(screen, tileset.texture, srcRect, dstRect, flip, tint);
	}
	else
	{
		/// テクスチャなし: タイルIDに基づく色で塗りつぶす
		const float hue = static_cast<float>(localTileId % 12) / 12.0f;
		const sgc::Colorf color{hue, 0.6f, 0.8f, tint.a};
		drawRectOnScreen(screen, dstRect, color);
	}

	++m_lastDrawnTileCount;
}

/// @brief テクスチャからタイルをCPU描画する
inline void TilemapRenderer::drawTileFromTexture(Screen& screen,
                                                 const Texture& texture,
                                                 const sgc::Rectf& srcRect,
                                                 const sgc::Rectf& dstRect,
                                                 TileFlip flip,
                                                 const sgc::Colorf& tint) const
{
	/// CPU簡易描画: ソース矩形の平均色を計算して矩形描画する
	/// 本格的なテクスチャマッピングはGPUパスで行う
	const auto& pixels = texture.pixels();
	const int texW = texture.width();
	const int texH = texture.height();

	const int sx = static_cast<int>(srcRect.x());
	const int sy = static_cast<int>(srcRect.y());
	const int sw = static_cast<int>(srcRect.width());
	const int sh = static_cast<int>(srcRect.height());

	/// 平均色を計算する
	float avgR = 0.0f, avgG = 0.0f, avgB = 0.0f, avgA = 0.0f;
	int sampleCount = 0;

	for (int py = sy; py < sy + sh && py < texH; ++py)
	{
		for (int px = sx; px < sx + sw && px < texW; ++px)
		{
			const auto idx = static_cast<std::size_t>((py * texW + px) * 4);
			if (idx + 3 < pixels.size())
			{
				avgR += static_cast<float>(pixels[idx + 0]) / 255.0f;
				avgG += static_cast<float>(pixels[idx + 1]) / 255.0f;
				avgB += static_cast<float>(pixels[idx + 2]) / 255.0f;
				avgA += static_cast<float>(pixels[idx + 3]) / 255.0f;
				++sampleCount;
			}
		}
	}

	if (sampleCount > 0)
	{
		const float inv = 1.0f / static_cast<float>(sampleCount);
		const sgc::Colorf color{
			avgR * inv * tint.r,
			avgG * inv * tint.g,
			avgB * inv * tint.b,
			avgA * inv * tint.a
		};
		drawRectOnScreen(screen, dstRect, color);
	}

	static_cast<void>(flip); // GPU描画パスで使用する
}

/// @brief 隣接マスクを計算する
inline std::uint8_t TilemapRenderer::computeNeighborMask(
	const TilemapLayer& layer, int x, int y, int terrainId) noexcept
{
	std::uint8_t mask = 0;

	const auto check = [&](int dx, int dy, std::uint8_t bit)
	{
		if (layer.getTile(x + dx, y + dy) == terrainId)
		{
			mask |= bit;
		}
	};

	check( 0, -1, static_cast<std::uint8_t>(AutoTileNeighbor::Top));
	check( 1, -1, static_cast<std::uint8_t>(AutoTileNeighbor::TopRight));
	check( 1,  0, static_cast<std::uint8_t>(AutoTileNeighbor::Right));
	check( 1,  1, static_cast<std::uint8_t>(AutoTileNeighbor::BottomRight));
	check( 0,  1, static_cast<std::uint8_t>(AutoTileNeighbor::Bottom));
	check(-1,  1, static_cast<std::uint8_t>(AutoTileNeighbor::BottomLeft));
	check(-1,  0, static_cast<std::uint8_t>(AutoTileNeighbor::Left));
	check(-1, -1, static_cast<std::uint8_t>(AutoTileNeighbor::TopLeft));

	return mask;
}

/// @brief タイルIDをアニメーション置換する
inline int TilemapRenderer::resolveAnimatedTile(int tileId) const noexcept
{
	const auto it = m_animatedTiles.find(tileId);
	if (it != m_animatedTiles.end())
	{
		return it->second.currentTileId(m_elapsedTime);
	}
	return tileId;
}

} // namespace mitiru::render
