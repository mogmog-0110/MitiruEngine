#pragma once

/// @file RmlFastOutline.hpp
/// @brief font-effect: outline(...) を RmlUi の既定と同じ絵のまま速く作る (ADR 0080)
/// @details RmlUi の既定は字ごとに (2w+1)^2 の核を全部なめるので、大きな字に太い縁を付けると最初のフレームで字を作る間
///          ゲームが止まる。核の内側は重み 1 の円なので行ごとの区間の最大 (van Herk) で求め、外側の 1 画素だけ重みを掛ける。
///          書く byte は RmlUi の ConvolutionFilter と同じ。

#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEffectInstancer.h>
#include <RmlUi/Core/FontGlyph.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mitiru::ui_rml
{

/// @brief 核の 1 行。|dx| <= inner は重み 1、fringe は重みが 0 と 1 の間になる dx と重み
struct OutlineKernelRow
{
	int inner = -1;
	std::vector<std::pair<int, float>> fringe;
};

/// @brief RmlUi の FontEffectOutline と同じ核。中心から width までは 1、その外側の 1 画素は width + 1 - 距離
[[nodiscard]] inline std::vector<OutlineKernelRow> outlineKernel(int width)
{
	std::vector<OutlineKernelRow> rows(static_cast<std::size_t>(2 * width + 1));
	for (int dy = -width; dy <= width; ++dy)
	{
		OutlineKernelRow& row = rows[static_cast<std::size_t>(dy + width)];
		for (int dx = -width; dx <= width; ++dx)
		{
			const float distance = Rml::Math::SquareRoot(float(dx * dx + dy * dy));
			if (!(distance > float(width)))
			{
				row.inner = std::max(row.inner, dx);
				continue;
			}
			const float weight = Rml::Math::Max(float(width + 1) - distance, 0.0f);
			if (weight > 0.0f) { row.fringe.emplace_back(dx, weight); }
		}
	}
	return rows;
}

/// @brief van Herk / Gil-Werman 法で、窓 [i - half, i + half] の最大をすべての i について求める。端の外は 0
inline void slidingMax(const std::uint8_t* in, int n, int half, std::uint8_t* out, std::vector<std::uint8_t>& g,
                       std::vector<std::uint8_t>& h)
{
	if (half <= 0)
	{
		std::copy(in, in + n, out);
		return;
	}
	const int k = 2 * half + 1;
	const int padded = n + 2 * half;
	g.assign(static_cast<std::size_t>(padded), 0);
	h.assign(static_cast<std::size_t>(padded), 0);
	auto at = [&](int i) -> std::uint8_t
	{
		const int s = i - half;
		return (s >= 0 && s < n) ? in[s] : std::uint8_t{0};
	};
	for (int i = 0; i < padded; ++i) { g[i] = (i % k == 0) ? at(i) : std::max(g[i - 1], at(i)); }
	for (int i = padded - 1; i >= 0; --i)
	{
		h[i] = (i % k == k - 1 || i == padded - 1) ? at(i) : std::max(h[i + 1], at(i));
	}
	for (int i = 0; i < n; ++i) { out[i] = std::max(h[i], g[i + 2 * half]); }
}

namespace detail
{

/// 左右に width ずつ 0 を足した行と、核の内側の半幅ごとに区間の最大を求めた平面。行の幅は sw + 2 width
struct OutlineSpans
{
	int rowWidth = 0;
	std::vector<std::vector<std::uint8_t>> byHalf;   ///< 添字は半幅。使わない半幅は空
	std::vector<std::uint8_t> padded;                ///< 0 を足しただけの行 (縁の外側の重みを掛ける所)
};

inline OutlineSpans buildOutlineSpans(const std::vector<OutlineKernelRow>& kernel, int width, const std::uint8_t* alpha,
                                      int sw, int sh)
{
	OutlineSpans s;
	s.rowWidth = sw + 2 * width;
	const auto plane = static_cast<std::size_t>(s.rowWidth) * static_cast<std::size_t>(sh);
	s.byHalf.resize(static_cast<std::size_t>(width + 1));
	s.padded.assign(plane, 0);
	for (int y = 0; y < sh; ++y)
	{
		const auto from = static_cast<std::size_t>(y) * static_cast<std::size_t>(sw);
		const auto to = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.rowWidth) + static_cast<std::size_t>(width);
		std::copy(alpha + from, alpha + from + static_cast<std::size_t>(sw), s.padded.begin() + static_cast<std::ptrdiff_t>(to));
	}
	for (const OutlineKernelRow& row : kernel)
	{
		if (row.inner >= 0) { s.byHalf[static_cast<std::size_t>(row.inner)].assign(plane, 0); }
	}
	std::vector<std::uint8_t> g, h;
	for (std::size_t half = 0; half < s.byHalf.size(); ++half)
	{
		if (s.byHalf[half].empty()) { continue; }
		for (int y = 0; y < sh; ++y)
		{
			const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.rowWidth);
			slidingMax(s.padded.data() + at, s.rowWidth, int(half), s.byHalf[half].data() + at, g, h);
		}
	}
	return s;
}

}  // namespace detail

