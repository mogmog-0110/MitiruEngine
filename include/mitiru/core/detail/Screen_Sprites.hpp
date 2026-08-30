#pragma once
// mitiru::Screen 用の detail header。直接インクルードしない。core/Screen.hpp 経由で取り込む

// テクスチャ全体を描く（白ティント）。
inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect)
{
	drawSprite(texture, dstRect, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
}

// 文字を CPU 側で描く場面か (テクスチャ描画が使えず、画面バッファへ直接書けるとき)。
inline bool mitiru::Screen::softwareTextPath() const noexcept
{
	const bool gpuTextured = (m_pipeline != nullptr) && m_pipeline->supportsTexturedBatch();
	return !gpuTextured && hasSoftwareFramebuffer();
}

// グリフアトラスの 1 コマをソフトウェアフレームバッファへ塗る (文字用)。
inline void mitiru::Screen::blitAtlasGlyph(const render::Texture& atlas, const sgc::Rectf& dstRect,
                                           const sgc::Rectf& srcRect, const sgc::Colorf& color)
{
	if (!atlas.valid() || !hasSoftwareFramebuffer() || m_width <= 0 || m_height <= 0) { return; }
	if (dstRect.width() <= 0.0f || dstRect.height() <= 0.0f) { return; }
	validateDrawCall(dstRect, "text");
	// 観測しないフレームは塗らない (#53)。
	if (!m_swFbActive) { ++m_drawCallCount; return; }

	const int texW = atlas.width();
	const int texH = atlas.height();
	const int sx0 = std::clamp(static_cast<int>(srcRect.x()), 0, texW);
	const int sy0 = std::clamp(static_cast<int>(srcRect.y()), 0, texH);
	const int sw  = std::min(static_cast<int>(srcRect.width()),  texW - sx0);
	const int sh  = std::min(static_cast<int>(srcRect.height()), texH - sy0);
	if (sw <= 0 || sh <= 0) { return; }

	// 描画順を保つため、先に積んだ図形をフレームバッファへ焼く。
	flushCurrentBatch();
	if (!m_shapeRenderer.vertices().empty())
	{
		rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
		m_shapeRenderer.flush();
	}

	// カメラ/スケール変換を適用する (回転は矩形近似)。
	const sgc::Rectf d = applyTransform(dstRect);
	if (d.width() <= 0.0f || d.height() <= 0.0f) { return; }
	const int dx0 = std::max(0, static_cast<int>(d.x()));
	const int dy0 = std::max(0, static_cast<int>(d.y()));
	const int dx1 = std::min(m_width,  static_cast<int>(d.x() + d.width()  + 0.999f));
	const int dy1 = std::min(m_height, static_cast<int>(d.y() + d.height() + 0.999f));
	const float su = static_cast<float>(sw) / d.width();
	const float sv = static_cast<float>(sh) / d.height();
	const auto& px = atlas.pixels();
	auto cl = [](float v) -> std::uint8_t {
		return static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, v * 255.0f)));
	};
	for (int dy = dy0; dy < dy1; ++dy)
	{
		int syi = static_cast<int>((static_cast<float>(dy) - d.y()) * sv);
		syi = std::min(sh - 1, std::max(0, syi));
		const int ty = sy0 + syi;
		for (int dx = dx0; dx < dx1; ++dx)
		{
			int sxi = static_cast<int>((static_cast<float>(dx) - d.x()) * su);
			sxi = std::min(sw - 1, std::max(0, sxi));
			const int tx = sx0 + sxi;
			const std::size_t i = static_cast<std::size_t>((ty * texW + tx) * 4);
			if (i + 3 >= px.size()) { continue; }
			const std::uint8_t cov = px[i + 3];
			if (cov == 0) { continue; }
			const float a = (cov / 255.0f) * color.a;
			if (a <= 0.0f) { continue; }
			const std::size_t o =
				(static_cast<std::size_t>(dy) * m_width + static_cast<std::size_t>(dx)) * 4;
			m_pixels[o]     = cl(color.r * a + (m_pixels[o]     / 255.0f) * (1.0f - a));
			m_pixels[o + 1] = cl(color.g * a + (m_pixels[o + 1] / 255.0f) * (1.0f - a));
			m_pixels[o + 2] = cl(color.b * a + (m_pixels[o + 2] / 255.0f) * (1.0f - a));
			m_pixels[o + 3] = 255;
		}
	}
	++m_drawCallCount;
}

