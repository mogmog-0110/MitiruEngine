#pragma once

/// @file Interactables.hpp
/// @brief GraphWalkerインタラクティブオブジェクト群
///
/// 収集アイテム・チェックポイント・NPC・ゲート・ゴールを定義する。
///
/// @code
/// mitiru::gw::CollectibleBlock block({100, 200}, {30, 30}, "btn_A");
/// block.collect();
/// @endcode

#include <string>
#include <vector>
#include <algorithm>

#include "mitiru/graphwalker/GameObject.hpp"

namespace mitiru::gw
{

/// @brief 収集アイテムブロック
///
/// プレイヤーが触れると収集済みになるボタン的オブジェクト。
/// ゲートの解錠条件として使用される。
class CollectibleBlock final : public IGameObject
{
public:
	/// @brief 収集アイテムを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param buttonId ボタン識別子
	CollectibleBlock(sgc::Vec2f pos, sgc::Vec2f size, std::string buttonId)
		: m_position(pos), m_size(size), m_buttonId(std::move(buttonId)) {}

	void update(float /*dt*/) override {}

	/// @brief 収集する
	void collect()
	{
		m_isCollected = true;
		m_active = false;
	}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::CollectibleBlock; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active && !m_isCollected; }
	void setActive(bool active) override { m_active = active; }

	/// @brief 収集済みか取得する
	[[nodiscard]] bool isCollected() const { return m_isCollected; }
	/// @brief ボタンIDを取得する
	[[nodiscard]] const std::string& getButtonId() const { return m_buttonId; }

private:
	sgc::Vec2f m_position;       ///< 左上座標
	sgc::Vec2f m_size;           ///< 幅と高さ
	std::string m_buttonId;      ///< ボタン識別子
	bool m_isCollected{false};   ///< 収集済みフラグ
	bool m_active{true};         ///< アクティブフラグ
};

/// @brief チェックポイントオブジェクト
///
/// プレイヤーが通過するとアクティブ化される復帰地点。
class CheckpointObj final : public IGameObject
{
public:
	/// @brief チェックポイントのデフォルトサイズ
	static constexpr sgc::Vec2f DEFAULT_SIZE{20.0f, 40.0f};

	/// @brief チェックポイントを構築する
	/// @param pos 位置
	/// @param checkpointId チェックポイント識別子
	CheckpointObj(sgc::Vec2f pos, std::string checkpointId)
		: m_position(pos), m_checkpointId(std::move(checkpointId)) {}

	void update(float /*dt*/) override {}

	/// @brief チェックポイントを有効化する
	void activate() { m_isActivated = true; }

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::Checkpoint; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, DEFAULT_SIZE.x, DEFAULT_SIZE.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief 有効化済みか取得する
	[[nodiscard]] bool isActivated() const { return m_isActivated; }
	/// @brief チェックポイントIDを取得する
	[[nodiscard]] const std::string& getCheckpointId() const { return m_checkpointId; }

private:
	sgc::Vec2f m_position;          ///< 位置
	std::string m_checkpointId;     ///< チェックポイント識別子
	bool m_isActivated{false};      ///< 有効化フラグ
	bool m_active{true};            ///< アクティブフラグ
};

/// @brief NPCオブジェクト
///
/// プレイヤーが近づくと対話可能なNPCキャラクター。
class NpcObj final : public IGameObject
{
public:
	/// @brief NPCのデフォルトサイズ
	static constexpr sgc::Vec2f DEFAULT_SIZE{30.0f, 50.0f};
	/// @brief デフォルトの対話半径
	static constexpr float DEFAULT_INTERACTION_RADIUS = 60.0f;

	/// @brief NPCを構築する
	/// @param pos 位置
	/// @param npcId NPC識別子
	/// @param dialogue 対話テキスト
	/// @param interactionRadius 対話可能半径
	NpcObj(sgc::Vec2f pos, std::string npcId, std::string dialogue,
	       float interactionRadius = DEFAULT_INTERACTION_RADIUS)
		: m_position(pos)
		, m_npcId(std::move(npcId))
		, m_dialogue(std::move(dialogue))
		, m_interactionRadius(interactionRadius)
	{
	}