/// @brief 不透明度の平面 alpha (sw × sh) を円の核で膨らませ、(sw + 2w) × (sh + 2w) の dest の a へ書く。dest は RGBA8
inline void dilateOutline(const std::vector<OutlineKernelRow>& kernel, int width, const std::uint8_t* alpha, int sw,
                          int sh, std::uint8_t* dest, int dw, int dh, int destStride)
{
	const detail::OutlineSpans s = detail::buildOutlineSpans(kernel, width, alpha, sw, sh);
	std::vector<float> opacity(static_cast<std::size_t>(dw));
	for (int y = 0; y < dh; ++y)
	{
		std::fill(opacity.begin(), opacity.end(), 0.0f);
		for (int dy = -width; dy <= width; ++dy)
		{
			const int sy = y - width + dy;
			if (sy < 0 || sy >= sh) { continue; }
			const OutlineKernelRow& row = kernel[static_cast<std::size_t>(dy + width)];
			const std::size_t at = static_cast<std::size_t>(sy) * static_cast<std::size_t>(s.rowWidth);
			if (row.inner >= 0)
			{
				const std::uint8_t* span = s.byHalf[static_cast<std::size_t>(row.inner)].data() + at;
				for (int x = 0; x < dw; ++x) { opacity[x] = Rml::Math::Max(opacity[x], float(span[x])); }
			}
			// 行の左に width 個の 0 があるため、dest の x に dx を足した位置が行の添字になる
			for (const auto& [dx, weight] : row.fringe)
			{
				const std::uint8_t* src = s.padded.data() + at;
				for (int x = std::max(0, -dx); x < std::min(dw, s.rowWidth - dx); ++x)
				{
					opacity[x] = Rml::Math::Max(opacity[x], float(src[x + dx]) * weight);
				}
			}
		}
		std::uint8_t* out = dest + static_cast<std::size_t>(y) * static_cast<std::size_t>(destStride) + 3;
		for (int x = 0; x < dw; ++x) { out[static_cast<std::size_t>(x) * 4] = Rml::byte(Rml::Math::Min(255.0f, opacity[x])); }
	}
}

class FastOutlineEffect final : public Rml::FontEffect
{
public:
	explicit FastOutlineEffect(int width) : m_width(width), m_kernel(outlineKernel(width)) { SetLayer(Layer::Back); }

	bool HasUniqueTexture() const override { return true; }

	bool GetGlyphMetrics(Rml::Vector2i& origin, Rml::Vector2i& dimensions, const Rml::FontGlyph& /*glyph*/) const override
	{
		if (dimensions.x * dimensions.y <= 0) { return false; }
		origin -= Rml::Vector2i(m_width);
		dimensions += Rml::Vector2i(2 * m_width);
		return true;
	}

	void GenerateGlyphTexture(Rml::byte* destination, Rml::Vector2i size, int stride,
	                          const Rml::FontGlyph& glyph) const override
	{
		const int sw = glyph.bitmap_dimensions.x;
		const int sh = glyph.bitmap_dimensions.y;
		std::vector<std::uint8_t> alpha(static_cast<std::size_t>(std::max(sw * sh, 0)), 0);
		const bool rgba = glyph.color_format == Rml::ColorFormat::RGBA8;
		for (std::size_t i = 0; i < alpha.size() && glyph.bitmap_data != nullptr; ++i)
		{
			alpha[i] = rgba ? glyph.bitmap_data[i * 4 + 3] : glyph.bitmap_data[i];
		}
		dilateOutline(m_kernel, m_width, alpha.data(), sw, sh, destination, size.x, size.y, stride);
		FillColorValuesFromAlpha(destination, size, stride);
	}

private:
	int m_width;
	std::vector<OutlineKernelRow> m_kernel;
};

class FastOutlineInstancer final : public Rml::FontEffectInstancer
{
public:
	FastOutlineInstancer()
	{
		m_width = RegisterProperty("width", "1px", true).AddParser("length").GetId();
		m_color = RegisterProperty("color", "white", false).AddParser("color").GetId();
		RegisterShorthand("font-effect", "width, color", Rml::ShorthandType::FallThrough);
	}

	Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String& /*name*/,
	                                                   const Rml::PropertyDictionary& properties) override
	{
		const int width = int(properties.GetProperty(m_width)->Get<float>());
		if (width <= 0) { return nullptr; }
		auto effect = Rml::MakeShared<FastOutlineEffect>(width);
		effect->SetColour(properties.GetProperty(m_color)->Get<Rml::Colourb>());
		return effect;
	}

	/// @brief RmlUi の既定の outline と差し替える。Rml::Initialise の後に 1 回呼ぶ
	static void registerInstancer()
	{
		static FastOutlineInstancer instancer;
		Rml::Factory::RegisterFontEffectInstancer("outline", &instancer);
	}

private:
	Rml::PropertyId m_width = Rml::PropertyId::Invalid;
	Rml::PropertyId m_color = Rml::PropertyId::Invalid;
};

}  // namespace mitiru::ui_rml
