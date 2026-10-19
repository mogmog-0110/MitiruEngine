#pragma once

/// @file ToonStyleGpu.hpp
/// @brief ToonStyle を GPU の定数へ詰める式と、フレームの中の線の印の表 (ADR 0079)
/// @details 印は法線 RT の a の上位 3 bit に詰める描画ごとの番号で、0 は場面の値 (setOutline* と outlineDarken) を表す。
///          輪郭線と止め絵は画素の印から、その描画の線の色・太さ・距離での細り方と、色のまま残すかを引く。
///          同じ線の指定は同じ印を使い回すので、1 フレームに違う線の指定は 7 個まで使える。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <sgc/types/Color.hpp>

#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/ToonStyle.hpp>

namespace mitiru::render
{

inline constexpr int kToonSlotCount = 8;   ///< 印の数。0 は場面

/// @brief 段の定数 (96 byte)。前方の描画の CbDrawEx と clod の CB に同じ並びで載せる
struct ToonRampGpu
{
	float params[4] = {};       ///< x = 段の数 (0 は前方では場面のまま、clod では段なし) y = 境の半幅 z = 落ちた影で下げる段 w = 印
	float thresholds[4] = {};   ///< xyz = 段の境
	float tints[4][4] = {};     ///< 暗い段から順に掛ける色 (線形)
};
static_assert(sizeof(ToonRampGpu) == 96);

/// @brief 縁の帯とつや (48 byte、CbDrawEx の ToonRamp の後ろ)
struct ToonAccentGpu
{
	float rim[4] = {};     ///< rgb = 帯の色 (線形) w = N·V の上限 (0 で帯なし)
	float gloss[4] = {};   ///< rgb = つやの色 (線形) w = N·H の下限 (0 でつやなし)
	float rimLimit[4] = {};   ///< x = 帯にする N·L の上限、yz = 帯・つやの半幅、w = つやの強さ
};

/// @brief 解析的な金属反射 (80 byte、CbDrawEx の ToonAccent の後ろ)
struct ToonMetalGpu
{
	float a[4] = {};         ///< xyz = amount, tint, shadowFollow、w = bands
	float b[4] = {};         ///< xy = horizonHeight, horizonSoft
	float sky[4] = {};       ///< rgb = 真上の色 (線形)
	float horizon[4] = {};   ///< rgb = 地平の色 (線形)
	float ground[4] = {};    ///< rgb = 地面の色 (線形)
};
static_assert(sizeof(ToonMetalGpu) == 80);

[[nodiscard]] inline float finiteToonMetalValue(float value, float lo, float hi) noexcept
{
	return std::isfinite(value) ? std::clamp(value, lo, hi) : 0.0f;
}

[[nodiscard]] inline ToonMetalGpu makeToonMetalGpu(const ToonStyle& s) noexcept
{
	ToonMetalGpu g;
	const float amount = finiteToonMetalValue(s.metal.amount, 0.0f, 1.0f);
	if (amount <= 0.0f) { return g; }
	g.a[0] = amount;
	g.a[1] = finiteToonMetalValue(s.metal.tint, 0.0f, 1.0f);
	g.a[2] = finiteToonMetalValue(s.metal.shadowFollow, 0.0f, 1.0f);
	g.a[3] = static_cast<float>(std::min<std::uint32_t>(s.metal.bands, 8u));
	g.b[0] = finiteToonMetalValue(s.metal.horizonHeight, -0.9f, 0.9f);
	g.b[1] = finiteToonMetalValue(s.metal.horizonSoft, 0.0f, 1.0f);
	const auto color = [](const float c[3]) {
		return linearRgb(std::isfinite(c[0]) ? c[0] : 0.0f,
		                 std::isfinite(c[1]) ? c[1] : 0.0f,
		                 std::isfinite(c[2]) ? c[2] : 0.0f);
	};
	const auto sky = color(s.metal.sky);
	const auto horizon = color(s.metal.horizon);
	const auto ground = color(s.metal.ground);
	for (int i = 0; i < 3; ++i)
	{
		g.sky[i] = sky[i];
		g.horizon[i] = horizon[i];
		g.ground[i] = ground[i];
	}
	return g;
}

[[nodiscard]] inline ToonRampGpu makeToonRampGpu(const ToonStyle& s, int slot) noexcept
{
	ToonRampGpu g;
	g.params[0] = static_cast<float>(std::min<std::uint32_t>(s.bands, 4u));
	g.params[1] = std::clamp(s.softness, 0.0f, 1.0f) * 0.5f;
	g.params[2] = static_cast<float>(std::min<std::uint32_t>(s.shadowSteps, 3u));
	g.params[3] = static_cast<float>(slot);
	float prev = 0.0f;
	for (int i = 0; i < 3; ++i)
	{
		// 昇順でない境は前の境へ寄せる。段が入れ替わると暗い段の色が明るい側に出る
		prev = std::max(prev, std::clamp(s.thresholds[i], 0.0f, 1.0f));
		g.thresholds[i] = prev;
	}
	for (int k = 0; k < 4; ++k)
	{
		const auto c = linearRgb(s.bandTint[k][0], s.bandTint[k][1], s.bandTint[k][2]);
		for (int i = 0; i < 3; ++i) { g.tints[k][i] = c[i]; }
		g.tints[k][3] = 1.0f;
	}
	return g;
}

[[nodiscard]] inline ToonAccentGpu makeToonAccentGpu(const ToonStyle& s) noexcept
{
	ToonAccentGpu a;
	const auto rim = linearRgb(s.rimColor[0], s.rimColor[1], s.rimColor[2]);
	const auto gloss = linearRgb(s.glossColor[0], s.glossColor[1], s.glossColor[2]);
	for (int i = 0; i < 3; ++i)
	{
		a.rim[i] = rim[i];
		a.gloss[i] = gloss[i];
	}
	a.rim[3] = std::clamp(s.rimMaxNdotV, 0.0f, 1.0f);
	a.gloss[3] = std::clamp(s.glossMinNdotH, 0.0f, 1.0f);
	a.rimLimit[0] = std::clamp(s.rimMaxNdotL, 0.0f, 1.0f);
	a.rimLimit[1] = finiteToonMetalValue(s.rimSoftness, 0.0f, 1.0f) * 0.5f;
	a.rimLimit[2] = finiteToonMetalValue(s.glossSoftness, 0.0f, 1.0f) * 0.5f;
	a.rimLimit[3] = finiteToonMetalValue(s.glossStrength, 0.0f, 8.0f);
	return a;
}

/// @brief 場面の段 (setToonRamp と toon3D の影の色) を、段の境と段ごとの色の形にした値 (clod が使う)
/// @details 前方の描画の toonRamp / toonTone と同じ段になる。bands 0 は段なし (巻き込んだ lambert で 2 色の間を滑らかに)
[[nodiscard]] inline ToonRampGpu sceneToonRampGpu(int bands, float softness, const sgc::Colorf& shadowLinear,
                                                  const sgc::Colorf& midLinear) noexcept
{
	ToonRampGpu g;
	const float sh[3] = {shadowLinear.r, shadowLinear.g, shadowLinear.b};
	const float mid[3] = {midLinear.r, midLinear.g, midLinear.b};
	const auto tone = [&](float t, int i) {
		if (bands < 3) { return sh[i] + (1.0f - sh[i]) * t; }
		return (t < 0.5f) ? sh[i] + (mid[i] - sh[i]) * t * 2.0f : mid[i] + (1.0f - mid[i]) * (t * 2.0f - 1.0f);
	};
	const int n = (bands <= 0) ? 0 : std::clamp(bands, 2, 4);
	g.params[0] = static_cast<float>(n);
	g.params[1] = (n == 0) ? std::max(softness, 0.001f) : softness * 0.5f;
	for (int k = 1; k < 4; ++k) { g.thresholds[k - 1] = (k < n) ? static_cast<float>(k) / static_cast<float>(n) : 1.0f; }
	const int last = std::max(n - 1, 1);
	for (int k = 0; k < 4; ++k)
	{
		const float t = std::min(static_cast<float>(k) / static_cast<float>(last), 1.0f);
		for (int i = 0; i < 3; ++i) { g.tints[k][i] = tone(t, i); }
		g.tints[k][3] = 1.0f;
	}
	return g;
}

/// @brief 1 個の印が持つ線の指定。太さは 1080p の画素
struct ToonLineSlot
{
	float color[3] = {0.1f, 0.08f, 0.06f};
	float mix = 1.0f;
	float width = 0.0f;
	float farWidth = 0.0f;
	float farDistance = 0.0f;
	float fadeStart = 0.0f;
	float fadeEnd = 0.0f;
	float fadeMin = 0.0f;   ///< 消えきる距離より遠くで残す不透明度 (場面の線の outlineFadeMin。描画ごとの線は 0)
	std::uint32_t flags = 0;
};

[[nodiscard]] inline bool sameLine(const ToonLineSlot& a, const ToonLineSlot& b) noexcept
{
	return std::memcmp(&a, &b, sizeof(ToonLineSlot)) == 0;
}

[[nodiscard]] inline ToonLineSlot toonLineOf(const ToonStyle& s) noexcept
{
	ToonLineSlot l;
	for (int i = 0; i < 3; ++i) { l.color[i] = std::clamp(s.outlineColor[i], 0.0f, 1.0f); }
	l.mix = std::clamp(s.outlineMix, 0.0f, 1.0f);
	l.width = std::max(s.outlineWidthPx, 0.0f);
	l.farWidth = (s.outlineFarWidthPx > 0.0f) ? s.outlineFarWidthPx : l.width;
	l.farDistance = (s.outlineFarWidthPx > 0.0f) ? std::max(s.outlineFarDistance, 0.0f) : 0.0f;
	l.fadeStart = std::max(s.outlineFadeStart, 0.0f);
	l.fadeEnd = std::max(s.outlineFadeEnd, 0.0f);
	l.flags = s.flags;
	return l;
}

/// @brief 目から distance の所の線の太さ (線の指定と同じ単位)
[[nodiscard]] inline float toonLineWidthAt(const ToonLineSlot& l, float distance) noexcept
{
	return (l.farDistance > 0.0f && distance > l.farDistance) ? l.farWidth : l.width;
}

/// @brief 目から distance の所の線の不透明度 (線の色の割合 mix は含まない)
[[nodiscard]] inline float toonLineAlphaAt(const ToonLineSlot& l, float distance) noexcept
{
	if (!(l.fadeEnd > l.fadeStart)) { return 1.0f; }
	const float t = std::clamp((distance - l.fadeStart) / (l.fadeEnd - l.fadeStart), 0.0f, 1.0f);
	return 1.0f + (l.fadeMin - 1.0f) * t;
}

/// @brief フレームの中の印の表。印 0 は場面の線で、描くときに埋める
class ToonSlotTable
{
public:
	void reset() noexcept
	{
		m_count = 1;
		m_overflow = false;
	}

