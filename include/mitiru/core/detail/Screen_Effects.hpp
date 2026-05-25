#pragma once
// mitiru::Screen 用の detail header — 直接インクルードしない。core/Screen.hpp 経由で取り込む

inline void mitiru::Screen::drawGradientRectH(const sgc::Rectf& rect,
                                               const sgc::Colorf& leftColor, const sgc::Colorf& rightColor)
{
	emitGradientRect(rect, leftColor, rightColor, rightColor, leftColor);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawGradientRect4(const sgc::Rectf& rect,
                                               const sgc::Colorf& topLeft, const sgc::Colorf& topRight,
                                               const sgc::Colorf& bottomRight, const sgc::Colorf& bottomLeft)
{
	emitGradientRect(rect, topLeft, topRight, bottomRight, bottomLeft);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRectRotated(const sgc::Rectf& rect, const sgc::Colorf& color, float angleDeg)
{
	const float cx = rect.x() + rect.width() * 0.5f;
	const float cy = rect.y() + rect.height() * 0.5f;
	const float hw = rect.width() * 0.5f;
	const float hh = rect.height() * 0.5f;
	const float rad = angleDeg * 3.14159265f / 180.0f;
	const float cosA = std::cos(rad);
	const float sinA = std::sin(rad);

	auto rot = [&](float lx, float ly) -> sgc::Vec2f {
		return {cx + lx * cosA - ly * sinA, cy + lx * sinA + ly * cosA};
	};

	const std::vector<sgc::Vec2f> pts = {
		rot(-hw, -hh), rot(hw, -hh), rot(hw, hh), rot(-hw, hh)
	};
	drawPolygon(pts, color);
}

inline void mitiru::Screen::drawGradientRect(const sgc::Rectf& rect,
                                              const sgc::Colorf& topColor, const sgc::Colorf& bottomColor)
{
	validateDrawCall(rect, "drawGradientRect");
	validateColor(topColor, "drawGradientRect", rect);
	validateColor(bottomColor, "drawGradientRect", rect);
	const int steps = std::max(1, static_cast<int>(rect.height() / 4.0f));
	const float stepH = rect.height() / static_cast<float>(steps);
	for (int i = 0; i < steps; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		const sgc::Colorf color{
			topColor.r + (bottomColor.r - topColor.r) * t,
			topColor.g + (bottomColor.g - topColor.g) * t,
			topColor.b + (bottomColor.b - topColor.b) * t,
			topColor.a + (bottomColor.a - topColor.a) * t
		};
		emitRect(
			sgc::Rectf{
				rect.x(),
				rect.y() + static_cast<float>(i) * stepH,
				rect.width(),
				stepH},
			color);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRectPattern(const sgc::Rectf& rect, PatternType pattern,
                                             float cellSize, const sgc::Colorf& color1,
                                             const sgc::Colorf& color2)
{
	if (cellSize < 1.0f) cellSize = 1.0f;
	const int cols = static_cast<int>(std::ceil(rect.width() / cellSize));
	const int rows = static_cast<int>(std::ceil(rect.height() / cellSize));

	switch (pattern)
	{
	case PatternType::Checkerboard:
		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < cols; ++c)
			{
				const auto& col = ((r + c) % 2 == 0) ? color1 : color2;
				const float cw = std::min(cellSize, rect.width() - c * cellSize);
				const float ch = std::min(cellSize, rect.height() - r * cellSize);
				emitRect(
					{rect.x() + c * cellSize, rect.y() + r * cellSize, cw, ch}, col);
			}
		}
		break;

	case PatternType::HStripes:
		for (int r = 0; r < rows; ++r)
		{
			const auto& col = (r % 2 == 0) ? color1 : color2;
			const float ch = std::min(cellSize, rect.height() - r * cellSize);
			emitRect(
				{rect.x(), rect.y() + r * cellSize, rect.width(), ch}, col);
		}
		break;

	case PatternType::VStripes:
		for (int c = 0; c < cols; ++c)
		{
			const auto& col = (c % 2 == 0) ? color1 : color2;
			const float cw = std::min(cellSize, rect.width() - c * cellSize);
			emitRect(
				{rect.x() + c * cellSize, rect.y(), cw, rect.height()}, col);
		}
		break;

	case PatternType::DiagStripes:
	{
		emitRect(rect, color1);
		const float w = rect.width(), h = rect.height();
		const float stride = cellSize * 2.0f;
		for (float offset = -h; offset < w + h; offset += stride)
		{
			const float x0 = rect.x() + offset;
			const float x1 = x0 + cellSize;
			const sgc::Vec2f p0{std::max(rect.x(), x0), rect.y()};
			const sgc::Vec2f p1{std::max(rect.x(), x1), rect.y()};
			const sgc::Vec2f p2{std::max(rect.x(), std::min(rect.x() + w, x0 - h)), rect.y() + h};
			const sgc::Vec2f p3{std::max(rect.x(), std::min(rect.x() + w, x1 - h)), rect.y() + h};
			emitTriangle(p0, p1, p3, color2);
			emitTriangle(p0, p3, p2, color2);
		}
		break;
	}

	case PatternType::Dots:
	{
		emitRect(rect, color1);
		const float dotR = cellSize * 0.25f;
		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < cols; ++c)
			{
				const sgc::Vec2f center{
					rect.x() + (c + 0.5f) * cellSize,
					rect.y() + (r + 0.5f) * cellSize};
				if (center.x + dotR > rect.x() + rect.width()) continue;
				if (center.y + dotR > rect.y() + rect.height()) continue;
				drawCircle(center, dotR, color2);
			}
		}
		break;
	}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawInnerShadow(const sgc::Rectf& rect,
                                             const sgc::Colorf& shadowColor,
                                             float blurSize,
                                             float offsetX, float offsetY)
{
	const sgc::Colorf transparent{shadowColor.r, shadowColor.g, shadowColor.b, 0.0f};
	const float blur = std::min(blurSize, std::min(rect.width(), rect.height()) * 0.5f);

	// 上辺
	if (offsetY >= 0.0f)
	{
		const float h = blur + offsetY;
		if (h > 0.0f)
		{
			emitGradientRect(
				{rect.x(), rect.y(), rect.width(), std::min(h, rect.height())},
				shadowColor, shadowColor, transparent, transparent);
		}
	}

	// 下辺
	if (offsetY <= 0.0f)
	{
		const float h = blur - offsetY;
		if (h > 0.0f)
		{
			const float top = rect.y() + rect.height() - std::min(h, rect.height());
			emitGradientRect(
				{rect.x(), top, rect.width(), std::min(h, rect.height())},
				transparent, transparent, shadowColor, shadowColor);
		}
	}

	// 左辺
	if (offsetX >= 0.0f)
	{
		const float w = blur + offsetX;
		if (w > 0.0f)
		{
			emitGradientRect(
				{rect.x(), rect.y(), std::min(w, rect.width()), rect.height()},
				shadowColor, transparent, transparent, shadowColor);
		}
	}

	// 右辺
	if (offsetX <= 0.0f)
	{
		const float w = blur - offsetX;
		if (w > 0.0f)
		{
			const float left = rect.x() + rect.width() - std::min(w, rect.width());
			emitGradientRect(
				{left, rect.y(), std::min(w, rect.width()), rect.height()},
				transparent, shadowColor, shadowColor, transparent);
		}
	}

	++m_drawCallCount;
}

inline void mitiru::Screen::drawFrostedRect(const sgc::Rectf& rect,
                                             const sgc::Colorf& tintColor,
                                             int layers)
{
	const float layerAlpha = tintColor.a / static_cast<float>(std::max(1, layers));
	const sgc::Colorf layerColor{tintColor.r, tintColor.g, tintColor.b, layerAlpha};

	for (int i = 0; i < layers; ++i)
	{
		const float spread = static_cast<float>(i) * 0.5f;
		emitRect(
			{rect.x() - spread, rect.y() - spread,
			 rect.width() + spread * 2.0f, rect.height() + spread * 2.0f},
			layerColor);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRectPerspective(const sgc::Rectf& rect, const sgc::Colorf& color,
                                                 bool vanishTop, float strength)
{
	const float x = rect.x(), y = rect.y();
	const float w = rect.width(), h = rect.height();
	const float inset = w * 0.5f * std::clamp(strength, 0.0f, 0.95f);

	std::vector<sgc::Vec2f> pts;
	if (vanishTop)
	{
		pts = {
			{x + inset, y},
			{x + w - inset, y},
			{x + w, y + h},
			{x, y + h},
		};
	}
	else
	{
		pts = {
			{x, y},
			{x + w, y},
			{x + w - inset, y + h},
			{x + inset, y + h},
		};
	}
	drawPolygon(pts, color);
}

inline void mitiru::Screen::drawRectPerspectiveGradient(const sgc::Rectf& rect,
                                                         const sgc::Colorf& topColor,
                                                         const sgc::Colorf& bottomColor,
                                                         bool vanishTop, float strength)
{
	const float x = rect.x(), y = rect.y();
	const float w = rect.width(), h = rect.height();
	const float inset = w * 0.5f * std::clamp(strength, 0.0f, 0.95f);

	constexpr int kSteps = 16;
	const float stepH = h / kSteps;
	for (int i = 0; i < kSteps; ++i)
	{
		const float t0 = static_cast<float>(i) / kSteps;
		const float t1 = static_cast<float>(i + 1) / kSteps;
		const float sy0 = y + t0 * h;
		const float sy1 = y + t1 * h;

		float inset0, inset1;
		if (vanishTop)
		{
			inset0 = inset * (1.0f - t0);
			inset1 = inset * (1.0f - t1);
		}
		else
		{
			inset0 = inset * t0;
			inset1 = inset * t1;
		}

		const sgc::Colorf c0{
			topColor.r + (bottomColor.r - topColor.r) * t0,
			topColor.g + (bottomColor.g - topColor.g) * t0,
			topColor.b + (bottomColor.b - topColor.b) * t0,
			topColor.a + (bottomColor.a - topColor.a) * t0};

		const sgc::Vec2f p0{x + inset0, sy0};
		const sgc::Vec2f p1{x + w - inset0, sy0};
		const sgc::Vec2f p2{x + w - inset1, sy1};
		const sgc::Vec2f p3{x + inset1, sy1};

		emitTriangle(p0, p1, p2, c0);
		emitTriangle(p0, p2, p3, c0);
	}
	++m_drawCallCount;
}
