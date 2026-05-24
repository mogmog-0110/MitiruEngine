#pragma once

/// @file Mat3.hpp
/// @brief 3x3行列型 Mat3<T>
///
/// 2D変換（回転、拡大縮小、平行移動）に使用する3x3行列。
/// 行優先（row-major）格納。constexprで全演算が可能。
///
/// @code
/// auto transform = sgc::Mat3f::translation({10.0f, 20.0f})
///                * sgc::Mat3f::rotation(sgc::toRadians(45.0f))
///                * sgc::Mat3f::scaling({2.0f, 2.0f});
/// @endcode

#include <cmath>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"

namespace sgc
{

/// @brief 3x3行列（行優先格納）
/// @tparam T 浮動小数点型
///
/// メモリレイアウト:
/// @code
/// | m[0][0]  m[0][1]  m[0][2] |   | 行0 |
/// | m[1][0]  m[1][1]  m[1][2] | = | 行1 |
/// | m[2][0]  m[2][1]  m[2][2] |   | 行2 |
/// @endcode
template <FloatingPoint T>
struct Mat3
{
	/// @brief 行列要素（行優先: m[行][列]）
	T m[3][3]{};

	/// @brief デフォルトコンストラクタ（ゼロ行列）
	constexpr Mat3() noexcept = default;

	/// @brief 全要素を指定して構築する（行優先順）
	constexpr Mat3(
		T m00, T m01, T m02,
		T m10, T m11, T m12,
		T m20, T m21, T m22
	) noexcept
		: m{{m00, m01, m02},
		    {m10, m11, m12},
		    {m20, m21, m22}}
	{}

	// ── 算術演算子 ──────────────────────────────────────────

	/// @brief 行列同士の加算
	[[nodiscard]] constexpr Mat3 operator+(const Mat3& rhs) const noexcept
	{
		Mat3 result;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.m[r][c] = m[r][c] + rhs.m[r][c];
		return result;
	}

	/// @brief 行列同士の減算
	[[nodiscard]] constexpr Mat3 operator-(const Mat3& rhs) const noexcept
	{
		Mat3 result;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.m[r][c] = m[r][c] - rhs.m[r][c];
		return result;
	}

	/// @brief 行列同士の乗算
	[[nodiscard]] constexpr Mat3 operator*(const Mat3& rhs) const noexcept
	{
		Mat3 result;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
			{
				result.m[r][c] = T{0};
				for (int k = 0; k < 3; ++k)
					result.m[r][c] += m[r][k] * rhs.m[k][c];
			}
		return result;
	}

	/// @brief スカラー乗算
	[[nodiscard]] constexpr Mat3 operator*(T scalar) const noexcept
	{
		Mat3 result;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.m[r][c] = m[r][c] * scalar;
		return result;
	}

