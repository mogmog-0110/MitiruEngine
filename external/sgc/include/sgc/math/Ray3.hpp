#pragma once

/// @file Ray3.hpp
/// @brief 3Dレイ（半直線）型 Ray3<T>

#include "sgc/math/Vec3.hpp"

namespace sgc
{

/// @brief 3Dレイ（半直線）
/// @tparam T 浮動小数点型
///
/// @code
/// sgc::Ray3f ray{{0,0,0}, {0,0,-1}};
/// auto p = ray.pointAt(5.0f);  // {0, 0, -5}
/// @endcode
template <FloatingPoint T>
struct Ray3
{
	Vec3<T> origin{};     ///< 始点
	Vec3<T> direction{};  ///< 方向（正規化済みを推奨）

	/// @brief デフォルトコンストラクタ
	constexpr Ray3() noexcept = default;

	/// @brief 始点と方向を指定して構築する
	constexpr Ray3(const Vec3<T>& origin, const Vec3<T>& direction) noexcept
		: origin(origin), direction(direction) {}

	/// @brief レイ上の指定距離の点を返す
	/// @param t 距離パラメータ
	[[nodiscard]] constexpr Vec3<T> pointAt(T t) const noexcept
	{
		return origin + direction * t;
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Ray3f = Ray3<float>;    ///< float版 Ray3
using Ray3d = Ray3<double>;   ///< double版 Ray3

} // namespace sgc
