#pragma once

/// @file CameraEffects.hpp
/// @brief 2Dカメラエフェクト（スムース追従・ズーム・範囲制限）
/// @details ゲームカメラの動きを滑らかにし、視覚効果を付与する。
///
/// @code
/// mitiru::effects::CameraEffects cam;
/// cam.setSmoothSpeed(5.0f);
/// cam.setBounds(0, 0, mapWidth, mapHeight);
/// // 毎フレーム:
/// cam.setTarget(playerPos);
/// cam.update(dt);
/// screen.setCamera(cam.offset(), cam.zoom());
/// @endcode

#include <algorithm>
#include <cmath>

#include <sgc/math/Vec2.hpp>

namespace mitiru::effects
{

/// @brief 2Dカメラエフェクト
class CameraEffects
{
public:
	/// @brief 追従ターゲットを設定する
	/// @param target ターゲット位置
	void setTarget(const sgc::Vec2f& target) noexcept
	{
		m_target = target;
	}

	/// @brief スムース追従速度を設定する
	/// @param speed 追従速度（高いほど速く追従）
	void setSmoothSpeed(float speed) noexcept
	{
		m_smoothSpeed = std::max(0.0f, speed);
	}

	/// @brief ズームを指定時間で変更する
	/// @param targetZoom 目標ズーム倍率
	/// @param duration アニメーション時間（秒）
	void zoomTo(float targetZoom, float duration = 0.5f)
	{
		m_zoomStart = m_currentZoom;
		m_zoomTarget = std::max(0.1f, targetZoom);
		m_zoomDuration = std::max(0.001f, duration);
		m_zoomElapsed = 0.0f;
		m_zooming = true;
	}

	/// @brief カメラ移動範囲を設定する
	/// @param minX 最小X
	/// @param minY 最小Y
	/// @param maxX 最大X
	/// @param maxY 最大Y
	void setBounds(float minX, float minY, float maxX, float maxY) noexcept
	{
		m_boundsMin = {minX, minY};
		m_boundsMax = {maxX, maxY};
		m_hasBounds = true;
	}

	/// @brief 範囲制限を解除する
	void clearBounds() noexcept { m_hasBounds = false; }

	/// @brief 更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		/// スムース追従
		if (m_smoothSpeed > 0.0f)
		{
			const float t = 1.0f - std::exp(-m_smoothSpeed * dt);
			m_currentPos.x += (m_target.x - m_currentPos.x) * t;
			m_currentPos.y += (m_target.y - m_currentPos.y) * t;
		}
		else
		{
			m_currentPos = m_target;
		}

		/// 範囲制限
		if (m_hasBounds)
		{
			m_currentPos.x = std::clamp(m_currentPos.x, m_boundsMin.x, m_boundsMax.x);
			m_currentPos.y = std::clamp(m_currentPos.y, m_boundsMin.y, m_boundsMax.y);
		}

		/// ズームアニメーション
		if (m_zooming)
		{
			m_zoomElapsed += dt;
			const float t = std::clamp(m_zoomElapsed / m_zoomDuration, 0.0f, 1.0f);
			/// smoothstep 補間
			const float s = t * t * (3.0f - 2.0f * t);
			m_currentZoom = m_zoomStart + (m_zoomTarget - m_zoomStart) * s;
			if (m_zoomElapsed >= m_zoomDuration)
			{
				m_currentZoom = m_zoomTarget;
				m_zooming = false;
			}
		}
	}

	/// @brief 現在のカメラオフセットを取得する
	[[nodiscard]] sgc::Vec2f offset() const noexcept { return m_currentPos; }

	/// @brief 現在のズーム倍率を取得する
	[[nodiscard]] float zoom() const noexcept { return m_currentZoom; }

	/// @brief ズームアニメーション中かどうか
	[[nodiscard]] bool isZooming() const noexcept { return m_zooming; }

	/// @brief 即座に位置を設定する（スムース追従をスキップ）
	void setPosition(const sgc::Vec2f& pos) noexcept
	{
		m_currentPos = pos;
		m_target = pos;
	}

	/// @brief 即座にズームを設定する
	void setZoom(float z) noexcept
	{
		m_currentZoom = std::max(0.1f, z);
		m_zoomTarget = m_currentZoom;
		m_zooming = false;
	}

private:
	sgc::Vec2f m_target{0, 0};        ///< 追従ターゲット
	sgc::Vec2f m_currentPos{0, 0};    ///< 現在位置
	float m_smoothSpeed = 5.0f;        ///< 追従速度

	float m_currentZoom = 1.0f;        ///< 現在のズーム
	float m_zoomTarget = 1.0f;         ///< 目標ズーム
	float m_zoomStart = 1.0f;          ///< ズーム開始値
	float m_zoomDuration = 0.5f;       ///< ズーム時間
	float m_zoomElapsed = 0.0f;        ///< ズーム経過時間
	bool m_zooming = false;            ///< ズーム中フラグ

	sgc::Vec2f m_boundsMin{0, 0};      ///< 範囲最小値
	sgc::Vec2f m_boundsMax{0, 0};      ///< 範囲最大値
	bool m_hasBounds = false;          ///< 範囲制限有効フラグ
};

} // namespace mitiru::effects
