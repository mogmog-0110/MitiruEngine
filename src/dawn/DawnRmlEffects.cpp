// 層、filter、グラデーションの実装。DX12 の RmlRenderInterfaceDx12_Effects.cpp と同じ手順。
// D3D、テクスチャ、描画先はすべて左上が原点なので、上下を反転しない。

#include "DawnRmlRenderer.hpp"

#include <algorithm>
#include <cstring>

#include <RmlUi/Core/Log.h>

#include "DawnDeviceTarget.hpp"

namespace mitiru::ui_rml::dawn_ui
{

namespace
{

// 範囲外の画素が双線形補間で混ざらないよう、端の画素の中心までに留める
void setTexCoordLimits(DrawConstants& c, const Rml::Rectanglei& r, int w, int h)
{
	c.c[0] = (static_cast<float>(r.Left()) + 0.5f) / static_cast<float>(w);
	c.c[1] = (static_cast<float>(r.Top()) + 0.5f) / static_cast<float>(h);
	c.c[2] = (static_cast<float>(r.Right()) - 0.5f) / static_cast<float>(w);
	c.c[3] = (static_cast<float>(r.Bottom()) - 0.5f) / static_cast<float>(h);
}

} // namespace

// 最終色を含む層を作業用の Primary へ写し、Primary の上でフィルターを重ねる
void DawnRmlRenderer::blitLayerToPrimary(int layer)
{
	endPass();
	wgpu::TexelCopyTextureInfo src{};
	src.texture = m_layers[static_cast<std::size_t>(layer)].texture;
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = post(Primary).texture;
	const wgpu::Extent3D size{static_cast<std::uint32_t>(m_targetWidth), static_cast<std::uint32_t>(m_targetHeight), 1};
	m_encoder.CopyTextureToTexture(&src, &dst, &size);
}

// コピーは切り抜き範囲の外に触れないため、層の region だけを透明にする
void DawnRmlRenderer::clearRegion(Pso pso, const Rect& region)
{
	const Rect saved = m_curScissor;
	setScissor(region);
	const Target& dst = (pso == Pso::LayerClear) ? m_layers[static_cast<std::size_t>(m_layerCount - 1)] : post(Tertiary);
	drawFullscreen(pso, dst, m_white->view, baseConstants({0.0f, 0.0f}));
	setScissor(saved);
}

Rml::LayerHandle DawnRmlRenderer::PushLayer()
{
	if (!m_open) { return {}; }
	if (m_layerCount >= kMaxLayers)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi layer stack exhausted");
		++m_layerOverflow;
		return static_cast<Rml::LayerHandle>(m_layerCount - 1);
	}
	ensureLayer(m_layerCount);
	++m_layerCount;
	clearRegion(Pso::LayerClear, m_scissor);
	return static_cast<Rml::LayerHandle>(m_layerCount - 1);
}

void DawnRmlRenderer::PopLayer()
{
	if (!m_open) { return; }
	if (m_layerOverflow > 0) { --m_layerOverflow; }
	else if (m_layerCount > 1) { --m_layerCount; }
}

void DawnRmlRenderer::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blendMode,
                                      Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	if (!m_open) { return; }
	blitLayerToPrimary(static_cast<int>(source));
	renderFilters(filters);
	const bool replace = blendMode == Rml::BlendMode::Replace;
	const Pso pso = replace ? clipped(Pso::LayerReplace, Pso::LayerReplaceClip) : clipped(Pso::LayerOver, Pso::LayerOverClip);
	drawFullscreen(pso, m_layers[static_cast<std::size_t>(destination)], post(Primary).view, baseConstants({0.0f, 0.0f}));
}

Rml::TextureHandle DawnRmlRenderer::SaveLayerAsTexture()
{
	const Rect bounds = m_scissor;
	if (!m_open || bounds.Width() <= 0 || bounds.Height() <= 0) { return {}; }
	endPass();
	auto t = std::make_shared<Texture>();
	wgpu::TextureDescriptor td{};
	td.size = {static_cast<std::uint32_t>(bounds.Width()), static_cast<std::uint32_t>(bounds.Height()), 1};
	td.format = m_device.target().format;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	t->texture = m_ctx.device.CreateTexture(&td);
	t->view = t->texture.CreateView();
	t->width = bounds.Width();
	t->height = bounds.Height();
	t->bindGroup = makeGroup(t->view, m_white->view);
	wgpu::TexelCopyTextureInfo src{};
	src.texture = m_layers[static_cast<std::size_t>(m_layerCount - 1)].texture;
	src.origin = {static_cast<std::uint32_t>(bounds.Left()), static_cast<std::uint32_t>(bounds.Top()), 0};
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = t->texture;
	const wgpu::Extent3D size{static_cast<std::uint32_t>(bounds.Width()), static_cast<std::uint32_t>(bounds.Height()), 1};
	m_encoder.CopyTextureToTexture(&src, &dst, &size);
	return static_cast<Rml::TextureHandle>(m_textures.add(std::move(t)));
}

