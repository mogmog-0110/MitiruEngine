#pragma once
// This header is included by Screen.hpp — do not include directly.

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

	// NullDevice / headless: no-op.
	// The per-pixel emitRect path (57,600 drawRect calls) is what this API
	// replaces; falling back to it here would defeat the purpose.
	if (!m_pipeline)
	{
		return;
	}

	// Flush the sprite batch so in-flight geometry lands before the textured quad.
	// SpriteBatch does not expose an explicit flush; present() is the normal drain
	// point. If a mid-frame flush helper is added in future, call it here instead.
	// For now this draw is intentionally unbatched (one GPU call per invocation).

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

	// NullDevice / headless: no-op. See 4-arg overload for rationale —
	// per-pixel fallback would defeat the purpose of this API.
	if (!m_pipeline)
	{
		return;
	}

	// Mid-frame flush note: SpriteBatch does not expose an explicit flush.
	// This draw is intentionally unbatched (one GPU call per invocation).
	m_pipeline->submitPixelGrid(
		dest, pixels, pixelWidth, pixelHeight,
		static_cast<float>(m_width),
		static_cast<float>(m_height),
		filter);

	++m_drawCallCount;
}

} // namespace mitiru
