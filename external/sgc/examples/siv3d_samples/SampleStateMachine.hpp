#pragma once

/// @file SampleStateMachine.hpp
/// @brief StateMachine（状態マシン）ビジュアルサンプル
///
/// variantベースの状態マシンでキャラクターの5状態を可視化する。
/// 各状態は色・アニメーションで視覚的に区別される。
/// - 矢印キー左右: Walking/Running
/// - 矢印キー上: Jumping
/// - 物理演算で自動遷移: Falling
/// - ESCキー: メニューに戻る

#include <cmath>
#include <optional>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/patterns/StateMachine.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

namespace sample_fsm
{

/// @brief 待機状態（青、脈動アニメーション）
struct Idle
{
	float timer{0.0f};  ///< 脈動用タイマー
};

/// @brief 歩行状態（緑、低速移動）
struct Walking
{
	float direction{0.0f};  ///< 移動方向（-1: 左、1: 右）
	float holdTime{0.0f};   ///< 押し続け時間（Running遷移判定用）
};

/// @brief 走行状態（シアン、高速移動）
struct Running
{
	float direction{0.0f};  ///< 移動方向（-1: 左、1: 右）
};

/// @brief ジャンプ状態（黄、上昇中）
struct Jumping
{
	float vy{0.0f};        ///< 垂直速度(px/s)
	float startY{0.0f};    ///< ジャンプ開始Y座標
};

/// @brief 落下状態（橙、下降中）
struct Falling
{
	float vy{0.0f};  ///< 垂直速度(px/s)
};

/// @brief キャラクターFSM型
using CharacterFSM = sgc::StateMachine<Idle, Walking, Running, Jumping, Falling>;

} // namespace sample_fsm

