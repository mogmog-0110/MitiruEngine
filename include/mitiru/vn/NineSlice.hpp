#pragma once

/// @file NineSlice.hpp
/// @brief 9-slice image rendering for scalable window skins.
/// @details Splits a source texture into 9 regions (4 corners, 4 edges, 1 center)
///          and generates vertex/UV data suitable for SpriteBatch rendering.
///          Corners remain at fixed size while edges and center scale to fill
///          the target rectangle.

#include <cstdint>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

/// @brief Configuration for a 9-slice texture.
struct NineSliceConfig
{
	std::uint32_t textureId = 0;         ///< Source texture identifier.

	float cornerW   = 16.0f;             ///< Corner width in source pixels.
	float cornerH   = 16.0f;             ///< Corner height in source pixels.

	float edgeInsetLeft   = 16.0f;       ///< Left edge inset in source pixels.
	float edgeInsetRight  = 16.0f;       ///< Right edge inset in source pixels.
	float edgeInsetTop    = 16.0f;       ///< Top edge inset in source pixels.
	float edgeInsetBottom = 16.0f;       ///< Bottom edge inset in source pixels.

	float textureW  = 64.0f;            ///< Full source texture width.
	float textureH  = 64.0f;            ///< Full source texture height.

	/// @brief Create a config with uniform corner size.
	/// @param texId Texture identifier.
	/// @param corner Corner size in source pixels.
	/// @param texW Source texture width.
	/// @param texH Source texture height.
	/// @return Configured NineSliceConfig.
	[[nodiscard]] static NineSliceConfig uniform(
		std::uint32_t texId, float corner, float texW, float texH) noexcept
	{
		NineSliceConfig cfg;
		cfg.textureId       = texId;
		cfg.cornerW         = corner;
		cfg.cornerH         = corner;
		cfg.edgeInsetLeft   = corner;
		cfg.edgeInsetRight  = corner;
		cfg.edgeInsetTop    = corner;
		cfg.edgeInsetBottom = corner;
		cfg.textureW        = texW;
		cfg.textureH        = texH;
		return cfg;
	}
};

/// @brief Renders a 9-slice texture into a SpriteBatch.
/// @details Each of the 9 regions is emitted as a separate sprite draw call.
///          Corners keep their source aspect, edges stretch in one axis,
///          and the center stretches in both.
///
/// @code
/// mitiru::vn::NineSliceConfig cfg =
///     mitiru::vn::NineSliceConfig::uniform(texId, 16.0f, 64.0f, 64.0f);
/// mitiru::vn::NineSlice slice(cfg);
///
/// batch.begin();
/// slice.draw(batch, destRect, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.9f});
/// batch.end();
/// @endcode
class NineSlice
{
	NineSliceConfig m_config;

public:
	/// @brief Construct with the given configuration.
	/// @param config 9-slice parameters.
	explicit NineSlice(NineSliceConfig config) noexcept
		: m_config(config)
	{
	}

	/// @brief Access the current configuration.
	[[nodiscard]] const NineSliceConfig& config() const noexcept { return m_config; }

	/// @brief Replace the configuration.
	/// @param config New 9-slice parameters.
	void setConfig(NineSliceConfig config) noexcept { m_config = config; }

	/// @brief Draw the 9-slice into a SpriteBatch.
	/// @param batch Target SpriteBatch (must be between begin/end).
	/// @param dest Destination rectangle in screen space.
	/// @param tint Colour tint / alpha modulation.
	void draw(render::SpriteBatch& batch,
	          const sgc::Rectf& dest,
	          const sgc::Colorf& tint) const
	{
		const float il = m_config.edgeInsetLeft;
		const float ir = m_config.edgeInsetRight;
		const float it = m_config.edgeInsetTop;
		const float ib = m_config.edgeInsetBottom;
		const float tw = m_config.textureW;
		const float th = m_config.textureH;

		// UV coordinates for the three columns and rows.
		const float u0 = 0.0f;
		const float u1 = il / tw;
		const float u2 = (tw - ir) / tw;
		const float u3 = 1.0f;

		const float v0 = 0.0f;
		const float v1 = it / th;
		const float v2 = (th - ib) / th;
		const float v3 = 1.0f;

		// Destination coordinates for the three columns and rows.
		const float dx0 = dest.x();
		const float dx1 = dest.x() + m_config.cornerW;
		const float dx2 = dest.x() + dest.width() - m_config.cornerW;
		const float dx3 = dest.x() + dest.width();

		const float dy0 = dest.y();
		const float dy1 = dest.y() + m_config.cornerH;
		const float dy2 = dest.y() + dest.height() - m_config.cornerH;
		const float dy3 = dest.y() + dest.height();

		const auto texId = m_config.textureId;

		// Row 0: top-left, top-edge, top-right
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy0, dx1 - dx0, dy1 - dy0},
			sgc::Rectf{u0, v0, u1 - u0, v1 - v0}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy0, dx2 - dx1, dy1 - dy0},
			sgc::Rectf{u1, v0, u2 - u1, v1 - v0}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy0, dx3 - dx2, dy1 - dy0},
			sgc::Rectf{u2, v0, u3 - u2, v1 - v0}, tint);

		// Row 1: left-edge, center, right-edge
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy1, dx1 - dx0, dy2 - dy1},
			sgc::Rectf{u0, v1, u1 - u0, v2 - v1}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy1, dx2 - dx1, dy2 - dy1},
			sgc::Rectf{u1, v1, u2 - u1, v2 - v1}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy1, dx3 - dx2, dy2 - dy1},
			sgc::Rectf{u2, v1, u3 - u2, v2 - v1}, tint);

		// Row 2: bottom-left, bottom-edge, bottom-right
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy2, dx1 - dx0, dy3 - dy2},
			sgc::Rectf{u0, v2, u1 - u0, v3 - v2}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy2, dx2 - dx1, dy3 - dy2},
			sgc::Rectf{u1, v2, u2 - u1, v3 - v2}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy2, dx3 - dx2, dy3 - dy2},
			sgc::Rectf{u2, v2, u3 - u2, v3 - v2}, tint);
	}

	/// @brief Compute the minimum size this 9-slice can be drawn at.
	/// @return Minimum width and height as a Rectf (x=0, y=0, w=min, h=min).
	[[nodiscard]] sgc::Rectf minimumSize() const noexcept
	{
		return sgc::Rectf{
			0.0f, 0.0f,
			m_config.cornerW * 2.0f,
			m_config.cornerH * 2.0f
		};
	}
};

} // namespace mitiru::vn
