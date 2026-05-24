#pragma once

/// @file Camera.hpp
/// @brief 3Dカメラ
///
/// 透視投影・正射影の切り替え、カメラシェイク、ターゲット追従を提供する。
///
/// @code
/// sgc::Cameraf camera;
/// camera.setPosition({0, 5, -10});
/// camera.setTarget({0, 0, 0});
/// camera.setPerspective(sgc::toRadians(60.0f), 16.0f/9.0f, 0.1f, 1000.0f);
///
/// auto vp = camera.projectionMatrix() * camera.viewMatrix();
/// @endcode

#include <cmath>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/Interpolation.hpp"

namespace sgc
{

/// @brief 3Dカメラ
/// @tparam T 浮動小数点型
template <FloatingPoint T>
class Camera
{
public:
	/// @brief カメラ位置を設定する
	/// @param pos 位置
	void setPosition(const Vec3<T>& pos) { m_position = pos; }

	/// @brief カメラ位置を取得する
	/// @return 位置
	[[nodiscard]] const Vec3<T>& position() const noexcept { return m_position; }

	/// @brief 注視点を設定する
	/// @param target 注視点
	void setTarget(const Vec3<T>& target) { m_target = target; }

	/// @brief 注視点を取得する
	/// @return 注視点
	[[nodiscard]] const Vec3<T>& target() const noexcept { return m_target; }

	/// @brief 上方向ベクトルを設定する
	/// @param up 上方向
	void setUp(const Vec3<T>& up) { m_up = up; }

	/// @brief 上方向ベクトルを取得する
	/// @return 上方向
	[[nodiscard]] const Vec3<T>& up() const noexcept { return m_up; }

	/// @brief 透視投影を設定する
	/// @param fovY 垂直視野角（ラジアン）
	/// @param aspect アスペクト比
	/// @param nearZ ニアクリップ距離
	/// @param farZ ファークリップ距離
	void setPerspective(T fovY, T aspect, T nearZ, T farZ)
	{
		m_fovY = fovY;
		m_aspect = aspect;
		m_nearZ = nearZ;
		m_farZ = farZ;
		m_orthographic = false;
	}

	/// @brief 正射影を設定する
	/// @param left 左端
	/// @param right 右端
	/// @param bottom 下端
	/// @param top 上端
	/// @param nearZ ニアクリップ距離
	/// @param farZ ファークリップ距離
	void setOrthographic(T left, T right, T bottom, T top, T nearZ, T farZ)
	{
		m_orthoLeft = left;
		m_orthoRight = right;
		m_orthoBottom = bottom;
		m_orthoTop = top;
		m_nearZ = nearZ;
		m_farZ = farZ;
		m_orthographic = true;
	}

	/// @brief ビュー行列を計算する
	/// @return ビュー行列
	[[nodiscard]] Mat4<T> viewMatrix() const
	{
		auto shakeOffset = Vec3<T>{m_shakeOffsetX, m_shakeOffsetY, T{0}};
		return Mat4<T>::lookAt(m_position + shakeOffset, m_target, m_up);
	}

	/// @brief 投影行列を計算する
	/// @return 投影行列
	[[nodiscard]] Mat4<T> projectionMatrix() const
	{
		if (m_orthographic)
		{
			return Mat4<T>::orthographic(
				m_orthoLeft, m_orthoRight,
				m_orthoBottom, m_orthoTop,
				m_nearZ, m_farZ);
		}
		return Mat4<T>::perspective(m_fovY * m_zoom, m_aspect, m_nearZ, m_farZ);
	}

	/// @brief カメラシェイクを開始する
	/// @param intensity 揺れの強度
	/// @param duration 揺れの持続時間（秒）
	void shake(T intensity, T duration)
	{
		m_shakeIntensity = intensity;
		m_shakeDuration = duration;
		m_shakeTimer = duration;
	}

	/// @brief ターゲットにスムーズに追従する
	/// @param targetPos 追従先の位置
	/// @param smoothing スムージング係数（0〜1、小さいほど遅延大）
	/// @param dt デルタタイム
	void follow(const Vec3<T>& targetPos, T smoothing, T dt)
	{
		const T t = T{1} - std::pow(T{1} - smoothing, dt);
		m_position.x = lerp(m_position.x, targetPos.x, t);
		m_position.y = lerp(m_position.y, targetPos.y, t);
		m_position.z = lerp(m_position.z, targetPos.z, t);
	}

	/// @brief ズーム係数を設定する
	/// @param factor ズーム係数（1.0で等倍、小さいほどズームイン）
	void setZoom(T factor)
	{
		m_zoom = factor;
	}

	/// @brief ズーム係数を取得する
	/// @return ズーム係数
	[[nodiscard]] T zoom() const noexcept { return m_zoom; }

	/// @brief カメラを更新する（シェイクのタイマー進行）
	/// @param dt デルタタイム
	void update(T dt)
	{
		if (m_shakeTimer > T{0})
		{
			m_shakeTimer -= dt;
			if (m_shakeTimer <= T{0})
			{
				// シェイク終了
				m_shakeTimer = T{0};
				m_shakeOffsetX = T{0};
				m_shakeOffsetY = T{0};
			}
			else
			{
				const T ratio = m_shakeTimer / m_shakeDuration;
				const T currentIntensity = m_shakeIntensity * ratio;

				// 簡易的な擬似ランダムシェイク（sinベース）
				m_shakeOffsetX = currentIntensity
					* std::sin(m_shakeTimer * T{73.13});
				m_shakeOffsetY = currentIntensity
					* std::cos(m_shakeTimer * T{91.07});
			}
		}
	}

	/// @brief 正射影モードかどうか
	/// @return 正射影ならtrue
	[[nodiscard]] bool isOrthographic() const noexcept { return m_orthographic; }

private:
	Vec3<T> m_position{T{0}, T{0}, T{-10}};  ///< カメラ位置
	Vec3<T> m_target{T{0}, T{0}, T{0}};      ///< 注視点
	Vec3<T> m_up{T{0}, T{1}, T{0}};          ///< 上方向

	// 透視投影パラメータ
	T m_fovY{T{1.0472}};    ///< 垂直視野角（約60度）
	T m_aspect{T{1.7778}};  ///< アスペクト比（16:9）
	T m_nearZ{T{0.1}};      ///< ニアクリップ
	T m_farZ{T{1000}};      ///< ファークリップ

	// 正射影パラメータ
	T m_orthoLeft{T{-1}};   ///< 左端
	T m_orthoRight{T{1}};   ///< 右端
	T m_orthoBottom{T{-1}}; ///< 下端
	T m_orthoTop{T{1}};     ///< 上端
	bool m_orthographic{false}; ///< 正射影フラグ

	T m_zoom{T{1}};  ///< ズーム係数

	// シェイク
	T m_shakeIntensity{T{0}};  ///< シェイク強度
	T m_shakeDuration{T{0}};   ///< シェイク持続時間
	T m_shakeTimer{T{0}};      ///< シェイクタイマー
	T m_shakeOffsetX{T{0}};    ///< シェイクオフセットX
	T m_shakeOffsetY{T{0}};    ///< シェイクオフセットY
};

/// @brief float版 Camera
using Cameraf = Camera<float>;

/// @brief double版 Camera
using Camerad = Camera<double>;

} // namespace sgc