/// @brief StateMachineビジュアルサンプルシーン
///
/// 画面中央のキャラクター（矩形）が5つの状態（Idle, Walking, Running,
/// Jumping, Falling）を遷移する。各状態は色とアニメーションで区別される。
class SampleStateMachine : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_charX = getData().screenWidth * 0.5f;
		m_charY = GROUND_Y;
		m_fsm = sample_fsm::CharacterFSM{sample_fsm::Idle{}};
		m_stateName = "Idle";
		m_stateColor = COLOR_IDLE;
		m_bobOffset = 0.0f;

		// 遷移コールバック: 状態名と色を自動更新
		m_fsm.setOnEnter([this](const sample_fsm::CharacterFSM::StateVariant& state)
		{
			std::visit([this](const auto& s)
			{
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, sample_fsm::Idle>)
				{
					m_stateName = "Idle";
					m_stateColor = COLOR_IDLE;
				}
				else if constexpr (std::is_same_v<T, sample_fsm::Walking>)
				{
					m_stateName = "Walking";
					m_stateColor = COLOR_WALKING;
				}
				else if constexpr (std::is_same_v<T, sample_fsm::Running>)
				{
					m_stateName = "Running";
					m_stateColor = COLOR_RUNNING;
				}
				else if constexpr (std::is_same_v<T, sample_fsm::Jumping>)
				{
					m_stateName = "Jumping";
					m_stateColor = COLOR_JUMPING;
				}
				else if constexpr (std::is_same_v<T, sample_fsm::Falling>)
				{
					m_stateName = "Falling";
					m_stateColor = COLOR_FALLING;
				}
			}, state);
		});
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto& input = *getData().inputProvider;

		// ESCでメニューに戻る
		if (input.isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		m_time += dt;

		// 入力読み取り
		const bool leftHeld = input.isKeyDown(KeyCode::LEFT);
		const bool rightHeld = input.isKeyDown(KeyCode::RIGHT);
		const bool upPressed = input.isKeyJustPressed(KeyCode::UP);

		// 状態マシン更新
		m_fsm.update([&](auto& state) -> std::optional<sample_fsm::CharacterFSM::StateVariant>
		{
			using T = std::decay_t<decltype(state)>;

			if constexpr (std::is_same_v<T, sample_fsm::Idle>)
			{
				state.timer += dt;
				// 脈動アニメーション
				m_bobOffset = 3.0f * std::sin(state.timer * 3.0f);

				if (upPressed)
				{
					return sample_fsm::Jumping{-JUMP_VELOCITY, m_charY};
				}
				if (leftHeld || rightHeld)
				{
					const float dir = rightHeld ? 1.0f : -1.0f;
					return sample_fsm::Walking{dir};
				}
			}
			else if constexpr (std::is_same_v<T, sample_fsm::Walking>)
			{
				m_bobOffset = 0.0f;
				m_charX += state.direction * WALK_SPEED * dt;
				clampCharX();

				if (upPressed)
				{
					return sample_fsm::Jumping{-JUMP_VELOCITY, m_charY};
				}
				// 左右両方押し or どちらも離した → Idle
				if (!leftHeld && !rightHeld)
				{
					return sample_fsm::Idle{};
				}
				// 方向更新
				if (leftHeld && !rightHeld)
				{
					state.direction = -1.0f;
				}
				else if (rightHeld && !leftHeld)
				{
					state.direction = 1.0f;
				}
				// 一定時間歩き続けたらRunningに遷移
				state.holdTime += dt;
				if (state.holdTime >= WALK_TO_RUN_TIME)
				{
					return sample_fsm::Running{state.direction};
				}
			}
			else if constexpr (std::is_same_v<T, sample_fsm::Running>)
			{
				m_bobOffset = 0.0f;
				m_charX += state.direction * RUN_SPEED * dt;
				clampCharX();

				if (upPressed)
				{
					return sample_fsm::Jumping{-JUMP_VELOCITY, m_charY};
				}
				if (!leftHeld && !rightHeld)
				{
					return sample_fsm::Idle{};
				}
				if (leftHeld && !rightHeld)
				{
					state.direction = -1.0f;
				}
				else if (rightHeld && !leftHeld)
				{
					state.direction = 1.0f;
				}
			}
			else if constexpr (std::is_same_v<T, sample_fsm::Jumping>)
			{
				m_bobOffset = 0.0f;
				state.vy += GRAVITY * dt;
				m_charY += state.vy * dt;

				// 空中で左右移動
				if (leftHeld)
				{
					m_charX -= WALK_SPEED * 0.7f * dt;
				}
				if (rightHeld)
				{
					m_charX += WALK_SPEED * 0.7f * dt;
				}
				clampCharX();

				// 上昇速度が0以下になったら落下状態へ
				if (state.vy >= 0.0f)
				{
					return sample_fsm::Falling{state.vy};
				}
			}
			else if constexpr (std::is_same_v<T, sample_fsm::Falling>)
			{
				m_bobOffset = 0.0f;
				state.vy += GRAVITY * dt;
				m_charY += state.vy * dt;

				// 空中で左右移動
				if (leftHeld)
				{
					m_charX -= WALK_SPEED * 0.7f * dt;
				}
				if (rightHeld)
				{
					m_charX += WALK_SPEED * 0.7f * dt;
				}
				clampCharX();

				// 地面に着地
				if (m_charY >= GROUND_Y)
				{
					m_charY = GROUND_Y;
					return sample_fsm::Idle{};
				}
			}

			return std::nullopt;
		});
	}

	/// @brief 描画処理
	void draw() const override
	{
		const auto& data = getData();
		auto* r = data.renderer;
		auto* tr = data.textRenderer;

		// 背景
		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// 地面ライン
		r->drawLine(
			{0.0f, GROUND_Y + CHAR_HALF_H + 2.0f},
			{data.screenWidth, GROUND_Y + CHAR_HALF_H + 2.0f},
			2.0f, sgc::Colorf{0.3f, 0.3f, 0.35f, 1.0f});

		// 地面グリッド（奥行き感）
		for (int i = 0; i < 20; ++i)
		{
			const float gx = static_cast<float>(i) * 40.0f;
			r->drawLine(
				{gx, GROUND_Y + CHAR_HALF_H + 2.0f},
				{gx, data.screenHeight},
				1.0f, sgc::Colorf{0.15f, 0.15f, 0.2f, 0.3f});
		}

		// キャラクター影
		const float shadowY = GROUND_Y + CHAR_HALF_H + 1.0f;
		const float heightFromGround = GROUND_Y - m_charY;
		const float shadowScale = 1.0f - heightFromGround / 200.0f;
		if (shadowScale > 0.1f)
		{
			const float shadowW = CHAR_HALF_W * shadowScale;
			r->drawRect(
				sgc::AABB2f{
					{m_charX - shadowW, shadowY - 3.0f},
					{m_charX + shadowW, shadowY + 3.0f}},
				sgc::Colorf{0.0f, 0.0f, 0.0f, 0.25f * shadowScale});
		}

		// キャラクター本体（色付き矩形）
		const float drawY = m_charY + m_bobOffset;
		r->drawRect(
			sgc::AABB2f{
				{m_charX - CHAR_HALF_W, drawY - CHAR_HALF_H},
				{m_charX + CHAR_HALF_W, drawY + CHAR_HALF_H}},
			m_stateColor);

		// 内側ハイライト
		r->drawRect(
			sgc::AABB2f{
				{m_charX - CHAR_HALF_W + 4.0f, drawY - CHAR_HALF_H + 4.0f},
				{m_charX - 2.0f, drawY - 4.0f}},
			sgc::Colorf{1.0f, 1.0f, 1.0f, 0.2f});

		// 外枠
		r->drawRectFrame(
			sgc::AABB2f{
				{m_charX - CHAR_HALF_W - 1.0f, drawY - CHAR_HALF_H - 1.0f},
				{m_charX + CHAR_HALF_W + 1.0f, drawY + CHAR_HALF_H + 1.0f}},
			2.0f,
			sgc::Colorf{m_stateColor.r * 0.6f, m_stateColor.g * 0.6f,
				m_stateColor.b * 0.6f, 1.0f});

		// 現在の状態名をキャラクター上部に大きく表示
		tr->drawTextCentered(
			m_stateName, 28.0f,
			{m_charX, drawY - CHAR_HALF_H - 30.0f},
			m_stateColor);

		// 状態遷移図を下部に描画
		drawStateDiagram();

		// 情報パネル
		r->drawRect(
			sgc::AABB2f{{0.0f, 0.0f}, {340.0f, 80.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.7f});

		tr->drawText(
			"State Machine Demo", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.9f, 0.6f, 1.0f, 1.0f});

		tr->drawText(
			"Left/Right: Walk (hold: Run)  Up: Jump", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		tr->drawText(
			"[Esc] Back to Menu", 14.0f,
			{10.0f, 56.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

private:
	static constexpr float CHAR_HALF_W = 20.0f;     ///< キャラクター半幅
	static constexpr float CHAR_HALF_H = 30.0f;     ///< キャラクター半高さ
	static constexpr float GROUND_Y = 340.0f;       ///< 地面のY座標（キャラ中心基準）
	static constexpr float WALK_SPEED = 180.0f;     ///< 歩行速度(px/s)
	static constexpr float RUN_SPEED = 350.0f;      ///< 走行速度(px/s)
	static constexpr float JUMP_VELOCITY = 500.0f;  ///< ジャンプ初速(px/s)
	static constexpr float GRAVITY = 900.0f;        ///< 重力加速度(px/s^2)
	static constexpr float WALK_TO_RUN_TIME = 0.8f; ///< 歩行→走行遷移時間(秒)

	/// @brief 状態別カラー定数
	static constexpr sgc::Colorf COLOR_IDLE   {0.3f, 0.5f, 1.0f, 1.0f};
	static constexpr sgc::Colorf COLOR_WALKING{0.3f, 0.9f, 0.4f, 1.0f};
	static constexpr sgc::Colorf COLOR_RUNNING{0.2f, 0.9f, 0.9f, 1.0f};
	static constexpr sgc::Colorf COLOR_JUMPING{1.0f, 0.9f, 0.2f, 1.0f};
	static constexpr sgc::Colorf COLOR_FALLING{1.0f, 0.5f, 0.15f, 1.0f};

	sample_fsm::CharacterFSM m_fsm{sample_fsm::Idle{}};  ///< キャラクターFSM
	float m_charX{400.0f};         ///< キャラクターX座標
	float m_charY{GROUND_Y};       ///< キャラクターY座標
	float m_bobOffset{0.0f};       ///< 脈動オフセット
	sgc::Colorf m_stateColor{COLOR_IDLE};  ///< 現在の状態色
	std::string m_stateName{"Idle"};        ///< 現在の状態名
	float m_time{0.0f};            ///< 経過時間

	/// @brief キャラクターX座標を画面内にクランプする
	void clampCharX()
	{
		const float sw = getData().screenWidth;
		if (m_charX < CHAR_HALF_W)
		{
			m_charX = CHAR_HALF_W;
		}
		if (m_charX > sw - CHAR_HALF_W)
		{
			m_charX = sw - CHAR_HALF_W;
		}
	}

	/// @brief 状態遷移図を画面下部に描画する
	void drawStateDiagram() const
	{
		const auto& data = getData();
		auto* r = data.renderer;
		auto* tr = data.textRenderer;
		const float diagramY = 480.0f;
		const float boxW = 90.0f;
		const float boxH = 32.0f;
		const float spacing = 30.0f;
		const float totalW = 5.0f * boxW + 4.0f * spacing;
		const float startX = (data.screenWidth - totalW) * 0.5f;

		struct StateBox
		{
			const char* name;
			sgc::Colorf color;
			float x;
		};

		const StateBox boxes[5] = {
			{"Idle",    COLOR_IDLE,    startX},
			{"Walking", COLOR_WALKING, startX + (boxW + spacing)},
			{"Running", COLOR_RUNNING, startX + 2.0f * (boxW + spacing)},
			{"Jumping", COLOR_JUMPING, startX + 3.0f * (boxW + spacing)},
			{"Falling", COLOR_FALLING, startX + 4.0f * (boxW + spacing)}
		};

		// ボックスと矢印を描画
		for (int i = 0; i < 5; ++i)
		{
			const auto& box = boxes[i];
			const sgc::AABB2f rect{
				{box.x, diagramY},
				{box.x + boxW, diagramY + boxH}};

			const bool isCurrent = (m_stateName == box.name);
			if (isCurrent)
			{
				r->drawRect(rect, box.color);
				tr->drawTextCentered(
					box.name, 12.0f,
					{box.x + boxW * 0.5f, diagramY + boxH * 0.5f},
					sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f});
			}
			else
			{
				r->drawRectFrame(rect, 1.0f, box.color);
				tr->drawTextCentered(
					box.name, 12.0f,
					{box.x + boxW * 0.5f, diagramY + boxH * 0.5f},
					box.color);
			}

			// 次の状態への矢印
			if (i < 4)
			{
				const float arrowStartX = box.x + boxW;
				const float arrowEndX = boxes[i + 1].x;
				const float arrowY = diagramY + boxH * 0.5f;
				r->drawLine(
					{arrowStartX + 3.0f, arrowY},
					{arrowEndX - 3.0f, arrowY},
					1.0f, sgc::Colorf{0.5f, 0.5f, 0.5f, 0.5f});

				// 矢印先端
				const float tipX = arrowEndX - 3.0f;
				r->drawTriangle(
					{tipX, arrowY},
					{tipX - 6.0f, arrowY - 3.0f},
					{tipX - 6.0f, arrowY + 3.0f},
					sgc::Colorf{0.5f, 0.5f, 0.5f, 0.5f});
			}
		}

		// Idle へ戻る矢印（下側を回り込む弧）
		const float returnY = diagramY + boxH + 14.0f;
		const float returnStartX = boxes[4].x + boxW * 0.5f;
		const float returnEndX = boxes[0].x + boxW * 0.5f;
		const sgc::Colorf returnColor{0.4f, 0.4f, 0.5f, 0.5f};

		r->drawLine({returnStartX, diagramY + boxH}, {returnStartX, returnY}, 1.0f, returnColor);
		r->drawLine({returnStartX, returnY}, {returnEndX, returnY}, 1.0f, returnColor);
		r->drawLine({returnEndX, returnY}, {returnEndX, diagramY + boxH}, 1.0f, returnColor);

		tr->drawTextCentered(
			"land -> Idle", 10.0f,
			{(returnStartX + returnEndX) * 0.5f, returnY + 12.0f},
			returnColor);

		// 遷移ラベル
		tr->drawTextCentered("L/R", 9.0f,
			{(boxes[0].x + boxW + boxes[1].x) * 0.5f, diagramY - 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 0.7f});
		tr->drawTextCentered("hold", 9.0f,
			{(boxes[1].x + boxW + boxes[2].x) * 0.5f, diagramY - 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 0.7f});
		tr->drawTextCentered("Up", 9.0f,
			{(boxes[2].x + boxW + boxes[3].x) * 0.5f, diagramY - 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 0.7f});
		tr->drawTextCentered("vy>=0", 9.0f,
			{(boxes[3].x + boxW + boxes[4].x) * 0.5f, diagramY - 8.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 0.7f});
	}
};
