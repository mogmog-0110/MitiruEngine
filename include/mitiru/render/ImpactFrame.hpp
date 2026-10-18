#pragma once

/// @file ImpactFrame.hpp
/// @brief 大きな当たりの止め絵 (ADR 0079)。そのフレームだけ、画面を明るさで 2 色に塗り分けるか反転する
/// @details HitFeel と同じく、ゲームが GameMemory の値から毎フレーム渡し、渡さなければ何もしない。
///          掛けるのは 3D の後処理の最後 (アップスケールの後、HitFeel の前、HUD の前) なので、TAA の履歴に残らない。
///          ToonStyle::flags に kToonKeepColorInImpact を立てて描いた物は色のまま残る。

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

enum class ImpactFrameMode : std::uint32_t
{
	Off = 0,
	TwoTone = 1,   ///< 明るさが threshold 以上の画素を lightColor、未満を darkColor にする
	Invert = 2,    ///< 色を反転する
};

/// @brief 1 フレームぶんの止め絵 (48 byte の POD)
struct ImpactFrame
{
	ImpactFrameMode mode = ImpactFrameMode::Off;
	float threshold = 0.5f;   ///< 画面の色 (sRGB) の輝度 0..1 で測る
	float amount = 1.0f;      ///< 元の色から塗った色へ寄せる割合 0..1
	float lightColor[3] = {0.965f, 0.933f, 0.863f};   ///< 画面に出る色そのもの (既定 #F6EEDC)
	float darkColor[3] = {0.149f, 0.125f, 0.251f};    ///< 既定 #262040
	float reserved[3] = {};
};

static_assert(sizeof(ImpactFrame) == 48 && std::is_trivially_copyable_v<ImpactFrame>, "ImpactFrame は 48 byte の POD");

[[nodiscard]] inline bool impactFrameActive(const ImpactFrame& f) noexcept
{
	return (f.mode == ImpactFrameMode::TwoTone || f.mode == ImpactFrameMode::Invert) && f.amount > 0.0f;
}

/// @brief 範囲外の値を丸めた写し
[[nodiscard]] inline ImpactFrame clampImpactFrame(const ImpactFrame& f) noexcept
{
	ImpactFrame o = f;
	o.threshold = std::clamp(f.threshold, 0.0f, 1.0f);
	o.amount = std::clamp(f.amount, 0.0f, 1.0f);
	for (int i = 0; i < 3; ++i)
	{
		o.lightColor[i] = std::clamp(f.lightColor[i], 0.0f, 1.0f);
		o.darkColor[i] = std::clamp(f.darkColor[i], 0.0f, 1.0f);
	}
	return o;
}

} // namespace mitiru::render