	void update(float /*dt*/) override {}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::NPC; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, DEFAULT_SIZE.x, DEFAULT_SIZE.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	/// @brief NPC IDを取得する
	[[nodiscard]] const std::string& getNpcId() const { return m_npcId; }
	/// @brief 対話テキストを取得する
	[[nodiscard]] const std::string& getDialogue() const { return m_dialogue; }
	/// @brief 対話可能半径を取得する
	[[nodiscard]] float getInteractionRadius() const { return m_interactionRadius; }

	/// @brief プレイヤーが対話範囲内にいるか判定する
	/// @param playerPos プレイヤー位置
	/// @return 範囲内ならtrue
	[[nodiscard]] bool isInRange(sgc::Vec2f playerPos) const
	{
		const sgc::Vec2f center{m_position.x + DEFAULT_SIZE.x * 0.5f,
		                        m_position.y + DEFAULT_SIZE.y * 0.5f};
		const sgc::Vec2f diff = playerPos - center;
		return diff.lengthSquared() <= m_interactionRadius * m_interactionRadius;
	}

private:
	sgc::Vec2f m_position;            ///< 位置
	std::string m_npcId;              ///< NPC識別子
	std::string m_dialogue;           ///< 対話テキスト
	float m_interactionRadius;        ///< 対話可能半径
	bool m_active{true};              ///< アクティブフラグ
};

/// @brief ゲートオブジェクト
///
/// 必要なボタンがすべて収集されると開くゲート。
class GateObj final : public IGameObject
{
public:
	/// @brief ゲートを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param requiredButtons 解錠に必要なボタンIDリスト
	GateObj(sgc::Vec2f pos, sgc::Vec2f size, std::vector<std::string> requiredButtons)
		: m_position(pos)
		, m_size(size)
		, m_requiredButtons(std::move(requiredButtons))
	{
	}

	void update(float /*dt*/) override {}

	/// @brief ゲートの開閉状態を確認・更新する
	/// @param collectedButtons 収集済みボタンIDリスト
	void tryOpen(const std::vector<std::string>& collectedButtons)
	{
		m_isOpen = std::all_of(
			m_requiredButtons.begin(), m_requiredButtons.end(),
			[&collectedButtons](const std::string& req)
			{
				return std::find(collectedButtons.begin(), collectedButtons.end(), req)
					!= collectedButtons.end();
			}
		);
	}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::Gate; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	/// @brief ゲートが開いているときは衝突しない（非アクティブ）
	[[nodiscard]] bool isActive() const override { return m_active && !m_isOpen; }
	void setActive(bool active) override { m_active = active; }

	/// @brief ゲートが開いているか取得する
	[[nodiscard]] bool isOpen() const { return m_isOpen; }
	/// @brief 必要なボタンリストを取得する
	[[nodiscard]] const std::vector<std::string>& getRequiredButtons() const { return m_requiredButtons; }

private:
	sgc::Vec2f m_position;                       ///< 左上座標
	sgc::Vec2f m_size;                           ///< 幅と高さ
	std::vector<std::string> m_requiredButtons;  ///< 解錠に必要なボタンIDリスト
	bool m_isOpen{false};                        ///< 開閉フラグ
	bool m_active{true};                         ///< アクティブフラグ
};

/// @brief ゴールオブジェクト
///
/// ステージクリアの到達地点。
class Goal final : public IGameObject
{
public:
	/// @brief デフォルトのゴールサイズ
	static constexpr sgc::Vec2f DEFAULT_SIZE{40.0f, 60.0f};

	/// @brief ゴールを構築する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	Goal(sgc::Vec2f pos, sgc::Vec2f size = DEFAULT_SIZE)
		: m_position(pos), m_size(size) {}

	void update(float /*dt*/) override {}

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::Goal; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{m_position.x, m_position.y, m_size.x, m_size.y};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

private:
	sgc::Vec2f m_position;    ///< 左上座標
	sgc::Vec2f m_size;        ///< 幅と高さ
	bool m_active{true};      ///< アクティブフラグ
};

} // namespace mitiru::gw