Rml::CompiledFilterHandle DawnRmlRenderer::SaveLayerAsMaskImage()
{
	if (!m_open) { return {}; }
	blitLayerToPrimary(m_layerCount - 1);
	wgpu::TexelCopyTextureInfo src{};
	src.texture = post(Primary).texture;
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = post(BlendMask).texture;
	const wgpu::Extent3D size{static_cast<std::uint32_t>(m_targetWidth), static_cast<std::uint32_t>(m_targetHeight), 1};
	m_encoder.CopyTextureToTexture(&src, &dst, &size);
	auto f = std::make_shared<RmlFilter>();
	f->type = RmlFilterType::MaskImage;
	return static_cast<Rml::CompiledFilterHandle>(m_filters.add(std::move(f)));
}

Rml::CompiledFilterHandle DawnRmlRenderer::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto f = std::make_shared<RmlFilter>(compileRmlFilter(name, parameters));
	if (f->type == RmlFilterType::Invalid)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
		return {};
	}
	return static_cast<Rml::CompiledFilterHandle>(m_filters.add(std::move(f)));
}

void DawnRmlRenderer::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	m_filters.remove(static_cast<std::uintptr_t>(filter));
}

void DawnRmlRenderer::renderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	for (const Rml::CompiledFilterHandle h : filters)
	{
		const auto f = m_filters.get(static_cast<std::uintptr_t>(h));
		if (!f) { continue; }
		switch (f->type)
		{
		case RmlFilterType::Opacity:     renderOpacity(f->opacity); break;
		case RmlFilterType::ColorMatrix: renderColorMatrix(*f); break;
		case RmlFilterType::MaskImage:   renderMaskImage(); break;
		case RmlFilterType::DropShadow:  renderDropShadow(*f); break;
		case RmlFilterType::Blur:        renderBlur(f->sigma, post(Primary), post(Secondary), m_scissor); break;
		case RmlFilterType::Invalid:     break;
		}
	}
}

void DawnRmlRenderer::renderOpacity(float value)
{
	DrawConstants c = baseConstants({0.0f, 0.0f});
	c.f[0] = value;
	drawFullscreen(Pso::PostCopy, post(Secondary), post(Primary).view, c);
	swapPrimarySecondary();
}

void DawnRmlRenderer::renderColorMatrix(const RmlFilter& f)
{
	DrawConstants c = baseConstants({0.0f, 0.0f});
	std::memcpy(c.colorMatrix, f.colorMatrix.data(), sizeof(c.colorMatrix));
	drawFullscreen(Pso::PostColorMatrix, post(Secondary), post(Primary).view, c);
	swapPrimarySecondary();
}

void DawnRmlRenderer::renderMaskImage()
{
	drawFullscreen(Pso::PostBlendMask, post(Secondary), post(Primary).view, baseConstants({0.0f, 0.0f}), &post(BlendMask).view);
	swapPrimarySecondary();
}

void DawnRmlRenderer::renderDropShadow(const RmlFilter& f)
{
	Target& primary = post(Primary);
	Target& secondary = post(Secondary);
	const Rect window = m_scissor;
	DrawConstants c = baseConstants({0.0f, 0.0f});
	for (int i = 0; i < 4; ++i) { c.color[i] = static_cast<float>(f.color[i]) / 255.0f; }
	setTexCoordLimits(c, window, m_targetWidth, m_targetHeight);
	// 影を (dx, dy) ずらすため、同じ画素から (dx, dy) 戻った位置を読む
	c.d[0] = -f.offset.x * m_scale.x / static_cast<float>(m_targetWidth);
	c.d[1] = -f.offset.y * m_scale.y / static_cast<float>(m_targetHeight);
	drawFullscreen(Pso::PostDropShadow, secondary, primary.view, c);
	if (f.sigma >= 0.5f) { renderBlur(f.sigma, secondary, post(Tertiary), window); }
	drawFullscreen(Pso::PostCopyOver, secondary, primary.view, baseConstants({0.0f, 0.0f}));
	swapPrimarySecondary();
}

// 半分ずつ縮小し、小さい sigma で縦横にぼかしてから元の大きさへ戻す
void DawnRmlRenderer::renderBlur(float sigma, Target& srcDst, Target& temp, const Rect& window)
{
	if (window.Width() <= 0 || window.Height() <= 0) { return; }
	const BlurPlan plan = planBlur(sigma * pixelScale());
	Rect scissor = window;
	blurDownscale(plan.passLevel, srcDst, temp, scissor);
	if (plan.passLevel % 2 == 0)
	{
		setScissor(scissor);
		drawFullscreen(Pso::PostCopy, temp, srcDst.view, baseConstants({0.0f, 0.0f}));
	}
	blurPass(temp, srcDst, {0.0f, 1.0f}, scissor, plan.sigma);
	// 拡大時に範囲外の古い画素を補間しないよう、2 回目の前に周囲 1 画素を透明にする
	const Rect padded = scissor.Extend(1);
	const Rect clearRect = Rect::FromCorners({std::max(0, padded.Left()), std::max(0, padded.Top())},
	                                         {std::min(padded.Right(), m_targetWidth), std::min(padded.Bottom(), m_targetHeight)});
	setScissor(clearRect);
	drawFullscreen(Pso::PostClear, temp, m_white->view, baseConstants({0.0f, 0.0f}));
	blurPass(srcDst, temp, {1.0f, 0.0f}, scissor, plan.sigma);
	blurUpscale(plan.passLevel, temp, srcDst, scissor, window);
	setScissor(m_scissor);
	setViewport(0.0f, 0.0f, static_cast<float>(m_targetWidth), static_cast<float>(m_targetHeight));
}

