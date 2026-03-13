#pragma once

/// @file ObjectFactory.hpp
/// @brief GraphWalkerオブジェクトファクトリ
///
/// すべてのゲームオブジェクトの生成を一箇所に集約するファクトリクラス。
///
/// @code
/// mitiru::gw::ObjectFactory factory;
/// auto player = factory.createPlayer({100, 200});
/// auto plat = factory.createStaticPlatform({0, 500}, {800, 20});
/// @endcode

#include <memory>
#include <string>
#include <vector>

#include "mitiru/graphwalker/GameObject.hpp"
#include "mitiru/graphwalker/Player.hpp"
#include "mitiru/graphwalker/Platforms.hpp"
#include "mitiru/graphwalker/Hazards.hpp"
#include "mitiru/graphwalker/Interactables.hpp"

namespace mitiru::gw
{

/// @brief ゲームオブジェクトファクトリ
///
/// 各種ゲームオブジェクトのunique_ptrを生成して返す。
/// オブジェクト生成ロジックを一箇所に集約する。
class ObjectFactory
{
public:
	// ── プレイヤー ─────────────────────────────────────────

	/// @brief プレイヤーを生成する
	/// @param pos 初期位置
	/// @param radius 半径
	/// @return プレイヤーのunique_ptr
	[[nodiscard]] static std::unique_ptr<Player> createPlayer(
		sgc::Vec2f pos, float radius = Player::DEFAULT_RADIUS)
	{
		return std::make_unique<Player>(pos, radius);
	}

	// ── プラットフォーム ───────────────────────────────────

	/// @brief 静的プラットフォームを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	[[nodiscard]] static std::unique_ptr<StaticPlatform> createStaticPlatform(
		sgc::Vec2f pos, sgc::Vec2f size)
	{
		return std::make_unique<StaticPlatform>(pos, size);
	}

	/// @brief 移動プラットフォームを生成する
	/// @param posA 始点
	/// @param posB 終点
	/// @param size 幅と高さ
	/// @param speed 補間速度
	[[nodiscard]] static std::unique_ptr<MovingPlatform> createMovingPlatform(
		sgc::Vec2f posA, sgc::Vec2f posB, sgc::Vec2f size, float speed)
	{
		return std::make_unique<MovingPlatform>(posA, posB, size, speed);
	}

	/// @brief 崩壊プラットフォームを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param crumbleTime 崩壊時間
	[[nodiscard]] static std::unique_ptr<CrumblingPlatform> createCrumblingPlatform(
		sgc::Vec2f pos, sgc::Vec2f size,
		float crumbleTime = CrumblingPlatform::DEFAULT_CRUMBLE_TIME)
	{
		return std::make_unique<CrumblingPlatform>(pos, size, crumbleTime);
	}

	/// @brief スプリングプラットフォームを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param bounceForce バウンス力
	[[nodiscard]] static std::unique_ptr<SpringPlatform> createSpringPlatform(
		sgc::Vec2f pos, sgc::Vec2f size,
		float bounceForce = SpringPlatform::DEFAULT_BOUNCE_FORCE)
	{
		return std::make_unique<SpringPlatform>(pos, size, bounceForce);
	}

	// ── 障害物 ────────────────────────────────────────────

	/// @brief トゲ障害物を生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param spikeCount トゲの本数
	[[nodiscard]] static std::unique_ptr<SpikeHazard> createSpikeHazard(
		sgc::Vec2f pos, sgc::Vec2f size, int spikeCount = 5)
	{
		return std::make_unique<SpikeHazard>(pos, size, spikeCount);
	}

	/// @brief レーザーバリアを生成する
	/// @param pos 基準位置
	/// @param length レーザーの長さ
	/// @param onTime オン時間
	/// @param offTime オフ時間
	[[nodiscard]] static std::unique_ptr<LaserBarrier> createLaserBarrier(
		sgc::Vec2f pos, float length, float onTime, float offTime)
	{
		return std::make_unique<LaserBarrier>(pos, length, onTime, offTime);
	}

	/// @brief 巡回敵を生成する
	/// @param patrolStart 巡回始点
	/// @param patrolEnd 巡回終点
	/// @param size 幅と高さ
	/// @param speed 移動速度
	[[nodiscard]] static std::unique_ptr<PatrolEnemy> createPatrolEnemy(
		sgc::Vec2f patrolStart, sgc::Vec2f patrolEnd,
		sgc::Vec2f size, float speed)
	{
		return std::make_unique<PatrolEnemy>(patrolStart, patrolEnd, size, speed);
	}

	// ── インタラクティブ ──────────────────────────────────

	/// @brief 収集アイテムを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param buttonId ボタン識別子
	[[nodiscard]] static std::unique_ptr<CollectibleBlock> createCollectible(
		sgc::Vec2f pos, sgc::Vec2f size, std::string buttonId)
	{
		return std::make_unique<CollectibleBlock>(pos, size, std::move(buttonId));
	}

	/// @brief チェックポイントを生成する
	/// @param pos 位置
	/// @param checkpointId チェックポイント識別子
	[[nodiscard]] static std::unique_ptr<CheckpointObj> createCheckpoint(
		sgc::Vec2f pos, std::string checkpointId)
	{
		return std::make_unique<CheckpointObj>(pos, std::move(checkpointId));
	}

	/// @brief NPCを生成する
	/// @param pos 位置
	/// @param npcId NPC識別子
	/// @param dialogue 対話テキスト
	/// @param interactionRadius 対話可能半径
	[[nodiscard]] static std::unique_ptr<NpcObj> createNpc(
		sgc::Vec2f pos, std::string npcId, std::string dialogue,
		float interactionRadius = NpcObj::DEFAULT_INTERACTION_RADIUS)
	{
		return std::make_unique<NpcObj>(
			pos, std::move(npcId), std::move(dialogue), interactionRadius);
	}

	/// @brief ゲートを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param requiredButtons 必要なボタンIDリスト
	[[nodiscard]] static std::unique_ptr<GateObj> createGate(
		sgc::Vec2f pos, sgc::Vec2f size, std::vector<std::string> requiredButtons)
	{
		return std::make_unique<GateObj>(pos, size, std::move(requiredButtons));
	}

	/// @brief ゴールを生成する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	[[nodiscard]] static std::unique_ptr<Goal> createGoal(
		sgc::Vec2f pos, sgc::Vec2f size = Goal::DEFAULT_SIZE)
	{
		return std::make_unique<Goal>(pos, size);
	}
};

} // namespace mitiru::gw
