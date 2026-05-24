#pragma once

/// @file Camera3D.hpp
/// @brief 3Dカメラ（ビュー・射影行列の生成）
///
/// FPS/TPSカメラ、オービットカメラなど3Dシーンのカメラ制御に使用する。
///
/// @code
/// sgc::graphics3d::Camera3D cam;
/// cam.position = {0, 5, 10};
/// cam.lookAt({0, 0, 0});
/// auto vp = cam.viewProjectionMatrix();
/// @endcode

#include <cmath>
#include <numbers>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc::graphics3d
{

/// @brief 3Dカメラ
///
/// ビュー行列・透視投影行列の生成、前方/右方ベクトルの取得、
/// lookAt・orbit操作を提供する。
struct Camera3D
{
	Vec3f position{0.0f, 0.0f, 5.0f};   ///< カメラ位置
	Vec3f target{0.0f, 0.0f, 0.0f};     ///< 注視点
	Vec3f up{0.0f, 1.0f, 0.0f};         ///< 上方向ベクトル
	float fovY = 60.0f;                  ///< 垂直視野角（度）
	float nearClip = 0.1f;              ///< ニアクリップ面
	float farClip = 1000.0f;            ///< ファークリップ面
	float aspectRatio = 16.0f / 9.0f;   ///< アスペクト比

	/// @brief ビュー行列を計算する
	/// @return ビュー行列（右手座標系）
	[[nodiscard]] Mat4f viewMatrix() const
	{
		return Mat4f::lookAt(position, target, up);
	}

	/// @brief 透視投影行列を計算する
	/// @return 透視投影行列
	[[nodiscard]] Mat4f projectionMatrix() const
	{
		const float fovRad = fovY * std::numbers::pi_v<float> / 180.0f;
		return Mat4f::perspective(fovRad, aspectRatio, nearClip, farClip);
	}

	/// @brief ビュー×投影の合成行列を計算する
	/// @return ビュー投影行列
	[[nodiscard]] Mat4f viewProjectionMatrix() const
	{
		return projectionMatrix() * viewMatrix();
	}

	/// @brief カメラの前方向ベクトルを返す（正規化済み）
	/// @return 前方向ベクトル（target方向）
	[[nodiscard]] Vec3f forward() const
	{
		return (target - position).normalized();
	}

	/// @brief カメラの右方向ベクトルを返す（正規化済み）
	/// @return 右方向ベクトル
	[[nodiscard]] Vec3f right() const
	{
		return forward().cross(up).normalized();
	}

	/// @brief 注視点を設定する
	/// @param newTarget 新しい注視点
	void lookAt(const Vec3f& newTarget)
	{
		target = newTarget;
	}

	/// @brief 注視点を中心にオービット回転する
	/// @param yaw ヨー角変化量（ラジアン）
	/// @param pitch ピッチ角変化量（ラジアン）
	/// @param distance 注視点からの距離
	void orbit(float yaw, float pitch, float distance)
	{
		const float cosP = std::cos(pitch);
		const float sinP = std::sin(pitch);
		const float cosY = std::cos(yaw);
		const float sinY = std::sin(yaw);

		position = target + Vec3f{
			distance * cosP * sinY,
			distance * sinP,
			distance * cosP * cosY
		};
	}
};

} // namespace sgc::graphics3d
