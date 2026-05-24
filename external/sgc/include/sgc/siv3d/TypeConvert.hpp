#pragma once

/// @file TypeConvert.hpp
/// @brief sgc型 ↔ Siv3D型の相互変換
///
/// sgcの型（float精度）とSiv3Dの型（double精度）を
/// 安全に変換するユーティリティ関数群。
///
/// @code
/// sgc::Vec2f pos{10.0f, 20.0f};
/// s3d::Vec2 sivPos = sgc::siv3d::toSiv(pos);
///
/// s3d::Vec2 cursor = s3d::Cursor::PosF();
/// sgc::Vec2f sgcCursor = sgc::siv3d::fromSiv(cursor);
/// @endcode

#include <Siv3D.hpp>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::siv3d
{

// ── Vec2 変換 ──────────────────────────────────────────

/// @brief sgc::Vec2f → s3d::Vec2 に変換する
/// @param v sgcの2Dベクトル（float）
/// @return Siv3Dの2Dベクトル（double）
[[nodiscard]] constexpr s3d::Vec2 toSiv(const Vec2f& v) noexcept
{
	return {static_cast<double>(v.x), static_cast<double>(v.y)};
}

/// @brief s3d::Vec2 → sgc::Vec2f に変換する
/// @param v Siv3Dの2Dベクトル（double）
/// @return sgcの2Dベクトル（float）
[[nodiscard]] constexpr Vec2f fromSiv(const s3d::Vec2& v) noexcept
{
	return {static_cast<float>(v.x), static_cast<float>(v.y)};
}

// ── Vec3 変換 ──────────────────────────────────────────

/// @brief sgc::Vec3f → s3d::Vec3 に変換する
/// @param v sgcの3Dベクトル（float）
/// @return Siv3Dの3Dベクトル（double）
[[nodiscard]] constexpr s3d::Vec3 toSiv(const Vec3f& v) noexcept
{
	return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

/// @brief s3d::Vec3 → sgc::Vec3f に変換する
/// @param v Siv3Dの3Dベクトル（double）
/// @return sgcの3Dベクトル（float）
[[nodiscard]] constexpr Vec3f fromSiv(const s3d::Vec3& v) noexcept
{
	return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// ── Color 変換 ─────────────────────────────────────────

/// @brief sgc::Colorf → s3d::ColorF に変換する
/// @param c sgcのカラー（float [0,1]）
/// @return Siv3Dの浮動小数点カラー（double [0,1]）
[[nodiscard]] inline s3d::ColorF toSivColorF(const Colorf& c) noexcept
{
	return {
		static_cast<double>(c.r),
		static_cast<double>(c.g),
		static_cast<double>(c.b),
		static_cast<double>(c.a)
	};
}

/// @brief sgc::Colorf → s3d::Color に変換する
/// @param c sgcのカラー（float [0,1]）
/// @return Siv3Dの整数カラー（uint8 [0,255]）
[[nodiscard]] inline s3d::Color toSivColor(const Colorf& c) noexcept
{
	const auto rgba = c.toRGBA8();
	return {rgba.r, rgba.g, rgba.b, rgba.a};
}

/// @brief s3d::ColorF → sgc::Colorf に変換する
/// @param c Siv3Dの浮動小数点カラー（double [0,1]）
/// @return sgcのカラー（float [0,1]）
[[nodiscard]] inline Colorf fromSivColorF(const s3d::ColorF& c) noexcept
{
	return {
		static_cast<float>(c.r),
		static_cast<float>(c.g),
		static_cast<float>(c.b),
		static_cast<float>(c.a)
	};
}

/// @brief s3d::Color → sgc::Colorf に変換する
/// @param c Siv3Dの整数カラー（uint8 [0,255]）
/// @return sgcのカラー（float [0,1]）
[[nodiscard]] inline Colorf fromSivColor(const s3d::Color& c) noexcept
{
	return Colorf::fromRGBA8(c.r, c.g, c.b, c.a);
}

// ── AABB2 ↔ RectF 変換 ────────────────────────────────

/// @brief sgc::AABB2f → s3d::RectF に変換する
/// @param aabb sgcの軸平行バウンディングボックス（min/max形式）
/// @return Siv3Dの矩形（pos+size形式）
[[nodiscard]] inline s3d::RectF toSivRect(const AABB2f& aabb) noexcept
{
	const auto sz = aabb.size();
	return {
		static_cast<double>(aabb.min.x),
		static_cast<double>(aabb.min.y),
		static_cast<double>(sz.x),
		static_cast<double>(sz.y)
	};
}

/// @brief s3d::RectF → sgc::AABB2f に変換する
/// @param rect Siv3Dの矩形（pos+size形式）
/// @return sgcの軸平行バウンディングボックス（min/max形式）
[[nodiscard]] inline AABB2f fromSivRect(const s3d::RectF& rect) noexcept
{
	const float x = static_cast<float>(rect.x);
	const float y = static_cast<float>(rect.y);
	const float w = static_cast<float>(rect.w);
	const float h = static_cast<float>(rect.h);
	return {
		.min = {x, y},
		.max = {x + w, y + h}
	};
}

// ── Rect 変換 ─────────────────────────────────────────

/// @brief sgc::Rectf → s3d::RectF に変換する
/// @param r sgcの矩形（float、position+size形式）
/// @return Siv3Dの矩形（double、pos+size形式）
///
/// @code
/// sgc::Rectf rect{{10.0f, 20.0f}, {100.0f, 50.0f}};
/// s3d::RectF sivRect = sgc::siv3d::toSiv(rect);
/// @endcode
[[nodiscard]] constexpr s3d::RectF toSiv(const Rectf& r) noexcept
{
	return {
		static_cast<double>(r.x()),
		static_cast<double>(r.y()),
		static_cast<double>(r.width()),
		static_cast<double>(r.height())
	};
}

/// @brief s3d::RectF → sgc::Rectf に変換する
/// @param r Siv3Dの矩形（double）
/// @return sgcの矩形（float、position+size形式）
///
/// @code
/// s3d::RectF sivRect{10.0, 20.0, 100.0, 50.0};
/// sgc::Rectf rect = sgc::siv3d::toRectf(sivRect);
/// @endcode
[[nodiscard]] constexpr Rectf toRectf(const s3d::RectF& r) noexcept
{
	return {
		static_cast<float>(r.x),
		static_cast<float>(r.y),
		static_cast<float>(r.w),
		static_cast<float>(r.h)
	};
}

// ── Circle 変換 ────────────────────────────────────────

/// @brief sgcの円パラメータ → s3d::Circle に変換する
/// @param center 中心座標
/// @param radius 半径
/// @return Siv3Dの円
[[nodiscard]] inline s3d::Circle toSivCircle(const Vec2f& center, float radius) noexcept
{
	return {toSiv(center), static_cast<double>(radius)};
}

/// @brief sgc::Circle → s3d::Circle に変換する
/// @param circle sgcの円
/// @return Siv3Dの円
[[nodiscard]] inline s3d::Circle toSivCircle(const sgc::Circle<float>& circle) noexcept
{
	return {toSiv(circle.center), static_cast<double>(circle.radius)};
}

// ── 追加変換エイリアス ─────────────────────────────────

/// @brief s3d::RectF → sgc::Rectf に変換する（fromSivエイリアス）
/// @param r Siv3Dの矩形（double）
/// @return sgcの矩形（float、position+size形式）
[[nodiscard]] constexpr Rectf fromSiv(const s3d::RectF& r) noexcept
{
	return {
		static_cast<float>(r.x),
		static_cast<float>(r.y),
		static_cast<float>(r.w),
		static_cast<float>(r.h)
	};
}

/// @brief s3d::Circle → sgc::Circle<float> に変換する
/// @param c Siv3Dの円
/// @return sgcの円
[[nodiscard]] inline Circle<float> fromSivCircle(const s3d::Circle& c) noexcept
{
	return {fromSiv(c.center), static_cast<float>(c.r)};
}

/// @brief sgc::Colorf → s3d::ColorF に変換する（統一名）
/// @param c sgcのカラー
/// @return Siv3Dの浮動小数点カラー
[[nodiscard]] inline s3d::ColorF toSiv(const Colorf& c) noexcept
{
	return toSivColorF(c);
}

/// @brief s3d::ColorF → sgc::Colorf に変換する（統一名）
/// @param c Siv3Dの浮動小数点カラー
/// @return sgcのカラー
[[nodiscard]] inline Colorf fromSiv(const s3d::ColorF& c) noexcept
{
	return fromSivColorF(c);
}

} // namespace sgc::siv3d
