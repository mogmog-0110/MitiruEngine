#pragma once

/// @file Platforms.hpp
/// @brief GraphWalkerプラットフォームオブジェクト群
///
/// 静的・移動・崩壊・スプリングの4種類のプラットフォームを定義する。
///
/// @code
/// mitiru::gw::StaticPlatform plat({100, 400}, {200, 20});
/// mitiru::gw::MovingPlatform mp({0, 300}, {400, 300}, {100, 20}, 50.0f);
/// mp.update(dt);
/// @endcode

#include <algorithm>
#include <cmath>

#include "mitiru/graphwalker/GameObject.hpp"

namespace mitiru::gw
{

/// @brief 静的プラットフォーム
///
/// 固定位置の矩形プラットフォーム。最も基本的な足場。
class StaticPlatform final : public IGameObject
{
public:
	/// @brief 静的プラットフォームを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	StaticPlatform(sgc::Vec2f pos, sgc::Vec2f size)
		: m_position(pos), m_size(size) {}

	void update(float /*dt*/) override {}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::StaticPlatform; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief サイズを取得する
	[[nodiscard]] sgc::Vec2f getSize() const { return m_size; }

private:
	sgc::Vec2f m_position;  ///< 左上座標
	sgc::Vec2f m_size;      ///< 幅と高さ
	bool m_active{true};    ///< アクティブフラグ
};

/// @brief 移動プラットフォーム
///
/// 2点間をピンポン往復する矩形プラットフォーム。
/// currentTが0〜1の間を行き来し、線形補間で位置を決定する。
class MovingPlatform final : public IGameObject
{
public:
	/// @brief 移動プラットフォームを構築する
	/// @param posA 始点
	/// @param posB 終点
	/// @param size 幅と高さ
	/// @param speed 移動速度（0〜1の補間速度/秒）
	MovingPlatform(sgc::Vec2f posA, sgc::Vec2f posB, sgc::Vec2f size, float speed)
		: m_posA(posA), m_posB(posB), m_size(size), m_speed(speed) {}

	/// @brief フレーム更新（ピンポン往復移動）
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		if (!m_active)
		{
			return;
		}

		if (m_movingForward)
		{
			m_currentT += m_speed * dt;
			if (m_currentT >= 1.0f)
			{
				m_currentT = 1.0f;
				m_movingForward = false;
			}
		}
		else
		{
			m_currentT -= m_speed * dt;
			if (m_currentT <= 0.0f)
			{
				m_currentT = 0.0f;
				m_movingForward = true;
			}
		}
	}

	[[nodiscard]] sgc::Vec2f getPosition() const override
	{
		return m_posA + (m_posB - m_posA) * m_currentT;
	}

	void setPosition(sgc::Vec2f pos) override { m_posA = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::MovingPlatform; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		const auto pos = getPosition();
		return sgc::Rectf{pos.x, pos.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief 現在の補間値を取得する（0.0〜1.0）
	[[nodiscard]] float getCurrentT() const { return m_currentT; }

	/// @brief サイズを取得する
	[[nodiscard]] sgc::Vec2f getSize() const { return m_size; }

private:
	sgc::Vec2f m_posA;           ///< 始点
	sgc::Vec2f m_posB;           ///< 終点
	sgc::Vec2f m_size;           ///< 幅と高さ
	float m_speed{1.0f};         ///< 補間速度（/秒）
	float m_currentT{0.0f};     ///< 現在の補間値
	bool m_movingForward{true}; ///< 順方向移動フラグ
	bool m_active{true};         ///< アクティブフラグ
};

/// @brief 崩壊プラットフォーム
///
/// プレイヤーが乗ると一定時間後に崩壊して消える。
/// isCrumblingがtrueになるとタイマー進行、破壊後はisDestroyedがtrue。
class CrumblingPlatform final : public IGameObject
{
public:
	/// @brief デフォルトの崩壊時間（秒）
	static constexpr float DEFAULT_CRUMBLE_TIME = 1.0f;

	/// @brief 崩壊プラットフォームを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param crumbleTime 崩壊までの時間（秒）
	CrumblingPlatform(sgc::Vec2f pos, sgc::Vec2f size,
	                  float crumbleTime = DEFAULT_CRUMBLE_TIME)
		: m_position(pos), m_size(size), m_crumbleTime(crumbleTime) {}

	/// @brief フレーム更新（崩壊タイマー進行）
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		if (!m_active || m_isDestroyed)
		{
			return;
		}

		if (m_isCrumbling)
		{
			m_crumbleTimer += dt;
			if (m_crumbleTimer >= m_crumbleTime)
			{
				m_isDestroyed = true;
				m_active = false;
			}
		}
	}

	/// @brief 崩壊を開始する（プレイヤー着地時に呼び出す）
	void startCrumbling() { m_isCrumbling = true; }

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::CrumblingPlatform; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active && !m_isDestroyed; }
	void setActive(bool active) override { m_active = active; }

	/// @brief 崩壊中か取得する
	[[nodiscard]] bool isCrumbling() const { return m_isCrumbling; }
	/// @brief 破壊済みか取得する
	[[nodiscard]] bool isDestroyed() const { return m_isDestroyed; }
	/// @brief サイズを取得する
	[[nodiscard]] sgc::Vec2f getSize() const { return m_size; }

private:
	sgc::Vec2f m_position;         ///< 左上座標
	sgc::Vec2f m_size;             ///< 幅と高さ
	float m_crumbleTime;           ///< 崩壊までの時間（秒）
	float m_crumbleTimer{0.0f};    ///< 崩壊タイマー
	bool m_isCrumbling{false};     ///< 崩壊中フラグ
	bool m_isDestroyed{false};     ///< 破壊済みフラグ
	bool m_active{true};           ///< アクティブフラグ
};

/// @brief スプリングプラットフォーム
///
/// プレイヤーが着地するとバウンスフォースで跳ね返す特殊足場。
class SpringPlatform final : public IGameObject
{
public:
	/// @brief デフォルトのバウンス力（ピクセル/秒）
	static constexpr float DEFAULT_BOUNCE_FORCE = 800.0f;

	/// @brief スプリングプラットフォームを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param bounceForce バウンス力
	SpringPlatform(sgc::Vec2f pos, sgc::Vec2f size,
	               float bounceForce = DEFAULT_BOUNCE_FORCE)
		: m_position(pos), m_size(size), m_bounceForce(bounceForce) {}

	void update(float /*dt*/) override {}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::SpringPlatform; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief バウンス力を取得する
	[[nodiscard]] float getBounceForce() const { return m_bounceForce; }
	/// @brief サイズを取得する
	[[nodiscard]] sgc::Vec2f getSize() const { return m_size; }

private:
	sgc::Vec2f m_position;         ///< 左上座標
	sgc::Vec2f m_size;             ///< 幅と高さ
	float m_bounceForce;           ///< バウンス力
	bool m_active{true};           ///< アクティブフラグ
};

} // namespace mitiru::gw
