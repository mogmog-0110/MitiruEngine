// 層・filter・グラデーション。D3D はテクスチャも描画先も左上が原点なので、上下を返す処理は無い。

#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Internal.hpp>

#include <RmlUi/Core/Log.h>

#include <algorithm>
#include <cstring>

namespace mitiru::ui_rml
{

using dx12::RmlPso;
using dx12::RmlRootParam;
using dx12::RmlTargetStack;
using dx12::transition;

namespace
{

// 範囲の端の画素の中心までに留める。外の画素が双線形補間で混ざらないようにするため。
void setTexCoordLimits(dx12::RmlDrawConstants& c, const Rml::Rectanglei& r, int w, int h)
{
	c.texCoordMin[0] = (static_cast<float>(r.Left()) + 0.5f) / static_cast<float>(w);
	c.texCoordMin[1] = (static_cast<float>(r.Top()) + 0.5f) / static_cast<float>(h);
	c.texCoordMax[0] = (static_cast<float>(r.Right()) - 0.5f) / static_cast<float>(w);
	c.texCoordMax[1] = (static_cast<float>(r.Bottom()) - 0.5f) / static_cast<float>(h);
}

} // namespace

float RmlRenderInterfaceDx12::pixelScale() const noexcept
{
	return 0.5f * (m_scale.x + m_scale.y);
}

void RmlRenderInterfaceDx12::blitLayerToPrimary(int layer)
{
	Target& src = m_targets.layer(layer);
	Target& dst = m_targets.post(RmlTargetStack::Primary);
	if (src.samples > 1)
	{
		transition(m_list.Get(), src, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		transition(m_list.Get(), dst, D3D12_RESOURCE_STATE_RESOLVE_DEST);
		m_list->ResolveSubresource(dst.resource.Get(), 0, src.resource.Get(), 0, dx12::kRmlColorFormat);
	}
	else
	{
		transition(m_list.Get(), src, D3D12_RESOURCE_STATE_COPY_SOURCE);
		transition(m_list.Get(), dst, D3D12_RESOURCE_STATE_COPY_DEST);
		m_list->CopyResource(dst.resource.Get(), src.resource.Get());
	}
	transition(m_list.Get(), src, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

Rml::LayerHandle RmlRenderInterfaceDx12::PushLayer()
{
	if (!m_frameOpen) { return {}; }
	const int index = m_targets.push();
	if (index < 0)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi layer stack exhausted or allocation failed");
		return static_cast<Rml::LayerHandle>(m_targets.topIndex());
	}
	bindTopLayer();
	const D3D12_RECT rect = { m_scissor.Left(), m_scissor.Top(), m_scissor.Right(), m_scissor.Bottom() };
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_list->ClearRenderTargetView(m_targets.layer(index).rtv, clear, 1, &rect);
	return static_cast<Rml::LayerHandle>(index);
}

void RmlRenderInterfaceDx12::PopLayer()
{
	if (!m_frameOpen) { return; }
	m_targets.pop();
	bindTopLayer();
}

void RmlRenderInterfaceDx12::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blendMode,
                                             Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	if (!m_frameOpen) { return; }
	blitLayerToPrimary(static_cast<int>(source));
	renderFilters(filters);
	const bool replace = blendMode == Rml::BlendMode::Replace;
	const RmlPso pso = replace ? clipped(RmlPso::LayerReplace, RmlPso::LayerReplaceClip)
	                           : clipped(RmlPso::LayerOver, RmlPso::LayerOverClip);
	drawFullscreen(pso, m_targets.layer(static_cast<int>(destination)), m_targets.post(RmlTargetStack::Primary),
	               baseConstants({ 0.0f, 0.0f }));
	bindTopLayer();
}

Rml::TextureHandle RmlRenderInterfaceDx12::SaveLayerAsTexture()
{
	const Rect bounds = m_scissor;
	if (!m_frameOpen || bounds.Width() <= 0 || bounds.Height() <= 0) { return {}; }
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = static_cast<UINT64>(bounds.Width());
	desc.Height = static_cast<UINT>(bounds.Height());
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = dx12::kRmlColorFormat;
	desc.SampleDesc.Count = 1;
	gfx::GpuResource tex;
	if (FAILED(gfx::createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, tex))) { return {}; }
	blitLayerToPrimary(m_targets.topIndex());
	Target& primary = m_targets.post(RmlTargetStack::Primary);
	transition(m_list.Get(), primary, D3D12_RESOURCE_STATE_COPY_SOURCE);
	dx12::copyRegion(m_list.Get(), tex.Get(), primary.resource.Get(), bounds.Left(), bounds.Top(), bounds.Width(), bounds.Height());
	transition(m_list.Get(), tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return addTexture(std::move(tex), bounds.Width(), bounds.Height(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

Rml::CompiledFilterHandle RmlRenderInterfaceDx12::SaveLayerAsMaskImage()
{
	if (!m_frameOpen) { return {}; }
	blitLayerToPrimary(m_targets.topIndex());
	Target& primary = m_targets.post(RmlTargetStack::Primary);
	Target& mask = m_targets.post(RmlTargetStack::BlendMask);
	transition(m_list.Get(), primary, D3D12_RESOURCE_STATE_COPY_SOURCE);
	transition(m_list.Get(), mask, D3D12_RESOURCE_STATE_COPY_DEST);
	m_list->CopyResource(mask.resource.Get(), primary.resource.Get());
	auto f = std::make_unique<RmlFilter>();
	f->type = RmlFilterType::MaskImage;
	return static_cast<Rml::CompiledFilterHandle>(m_filters.add(std::move(f)));
}

Rml::CompiledFilterHandle RmlRenderInterfaceDx12::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto f = std::make_unique<RmlFilter>(compileRmlFilter(name, parameters));
	if (f->type == RmlFilterType::Invalid)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
		return {};
	}
	return static_cast<Rml::CompiledFilterHandle>(m_filters.add(std::move(f)));
}

void RmlRenderInterfaceDx12::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	(void)m_filters.take(filter);
}

void RmlRenderInterfaceDx12::renderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	for (const Rml::CompiledFilterHandle h : filters)
	{
		const RmlFilter* f = m_filters.get(h);
		if (f == nullptr) { continue; }
		switch (f->type)
		{
		case RmlFilterType::Opacity:     renderOpacity(f->opacity); break;
		case RmlFilterType::ColorMatrix: renderColorMatrix(*f); break;
		case RmlFilterType::MaskImage:   renderMaskImage(); break;
		case RmlFilterType::DropShadow:  renderDropShadow(*f); break;
		case RmlFilterType::Blur:
			renderBlur(f->sigma, m_targets.post(RmlTargetStack::Primary), m_targets.post(RmlTargetStack::Secondary), m_scissor);
			break;
		case RmlFilterType::Invalid:     break;
		}
	}
}

void RmlRenderInterfaceDx12::renderOpacity(float value)
{
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	c.opacity = value;
	drawFullscreen(RmlPso::PostCopy, m_targets.post(RmlTargetStack::Secondary), m_targets.post(RmlTargetStack::Primary), c);
	m_targets.swapPrimarySecondary();
}

void RmlRenderInterfaceDx12::renderColorMatrix(const RmlFilter& f)
{
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	std::memcpy(c.colorMatrix, f.colorMatrix.data(), sizeof(c.colorMatrix));
	drawFullscreen(RmlPso::PostColorMatrix, m_targets.post(RmlTargetStack::Secondary), m_targets.post(RmlTargetStack::Primary), c);
	m_targets.swapPrimarySecondary();
}

void RmlRenderInterfaceDx12::renderMaskImage()
{
	Target& mask = m_targets.post(RmlTargetStack::BlendMask);
	transition(m_list.Get(), mask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	bindTexture(RmlRootParam::Mask, mask.srv);
	drawFullscreen(RmlPso::PostBlendMask, m_targets.post(RmlTargetStack::Secondary), m_targets.post(RmlTargetStack::Primary),
	               baseConstants({ 0.0f, 0.0f }));
	m_targets.swapPrimarySecondary();
}

void RmlRenderInterfaceDx12::renderDropShadow(const RmlFilter& f)
{
	Target& primary = m_targets.post(RmlTargetStack::Primary);
	Target& secondary = m_targets.post(RmlTargetStack::Secondary);
	const Rect window = m_scissor;
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	for (int i = 0; i < 4; ++i) { c.color[i] = static_cast<float>(f.color[i]) / 255.0f; }
	setTexCoordLimits(c, window, primary.width, primary.height);
	// 影を (dx, dy) ずらして見せるには、同じ画素で (dx, dy) 戻った所を読む。
	c.uvOffset[0] = -f.offset.x * m_scale.x / static_cast<float>(primary.width);
	c.uvOffset[1] = -f.offset.y * m_scale.y / static_cast<float>(primary.height);
	drawFullscreen(RmlPso::PostDropShadow, secondary, primary, c);
	if (f.sigma >= 0.5f) { renderBlur(f.sigma, secondary, m_targets.post(RmlTargetStack::Tertiary), window); }
	drawFullscreen(RmlPso::PostCopyOver, secondary, primary, baseConstants({ 0.0f, 0.0f }));
	m_targets.swapPrimarySecondary();
}

// ぼかし: 半分ずつ縮めて小さい sigma で縦横に掛け、元の大きさへ戻す。
void RmlRenderInterfaceDx12::renderBlur(float sigma, Target& srcDst, Target& temp, const Rect& window)
{
	if (window.Width() <= 0 || window.Height() <= 0) { return; }
	const BlurPlan plan = planBlur(sigma * pixelScale());
	Rect scissor = window;
	blurDownscale(plan.passLevel, srcDst, temp, scissor);
	if (plan.passLevel % 2 == 0)
	{
		setScissor(scissor);
		drawFullscreen(RmlPso::PostCopy, temp, srcDst, baseConstants({ 0.0f, 0.0f }));
	}
	blurPass(temp, srcDst, { 0.0f, 1.0f }, scissor, plan.sigma);
	// 2 回目の前に周り 1 画素を透明にしておく。拡大の補間が範囲の外の古い画素を拾わないように。
	bindTarget(temp, false);
	const Rect padded = scissor.Extend(1);
	const D3D12_RECT clearRect = { std::max(0, padded.Left()), std::max(0, padded.Top()), padded.Right(), padded.Bottom() };
	const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_list->ClearRenderTargetView(temp.rtv, transparent, 1, &clearRect);
	blurPass(srcDst, temp, { 1.0f, 0.0f }, scissor, plan.sigma);
	blurUpscale(plan.passLevel, temp, srcDst, scissor, window);
	setScissor(m_scissor);
	setViewport(0.0f, 0.0f, static_cast<float>(m_targets.width()), static_cast<float>(m_targets.height()));
}

void RmlRenderInterfaceDx12::blurDownscale(int passLevel, Target& srcDst, Target& temp, Rect& scissor)
{
	const int w = srcDst.width, h = srcDst.height;
	setViewport(0.0f, 0.0f, static_cast<float>(w / 2), static_cast<float>(h / 2));
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	// 幅が奇数の時は、縮めた 1 画素が元の 2 画素のちょうど中間を読む位置にする。
	c.uvScale[0] = (w % 2 == 1) ? (1.0f - 1.0f / static_cast<float>(w)) : 1.0f;
	c.uvScale[1] = (h % 2 == 1) ? (1.0f - 1.0f / static_cast<float>(h)) : 1.0f;
	for (int i = 0; i < passLevel; ++i)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool fromSource = (i % 2 == 0);
		setScissor(scissor);
		drawFullscreen(RmlPso::PostSample, fromSource ? temp : srcDst, fromSource ? srcDst : temp, c);
	}
	setViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
}

void RmlRenderInterfaceDx12::blurPass(Target& from, Target& to, Rml::Vector2f texelOffset, const Rect& scissor, float sigma)
{
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	c.texelOffset[0] = texelOffset.x / static_cast<float>(from.width);
	c.texelOffset[1] = texelOffset.y / static_cast<float>(from.height);
	const auto weights = blurWeights(sigma);
	std::copy(weights.begin(), weights.end(), c.weights);
	setTexCoordLimits(c, scissor, from.width, from.height);
	setScissor(scissor);
	drawFullscreen(RmlPso::PostBlur, to, from, c);
}

void RmlRenderInterfaceDx12::blurUpscale(int passLevel, Target& temp, Target& dst, const Rect& reduced, const Rect& window)
{
	dx12::RmlDrawConstants c = baseConstants({ 0.0f, 0.0f });
	c.uvOffset[0] = static_cast<float>(reduced.Left()) / static_cast<float>(temp.width);
	c.uvOffset[1] = static_cast<float>(reduced.Top()) / static_cast<float>(temp.height);
	c.uvScale[0] = static_cast<float>(reduced.Width()) / static_cast<float>(temp.width);
	c.uvScale[1] = static_cast<float>(reduced.Height()) / static_cast<float>(temp.height);
	setScissor(window);
	const auto stretchTo = [&](const Rect& r) {
		setViewport(static_cast<float>(r.Left()), static_cast<float>(r.Top()), static_cast<float>(r.Width()), static_cast<float>(r.Height()));
		drawFullscreen(RmlPso::PostSample, dst, temp, c);
	};
	stretchTo(window);
	// 縮めた段数ちょうどの 2 の冪で戻した方が位置がぶれない。ただ端が埋まらないことがあるので、
	// 窓いっぱいへ引き伸ばした絵を下地にしてその上に重ねる。
	const Rect exact = Rect::FromCorners(reduced.p0 * (1 << passLevel), reduced.p1 * (1 << passLevel));
	if (!(exact == window)) { stretchTo(exact); }
}

// グラデーション ──────────────────────────────────────────────────

Rml::CompiledShaderHandle RmlRenderInterfaceDx12::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto g = std::make_unique<RmlGradient>(compileRmlGradient(name, parameters));
	if (!g->valid)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
		return {};
	}
	return static_cast<Rml::CompiledShaderHandle>(m_gradients.add(std::move(g)));
}

void RmlRenderInterfaceDx12::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                                          Rml::Vector2f translation, Rml::TextureHandle)
{
	const RmlGradient* g = m_gradients.get(shader);
	Geometry* geo = m_geometries.get(geometry);
	if (!m_frameOpen || g == nullptr || geo == nullptr) { return; }
	dx12::RmlDrawConstants c = baseConstants(translation);
	c.func = static_cast<std::int32_t>(g->func);
	c.numStops = g->numStops;
	c.gradP[0] = g->p.x;
	c.gradP[1] = g->p.y;
	c.gradV[0] = g->v.x;
	c.gradV[1] = g->v.y;
	for (int i = 0; i < g->numStops; ++i)
	{
		c.stopPositions[i] = g->positions[static_cast<std::size_t>(i)];
		for (int k = 0; k < 4; ++k) { c.stopColors[i][k] = g->colors[static_cast<std::size_t>(i)][k]; }
	}
	bindTopLayer();
	setPso(clipped(RmlPso::Gradient, RmlPso::GradientClip));
	pushConstants(c);
	(void)drawGeometry(*geo);
}

void RmlRenderInterfaceDx12::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	(void)m_gradients.take(shader);
}

} // namespace mitiru::ui_rml
