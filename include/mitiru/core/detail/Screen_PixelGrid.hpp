#pragma once
// このヘッダーは Screen.hpp からインクルードされる。直接インクルードしない。

namespace mitiru
{

namespace detail
{
// pixel buffer を sprite batch へ texture として流し込む共通ロジック。
// true を返したら batched path で処理済み。
// NPR #19a: 旧 submitPixelGrid は自前 command list を即時 execute していたため、
// present() で drain される batched geometry (fillScreen 等) に上書きされて消えていた。
// drawSprite と同じ batched textured-quad path に通すと描画順が呼び出し順と一致する。
template <typename EnsureTexFn, typename EmitQuadFn>
inline bool pixelGridIntoBatch(
	render::RenderPipeline2D* pipeline,
	const sgc::Rectf& dest,
	const std::uint32_t* pixels,
	int pixelWidth,
	int pixelHeight,
	EnsureTexFn&& ensureTex,
	EmitQuadFn&& emitQuad)
{
	if (pipeline == nullptr || !pipeline->supportsTexturedBatch())
	{
		return false;
	}
	// content-hash 込み (#19b) で per-frame 更新も正しく再アップロードされる。
	const std::uint32_t handle = ensureTex(
		static_cast<const void*>(pixels), pixelWidth, pixelHeight,
		reinterpret_cast<const std::uint8_t*>(pixels));
	if (handle == 0)
	{
		return false;
	}
	// drawPixelGrid は camera transform を尊重しない（screen-space 固定）ので raw 座標。
	const float x0 = dest.x();
	const float y0 = dest.y();
	const float x1 = dest.x() + dest.width();
	const float y1 = dest.y() + dest.height();
	const sgc::Vec2f corners[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	const sgc::Vec2f uvs[4]     = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	emitQuad(handle, corners, uvs);
	return true;
}
} // namespace detail

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
	if (!m_pipeline)
	{
		return;
	}

	// NPR #19a: batched textured-quad path（drawSprite と同じ drain 順）を優先。
	if (detail::pixelGridIntoBatch(
			m_pipeline, dest, pixels, pixelWidth, pixelHeight,
			[this](const void* key, int w, int h, const std::uint8_t* rgba) {
				// drawPixelGrid は同アドレスに毎フレーム作り直す動的バッファ → 内容ハッシュで変化検出。
				return m_pipeline->ensureSpriteTexture(key, w, h, rgba, /*contentMayChange=*/true);
			},
			[this](std::uint32_t handle,
				const sgc::Vec2f (&corners)[4], const sgc::Vec2f (&uvs)[4]) {
				drawSpriteTexturedQuad(handle, corners, uvs, sgc::Colorf{1, 1, 1, 1});
			}))
	{
		++m_drawCallCount;
		return;
	}

	// fallback: textured batch 非対応 backend は従来 path。
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

	if (!m_pipeline)
	{
		return;
	}

	// NPR #19a: batched path を優先（filter は batch sampler 既定に従う）。
	if (detail::pixelGridIntoBatch(
			m_pipeline, dest, pixels, pixelWidth, pixelHeight,
			[this](const void* key, int w, int h, const std::uint8_t* rgba) {
				// drawPixelGrid は同アドレスに毎フレーム作り直す動的バッファ → 内容ハッシュで変化検出。
				return m_pipeline->ensureSpriteTexture(key, w, h, rgba, /*contentMayChange=*/true);
			},
			[this](std::uint32_t handle,
				const sgc::Vec2f (&corners)[4], const sgc::Vec2f (&uvs)[4]) {
				drawSpriteTexturedQuad(handle, corners, uvs, sgc::Colorf{1, 1, 1, 1});
			}))
	{
		++m_drawCallCount;
		return;
	}

	// fallback: textured batch 非対応 backend は従来 path。
	m_pipeline->submitPixelGrid(
		dest, pixels, pixelWidth, pixelHeight,
		static_cast<float>(m_width),
		static_cast<float>(m_height),
		filter);

	++m_drawCallCount;
}

} // namespace mitiru