	/// @brief style の線と flags の印。同じ指定は同じ印。7 個を超えたら 0 (場面の線) を返し、overflowed が立つ
	[[nodiscard]] int slotFor(const ToonStyle& style) noexcept
	{
		const ToonLineSlot line = toonLineOf(style);
		for (int i = 1; i < m_count; ++i)
		{
			if (sameLine(m_slots[static_cast<std::size_t>(i)], line)) { return i; }
		}
		if (m_count >= kToonSlotCount)
		{
			m_overflow = true;
			return 0;
		}
		m_slots[static_cast<std::size_t>(m_count)] = line;
		return m_count++;
	}

	[[nodiscard]] int count() const noexcept { return m_count; }
	[[nodiscard]] bool overflowed() const noexcept { return m_overflow; }
	[[nodiscard]] const ToonLineSlot& slot(int i) const noexcept { return m_slots[static_cast<std::size_t>(i)]; }

	/// @brief 止め絵で色のまま残す印の集合 (bit i が印 i)
	[[nodiscard]] std::uint32_t keepMask() const noexcept
	{
		std::uint32_t m = 0;
		for (int i = 1; i < m_count; ++i)
		{
			if ((m_slots[static_cast<std::size_t>(i)].flags & kToonKeepColorInImpact) != 0) { m |= 1u << i; }
		}
		return m;
	}

private:
	std::array<ToonLineSlot, kToonSlotCount> m_slots{};
	int m_count = 1;
	bool m_overflow = false;
};

} // namespace mitiru::render
