#pragma once

/// @file Mat4.hpp
/// @brief 4x4行列型 Mat4<T>
///
/// 3D変換（回転、拡大縮小、平行移動、射影）に使用する4x4行列。
/// 行優先（row-major）格納。
///
/// @code
/// auto view = sgc::Mat4f::lookAt(eye, target, up);
/// auto proj = sgc::Mat4f::perspective(fov, aspect, near, far);
/// auto mvp = proj * view * model;
/// @endcode

#include <cmath>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"

namespace sgc
{

/// @brief 4x4行列（行優先格納）
/// @tparam T 浮動小数点型
///
/// メモリレイアウト:
/// @code
/// | m[0][0]  m[0][1]  m[0][2]  m[0][3] |
/// | m[1][0]  m[1][1]  m[1][2]  m[1][3] |
/// | m[2][0]  m[2][1]  m[2][2]  m[2][3] |
/// | m[3][0]  m[3][1]  m[3][2]  m[3][3] |
/// @endcode
template <FloatingPoint T>
struct Mat4
{
	/// @brief 行列要素（行優先: m[行][列]）
	T m[4][4]{};

	/// @brief デフォルトコンストラクタ（ゼロ行列）
	constexpr Mat4() noexcept = default;

	/// @brief 全要素を指定して構築する（行優先順）
	constexpr Mat4(
		T m00, T m01, T m02, T m03,
		T m10, T m11, T m12, T m13,
		T m20, T m21, T m22, T m23,
		T m30, T m31, T m32, T m33
	) noexcept
		: m{{m00, m01, m02, m03},
		    {m10, m11, m12, m13},
		    {m20, m21, m22, m23},
		    {m30, m31, m32, m33}}
	{}

	// ── 算術演算子 ──────────────────────────────────────────

	/// @brief 行列同士の加算
	[[nodiscard]] constexpr Mat4 operator+(const Mat4& rhs) const noexcept
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				result.m[r][c] = m[r][c] + rhs.m[r][c];
		return result;
	}

