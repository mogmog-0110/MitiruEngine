#pragma once
// mitiru::Screen 用の detail header — 直接インクルードしない。core/Screen.hpp 経由で取り込む

inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect)
{
	if (!texture.valid()) return;
	validateDrawCall(dstRect, "drawSprite");
	const float scaleX = dstRect.width() / static_cast<float>(texture.width());
	const float scaleY = dstRect.height() / static_cast<float>(texture.height());
	const int step = std::max(1, static_cast<int>(1.0f / std::min(scaleX, scaleY)));
	const auto& px = texture.pixels();
	for (int ty = 0; ty < texture.height(); ty += step)
	{
		for (int tx = 0; tx < texture.width(); tx += step)
		{
			const auto i = static_cast<std::size_t>((ty * texture.width() + tx) * 4);
			if (i + 3 >= px.size()) continue;
			if (px[i + 3] < 128) continue;
			const sgc::Colorf color{
				px[i] / 255.0f, px[i + 1] / 255.0f,
				px[i + 2] / 255.0f, px[i + 3] / 255.0f};
			const float dx = dstRect.x() + static_cast<float>(tx) * scaleX;
			const float dy = dstRect.y() + static_cast<float>(ty) * scaleY;
			emitRect(
				sgc::Rectf{dx, dy, scaleX * step, scaleY * step}, color);
		}
	}
	++m_drawCallCount;
}

inline void mitiru::Screen::drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
                                       const sgc::Colorf& tintColor)
{
	if (!texture.valid()) return;
	validateDrawCall(dstRect, "drawSprite");
	const float scaleX = dstRect.width() / static_cast<float>(texture.width());
	const float scaleY = dstRect.height() / static_cast<float>(texture.height());
	const int step = std::max(1, static_cast<int>(1.0f / std::min(scaleX, scaleY)));
	const auto& px = texture.pixels();
	for (int ty = 0; ty < texture.height(); ty += step)
	{
		for (int tx = 0; tx < texture.width(); tx += step)
		{
			const auto i = static_cast<std::size_t>((ty * texture.width() + tx) * 4);
			if (i + 3 >= px.size()) continue;
			if (px[i + 3] < 128) continue;
			const sgc::Colorf color{
				(px[i] / 255.0f) * tintColor.r,
				(px[i + 1] / 255.0f) * tintColor.g,
				(px[i + 2] / 255.0f) * tintColor.b,
				(px[i + 3] / 255.0f) * tintColor.a};
			const float dx = dstRect.x() + static_cast<float>(tx) * scaleX;
			const float dy = dstRect.y() + static_cast<float>(ty) * scaleY;
			emitRect(
				sgc::Rectf{dx, dy, scaleX * step, scaleY * step}, color);
		}
	}
	++m_drawCallCount;
}
