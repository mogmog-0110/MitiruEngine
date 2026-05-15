#pragma once
// Detail header for mitiru::Screen — do not include directly; included via core/Screen.hpp

inline void mitiru::Screen::drawZigzagEdge(const sgc::Rectf& rect, const render::ZigzagStyle& zs)
{
	const float sz = zs.toothSize;
	const auto& c = zs.color;
	const float rx = rect.position.x;
	const float ry = rect.position.y;
	const float rw = rect.size.x;
	const float rh = rect.size.y;
	if (zs.edge == render::Edge::Bottom || zs.edge == render::Edge::Top)
	{
		const float ty = (zs.edge == render::Edge::Bottom) ? ry : ry + rh - sz;
		for (float tx = rx; tx < rx + rw; tx += sz)
		{
			const float tw = std::min(sz, rx + rw - tx);
			emitTriangle(
				{tx, ty}, {tx + tw, ty}, {tx + tw * 0.5f, ty + sz}, c);
		}
	}
	else
	{
		const float tx = (zs.edge == render::Edge::Left) ? rx : rx + rw - sz;
		for (float ty = ry; ty < ry + rh; ty += sz)
		{
			const float th = std::min(sz, ry + rh - ty);
			emitTriangle(
				{tx, ty}, {tx, ty + th}, {tx + sz, ty + th * 0.5f}, c);
		}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawScanlines(const sgc::Rectf& rect, float lineHeight, const sgc::Colorf& color)
{
	for (float sy = rect.position.y; sy < rect.position.y + rect.size.y; sy += lineHeight * 2)
	{
		emitRect({rect.position.x, sy, rect.size.x, lineHeight}, color);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawDashedLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
                                            float thickness, float dashLen, float gapLen,
                                            const sgc::Colorf& color)
{
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 0.001f) return;
	const float nx = dx / len;
	const float ny = dy / len;
	float t = 0.0f;
	while (t < len)
	{
		const float end = std::min(t + dashLen, len);
		emitLine(
			{from.x + nx * t, from.y + ny * t},
			{from.x + nx * end, from.y + ny * end},
			color, thickness);
		t = end + gapLen;
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRect(const sgc::Rectf& rect, const sgc::Colorf& color)
{
	validateDrawCall(rect, "drawRect");
	validateColor(color, "drawRect", rect);
	emitRect(rect, color);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color, float thickness)
{
	validateDrawCall(rect, "drawRectFrame");
	validateColor(color, "drawRectFrame", rect);
	const sgc::Vec2f tl{rect.x(), rect.y()};
	const sgc::Vec2f tr{rect.x() + rect.width(), rect.y()};
	const sgc::Vec2f bl{rect.x(), rect.y() + rect.height()};
	const sgc::Vec2f br{rect.x() + rect.width(), rect.y() + rect.height()};
	emitLine(tl, tr, color, thickness);
	emitLine(tr, br, color, thickness);
	emitLine(br, bl, color, thickness);
	emitLine(bl, tl, color, thickness);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRoundedRect(const sgc::Rectf& rect, const sgc::Colorf& color, float radius)
{
	validateDrawCall(rect, "drawRoundedRect");
	const float r = std::min(radius, std::min(rect.width(), rect.height()) * 0.5f);
	const float x = rect.x(), y = rect.y(), w = rect.width(), h = rect.height();

	// Center cross (3 rectangles) — emitRect handles transform
	emitRect(sgc::Rectf{x + r, y, w - r * 2, h}, color);
	emitRect(sgc::Rectf{x, y + r, r, h - r * 2}, color);
	emitRect(sgc::Rectf{x + w - r, y + r, r, h - r * 2}, color);

	// 4 corner arcs (approximated with triangle fans)
	constexpr int kSegments = 8;
	constexpr float kHalfPi = 1.5707963f;
	const sgc::Vec2f corners[] = {
		{x + r,     y + r},
		{x + w - r, y + r},
		{x + w - r, y + h - r},
		{x + r,     y + h - r},
	};
	const float startAngles[] = {kHalfPi * 2, kHalfPi * 3, 0.0f, kHalfPi};

	for (int c = 0; c < 4; ++c)
	{
		const auto& center = corners[c];
		const float sa = startAngles[c];
		for (int i = 0; i < kSegments; ++i)
		{
			const float a0 = sa + kHalfPi * static_cast<float>(i) / kSegments;
			const float a1 = sa + kHalfPi * static_cast<float>(i + 1) / kSegments;
			const sgc::Vec2f p0{center.x + r * std::cos(a0), center.y + r * std::sin(a0)};
			const sgc::Vec2f p1{center.x + r * std::cos(a1), center.y + r * std::sin(a1)};
			emitTriangle(center, p0, p1, color);
		}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRoundedRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color,
                                                  float radius, float thickness)
{
	validateDrawCall(rect, "drawRoundedRectFrame");
	const float r = std::min(radius, std::min(rect.width(), rect.height()) * 0.5f);
	const float x = rect.x(), y = rect.y(), w = rect.width(), h = rect.height();

	// 4 straight edges
	emitLine({x + r, y}, {x + w - r, y}, color, thickness);
	emitLine({x + w, y + r}, {x + w, y + h - r}, color, thickness);
	emitLine({x + w - r, y + h}, {x + r, y + h}, color, thickness);
	emitLine({x, y + h - r}, {x, y + r}, color, thickness);

	// 4 corner arcs
	constexpr int kSegments = 8;
	constexpr float kHalfPi = 1.5707963f;
	const sgc::Vec2f corners[] = {
		{x + r, y + r}, {x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}
	};
	const float startAngles[] = {kHalfPi * 2, kHalfPi * 3, 0.0f, kHalfPi};
	for (int c = 0; c < 4; ++c)
	{
		const auto& ctr = corners[c];
		const float sa = startAngles[c];
		for (int i = 0; i < kSegments; ++i)
		{
			const float a0 = sa + kHalfPi * static_cast<float>(i) / kSegments;
			const float a1 = sa + kHalfPi * static_cast<float>(i + 1) / kSegments;
			emitLine(
				{ctr.x + r * std::cos(a0), ctr.y + r * std::sin(a0)},
				{ctr.x + r * std::cos(a1), ctr.y + r * std::sin(a1)},
				color, thickness);
		}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawPie(const sgc::Vec2f& center, float radius,
                                     float startAngle, float endAngle, const sgc::Colorf& color)
{
	validateDrawCall(sgc::Rectf{center.x-radius, center.y-radius, radius*2, radius*2}, "drawPie");
	const int segments = std::max(4, static_cast<int>((endAngle - startAngle) * 8.0f));
	const float step = (endAngle - startAngle) / segments;
	for (int i = 0; i < segments; ++i)
	{
		const float a0 = startAngle + step * i;
		const float a1 = startAngle + step * (i + 1);
		emitTriangle(
			center,
			{center.x + radius * std::cos(a0), center.y + radius * std::sin(a0)},
			{center.x + radius * std::cos(a1), center.y + radius * std::sin(a1)},
			color);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawArc(const sgc::Vec2f& center, float radius,
                                     float startAngle, float endAngle,
                                     const sgc::Colorf& color, float thickness)
{
	validateDrawCall(sgc::Rectf{center.x-radius, center.y-radius, radius*2, radius*2}, "drawArc");
	const int segments = std::max(4, static_cast<int>((endAngle - startAngle) * 8.0f));
	const float step = (endAngle - startAngle) / segments;
	for (int i = 0; i < segments; ++i)
	{
		const float a0 = startAngle + step * i;
		const float a1 = startAngle + step * (i + 1);
		emitLine(
			{center.x + radius * std::cos(a0), center.y + radius * std::sin(a0)},
			{center.x + radius * std::cos(a1), center.y + radius * std::sin(a1)},
			color, thickness);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawTriangle(const sgc::Vec2f& p0, const sgc::Vec2f& p1,
                                          const sgc::Vec2f& p2, const sgc::Colorf& color)
{
	{
		const sgc::Rectf bounds{std::min({p0.x,p1.x,p2.x}), std::min({p0.y,p1.y,p2.y}), std::max({p0.x,p1.x,p2.x})-std::min({p0.x,p1.x,p2.x}), std::max({p0.y,p1.y,p2.y})-std::min({p0.y,p1.y,p2.y})};
		validateDrawCall(bounds, "drawTriangle");
		validateColor(color, "drawTriangle", bounds);
	}
	emitTriangle(p0, p1, p2, color);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color)
{
	{
		const sgc::Rectf bounds{center.x-radius, center.y-radius, radius*2.0f, radius*2.0f};
		validateDrawCall(bounds, "drawCircle");
		validateColor(color, "drawCircle", bounds);
	}
	const auto t = currentTransform();
	if (t.isIdentity())
	{
		m_shapeRenderer.drawCircle(center, radius, color);
	}
	else
	{
		m_shapeRenderer.drawCircle(t.apply(center), radius * t.avgScale(), color);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
                                      const sgc::Colorf& color, float thickness)
{
	{
		const sgc::Rectf bounds{std::min(from.x,to.x), std::min(from.y,to.y), std::abs(to.x-from.x)+thickness, std::abs(to.y-from.y)+thickness};
		validateDrawCall(bounds, "drawLine");
		validateColor(color, "drawLine", bounds);
	}
	emitLine(from, to, color, thickness);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawPolygon(const std::vector<sgc::Vec2f>& points, const sgc::Colorf& color)
{
	if (points.size() < 3) return;
	{
		float minX=points[0].x, minY=points[0].y, maxX=points[0].x, maxY=points[0].y;
		for (const auto& p : points) { minX=std::min(minX,p.x); minY=std::min(minY,p.y); maxX=std::max(maxX,p.x); maxY=std::max(maxY,p.y); }
		const sgc::Rectf bounds{minX, minY, maxX-minX, maxY-minY};
		validateDrawCall(bounds, "drawPolygon");
		validateColor(color, "drawPolygon", bounds);
	}
	for (std::size_t i = 1; i + 1 < points.size(); ++i)
	{
		emitTriangle(points[0], points[i], points[i + 1], color);
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawEllipse(const sgc::Vec2f& center, float radiusX, float radiusY,
                                         const sgc::Colorf& color)
{
	const auto t = currentTransform();
	const auto c = t.apply(center);
	const float rx = radiusX * t.scaleX();
	const float ry = radiusY * t.scaleY();
	m_shapeRenderer.drawEllipse(c, rx, ry, color);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRing(const sgc::Vec2f& center, float outerRadius, float innerRadius,
                                      const sgc::Colorf& color)
{
	const auto t = currentTransform();
	const auto c = t.apply(center);
	const float s = t.avgScale();
	m_shapeRenderer.drawRing(c, outerRadius * s, innerRadius * s, color);
	++m_drawCallCount;
}

inline void mitiru::Screen::drawRoundedRect4(const sgc::Rectf& rect, const sgc::Colorf& color,
                                              float tl, float tr, float br, float bl)
{
	const float x = rect.x(), y = rect.y(), w = rect.width(), h = rect.height();
	const float maxR = std::min(w, h) * 0.5f;
	const float r[4] = {
		std::min(tl, maxR), std::min(tr, maxR),
		std::min(br, maxR), std::min(bl, maxR)
	};

	// Center rectangles (fill non-corner areas)
	const float topBarLeft = x + r[0], topBarRight = x + w - r[1];
	emitRect({topBarLeft, y, topBarRight - topBarLeft, h}, color);
	emitRect({x, y + r[0], r[0], h - r[0] - r[3]}, color);
	emitRect({x + w - r[1], y + r[1], r[1], h - r[1] - r[2]}, color);
	const float botBarLeft = x + r[3], botBarRight = x + w - r[2];
	if (botBarRight > botBarLeft)
		emitRect({botBarLeft, y + h - std::max(r[2], r[3]),
		          botBarRight - botBarLeft, std::max(r[2], r[3])}, color);

	// 4 corner arcs with individual radii
	constexpr int kSeg = 8;
	constexpr float kHalfPi = 1.5707963f;
	const sgc::Vec2f centers[4] = {
		{x + r[0], y + r[0]},
		{x + w - r[1], y + r[1]},
		{x + w - r[2], y + h - r[2]},
		{x + r[3], y + h - r[3]},
	};
	const float startAngles[4] = {kHalfPi * 2, kHalfPi * 3, 0.0f, kHalfPi};

	for (int c = 0; c < 4; ++c)
	{
		if (r[c] < 0.5f) continue;
		const auto& ctr = centers[c];
		const float sa = startAngles[c];
		for (int i = 0; i < kSeg; ++i)
		{
			const float a0 = sa + kHalfPi * static_cast<float>(i) / kSeg;
			const float a1 = sa + kHalfPi * static_cast<float>(i + 1) / kSeg;
			emitTriangle(
				ctr,
				{ctr.x + r[c] * std::cos(a0), ctr.y + r[c] * std::sin(a0)},
				{ctr.x + r[c] * std::cos(a1), ctr.y + r[c] * std::sin(a1)},
				color);
		}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawCircleFrame(const sgc::Vec2f& center, float radius,
                                             const sgc::Colorf& color, float thickness)
{
	constexpr float kTwoPi = 6.28318530717958647692f;
	drawArc(center, radius, 0.0f, kTwoPi, color, thickness);
}
