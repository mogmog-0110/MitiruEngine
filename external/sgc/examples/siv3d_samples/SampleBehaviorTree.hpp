#pragma once

/// @file SampleBehaviorTree.hpp
/// @brief ビヘイビアツリーAI可視化デモ
///
/// マウスカーソル = プレイヤー。3体のNPCがビヘイビアツリーで行動する。
/// - 150px以内: Chase（赤、プレイヤーに接近）
/// - 300px以内: Alert（黄、プレイヤーを注視）
/// - それ以外:  Patrol（青、ランダム巡回）
/// - ESC: メニューに戻る

#include <array>
#include <cmath>
#include <random>
#include <string>

#include "sgc/ai/BehaviorTree.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief NPC1体分のデータ
struct NpcData
{
	sgc::Vec2f pos{};            ///< 現在位置
	sgc::Vec2f patrolTarget{};   ///< 巡回目標点
	float patrolTimer{0.0f};     ///< 巡回先変更タイマー
	std::string behavior{};      ///< 現在の行動ラベル
	sgc::Colorf color{};         ///< 描画色
};

/// @brief ビヘイビアツリーAIデモシーン
class SampleBehaviorTree : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		// NPC初期配置
		m_npcs[0] = NpcData{{200.0f, 200.0f}, {200.0f, 200.0f}, 0.0f, "Patrol", sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f}};
		m_npcs[1] = NpcData{{600.0f, 150.0f}, {600.0f, 150.0f}, 0.0f, "Patrol", sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f}};
		m_npcs[2] = NpcData{{400.0f, 400.0f}, {400.0f, 400.0f}, 0.0f, "Patrol", sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f}};

		// ビヘイビアツリー構築（全NPC共通のロジック）
		buildBehaviorTree();
	}

	/// @brief 更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// プレイヤー位置 = マウス
		m_playerPos = input->mousePosition();

		// 各NPCのAIを更新
		for (std::size_t i = 0; i < m_npcs.size(); ++i)
		{
			updateNpc(i, dt);
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;

		r->clearBackground(sgc::Colorf{0.05f, 0.08f, 0.05f, 1.0f});

		// 背景の草原ドット（雰囲気）
		for (int gx = 0; gx < 40; ++gx)
		{
			for (int gy = 0; gy < 30; ++gy)
			{
				if ((gx + gy) % 7 == 0)
				{
					const float dotX = static_cast<float>(gx) * 20.0f + 10.0f;
					const float dotY = static_cast<float>(gy) * 20.0f + 10.0f;
					r->drawCircle({dotX, dotY}, 1.5f,
						sgc::Colorf{0.1f, 0.2f, 0.1f, 0.4f});
				}
			}
		}

		// NPC検出範囲リングと本体を描画
		for (const auto& npc : m_npcs)
		{
			// 巡回範囲（外円 = 300px）
			r->drawCircleFrame(npc.pos, ALERT_RANGE, 1.0f,
				sgc::Colorf{0.4f, 0.4f, 0.2f, 0.3f});
			// 追跡範囲（内円 = 150px）
			r->drawCircleFrame(npc.pos, CHASE_RANGE, 1.0f,
				sgc::Colorf{0.6f, 0.2f, 0.2f, 0.3f});

			// NPC本体
			r->drawCircle(npc.pos, NPC_RADIUS, npc.color);
			// 輪郭
			r->drawCircleFrame(npc.pos, NPC_RADIUS, 2.0f,
				sgc::Colorf{1.0f, 1.0f, 1.0f, 0.5f});

			// NPCの目（プレイヤー方向を向く）
			const sgc::Vec2f toPlayer = (m_playerPos - npc.pos);
			const float dist = toPlayer.length();
			sgc::Vec2f eyeDir = (dist > 0.01f) ? toPlayer / dist : sgc::Vec2f{0.0f, -1.0f};
			const sgc::Vec2f eyePos = npc.pos + eyeDir * 8.0f;
			r->drawCircle(eyePos, 4.0f, sgc::Colorf::white());
			r->drawCircle(eyePos + eyeDir * 2.0f, 2.0f, sgc::Colorf::black());

			// 行動ラベル
			tr->drawTextCentered(npc.behavior, 14.0f,
				{npc.pos.x, npc.pos.y - NPC_RADIUS - 14.0f},
				npc.color);
		}

		// プレイヤー（マウス位置に緑の円）
		r->drawCircle(m_playerPos, PLAYER_RADIUS, sgc::Colorf{0.2f, 1.0f, 0.3f, 0.8f});
		r->drawCircleFrame(m_playerPos, PLAYER_RADIUS, 2.0f, sgc::Colorf::white());
		tr->drawTextCentered("Player", 14.0f,
			{m_playerPos.x, m_playerPos.y - PLAYER_RADIUS - 14.0f},
			sgc::Colorf{0.2f, 1.0f, 0.3f, 1.0f});

		// タイトルと説明
		tr->drawText("Behavior Tree AI Demo", 24.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.8f, 1.0f, 0.8f, 1.0f});

		// 凡例
		const float legendY = 50.0f;
		r->drawCircle({20.0f, legendY + 6.0f}, 6.0f, sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f});
		tr->drawText("Chase (< 150px)", 14.0f, {32.0f, legendY}, sgc::Colorf::white());

		r->drawCircle({20.0f, legendY + 24.0f}, 6.0f, sgc::Colorf{1.0f, 0.9f, 0.2f, 1.0f});
		tr->drawText("Alert (< 300px)", 14.0f, {32.0f, legendY + 18.0f}, sgc::Colorf::white());

		r->drawCircle({20.0f, legendY + 42.0f}, 6.0f, sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f});
		tr->drawText("Patrol (wander)", 14.0f, {32.0f, legendY + 36.0f}, sgc::Colorf::white());

		tr->drawText("ESC: Back to Menu", 14.0f,
			{660.0f, 575.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

private:
	static constexpr float CHASE_RANGE = 150.0f;   ///< 追跡開始距離
	static constexpr float ALERT_RANGE = 300.0f;   ///< 警戒開始距離
	static constexpr float NPC_RADIUS = 18.0f;     ///< NPC半径
	static constexpr float PLAYER_RADIUS = 14.0f;  ///< プレイヤー半径
	static constexpr float CHASE_SPEED = 180.0f;   ///< 追跡速度
	static constexpr float PATROL_SPEED = 60.0f;   ///< 巡回速度
	static constexpr float PATROL_INTERVAL = 2.5f; ///< 巡回先変更間隔

	std::array<NpcData, 3> m_npcs;
	sgc::Vec2f m_playerPos{400.0f, 300.0f};
	std::unique_ptr<sgc::bt::Node> m_tree;
	sgc::bt::Blackboard m_bb;
	std::mt19937 m_rng{std::random_device{}()};

	/// @brief ビヘイビアツリーを構築する
	void buildBehaviorTree()
	{
		// ツリー構造:
		// Selector
		//   Sequence (Chase)
		//     Condition: distance < CHASE_RANGE
		//     Action: move toward player, set "Chase"
		//   Sequence (Alert)
		//     Condition: distance < ALERT_RANGE
		//     Action: face player, set "Alert"
		//   Action (Patrol)
		//     wander randomly, set "Patrol"

		m_tree = sgc::bt::Builder()
			.selector()
				.sequence()
					.condition([](sgc::bt::Blackboard& bb) -> bool
					{
						return bb.get<float>("distance").value_or(9999.0f) < CHASE_RANGE;
					})
					.action([](sgc::bt::Blackboard& bb) -> sgc::bt::Status
					{
						bb.set<std::string>("behavior", std::string("Chase"));
						return sgc::bt::Status::Success;
					})
				.end()
				.sequence()
					.condition([](sgc::bt::Blackboard& bb) -> bool
					{
						return bb.get<float>("distance").value_or(9999.0f) < ALERT_RANGE;
					})
					.action([](sgc::bt::Blackboard& bb) -> sgc::bt::Status
					{
						bb.set<std::string>("behavior", std::string("Alert"));
						return sgc::bt::Status::Success;
					})
				.end()
				.action([](sgc::bt::Blackboard& bb) -> sgc::bt::Status
				{
					bb.set<std::string>("behavior", std::string("Patrol"));
					return sgc::bt::Status::Success;
				})
			.end()
			.build();
	}

	/// @brief NPC1体の更新
	/// @param index NPCインデックス
	/// @param dt デルタタイム
	void updateNpc(std::size_t index, float dt)
	{
		auto& npc = m_npcs[index];

		// ブラックボードに距離を設定
		const float dist = npc.pos.distanceTo(m_playerPos);
		m_bb.set<float>("distance", dist);

		// ツリー実行
		m_tree->tick(m_bb);

		// 行動結果に基づいて移動
		const auto behaviorOpt = m_bb.get<std::string>("behavior");
		const std::string behavior = behaviorOpt.value_or("Patrol");
		npc.behavior = behavior;

		if (behavior == "Chase")
		{
			// 赤: プレイヤーに向かって移動
			npc.color = sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f};
			const sgc::Vec2f dir = (m_playerPos - npc.pos).normalized();
			npc.pos += dir * CHASE_SPEED * dt;
		}
		else if (behavior == "Alert")
		{
			// 黄: その場で注視（ゆっくり揺れる）
			npc.color = sgc::Colorf{1.0f, 0.9f, 0.2f, 1.0f};
			// わずかに揺れる
			std::uniform_real_distribution<float> jitter(-5.0f, 5.0f);
			npc.pos.x += jitter(m_rng) * dt;
			npc.pos.y += jitter(m_rng) * dt;
		}
		else
		{
			// 青: 巡回
			npc.color = sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f};
			npc.patrolTimer -= dt;
			if (npc.patrolTimer <= 0.0f)
			{
				// 新しい巡回先を設定
				std::uniform_real_distribution<float> distX(80.0f, 720.0f);
				std::uniform_real_distribution<float> distY(80.0f, 520.0f);
				npc.patrolTarget = {distX(m_rng), distY(m_rng)};
				npc.patrolTimer = PATROL_INTERVAL;
			}

			const sgc::Vec2f toTarget = npc.patrolTarget - npc.pos;
			const float targetDist = toTarget.length();
			if (targetDist > 2.0f)
			{
				npc.pos += (toTarget / targetDist) * PATROL_SPEED * dt;
			}
		}

		// 画面端クランプ
		npc.pos = npc.pos.clamped({NPC_RADIUS, NPC_RADIUS}, {800.0f - NPC_RADIUS, 600.0f - NPC_RADIUS});
	}
};
