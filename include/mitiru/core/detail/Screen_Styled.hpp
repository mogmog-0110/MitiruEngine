#pragma once
// mitiru::Screen 用の detail header — 直接インクルードしない。core/Screen.hpp 経由で取り込む

namespace mitiru::screen_detail
{
	/// @brief Style.transform を矩形中心まわりの Transform2D に変換する
	/// @details CSS の transform: rotate/scale/translate を、形状中心を pivot として
	///          適用する。anchor 0.5,0.5 を仮定（CSS の transform-origin デフォルト）。
	[[nodiscard]] inline mitiru::render::Transform2D styleTransformToMatrix(
		const mitiru::render::StyleTransform& t, float cx, float cy) noexcept
	{
		using Tf = mitiru::render::Transform2D;
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
		const bool noRot   = std::abs(t.rotate) < 1e-6f;
		const bool noScale = std::abs(t.scale.x - 1.0f) < 1e-6f
		                  && std::abs(t.scale.y - 1.0f) < 1e-6f;
		const bool noTr    = std::abs(t.translate.x) < 1e-6f
		                  && std::abs(t.translate.y) < 1e-6f;
		if (noRot && noScale && noTr) { return Tf::identity(); }
		// translate(cx+tx, cy+ty) * rotate(rad) * scale(sx, sy) * translate(-cx, -cy)
		const float rad = t.rotate * kDegToRad;
		Tf m = Tf::translate(cx + t.translate.x, cy + t.translate.y)
		     * Tf::rotate(rad)
		     * Tf::scale(t.scale.x, t.scale.y)
		     * Tf::translate(-cx, -cy);
		return m;
	}
}

inline void mitiru::Screen::drawStyledRect(
	const sgc::Rectf& rect, const render::Style& style)
{
	const float cx = rect.x() + rect.width()  * 0.5f;
	const float cy = rect.y() + rect.height() * 0.5f;
	const auto local = mitiru::screen_detail::styleTransformToMatrix(style.transform, cx, cy);
	const mitiru::render::Transform2D world = currentTransform() * local;

	static thread_local render::StyledRectBatch batch;  // 描画間で確保を使い回す
	batch.begin();
	batch.addRect(rect, style, world);
	batch.end();

	if (m_pipeline && m_pipeline->isValid())
	{
		m_pipeline->submitStyledRectBatch(
			batch.vertices(),
			batch.indices(),
			batch.currentStyle());
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawStyledCircle(
	const sgc::Vec2f& center, float radius, const render::Style& style)
{
	const auto local = mitiru::screen_detail::styleTransformToMatrix(
		style.transform, center.x, center.y);
	const mitiru::render::Transform2D world = currentTransform() * local;

	static thread_local render::StyledCircleBatch batch;  // 描画間で確保を使い回す
	batch.begin();
	batch.addCircle(center, radius, style, world);
	batch.end();

	if (m_pipeline && m_pipeline->isValid())
	{
		m_pipeline->submitStyledCircleBatch(
			batch.vertices(),
			batch.indices(),
			batch.currentStyle());
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawShape(
	const render::ShapeEllipse& s, const render::Style& st)
{
	const auto local = mitiru::screen_detail::styleTransformToMatrix(
		st.transform, s.center.x, s.center.y);
	const mitiru::render::Transform2D world = currentTransform() * local;

	static thread_local render::StyledCircleBatch batch;  // 描画間で確保を使い回す
	batch.begin();
	batch.addEllipse(s.center, s.rx, s.ry, st, world);
	batch.end();

	if (m_pipeline && m_pipeline->isValid())
	{
		m_pipeline->submitStyledCircleBatch(
			batch.vertices(),
			batch.indices(),
			batch.currentStyle());
	}
	++m_drawCallCount;
}