	/// @brief 行列同士の減算
	[[nodiscard]] constexpr Mat4 operator-(const Mat4& rhs) const noexcept
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				result.m[r][c] = m[r][c] - rhs.m[r][c];
		return result;
	}

	/// @brief 行列同士の乗算
	[[nodiscard]] constexpr Mat4 operator*(const Mat4& rhs) const noexcept
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
			{
				result.m[r][c] = T{0};
				for (int k = 0; k < 4; ++k)
					result.m[r][c] += m[r][k] * rhs.m[k][c];
			}
		return result;
	}

	/// @brief スカラー乗算
	[[nodiscard]] constexpr Mat4 operator*(T scalar) const noexcept
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				result.m[r][c] = m[r][c] * scalar;
		return result;
	}

	/// @brief 行列 * Vec4 乗算
	/// @param v 変換するベクトル
	/// @return 変換後のベクトル
	[[nodiscard]] constexpr Vec4<T> operator*(const Vec4<T>& v) const noexcept
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
			m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w
		};
	}

	/// @brief 3Dベクトルを位置として変換する（w=1として平行移動も適用）
	/// @param v 変換する位置
	/// @return 変換後の3Dベクトル
	[[nodiscard]] constexpr Vec3<T> transformPoint(const Vec3<T>& v) const noexcept
	{
		const T w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
		if (w == T{0}) return {};
		const T invW = T{1} / w;
		return {
			(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * invW,
			(m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * invW,
			(m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]) * invW
		};
	}

	/// @brief 3Dベクトルを方向として変換する（w=0として平行移動を無視）
	/// @param v 変換する方向ベクトル
	/// @return 変換後の方向ベクトル
	[[nodiscard]] constexpr Vec3<T> transformVector(const Vec3<T>& v) const noexcept
	{
		return {
			m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
		};
	}

	constexpr Mat4& operator*=(const Mat4& rhs) noexcept { *this = *this * rhs; return *this; }
	constexpr Mat4& operator*=(T scalar) noexcept
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				m[r][c] *= scalar;
		return *this;
	}

	[[nodiscard]] constexpr bool operator==(const Mat4& rhs) const noexcept
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				if (m[r][c] != rhs.m[r][c]) return false;
		return true;
	}

	// ── 行列演算 ────────────────────────────────────────────

	/// @brief 転置行列を返す
	[[nodiscard]] constexpr Mat4 transposed() const noexcept
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				result.m[r][c] = m[c][r];
		return result;
	}

	/// @brief 行列式を計算する
	/// @return 行列式の値
	[[nodiscard]] constexpr T determinant() const noexcept
	{
		// 余因子展開（1行目）
		const T a = m[0][0], b = m[0][1], c = m[0][2], d = m[0][3];

		// 3x3小行列式
		const T det3_0 = m[1][1] * (m[2][2] * m[3][3] - m[2][3] * m[3][2])
		               - m[1][2] * (m[2][1] * m[3][3] - m[2][3] * m[3][1])
		               + m[1][3] * (m[2][1] * m[3][2] - m[2][2] * m[3][1]);

		const T det3_1 = m[1][0] * (m[2][2] * m[3][3] - m[2][3] * m[3][2])
		               - m[1][2] * (m[2][0] * m[3][3] - m[2][3] * m[3][0])
		               + m[1][3] * (m[2][0] * m[3][2] - m[2][2] * m[3][0]);

		const T det3_2 = m[1][0] * (m[2][1] * m[3][3] - m[2][3] * m[3][1])
		               - m[1][1] * (m[2][0] * m[3][3] - m[2][3] * m[3][0])
		               + m[1][3] * (m[2][0] * m[3][1] - m[2][1] * m[3][0]);

		const T det3_3 = m[1][0] * (m[2][1] * m[3][2] - m[2][2] * m[3][1])
		               - m[1][1] * (m[2][0] * m[3][2] - m[2][2] * m[3][0])
		               + m[1][2] * (m[2][0] * m[3][1] - m[2][1] * m[3][0]);

		return a * det3_0 - b * det3_1 + c * det3_2 - d * det3_3;
	}

	/// @brief 逆行列を返す
	/// @return 逆行列
	/// @note 行列式が0の場合はゼロ行列を返す
	[[nodiscard]] constexpr Mat4 inversed() const noexcept
	{
		// 余因子行列法
		T cofactors[4][4];

		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				// 3x3小行列を構築
				T sub[3][3];
				int si = 0;
				for (int i = 0; i < 4; ++i)
				{
					if (i == r) continue;
					int sj = 0;
					for (int j = 0; j < 4; ++j)
					{
						if (j == c) continue;
						sub[si][sj] = m[i][j];
						++sj;
					}
					++si;
				}

				// 3x3行列式
				const T det3 = sub[0][0] * (sub[1][1] * sub[2][2] - sub[1][2] * sub[2][1])
				             - sub[0][1] * (sub[1][0] * sub[2][2] - sub[1][2] * sub[2][0])
				             + sub[0][2] * (sub[1][0] * sub[2][1] - sub[1][1] * sub[2][0]);

				cofactors[r][c] = ((r + c) % 2 == 0 ? T{1} : T{-1}) * det3;
			}
		}

		// 行列式（1行目と余因子から）
		const T det = m[0][0] * cofactors[0][0] + m[0][1] * cofactors[0][1]
		            + m[0][2] * cofactors[0][2] + m[0][3] * cofactors[0][3];

		if (det == T{0}) return {};

		const T invDet = T{1} / det;

		// 転置余因子行列 / 行列式
		Mat4 result;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				result.m[r][c] = cofactors[c][r] * invDet;

		return result;
	}

	// ── ファクトリ関数 ──────────────────────────────────────

	/// @brief 単位行列を返す
	[[nodiscard]] static constexpr Mat4 identity() noexcept
	{
		return {
			T{1}, T{0}, T{0}, T{0},
			T{0}, T{1}, T{0}, T{0},
			T{0}, T{0}, T{1}, T{0},
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief 3D平行移動行列を作成する
	/// @param offset 移動量
	/// @return 平行移動行列
	[[nodiscard]] static constexpr Mat4 translation(const Vec3<T>& offset) noexcept
	{
		return {
			T{1}, T{0}, T{0}, offset.x,
			T{0}, T{1}, T{0}, offset.y,
			T{0}, T{0}, T{1}, offset.z,
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief 3D拡大縮小行列を作成する
	/// @param scale 各軸の拡大率
	/// @return 拡大縮小行列
	[[nodiscard]] static constexpr Mat4 scaling(const Vec3<T>& scale) noexcept
	{
		return {
			scale.x, T{0}, T{0}, T{0},
			T{0}, scale.y, T{0}, T{0},
			T{0}, T{0}, scale.z, T{0},
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief 均一拡大縮小行列を作成する
	/// @param scale 拡大率
	/// @return 拡大縮小行列
	[[nodiscard]] static constexpr Mat4 scaling(T scale) noexcept
	{
		return scaling({scale, scale, scale});
	}

	/// @brief X軸回転行列を作成する
	/// @param radians 回転角度（ラジアン）
	/// @return 回転行列
	[[nodiscard]] static Mat4 rotationX(T radians) noexcept
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		return {
			T{1}, T{0}, T{0}, T{0},
			T{0},    c,   -s, T{0},
			T{0},    s,    c, T{0},
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief Y軸回転行列を作成する
	/// @param radians 回転角度（ラジアン）
	/// @return 回転行列
	[[nodiscard]] static Mat4 rotationY(T radians) noexcept
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		return {
			   c, T{0},    s, T{0},
			T{0}, T{1}, T{0}, T{0},
			  -s, T{0},    c, T{0},
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief Z軸回転行列を作成する
	/// @param radians 回転角度（ラジアン）
	/// @return 回転行列
	[[nodiscard]] static Mat4 rotationZ(T radians) noexcept
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		return {
			   c,   -s, T{0}, T{0},
			   s,    c, T{0}, T{0},
			T{0}, T{0}, T{1}, T{0},
			T{0}, T{0}, T{0}, T{1}
		};
	}

	/// @brief 任意軸回転行列を作成する（ロドリゲスの回転公式）
	/// @param axis 回転軸（正規化済み）
	/// @param radians 回転角度（ラジアン）
	/// @return 回転行列
	[[nodiscard]] static Mat4 rotationAxis(const Vec3<T>& axis, T radians) noexcept
	{
		const T c = std::cos(radians);
		const T s = std::sin(radians);
		const T t = T{1} - c;
		const T nx = axis.x, ny = axis.y, nz = axis.z;

		return {
			t * nx * nx + c,      t * nx * ny - s * nz, t * nx * nz + s * ny, T{0},
			t * nx * ny + s * nz, t * ny * ny + c,      t * ny * nz - s * nx, T{0},
			t * nx * nz - s * ny, t * ny * nz + s * nx, t * nz * nz + c,      T{0},
			T{0},                 T{0},                 T{0},                 T{1}
		};
	}

	/// @brief ビュー行列を作成する（右手座標系）
	/// @param eye カメラ位置
	/// @param target 注視点
	/// @param worldUp ワールド上方向
	/// @return ビュー行列
	[[nodiscard]] static Mat4 lookAt(const Vec3<T>& eye, const Vec3<T>& target, const Vec3<T>& worldUp) noexcept
	{
		const Vec3<T> forward = (eye - target).normalized();
		const Vec3<T> right = worldUp.cross(forward).normalized();
		const Vec3<T> up = forward.cross(right);

		return {
			right.x,   right.y,   right.z,   -right.dot(eye),
			up.x,      up.y,      up.z,      -up.dot(eye),
			forward.x, forward.y, forward.z, -forward.dot(eye),
			T{0},      T{0},      T{0},       T{1}
		};
	}

	/// @brief 透視投影行列を作成する（右手座標系、Zを[-1,1]にマッピング）
	/// @param fovY 垂直視野角（ラジアン）
	/// @param aspect アスペクト比（幅/高さ）
	/// @param nearZ ニアクリップ面
	/// @param farZ ファークリップ面
	/// @return 透視投影行列
	[[nodiscard]] static Mat4 perspective(T fovY, T aspect, T nearZ, T farZ) noexcept
	{
		const T tanHalf = std::tan(fovY / T{2});
		const T range = nearZ - farZ;

		return {
			T{1} / (aspect * tanHalf), T{0},            T{0},                          T{0},
			T{0},                      T{1} / tanHalf,  T{0},                          T{0},
			T{0},                      T{0},            (farZ + nearZ) / range,        (T{2} * farZ * nearZ) / range,
			T{0},                      T{0},            T{-1},                         T{0}
		};
	}

	/// @brief 正射影行列を作成する
	/// @param left 左端
	/// @param right 右端
	/// @param bottom 下端
	/// @param top 上端
	/// @param nearZ ニアクリップ面
	/// @param farZ ファークリップ面
	/// @return 正射影行列
	[[nodiscard]] static constexpr Mat4 orthographic(T left, T right, T bottom, T top, T nearZ, T farZ) noexcept
	{
		const T rl = right - left;
		const T tb = top - bottom;
		const T fn = farZ - nearZ;

		return {
			T{2} / rl, T{0},      T{0},       -(right + left) / rl,
			T{0},      T{2} / tb, T{0},       -(top + bottom) / tb,
			T{0},      T{0},      T{-2} / fn, -(farZ + nearZ) / fn,
			T{0},      T{0},      T{0},        T{1}
		};
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版、名前付き16フィールド）
	template <typename V>
	void visit(V& v) const
	{
		v.write("m00", m[0][0]); v.write("m01", m[0][1]); v.write("m02", m[0][2]); v.write("m03", m[0][3]);
		v.write("m10", m[1][0]); v.write("m11", m[1][1]); v.write("m12", m[1][2]); v.write("m13", m[1][3]);
		v.write("m20", m[2][0]); v.write("m21", m[2][1]); v.write("m22", m[2][2]); v.write("m23", m[2][3]);
		v.write("m30", m[3][0]); v.write("m31", m[3][1]); v.write("m32", m[3][2]); v.write("m33", m[3][3]);
	}

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v)
	{
		v.read("m00", m[0][0]); v.read("m01", m[0][1]); v.read("m02", m[0][2]); v.read("m03", m[0][3]);
		v.read("m10", m[1][0]); v.read("m11", m[1][1]); v.read("m12", m[1][2]); v.read("m13", m[1][3]);
		v.read("m20", m[2][0]); v.read("m21", m[2][1]); v.read("m22", m[2][2]); v.read("m23", m[2][3]);
		v.read("m30", m[3][0]); v.read("m31", m[3][1]); v.read("m32", m[3][2]); v.read("m33", m[3][3]);
	}
};

/// @brief スカラー * Mat4 演算子
template <FloatingPoint T>
[[nodiscard]] constexpr Mat4<T> operator*(T scalar, const Mat4<T>& mat) noexcept
{
	return mat * scalar;
}

// ── エイリアス ──────────────────────────────────────────────────

using Mat4f = Mat4<float>;    ///< float版 Mat4
using Mat4d = Mat4<double>;   ///< double版 Mat4

} // namespace sgc