// テクスチャ全体をティント付きで描く（= 全面 srcRect への委譲）。
inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
                                       const sgc::Colorf& tintColor)
{
	if (!texture.valid()) return;
	drawSprite(texture, dstRect,
	           sgc::Rectf{0.0f, 0.0f,
	                      static_cast<float>(texture.width()),
	                      static_cast<float>(texture.height())},
	           tintColor, false);
}

// スプライトシートの 1 コマ (srcRect) を、色かけ・左右反転付きで描く。
inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
                                       const sgc::Rectf& srcRect, const sgc::Colorf& tintColor,
                                       bool flipX)
{
	if (!texture.valid()) return;
	validateDrawCall(dstRect, "drawSprite");
	const int texW = texture.width();
	const int texH = texture.height();
	// srcRect (テクスチャ内ピクセル領域) を範囲内に clamp。
	const int sx0 = std::clamp(static_cast<int>(srcRect.x()), 0, texW);
	const int sy0 = std::clamp(static_cast<int>(srcRect.y()), 0, texH);
	const int sw  = std::min(static_cast<int>(srcRect.width()),  texW - sx0);
	const int sh  = std::min(static_cast<int>(srcRect.height()), texH - sy0);
	if (sw <= 0 || sh <= 0) return;

	// テクスチャをそのまま 1 枚のスプライトとして描けるとき。
	if (m_pipeline != nullptr && m_pipeline->supportsTexturedBatch())
	{
		const std::uint32_t handle = m_pipeline->ensureSpriteTexture(
			static_cast<const void*>(&texture), texW, texH, texture.pixels().data());
		if (handle != 0)
		{
			const auto t = currentTransform();
			const float x0 = dstRect.x();
			const float y0 = dstRect.y();
			const float x1 = dstRect.x() + dstRect.width();
			const float y1 = dstRect.y() + dstRect.height();
			// 4 隅に currentTransform を適用（translate/scale/rotate / カメラ対応）
			const sgc::Vec2f corners[4] = {
				t.apply(x0, y0), t.apply(x1, y0), t.apply(x1, y1), t.apply(x0, y1)};
			// UV（[0,1]）。flipX は u を左右入替。
			const float u0 = static_cast<float>(sx0)      / static_cast<float>(texW);
			const float v0 = static_cast<float>(sy0)      / static_cast<float>(texH);
			const float u1 = static_cast<float>(sx0 + sw) / static_cast<float>(texW);
			const float v1 = static_cast<float>(sy0 + sh) / static_cast<float>(texH);
			const float ua = flipX ? u1 : u0;
			const float ub = flipX ? u0 : u1;
			const sgc::Vec2f uvs[4] = {
				{ua, v0}, {ub, v0}, {ub, v1}, {ua, v1}};
			drawSpriteTexturedQuad(handle, corners, uvs, tintColor);
			return;
		}
		// 用意できなかったときは下の方法で描く。
	}

	const auto& px = texture.pixels();

	// 画面バッファへ直接書き込む描き方 (ソフトウェア描画のとき)。出力先の画素ごとに
	// 元画像から色を拾って重ねる。
	if (hasSoftwareFramebuffer() && m_width > 0 && m_height > 0 &&
	    dstRect.width() > 0.0f && dstRect.height() > 0.0f)
	{
		// 記録しないフレームは、描かずに終える。
		if (!m_swFbActive) { ++m_drawCallCount; return; }
		// 重なり順を保つため、この絵より前に積んだ図形を先に画面へ焼いてから書き込む。
		flushCurrentBatch();
		if (!m_shapeRenderer.vertices().empty())
		{
			rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}
		// 拡大・移動・カメラの変換を適用する (回転は外接矩形で近似)。
		const sgc::Rectf d = applyTransform(dstRect);
		if (d.width() <= 0.0f || d.height() <= 0.0f) { return; }
		const int dx0 = std::max(0, static_cast<int>(d.x()));
		const int dy0 = std::max(0, static_cast<int>(d.y()));
		const int dx1 = std::min(m_width,  static_cast<int>(d.x() + d.width()  + 0.999f));
		const int dy1 = std::min(m_height, static_cast<int>(d.y() + d.height() + 0.999f));
		const float su = static_cast<float>(sw) / d.width();
		const float sv = static_cast<float>(sh) / d.height();
		auto cl = [](float v) -> std::uint8_t {
			return static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, v * 255.0f)));
		};
		for (int dy = dy0; dy < dy1; ++dy)
		{
			int syi = static_cast<int>((static_cast<float>(dy) - d.y()) * sv);
			syi = std::min(sh - 1, std::max(0, syi));
			const int ty = sy0 + syi;
			for (int dx = dx0; dx < dx1; ++dx)
			{
				int sxi = static_cast<int>((static_cast<float>(dx) - d.x()) * su);
				sxi = std::min(sw - 1, std::max(0, sxi));
				if (flipX) { sxi = sw - 1 - sxi; }
				const int tx = sx0 + sxi;
				const std::size_t i = static_cast<std::size_t>((ty * texW + tx) * 4);
				if (i + 3 >= px.size()) { continue; }
				const std::uint8_t sa = px[i + 3];
				if (sa < 128) { continue; }   // ほぼ透明な画素は飛ばす
				const float a  = (sa / 255.0f) * tintColor.a;
				const float sr = (px[i]     / 255.0f) * tintColor.r;
				const float sg = (px[i + 1] / 255.0f) * tintColor.g;
				const float sb = (px[i + 2] / 255.0f) * tintColor.b;
				const std::size_t d =
					(static_cast<std::size_t>(dy) * m_width + static_cast<std::size_t>(dx)) * 4;
				m_pixels[d]     = cl(sr * a + (m_pixels[d]     / 255.0f) * (1.0f - a));
				m_pixels[d + 1] = cl(sg * a + (m_pixels[d + 1] / 255.0f) * (1.0f - a));
				m_pixels[d + 2] = cl(sb * a + (m_pixels[d + 2] / 255.0f) * (1.0f - a));
				m_pixels[d + 3] = 255;
			}
		}
		++m_drawCallCount;
		return;
	}

	// 上の 2 つが使えないときは、画素ごとに小さな矩形で描く。
	const float scaleX = dstRect.width()  / static_cast<float>(sw);
	const float scaleY = dstRect.height() / static_cast<float>(sh);
	const int step = std::max(1, static_cast<int>(1.0f / std::min(scaleX, scaleY)));
	for (int ry = 0; ry < sh; ry += step)
	{
		for (int rx = 0; rx < sw; rx += step)
		{
			const int tx = sx0 + rx;
			const int ty = sy0 + ry;
			const auto i = static_cast<std::size_t>((ty * texW + tx) * 4);
			if (i + 3 >= px.size()) continue;
			if (px[i + 3] < 128) continue; // 1-bit alpha cutout
			const sgc::Colorf color{
				(px[i] / 255.0f) * tintColor.r,
				(px[i + 1] / 255.0f) * tintColor.g,
				(px[i + 2] / 255.0f) * tintColor.b,
				(px[i + 3] / 255.0f) * tintColor.a};
			const float localCol = flipX ? static_cast<float>(sw - step - rx)
			                             : static_cast<float>(rx);
			const float dx = dstRect.x() + localCol * scaleX;
			const float dy = dstRect.y() + static_cast<float>(ry) * scaleY;
			emitRect(sgc::Rectf{dx, dy, scaleX * step, scaleY * step}, color);
		}
	}
	++m_drawCallCount;
}