void DawnRmlRenderer::blurDownscale(int passLevel, Target& srcDst, Target& temp, Rect& scissor)
{
	const int w = m_targetWidth;
	const int h = m_targetHeight;
	setViewport(0.0f, 0.0f, static_cast<float>(w / 2), static_cast<float>(h / 2));
	DrawConstants c = baseConstants({0.0f, 0.0f});
	// 幅が奇数のときは、縮小後の 1 画素が元の 2 画素の中間を読む位置にする
	c.d[2] = (w % 2 == 1) ? (1.0f - 1.0f / static_cast<float>(w)) : 1.0f;
	c.d[3] = (h % 2 == 1) ? (1.0f - 1.0f / static_cast<float>(h)) : 1.0f;
	for (int i = 0; i < passLevel; ++i)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool fromSource = (i % 2 == 0);
		setScissor(scissor);
		drawFullscreen(Pso::PostSample, fromSource ? temp : srcDst, (fromSource ? srcDst : temp).view, c);
	}
	setViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
}

void DawnRmlRenderer::blurPass(Target& from, Target& to, Rml::Vector2f texelOffset, const Rect& scissor, float sigma)
{
	DrawConstants c = baseConstants({0.0f, 0.0f});
	c.b[2] = texelOffset.x / static_cast<float>(m_targetWidth);
	c.b[3] = texelOffset.y / static_cast<float>(m_targetHeight);
	const auto weights = blurWeights(sigma);
	std::copy(weights.begin(), weights.end(), c.weights);
	setTexCoordLimits(c, scissor, m_targetWidth, m_targetHeight);
	setScissor(scissor);
	drawFullscreen(Pso::PostBlur, to, from.view, c);
}

void DawnRmlRenderer::blurUpscale(int passLevel, Target& temp, Target& dst, const Rect& reduced, const Rect& window)
{
	DrawConstants c = baseConstants({0.0f, 0.0f});
	c.d[0] = static_cast<float>(reduced.Left()) / static_cast<float>(m_targetWidth);
	c.d[1] = static_cast<float>(reduced.Top()) / static_cast<float>(m_targetHeight);
	c.d[2] = static_cast<float>(reduced.Width()) / static_cast<float>(m_targetWidth);
	c.d[3] = static_cast<float>(reduced.Height()) / static_cast<float>(m_targetHeight);
	setScissor(window);
	const auto stretchTo = [&](const Rect& r) {
		setViewport(static_cast<float>(r.Left()), static_cast<float>(r.Top()), static_cast<float>(r.Width()), static_cast<float>(r.Height()));
		drawFullscreen(Pso::PostSample, dst, temp.view, c);
	};
	stretchTo(window);
	// 縮小段数に対応する 2 の冪で戻すと位置がずれない。
	// 端が埋まらない場合に備え、窓全体へ引き伸ばした画像を下地にして重ねる。
	const Rect exact = Rect::FromCorners(reduced.p0 * (1 << passLevel), reduced.p1 * (1 << passLevel));
	if (!(exact == window)) { stretchTo(exact); }
}

// ── グラデーション ──

Rml::CompiledShaderHandle DawnRmlRenderer::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto g = std::make_shared<RmlGradient>(compileRmlGradient(name, parameters));
	if (!g->valid)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
		return {};
	}
	return static_cast<Rml::CompiledShaderHandle>(m_gradients.add(std::move(g)));
}

void DawnRmlRenderer::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                                   Rml::Vector2f translation, Rml::TextureHandle)
{
	const auto g = m_gradients.get(static_cast<std::uintptr_t>(shader));
	const auto geo = m_geometries.get(static_cast<std::uintptr_t>(geometry));
	if (!m_open || !g || !geo) { return; }
	DrawConstants c = baseConstants(translation);
	c.ints[0] = static_cast<std::int32_t>(g->func);
	c.ints[1] = g->numStops;
	c.a[2] = g->p.x;
	c.a[3] = g->p.y;
	c.b[0] = g->v.x;
	c.b[1] = g->v.y;
	for (int i = 0; i < g->numStops; ++i)
	{
		c.stopPositions[i] = g->positions[static_cast<std::size_t>(i)];
		for (int k = 0; k < 4; ++k) { c.stopColors[i][k] = g->colors[static_cast<std::size_t>(i)][k]; }
	}
	bindTopLayer();
	drawGeometry(*geo, clipped(Pso::Gradient, Pso::GradientClip), c, m_white->bindGroup);
}

void DawnRmlRenderer::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	m_gradients.remove(static_cast<std::uintptr_t>(shader));
}

} // namespace mitiru::ui_rml::dawn_ui
