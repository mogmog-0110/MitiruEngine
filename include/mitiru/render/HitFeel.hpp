#pragma once

/// @file HitFeel.hpp
/// @brief 当たった瞬間の画面の演出 (白飛び・色ずれ・放射状のぼけ)。そのフレームだけ効く
/// @details 画面の揺れと同じく、ゲームが GameMemory の値から毎フレーム渡し、渡さなければ何もしない。
///          状態を持たないので巻き戻しで戻した時刻の絵がそのまま出る。描くのは 3D の後処理の最後
///          (TAA・アップスケールの後、HUD の前) なので、履歴に残らず HUD にも掛からない。

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// @brief 1 フレームぶんの演出 (32 byte の POD)。どの値も 0 なら何もしない
struct HitFeel
{
	float flashColor[3] = {1.0f, 1.0f, 1.0f};
	float flash = 0.0f;        ///< flashColor へ寄せる割合 0..1
	float chromatic = 0.0f;    ///< 色ずれの幅。画面の端で出力の高さのこの割合だけ、赤の絵が中心へ・青の絵が外へずれる
	float radialBlur = 0.0f;   ///< 中心から外へ流れるぼけの強さ 0..1 (1 で端が画面の 1/8 流れる)
	float center[2] = {0.5f, 0.5f};   ///< 色ずれとぼけの中心 (画面の uv、左上が 0)
};

static_assert(sizeof(HitFeel) == 32 && std::is_trivially_copyable_v<HitFeel>, "HitFeel は 32 byte の POD");

[[nodiscard]] inline bool hitFeelActive(const HitFeel& h) noexcept
{
	return h.flash > 0.0f || h.chromatic > 0.0f || h.radialBlur > 0.0f;
}

/// @brief 範囲外の値を丸めた写し
[[nodiscard]] inline HitFeel clampHitFeel(const HitFeel& h) noexcept
{
	HitFeel o = h;
	for (float& c : o.flashColor) { c = std::max(c, 0.0f); }
	o.flash = std::clamp(h.flash, 0.0f, 1.0f);
	o.chromatic = std::clamp(h.chromatic, 0.0f, 0.05f);
	o.radialBlur = std::clamp(h.radialBlur, 0.0f, 1.0f);
	return o;
}

} // namespace mitiru::render
