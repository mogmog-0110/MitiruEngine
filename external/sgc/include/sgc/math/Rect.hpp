#pragma once

/// @file Rect.hpp
/// @brief 2D矩形型 Rect<T>
///
/// 位置とサイズで定義される2D矩形。
/// AABB2との相互変換、包含判定、交差判定を提供する。

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Geometry.hpp"

namespace sgc
{

/// @brief 2D矩形（位置 + サイズ）
/// @tparam T 算術型
///
/// @code
/// sgc::Rectf rect{{10.0f, 20.0f}, {100.0f, 50.0f}};
/// auto c = rect.center();
/// bool hit = rect.contains({50.0f, 30.0f});
/// @endcode
template <Arithmetic T>
struct Rect
{
	Vec2<T> position{};  ///< 左上座標
	Vec2<T> size{};      ///< 幅と高さ

	/// @brief デフォルトコンストラクタ
	constexpr Rect() noexcept = default;

	/// @brief 位置とサイズを指定して構築する
	constexpr Rect(const Vec2<T>& position, const Vec2<T>& size) noexcept
		: position(position), size(size) {}

	/// @brief 各成分を個別に指定して構築する
	constexpr Rect(T x, T y, T w, T h) noexcept
		: position(x, y), size(w, h) {}

	[[nodiscard]] constexpr bool operator==(const Rect& rhs) const noexcept = default;

	// ── ファクトリ関数 ──────────────────────────────────────

	/// @brief 最小・最大座標から構築する
	/// @param min 左上座標
	/// @param max 右下座標
	[[nodiscard]] static constexpr Rect fromMinMax(const Vec2<T>& min, const Vec2<T>& max) noexcept
	{
		return {min, max - min};
	}

	/// @brief 中心座標とサイズから構築する
	/// @param center 中心座標
	/// @param size 幅と高さ
	[[nodiscard]] static constexpr Rect fromCenter(const Vec2<T>& center, const Vec2<T>& size) noexcept
	{
		return {center - size / T{2}, size};
	}

	/// @brief AABB2に変換する
	/// @note T が浮動小数点型の場合のみ有効
	[[nodiscard]] constexpr auto toAABB2() const noexcept
		requires FloatingPoint<T>
	{
		return AABB2<T>{position, position + size};
	}

	// ── アクセサ ────────────────────────────────────────────

	/// @brief X座標を返す
	[[nodiscard]] constexpr T x() const noexcept { return position.x; }

	/// @brief Y座標を返す
	[[nodiscard]] constexpr T y() const noexcept { return position.y; }

	/// @brief 幅を返す
	[[nodiscard]] constexpr T width() const noexcept { return size.x; }

	/// @brief 高さを返す
	[[nodiscard]] constexpr T height() const noexcept { return size.y; }

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr Vec2<T> center() const noexcept
	{
		return position + size / T{2};
	}

	/// @brief 左辺のX座標を返す
	[[nodiscard]] constexpr T left() const noexcept { return position.x; }

	/// @brief 右辺のX座標を返す
	[[nodiscard]] constexpr T right() const noexcept { return position.x + size.x; }

	/// @brief 上辺のY座標を返す
	[[nodiscard]] constexpr T top() const noexcept { return position.y; }

	/// @brief 下辺のY座標を返す
	[[nodiscard]] constexpr T bottom() const noexcept { return position.y + size.y; }

	/// @brief 面積を返す
	[[nodiscard]] constexpr T area() const noexcept { return size.x * size.y; }

	// ── 判定 ────────────────────────────────────────────────

	/// @brief 点が矩形内に含まれるか判定する
	/// @param point 判定する点
	[[nodiscard]] constexpr bool contains(const Vec2<T>& point) const noexcept
	{
		return point.x >= position.x && point.x <= position.x + size.x
			&& point.y >= position.y && point.y <= position.y + size.y;
	}

	/// @brief 他の矩形が完全に内包されるか判定する
	/// @param other 判定する矩形
	[[nodiscard]] constexpr bool contains(const Rect& other) const noexcept
	{
		return other.position.x >= position.x
			&& other.position.y >= position.y
			&& other.position.x + other.size.x <= position.x + size.x
			&& other.position.y + other.size.y <= position.y + size.y;
	}

	/// @brief 他の矩形と交差しているか判定する
	/// @param other 判定する矩形
	[[nodiscard]] constexpr bool intersects(const Rect& other) const noexcept
	{
		return position.x < other.position.x + other.size.x
			&& position.x + size.x > other.position.x
			&& position.y < other.position.y + other.size.y
			&& position.y + size.y > other.position.y;
	}

	/// @brief 他の矩形との交差領域を返す
	/// @param other 交差判定する矩形
	/// @return 交差領域の矩形（交差しない場合はゼロサイズ）
	[[nodiscard]] constexpr Rect intersection(const Rect& other) const noexcept
	{
		const T x1 = (position.x > other.position.x) ? position.x : other.position.x;
		const T y1 = (position.y > other.position.y) ? position.y : other.position.y;
		const T x2r = position.x + size.x;
		const T x2o = other.position.x + other.size.x;
		const T x2 = (x2r < x2o) ? x2r : x2o;
		const T y2r = position.y + size.y;
		const T y2o = other.position.y + other.size.y;
		const T y2 = (y2r < y2o) ? y2r : y2o;

		if (x2 <= x1 || y2 <= y1)
		{
			return {};
		}
		return {{x1, y1}, {x2 - x1, y2 - y1}};
	}

	// ── 変換（不変性: 新しいRectを返す）─────────────────────

	/// @brief 全方向に指定量だけ拡張した矩形を返す
	/// @param amount 拡張量
	[[nodiscard]] constexpr Rect expanded(T amount) const noexcept
	{
		return {
			{position.x - amount, position.y - amount},
			{size.x + amount * T{2}, size.y + amount * T{2}}
		};
	}

	/// @brief 指定量だけ移動した矩形を返す
	/// @param offset 移動量
	[[nodiscard]] constexpr Rect moved(const Vec2<T>& offset) const noexcept
	{
		return {position + offset, size};
	}

	/// @brief サイズを指定倍率で拡縮した矩形を返す（中心基準）
	/// @param factor 拡縮倍率
	[[nodiscard]] constexpr Rect scaled(T factor) const noexcept
	{
		const Vec2<T> newSize = size * factor;
		const Vec2<T> diff = size - newSize;
		return {position + diff / T{2}, newSize};
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版、フラットに展開）
	template <typename V>
	void visit(V& v) const
	{
		v.write("px", position.x); v.write("py", position.y);
		v.write("sx", size.x); v.write("sy", size.y);
	}

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v)
	{
		v.read("px", position.x); v.read("py", position.y);
		v.read("sx", size.x); v.read("sy", size.y);
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Rectf = Rect<float>;    ///< float版 Rect
using Rectd = Rect<double>;   ///< double版 Rect
using Recti = Rect<int>;      ///< int版 Rect

} // namespace sgc
