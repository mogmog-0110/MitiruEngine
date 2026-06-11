#pragma once
// mitiru::Screen 用の detail header — 直接インクルードしない。core/Screen.hpp 経由で取り込む

// テクスチャ全体を描く（白ティント）。
inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect)
{
	drawSprite(texture, dstRect, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
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

// スプライトシートの 1 コマ（srcRect）を ティント / 左右反転付きで描く。
//   • DX12 path: 本物のテクスチャ付きクワッドを texture-keyed バッチに emit（ADR 0009）。
//   • それ以外（software / DX11 / WebGL / Null）: 従来の per-pixel emitRect に fallback。
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

	// ── 高速パス: DX12 textured batch（1 コマ = 1 クワッド）──
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
		// handle==0（アップロード失敗）は per-pixel fallback へ落ちる。
	}

	const auto& px = texture.pixels();

	// ── 高速パス: ソフトウェアフレームバッファ直接 blit (#46a) ──
	// headless (NullDevice + SW-FB) では従来 per-pixel emitRect が 1 スプライト数百万回の
	// 関数呼び出しになり、sprite 多用ゲームで 1 フレーム数秒に落ちていた。dest 画素を走査して
	// source を nearest サンプルし m_pixels へ直接ブレンドする (数十〜百倍速)。
	if (hasSoftwareFramebuffer() && m_width > 0 && m_height > 0 &&
	    dstRect.width() > 0.0f && dstRect.height() > 0.0f)
	{
		// 観測しないフレーム (#53): blit のピクセルループを丸ごと省く。
		// fallback (per-pixel emitRect) に落とすと逆に遅いのでここで return。
		if (!m_swFbActive) { ++m_drawCallCount; return; }
		// submit 順 (z 順) 維持: この sprite より前に積まれた rect/text/shape を先に
		// m_pixels へ焼いてから直書きする。さもないと「全 rect は present で後段 flush」
		// となり sprite が常に下/上で固定され重なり順が壊れる。batch が空なら安価。
		flushCurrentBatch();
		if (!m_shapeRenderer.vertices().empty())
		{
			rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}
		// currentTransform（kRootScale / カメラ等）を dst に適用する。GPU パスと同じ変換を
		// SW-FB でも通さないと scale 付きゲームが 2 倍/見切れになる。回転は fast blit 非対応の
		// ため AABB 近似（translate+scale は厳密）。
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
				if (sa < 128) { continue; }   // 1-bit alpha cutout（従来挙動）
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

	// ── fallback: per-pixel emitRect（テクスチャをサンプルできない backend 用）──
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
