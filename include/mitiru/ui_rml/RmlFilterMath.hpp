#pragma once
// RmlUi の filter / decorator が名前と引数で指定する効果を、描画層が GPU に渡す数値へ直す部分。
// GPU に触らないので単体で確かめられる。式は CSS Filter Effects の定義と RmlUi 同梱の
// GL3 backend が使っているものに合わせてある (backend を差し替えても同じ見た目になるように)。

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace mitiru::ui_rml
{

enum class RmlFilterType : int { Invalid = 0, Opacity, Blur, DropShadow, ColorMatrix, MaskImage };

struct RmlFilter
{
	RmlFilterType type = RmlFilterType::Invalid;
	float opacity = 1.0f;
	float sigma = 0.0f;
	Rml::Vector2f offset = { 0.0f, 0.0f };
	Rml::ColourbPremultiplied color;
	Rml::Matrix4f colorMatrix = Rml::Matrix4f::Identity();
};

namespace detail
{

[[nodiscard]] inline Rml::Matrix4f saturateMatrix(float v)
{
	return Rml::Matrix4f::FromRows(
		{ 0.213f + 0.787f * v, 0.715f - 0.715f * v, 0.072f - 0.072f * v, 0.0f },
		{ 0.213f - 0.213f * v, 0.715f + 0.285f * v, 0.072f - 0.072f * v, 0.0f },
		{ 0.213f - 0.213f * v, 0.715f - 0.715f * v, 0.072f + 0.928f * v, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] inline Rml::Matrix4f hueRotateMatrix(float radians)
{
	const float s = std::sin(radians), c = std::cos(radians);
	return Rml::Matrix4f::FromRows(
		{ 0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.0f },
		{ 0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.0f },
		{ 0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] inline Rml::Matrix4f mixMatrix(const Rml::Vector3f& r, const Rml::Vector3f& g, const Rml::Vector3f& b, float keep)
{
	return Rml::Matrix4f::FromRows(
		{ r.x + keep, r.y, r.z, 0.0f },
		{ g.x, g.y + keep, g.z, 0.0f },
		{ b.x, b.y, b.z + keep, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f });
}

// 色だけを変える filter (明るさ・コントラスト・反転・灰色・セピア・色相・彩度)。
// 乗算済みアルファのまま掛けるので、定数項は 4 列目に置いて shader 側でアルファを掛ける。
[[nodiscard]] inline bool colorMatrixFor(const Rml::String& name, float value, Rml::Matrix4f& out)
{
	if (name == "brightness") { out = Rml::Matrix4f::Diag(value, value, value, 1.0f); return true; }
	if (name == "contrast")
	{
		const float gray = 0.5f - 0.5f * value;
		out = Rml::Matrix4f::Diag(value, value, value, 1.0f);
		out.SetColumn(3, Rml::Vector4f(gray, gray, gray, 1.0f));
		return true;
	}
	if (name == "invert")
	{
		const float v = std::clamp(value, 0.0f, 1.0f), k = 1.0f - 2.0f * v;
		out = Rml::Matrix4f::Diag(k, k, k, 1.0f);
		out.SetColumn(3, Rml::Vector4f(v, v, v, 1.0f));
		return true;
	}
	if (name == "grayscale")
	{
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		out = mixMatrix(gray, gray, gray, 1.0f - value);
		return true;
	}
	if (name == "sepia")
	{
		out = mixMatrix(value * Rml::Vector3f(0.393f, 0.769f, 0.189f), value * Rml::Vector3f(0.349f, 0.686f, 0.168f),
		                value * Rml::Vector3f(0.272f, 0.534f, 0.131f), 1.0f - value);
		return true;
	}
	if (name == "hue-rotate") { out = hueRotateMatrix(value); return true; }
	if (name == "saturate")   { out = saturateMatrix(value); return true; }
	return false;
}

} // namespace detail

[[nodiscard]] inline RmlFilter compileRmlFilter(const Rml::String& name, const Rml::Dictionary& params)
{
	RmlFilter f;
	if (name == "opacity")
	{
		f.type = RmlFilterType::Opacity;
		f.opacity = Rml::Get(params, "value", 1.0f);
	}
	else if (name == "blur")
	{
		f.type = RmlFilterType::Blur;
		f.sigma = Rml::Get(params, "sigma", 1.0f);
	}
	else if (name == "drop-shadow")
	{
		f.type = RmlFilterType::DropShadow;
		f.sigma = Rml::Get(params, "sigma", 0.0f);
		f.color = Rml::Get(params, "color", Rml::Colourb()).ToPremultiplied();
		f.offset = Rml::Get(params, "offset", Rml::Vector2f(0.0f));
	}
	else if (detail::colorMatrixFor(name, Rml::Get(params, "value", 1.0f), f.colorMatrix))
	{
		f.type = RmlFilterType::ColorMatrix;
	}
	return f;
}

// 大きなぼかしは縮小を重ねてから小さく掛ける。1 回の畳み込みの sigma は 3 までに抑え、
// 残りを 2 倍ずつの縮小段数に回す (縮小は双線形の平均なので、それ自体がぼかしになる)。
struct BlurPlan
{
	int passLevel = 0;   ///< 半分に縮める回数
	float sigma = 0.0f;  ///< 縮めた解像度で掛ける sigma
};

inline constexpr int kBlurSize = 7;
inline constexpr int kBlurWeights = (kBlurSize + 1) / 2;

[[nodiscard]] inline BlurPlan planBlur(float desiredSigma)
{
	constexpr int kMaxPasses = 10;
	constexpr float kMaxSinglePassSigma = 3.0f;
	BlurPlan p;
	p.passLevel = std::clamp(Rml::Math::Log2(static_cast<int>(desiredSigma * (2.0f / kMaxSinglePassSigma))), 0, kMaxPasses);
	p.sigma = std::clamp(desiredSigma / static_cast<float>(1 << p.passLevel), 0.0f, kMaxSinglePassSigma);
	return p;
}

/// 中心から片側の重み (中心 1 + 両側 2 回ずつ使うので、合計がちょうど 1 になるよう正規化する)。
[[nodiscard]] inline std::array<float, kBlurWeights> blurWeights(float sigma)
{
	std::array<float, kBlurWeights> w{};
	float total = 0.0f;
	for (int i = 0; i < kBlurWeights; ++i)
	{
		w[i] = (std::fabs(sigma) < 0.1f)
			? (i == 0 ? 1.0f : 0.0f)
			: std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma)) / (std::sqrt(2.0f * Rml::Math::RMLUI_PI) * sigma);
		total += (i == 0 ? 1.0f : 2.0f) * w[i];
	}
	for (float& x : w) { x /= total; }
	return w;
}

// グラデーション decorator。GL3 backend と同じく 16 色まで (それ以上は捨てる)。
inline constexpr int kMaxGradientStops = 16;

enum class RmlGradientFunc : int { Linear = 0, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic };

struct RmlGradient
{
	bool valid = false;
	RmlGradientFunc func = RmlGradientFunc::Linear;
	Rml::Vector2f p = { 0.0f, 0.0f };
	Rml::Vector2f v = { 0.0f, 0.0f };
	int numStops = 0;
	std::array<float, kMaxGradientStops> positions{};
	std::array<Rml::Colourf, kMaxGradientStops> colors{};
};

namespace detail
{

inline void applyStops(RmlGradient& g, const Rml::Dictionary& params)
{
	const auto it = params.find("color_stop_list");
	if (it == params.end() || it->second.GetType() != Rml::Variant::COLORSTOPLIST) { return; }
	const auto& stops = it->second.GetReference<Rml::ColorStopList>();
	g.numStops = std::min(static_cast<int>(stops.size()), kMaxGradientStops);
	for (int i = 0; i < g.numStops; ++i)
	{
		const Rml::ColorStop& s = stops[static_cast<std::size_t>(i)];
		g.positions[static_cast<std::size_t>(i)] = s.position.number;
		for (int c = 0; c < 4; ++c) { g.colors[static_cast<std::size_t>(i)][c] = static_cast<float>(s.color[c]) / 255.0f; }
	}
	g.valid = g.numStops > 0;
}

[[nodiscard]] inline RmlGradientFunc withRepeat(RmlGradientFunc f, bool repeating)
{
	return repeating ? static_cast<RmlGradientFunc>(static_cast<int>(f) + 3) : f;
}

} // namespace detail

[[nodiscard]] inline RmlGradient compileRmlGradient(const Rml::String& name, const Rml::Dictionary& params)
{
	RmlGradient g;
	const bool repeating = Rml::Get(params, "repeating", false);
	if (name == "linear-gradient")
	{
		g.func = detail::withRepeat(RmlGradientFunc::Linear, repeating);
		g.p = Rml::Get(params, "p0", Rml::Vector2f(0.0f));
		g.v = Rml::Get(params, "p1", Rml::Vector2f(0.0f)) - g.p;
	}
	else if (name == "radial-gradient")
	{
		g.func = detail::withRepeat(RmlGradientFunc::Radial, repeating);
		g.p = Rml::Get(params, "center", Rml::Vector2f(0.0f));
		g.v = Rml::Vector2f(1.0f) / Rml::Get(params, "radius", Rml::Vector2f(1.0f));
	}
	else if (name == "conic-gradient")
	{
		g.func = detail::withRepeat(RmlGradientFunc::Conic, repeating);
		g.p = Rml::Get(params, "center", Rml::Vector2f(0.0f));
		const float angle = Rml::Get(params, "angle", 0.0f);
		g.v = { std::cos(angle), std::sin(angle) };
	}
	else
	{
		return g;
	}
	detail::applyStops(g, params);
	return g;
}

} // namespace mitiru::ui_rml
