#pragma once

/// @file UtilityScore.hpp
/// @brief 技選びの点数付け (utility)。0..1 に揃えた入力を応答曲線に通し、積が最大の技を選ぶ。
/// @details 条件数による低下を Dave Mark の方法で補正する。同点では表の先にある技を選ぶ。
///          ばらつきが必要なときは chooseUtilityWeighted に GameMemory の乱数を渡す。

#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::gameai
{

enum class CurveKind : std::uint8_t
{
	Linear,    ///< slope * (x - shift) + offset
	Power,     ///< slope * (x - shift)^exponent + offset
	Logistic,  ///< 1 / (1 + e^(-slope (x - shift))) * exponent + offset
	Step,      ///< x >= shift なら 1、未満なら 0
};

struct ResponseCurve
{
	CurveKind    kind     = CurveKind::Linear;
	std::uint8_t input    = 0;  ///< 入力の配列の添字
	std::uint8_t pad[2]{};
	float        slope    = 1.0f;
	float        exponent = 1.0f;
	float        shift    = 0.0f;
	float        offset   = 0.0f;

	[[nodiscard]] float evaluate(float x) const noexcept
	{
		const float u = action::saturate(x) - shift;
		float y = 0.0f;
		switch (kind)
		{
		case CurveKind::Linear:   y = slope * u + offset; break;
		case CurveKind::Power:    y = slope * std::pow(action::maxf(0.0f, u), exponent) + offset; break;
		case CurveKind::Logistic: y = exponent / (1.0f + std::exp(-slope * u)) + offset; break;
		case CurveKind::Step:     y = u >= 0.0f ? 1.0f : 0.0f; break;
		}
		return action::saturate(y);
	}
};

inline constexpr int kUtilityMaxConsiderations = 4;

struct UtilityOption
{
	ResponseCurve curves[kUtilityMaxConsiderations]{};
	float         weight = 1.0f;
	std::uint16_t id     = 0;      ///< ゲームが決める技の番号
	std::uint8_t  count  = 0;      ///< 使う curves の数
	std::uint8_t  pad    = 0;
};

static_assert(std::is_trivially_copyable_v<ResponseCurve>);
static_assert(std::is_trivially_copyable_v<UtilityOption>);

/// @brief 1 つの技を 0..weight で採点する。条件が 1 つでも 0 なら 0
[[nodiscard]] inline float scoreUtility(const UtilityOption& o, std::span<const float> inputs) noexcept
{
	if (o.count == 0) { return o.weight; }
	const int   n   = o.count < kUtilityMaxConsiderations ? o.count : kUtilityMaxConsiderations;
	const float mod = 1.0f - 1.0f / static_cast<float>(n);
	float score = 1.0f;
	for (int i = 0; i < n; ++i)
	{
		const ResponseCurve& c = o.curves[i];
		const float x = c.input < inputs.size() ? inputs[c.input] : 0.0f;
		const float v = c.evaluate(x);
		score *= v + (1.0f - v) * mod * v;
	}
	return score * o.weight;
}

/// @return minScore を超える最高点の技の添字。無ければ -1
inline int chooseUtility(std::span<const UtilityOption> options, std::span<const float> inputs, float minScore = 0.0f,
                         float* bestScore = nullptr) noexcept
{
	int   best = -1;
	float top  = minScore;
	for (std::size_t i = 0; i < options.size(); ++i)
	{
		const float s = scoreUtility(options[i], inputs);
		if (s > top) { top = s; best = static_cast<int>(i); }
	}
	if (bestScore != nullptr) { *bestScore = best >= 0 ? top : 0.0f; }
	return best;
}

/// @brief 最高点の band 倍以上から、点数に比例する重みで 1 つ選ぶ。random01 はゲームの乱数 [0, 1)
inline int chooseUtilityWeighted(std::span<const UtilityOption> options, std::span<const float> inputs,
                                 float random01, float band = 0.8f) noexcept
{
	constexpr int kMax = 32;
	float scores[kMax]{};
	const int n   = static_cast<int>(options.size() < kMax ? options.size() : kMax);
	float     top = 0.0f;
	for (int i = 0; i < n; ++i) { scores[i] = scoreUtility(options[static_cast<std::size_t>(i)], inputs); top = action::maxf(top, scores[i]); }
	if (!(top > 0.0f)) { return -1; }
	float sum = 0.0f;
	for (int i = 0; i < n; ++i) { if (scores[i] >= top * band) { sum += scores[i]; } }
	float pick = action::saturate(random01) * sum;
	int   last = -1;
	for (int i = 0; i < n; ++i)
	{
		if (scores[i] < top * band) { continue; }
		last = i;
		if (pick < scores[i]) { return i; }
		pick -= scores[i];
	}
	return last;
}

} // namespace mitiru::gameai
