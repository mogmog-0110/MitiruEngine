#pragma once

/// @file Hazards.hpp
/// @brief GraphWalker障害物オブジェクト群
///
/// トゲ・レーザーバリア・巡回敵の3種類の障害物を定義する。
///
/// @code
/// mitiru::gw::LaserBarrier laser({200, 100}, 150.0f, 2.0f, 1.0f);
/// laser.update(dt);
/// if (laser.isOn()) { /* レーザー有効 */ }
/// @endcode

#include <cmath>

#include "mitiru/graphwalker/GameObject.hpp"

namespace mitiru::gw
{

/// @brief トゲ障害物
///
/// 触れるとプレイヤーにダメージを与える固定障害物。
class SpikeHazard final : public IGameObject
{
public:
	/// @brief トゲ障害物を構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param spikeCount トゲの本数（描画用）
	SpikeHazard(sgc::Vec2f pos, sgc::Vec2f size, int spikeCount = 5)
		: m_position(pos), m_size(size), m_spikeCount(spikeCount) {}

	void update(float /*dt*/) override {}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::SpikeHazard; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief トゲの本数を取得する
	[[nodiscard]] int getSpikeCount() const { return m_spikeCount; }

private:
	sgc::Vec2f m_position;    ///< 左上座標
	sgc::Vec2f m_size;        ///< 幅と高さ
	int m_spikeCount;         ///< トゲの本数
	bool m_active{true};      ///< アクティブフラグ
};

/// @brief レーザーバリア
///
/// 一定間隔でオン・オフを切り替えるレーザー障害物。
/// オン状態のときに触れるとダメージを与える。
class LaserBarrier final : public IGameObject
{
public:
	/// @brief レーザーバリアを構築する
	/// @param pos 基準位置
	/// @param length レーザーの長さ
	/// @param onTime オン状態の持続時間（秒）
	/// @param offTime オフ状態の持続時間（秒）
	LaserBarrier(sgc::Vec2f pos, float length, float onTime, float offTime)
		: m_position(pos)
		, m_length(length)
		, m_onTime(onTime)
		, m_offTime(offTime)
	{
	}

	/// @brief フレーム更新（オン/オフ切り替え）
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		if (!m_active)
		{
			return;
		}

		m_timer += dt;

		if (m_isOn)
		{
			if (m_timer >= m_onTime)
			{
				m_isOn = false;
				m_timer = 0.0f;
			}
		}
		else
		{
			if (m_timer >= m_offTime)
			{
				m_isOn = true;
				m_timer = 0.0f;
			}
		}
	}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::LaserBarrier; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		// レーザーは縦方向の細い矩形として扱う
		return sgc::Rectf{m_position.x, m_position.y, 4.0f, m_length};
	}

	[[nodiscard]] bool isActive() const override { return m_active && m_isOn; }
	void setActive(bool active) override { m_active = active; }

	/// @brief レーザーがオン状態か取得する
	[[nodiscard]] bool isOn() const { return m_isOn; }
	/// @brief レーザーの長さを取得する
	[[nodiscard]] float getLength() const { return m_length; }

private:
	sgc::Vec2f m_position;    ///< 基準位置
	float m_length;           ///< レーザーの長さ
	float m_onTime;           ///< オン状態の持続時間
	float m_offTime;          ///< オフ状態の持続時間
	float m_timer{0.0f};      ///< 現在のタイマー
	bool m_isOn{true};        ///< オン状態フラグ
	bool m_active{true};      ///< アクティブフラグ
};

/// @brief 巡回敵
///
/// 2点間を往復巡回する敵オブジェクト。
/// 触れるとプレイヤーにダメージを与える。
class PatrolEnemy final : public IGameObject
{
public:
	/// @brief 巡回敵を構築する
	/// @param patrolStart 巡回始点
	/// @param patrolEnd 巡回終点
	/// @param size 幅と高さ
	/// @param speed 移動速度（ピクセル/秒）
	PatrolEnemy(sgc::Vec2f patrolStart, sgc::Vec2f patrolEnd,
	            sgc::Vec2f size, float speed)
		: m_position(patrolStart)
		, m_patrolStart(patrolStart)
		, m_patrolEnd(patrolEnd)
		, m_size(size)
		, m_speed(speed)
	{
	}

	/// @brief フレーム更新（巡回移動）
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		if (!m_active)
		{
			return;
		}

		const sgc::Vec2f direction = m_patrolEnd - m_patrolStart;
		const float totalDist = direction.length();

		if (totalDist < 1e-6f)
		{
			return;
		}

		if (m_movingForward)
		{
			m_currentT += (m_speed / totalDist) * dt;
			if (m_currentT >= 1.0f)
			{
				m_currentT = 1.0f;
				m_movingForward = false;
			}
		}
		else
		{
			m_currentT -= (m_speed / totalDist) * dt;
			if (m_currentT <= 0.0f)
			{
				m_currentT = 0.0f;
				m_movingForward = true;
			}
		}

		m_position = m_patrolStart + direction * m_currentT;
	}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::PatrolEnemy; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief 巡回始点を取得する
	[[nodiscard]] sgc::Vec2f getPatrolStart() const { return m_patrolStart; }
	/// @brief 巡回終点を取得する
	[[nodiscard]] sgc::Vec2f getPatrolEnd() const { return m_patrolEnd; }

private:
	sgc::Vec2f m_position;         ///< 現在位置
	sgc::Vec2f m_patrolStart;      ///< 巡回始点
	sgc::Vec2f m_patrolEnd;        ///< 巡回終点
	sgc::Vec2f m_size;             ///< 幅と高さ
	float m_speed;                 ///< 移動速度
	float m_currentT{0.0f};       ///< 現在の補間値
	bool m_movingForward{true};   ///< 順方向移動フラグ
	bool m_active{true};           ///< アクティブフラグ
};

} // namespace mitiru::gw
