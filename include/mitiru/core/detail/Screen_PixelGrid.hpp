#pragma once
// このヘッダーは Screen.hpp からインクルードされる。直接インクルードしない。

namespace mitiru
{

inline void Screen::drawPixelGrid(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pixelWidth,
	int pixelHeight)
{
	if (pixelWidth <= 0 || pixelHeight <= 0 || pixels == nullptr)
	{
		return;
	}

	// NullDevice / headless: no-op。
	// この API が置き換える対象が per-pixel emitRect path (57,600 回の drawRect)。
	// ここでそれにフォールバックすると API の目的を損なう。
	if (!m_pipeline)
	{
		return;
	}

	// textured quad の前に in-flight geometry が乗るよう sprite batch を flush する。
	// SpriteBatch は明示的な flush を公開しておらず、present() が通常の drain point。
	// 将来 mid-frame flush helper が追加されたら代わりにここで呼ぶ。
	// 現状この draw は意図的に unbatched (呼び出し 1 回 = GPU call 1 回)。

	m_pipeline->submitPixelGrid(
		dest, pixels, pixelWidth, pixelHeight,
		static_cast<float>(m_width),
		static_cast<float>(m_height));

	++m_drawCallCount;
}

inline void Screen::drawPixelGrid(
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pixelWidth,
	int pixelHeight,
	render::PixelArtFilter filter)
{
	if (pixelWidth <= 0 || pixelHeight <= 0 || pixels == nullptr)
	{
		return;
	}

	// NullDevice / headless: no-op。理由は 4 引数オーバーロードを参照 —
	// per-pixel fallback は API の目的を損なう。
	if (!m_pipeline)
	{
		return;
	}

	// mid-frame flush 注記: SpriteBatch は明示的な flush を公開していない。
	// この draw は意図的に unbatched (呼び出し 1 回 = GPU call 1 回)。
	m_pipeline->submitPixelGrid(
		dest, pixels, pixelWidth, pixelHeight,
		static_cast<float>(m_width),
		static_cast<float>(m_height),
		filter);

	++m_drawCallCount;
}

} // namespace mitiru
