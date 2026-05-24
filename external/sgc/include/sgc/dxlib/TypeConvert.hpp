#pragma once

/// @file TypeConvert.hpp
/// @brief sgc型 → DxLib型の変換ユーティリティ
///
/// DxLib.hに依存せず、純粋な数値変換のみを行う。
/// CIテスト環境でもコンパイル・テスト可能。
///
/// @code
/// sgc::Vec2f pos{10.5f, 20.3f};
/// auto dxPt = sgc::dxlib::toDxPoint(pos);
/// // dxPt.x == 10, dxPt.y == 20
///
/// sgc::Colorf red = sgc::Colorf::red();
/// unsigned int dxColor = sgc::dxlib::toDxColor(red);
/// @endcode

#include <algorithm>
#include <cstdint>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::dxlib
{

// ── DxLib互換の軽量構造体（DxLib.h不要） ────────────────

/// @brief DxLib互換の2D座標（整数）
struct DxPoint
{
	int x;  ///< X座標
	int y;  ///< Y座標
};

/// @brief DxLib互換の矩形（左上・右下の2点）
struct DxRect
{
	int x1;  ///< 左上X
	int y1;  ///< 左上Y
	int x2;  ///< 右下X
	int y2;  ///< 右下Y
};

// ── アルファ変換 ────────────────────────────────────────

/// @brief 浮動小数点アルファ [0,1] → DxLib整数アルファ [0,255]
/// @param alpha アルファ値 [0,1]
/// @return DxLib用整数アルファ [0,255]
[[nodiscard]] constexpr int toDxAlpha(float alpha) noexcept
{
	if (alpha <= 0.0f) return 0;
	if (alpha >= 1.0f) return 255;
	return static_cast<int>(alpha * 255.0f + 0.5f);
}

// ── 座標変換 ────────────────────────────────────────────

/// @brief sgc::Vec2f → DxLib整数座標に変換する
/// @param v sgcの2Dベクトル
/// @return DxLib互換の整数座標
[[nodiscard]] constexpr DxPoint toDxPoint(const Vec2f& v) noexcept
{
	return {static_cast<int>(v.x), static_cast<int>(v.y)};
}

// ── 矩形変換 ────────────────────────────────────────────

/// @brief sgc::AABB2f → DxLib矩形（左上・右下）に変換する
/// @param aabb sgcのバウンディングボックス
/// @return DxLib互換の矩形
[[nodiscard]] constexpr DxRect toDxRect(const AABB2f& aabb) noexcept
{
	return {
		static_cast<int>(aabb.min.x),
		static_cast<int>(aabb.min.y),
		static_cast<int>(aabb.max.x),
		static_cast<int>(aabb.max.y)
	};
}

// ── 色変換 ──────────────────────────────────────────────

/// @brief RGB8値をDxLib互換のunsigned intにパックする
///
/// DxLibの GetColor(r,g,b) と同等の結果を返す。
/// 内部形式: r | (g << 8) | (b << 16)
///
/// @param r 赤 [0,255]
/// @param g 緑 [0,255]
/// @param b 青 [0,255]
/// @return DxLib互換のパック済みカラー値
[[nodiscard]] constexpr unsigned int packDxColor(
	std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
{
	return static_cast<unsigned int>(r)
		 | (static_cast<unsigned int>(g) << 8)
		 | (static_cast<unsigned int>(b) << 16);
}

/// @brief sgc::Colorf → DxLib互換カラー値に変換する
///
/// アルファは無視される（DxLibは描画関数ごとにブレンドで制御する）。
///
/// @param color sgcのカラー
/// @return DxLib互換のパック済みカラー値
[[nodiscard]] constexpr unsigned int toDxColor(const Colorf& color) noexcept
{
	const auto rgba = color.toRGBA8();
	return packDxColor(rgba.r, rgba.g, rgba.b);
}

} // namespace sgc::dxlib