	/// @brief 行列 * Vec3 乗算
	/// @param v 変換するベクトル
	/// @return 変換後のベクトル
	[[nodiscard]] constexpr Vec3<T> operator*(const Vec3<T>& v) const noexcept
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
		};
	}

	/// @brief 2Dベクトルを同次座標で変換する（w=1として平行移動も適用）
	/// @param v 変換するベクトル
	/// @return 変換後の2Dベクトル
	[[nodiscard]] constexpr Vec2<T> transformPoint(const Vec2<T>& v) const noexcept
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2],
			m[1][0] * v.x + m[1][1] * v.y + m[1][2]
		};
	}

	/// @brief 2Dベクトルを方向として変換する（w=0として平行移動を無視）
	/// @param v 変換する方向ベクトル
	/// @return 変換後の方向ベクトル
	[[nodiscard]] constexpr Vec2<T> transformVector(const Vec2<T>& v) const noexcept
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y,
			m[1][0] * v.x + m[1][1] * v.y
		};
	}

	constexpr Mat3& operator*=(const Mat3& rhs) noexcept { *this = *this * rhs; return *this; }
	constexpr Mat3& operator*=(T scalar) noexcept
	{
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				m[r][c] *= scalar;
		return *this;
	}

	[[nodiscard]] constexpr bool operator==(const Mat3& rhs) const noexcept
	{
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				if (m[r][c] != rhs.m[r][c]) return false;
		return true;
	}

	// ── 行列演算 ────────────────────────────────────────────

	/// @brief 転置行列を返す
	[[nodiscard]] constexpr Mat3 transposed() const noexcept
	{
		return {
			m[0][0], m[1][0], m[2][0],
			m[0][1], m[1][1], m[2][1],
			m[0][2], m[1][2], m[2][2]
		};
	}

	/// @brief 行列式を計算する
	/// @return 行列式の値
	[[nodiscard]] constexpr T determinant() const noexcept
	{
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
			 - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
			 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	}

	/// @brief 逆行列を返す
	/// @return 逆行列
	/// @note 行列式が0の場合はゼロ行列を返す
	[[nodiscard]] constexpr Mat3 inversed() const noexcept
	{
		const T det = determinant();
		if (det == T{0}) return {};

		const T invDet = T{1} / det;
		return {
			(m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet,
			(m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet,
			(m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet,
			(m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invDet,
			(m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet,
			(m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet,
			(m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet,
			(m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet,
			(m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet
		};
	}

	// ── ファクトリ関数 ──────────────────────────────────────

	/// @brief 単位行列を返す
	[[nodiscard]] static constexpr Mat3 identity() noexcept
	{
		return {
			T{1}, T{0}, T{0},
			T{0}, T{1}, T{0},
			T{0}, T{0}, T{1}
		};
	}

	/// @brief 2D平行移動行列を作成する
	/// @param offset 移動量
	/// @return 平行移動行列
	[[nodiscard]] static constexpr Mat3 translation(const Vec2<T>& offset) noexcept
	{
		return {
			T{1}, T{0}, offset.x,
			T{0}, T{1}, offset.y,
			T{0}, T{0}, T{1}
		};
	}

	/// @brief 2D回転行列を作成する
	/// @param radians 回転角度（ラジアン）
	/// @return 回転行列
	[[nodiscard]] static Mat3 rotation(T radians) noexcept
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		return {
			 c, -s, T{0},
			 s,  c, T{0},
			T{0}, T{0}, T{1}
		};
	}

	/// @brief 2D拡大縮小行列を作成する
	/// @param scale 各軸の拡大率
	/// @return 拡大縮小行列
	[[nodiscard]] static constexpr Mat3 scaling(const Vec2<T>& scale) noexcept
	{
		return {
			scale.x, T{0}, T{0},
			T{0}, scale.y, T{0},
			T{0}, T{0}, T{1}
		};
	}

	/// @brief 均一拡大縮小行列を作成する
	/// @param scale 拡大率
	/// @return 拡大縮小行列
	[[nodiscard]] static constexpr Mat3 scaling(T scale) noexcept
	{
		return scaling({scale, scale});
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版、名前付き9フィールド）
	template <typename V>
	void visit(V& v) const
	{
		v.write("m00", m[0][0]); v.write("m01", m[0][1]); v.write("m02", m[0][2]);
		v.write("m10", m[1][0]); v.write("m11", m[1][1]); v.write("m12", m[1][2]);
		v.write("m20", m[2][0]); v.write("m21", m[2][1]); v.write("m22", m[2][2]);
	}

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v)
	{
		v.read("m00", m[0][0]); v.read("m01", m[0][1]); v.read("m02", m[0][2]);
		v.read("m10", m[1][0]); v.read("m11", m[1][1]); v.read("m12", m[1][2]);
		v.read("m20", m[2][0]); v.read("m21", m[2][1]); v.read("m22", m[2][2]);
	}
};

/// @brief スカラー * Mat3 演算子
template <FloatingPoint T>
[[nodiscard]] constexpr Mat3<T> operator*(T scalar, const Mat3<T>& mat) noexcept
{
	return mat * scalar;
}

// ── エイリアス ──────────────────────────────────────────────────

using Mat3f = Mat3<float>;    ///< float版 Mat3
using Mat3d = Mat3<double>;   ///< double版 Mat3

} // namespace sgc
