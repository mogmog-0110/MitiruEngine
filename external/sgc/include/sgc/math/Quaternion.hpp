#pragma once

/// @file Quaternion.hpp
/// @brief クォータニオン型 Quaternion<T>
///
/// 3D回転を表現するクォータニオン。ジンバルロックなし。
/// 球面線形補間(slerp)、Mat4への変換をサポート。
///
/// @code
/// auto rot = sgc::Quaternionf::fromAxisAngle({0, 1, 0}, sgc::toRadians(90.0f));
/// auto mat = rot.toMat4();
/// auto smoothRot = sgc::Quaternionf::slerp(from, to, 0.5f);
/// @endcode

#include <cmath>
#include <numbers>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc
{

/// @brief クォータニオン（四元数）
/// @tparam T 浮動小数点型
///
/// 内部表現: q = w + xi + yj + zk
/// (x, y, z) がベクトル部、w がスカラー部。
template <FloatingPoint T>
struct Quaternion
{
	T x{};  ///< ベクトル部 X
	T y{};  ///< ベクトル部 Y
	T z{};  ///< ベクトル部 Z
	T w{T{1}};  ///< スカラー部（デフォルトは単位クォータニオン）

	/// @brief デフォルトコンストラクタ（単位クォータニオン）
	constexpr Quaternion() noexcept = default;

	/// @brief 成分を指定して構築する
	/// @param x ベクトル部 X
	/// @param y ベクトル部 Y
	/// @param z ベクトル部 Z
	/// @param w スカラー部
	constexpr Quaternion(T x, T y, T z, T w) noexcept : x(x), y(y), z(z), w(w) {}

	// ── 算術演算子 ──────────────────────────────────────────

	/// @brief クォータニオン同士の乗算（回転の合成）
	[[nodiscard]] constexpr Quaternion operator*(const Quaternion& rhs) const noexcept
	{
		return {
			w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
			w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
			w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
			w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
		};
	}

	/// @brief スカラー乗算
	[[nodiscard]] constexpr Quaternion operator*(T scalar) const noexcept
	{
		return {x * scalar, y * scalar, z * scalar, w * scalar};
	}

	/// @brief クォータニオン同士の加算
	[[nodiscard]] constexpr Quaternion operator+(const Quaternion& rhs) const noexcept
	{
		return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
	}

	/// @brief クォータニオン同士の減算
	[[nodiscard]] constexpr Quaternion operator-(const Quaternion& rhs) const noexcept
	{
		return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
	}

	/// @brief 符号反転
	[[nodiscard]] constexpr Quaternion operator-() const noexcept
	{
		return {-x, -y, -z, -w};
	}

	constexpr Quaternion& operator*=(const Quaternion& rhs) noexcept { *this = *this * rhs; return *this; }

	[[nodiscard]] constexpr bool operator==(const Quaternion& rhs) const noexcept = default;

	// ── クォータニオン演算 ──────────────────────────────────

	/// @brief ノルム（長さ）の二乗を返す
	[[nodiscard]] constexpr T normSquared() const noexcept
	{
		return x * x + y * y + z * z + w * w;
	}

	/// @brief ノルム（長さ）を返す
	[[nodiscard]] T norm() const noexcept
	{
		return std::sqrt(normSquared());
	}

	/// @brief 正規化したクォータニオンを返す
	/// @return 単位クォータニオン
	/// @note ノルムが0の場合は単位クォータニオンを返す
	[[nodiscard]] Quaternion normalized() const noexcept
	{
		const T n = norm();
		if (n == T{0}) return identity();
		const T inv = T{1} / n;
		return {x * inv, y * inv, z * inv, w * inv};
	}

	/// @brief 共役クォータニオンを返す
	/// @return 共役 (x, y, z を反転)
	[[nodiscard]] constexpr Quaternion conjugate() const noexcept
	{
		return {-x, -y, -z, w};
	}

	/// @brief 逆クォータニオンを返す
	/// @return 逆元
	/// @note 単位クォータニオンの場合、逆 = 共役
	[[nodiscard]] Quaternion inversed() const noexcept
	{
		const T ns = normSquared();
		if (ns == T{0}) return identity();
		const T inv = T{1} / ns;
		return {-x * inv, -y * inv, -z * inv, w * inv};
	}

	/// @brief 内積を計算する
	/// @param rhs 右辺クォータニオン
	/// @return 内積の値
	[[nodiscard]] constexpr T dot(const Quaternion& rhs) const noexcept
	{
		return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
	}

	/// @brief ベクトルをこのクォータニオンで回転させる
	/// @param v 回転させるベクトル
	/// @return 回転後のベクトル
	[[nodiscard]] constexpr Vec3<T> rotate(const Vec3<T>& v) const noexcept
	{
		// q * v * q^-1 を効率的に計算
		const Vec3<T> qv{x, y, z};
		const Vec3<T> t = qv.cross(v) * T{2};
		return v + t * w + qv.cross(t);
	}

	/// @brief 4x4回転行列に変換する
	/// @return 回転行列
	[[nodiscard]] constexpr Mat4<T> toMat4() const noexcept
	{
		const T xx = x * x, yy = y * y, zz = z * z;
		const T xy = x * y, xz = x * z, yz = y * z;
		const T wx = w * x, wy = w * y, wz = w * z;

		return {
			T{1} - T{2} * (yy + zz), T{2} * (xy - wz),         T{2} * (xz + wy),         T{0},
			T{2} * (xy + wz),         T{1} - T{2} * (xx + zz), T{2} * (yz - wx),         T{0},
			T{2} * (xz - wy),         T{2} * (yz + wx),         T{1} - T{2} * (xx + yy), T{0},
			T{0},                     T{0},                     T{0},                     T{1}
		};
	}

	/// @brief オイラー角（ラジアン）に変換する
	/// @return Vec3(pitch, yaw, roll) ラジアン
	/// @note ジンバルロック付近では精度が低下する
	[[nodiscard]] Vec3<T> toEuler() const noexcept
	{
		Vec3<T> euler;

		// ピッチ (X軸回転)
		const T sinP = T{2} * (w * x + y * z);
		const T cosP = T{1} - T{2} * (x * x + y * y);
		euler.x = std::atan2(sinP, cosP);

		// ヨー (Y軸回転)
		const T sinY = T{2} * (w * y - z * x);
		if (sinY >= T{1})
			euler.y = std::numbers::pi_v<T> / T{2};
		else if (sinY <= T{-1})
			euler.y = -std::numbers::pi_v<T> / T{2};
		else
			euler.y = std::asin(sinY);

		// ロール (Z軸回転)
		const T sinR = T{2} * (w * z + x * y);
		const T cosR = T{1} - T{2} * (y * y + z * z);
		euler.z = std::atan2(sinR, cosR);

		return euler;
	}

	// ── ファクトリ関数 ──────────────────────────────────────

	/// @brief 単位クォータニオン（回転なし）を返す
	[[nodiscard]] static constexpr Quaternion identity() noexcept
	{
		return {T{0}, T{0}, T{0}, T{1}};
	}

	/// @brief 軸と角度からクォータニオンを作成する
	/// @param axis 回転軸（正規化済み）
	/// @param radians 回転角度（ラジアン）
	/// @return 回転クォータニオン
	[[nodiscard]] static Quaternion fromAxisAngle(const Vec3<T>& axis, T radians) noexcept
	{
		const T half = radians / T{2};
		const T s = std::sin(half);
		return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
	}

	/// @brief オイラー角からクォータニオンを作成する
	/// @param pitch X軸回転（ラジアン）
	/// @param yaw Y軸回転（ラジアン）
	/// @param roll Z軸回転（ラジアン）
	/// @return 回転クォータニオン
	[[nodiscard]] static Quaternion fromEuler(T pitch, T yaw, T roll) noexcept
	{
		const T cp = std::cos(pitch / T{2}), sp = std::sin(pitch / T{2});
		const T cy = std::cos(yaw / T{2}),   sy = std::sin(yaw / T{2});
		const T cr = std::cos(roll / T{2}),  sr = std::sin(roll / T{2});

		return {
			sp * cy * cr - cp * sy * sr,
			cp * sy * cr + sp * cy * sr,
			cp * cy * sr - sp * sy * cr,
			cp * cy * cr + sp * sy * sr
		};
	}

	/// @brief 球面線形補間（Slerp）
	/// @param a 開始クォータニオン
	/// @param b 終了クォータニオン
	/// @param t 補間係数 [0, 1]
	/// @return 補間結果
	[[nodiscard]] static Quaternion slerp(const Quaternion& a, const Quaternion& b, T t) noexcept
	{
		T cosTheta = a.dot(b);

		// 最短経路を選択（内積が負なら符号反転）
		Quaternion bAdj = b;
		if (cosTheta < T{0})
		{
			cosTheta = -cosTheta;
			bAdj = -b;
		}

		// 角度が非常に小さい場合は線形補間にフォールバック
		if (cosTheta > T{1} - static_cast<T>(1e-6))
		{
			return Quaternion{
				a.x + t * (bAdj.x - a.x),
				a.y + t * (bAdj.y - a.y),
				a.z + t * (bAdj.z - a.z),
				a.w + t * (bAdj.w - a.w)
			}.normalized();
		}

		const T theta = std::acos(cosTheta);
		const T sinTheta = std::sin(theta);
		const T wa = std::sin((T{1} - t) * theta) / sinTheta;
		const T wb = std::sin(t * theta) / sinTheta;

		return Quaternion{
			wa * a.x + wb * bAdj.x,
			wa * a.y + wb * bAdj.y,
			wa * a.z + wb * bAdj.z,
			wa * a.w + wb * bAdj.w
		};
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版）
	template <typename V>
	void visit(V& v) const { v.write("x", x); v.write("y", y); v.write("z", z); v.write("w", w); }

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v) { v.read("x", x); v.read("y", y); v.read("z", z); v.read("w", w); }
};

/// @brief スカラー * Quaternion 演算子
template <FloatingPoint T>
[[nodiscard]] constexpr Quaternion<T> operator*(T scalar, const Quaternion<T>& q) noexcept
{
	return q * scalar;
}

// ── エイリアス ──────────────────────────────────────────────────

using Quaternionf = Quaternion<float>;    ///< float版 Quaternion
using Quaterniond = Quaternion<double>;   ///< double版 Quaternion

} // namespace sgc
