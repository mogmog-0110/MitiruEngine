#pragma once

/// @file SampleFixedTimestep.hpp
/// @brief FixedTimestep（固定タイムステップ）物理更新デモ
///
/// 2つのボールを並べて表示し、固定タイムステップと可変タイムステップの
/// 動作の違いを可視化する。
/// - 1/2キー: ステップサイズ変更（60Hz / 200Hz）
/// - Rキー: リセット
/// - ESCキー: メニューに戻る

#include <cmath>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/physics/FixedTimestep.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief FixedTimestepサンプルシーン
///
/// 左側に固定タイムステップで更新されるボール、
/// 右側に可変タイムステップで更新されるボールを並べて比較する。
class SampleFixedTimestep : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		resetSimulation();
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rでリセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			resetSimulation();
			return;
		}

		// 1キー: 60Hz ステップ
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_stepper.setStepSize(1.0 / 60.0);
			m_stepper.resetAccumulator();
			m_stepLabel = "60 Hz";
		}

		// 2キー: 200Hz ステップ
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_stepper.setStepSize(1.0 / 200.0);
			m_stepper.resetAccumulator();
			m_stepLabel = "200 Hz";
		}

		const float sh = getData().screenHeight;
		const float floorY = sh - FLOOR_HEIGHT;

		// 固定タイムステップ側の更新
		m_fixedStepCount = m_stepper.update(static_cast<double>(dt),
			[&](double step)
			{
				const float s = static_cast<float>(step);
				m_fixedVy += GRAVITY * s;
				m_fixedY += m_fixedVy * s;

				// 床との衝突
				if (m_fixedY + BALL_RADIUS > floorY)
				{
					m_fixedY = floorY - BALL_RADIUS;
					m_fixedVy = -m_fixedVy * RESTITUTION;
					if (std::abs(m_fixedVy) < 5.0f)
					{
						m_fixedVy = 0.0f;
					}
				}
			});
		m_fixedInterp = static_cast<float>(m_stepper.interpolationFactor());

		// 可変タイムステップ側の更新
		m_variableVy += GRAVITY * dt;
		m_variableY += m_variableVy * dt;

		if (m_variableY + BALL_RADIUS > floorY)
		{
			m_variableY = floorY - BALL_RADIUS;
			m_variableVy = -m_variableVy * RESTITUTION;
			if (std::abs(m_variableVy) < 5.0f)
			{
				m_variableVy = 0.0f;
			}
		}

		// 時間経過
		m_elapsed += dt;
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const float floorY = sh - FLOOR_HEIGHT;

		// 背景
		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"FixedTimestep Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.5f, 0.9f, 0.7f, 1.0f});

		// 区切り線（中央）
		renderer->drawLine(
			sgc::Vec2f{sw * 0.5f, 70.0f},
			sgc::Vec2f{sw * 0.5f, floorY},
			2.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.6f});

		// 床
		renderer->drawRect(
			sgc::AABB2f{{0.0f, floorY}, {sw, sh}},
			sgc::Colorf{0.2f, 0.22f, 0.28f, 1.0f});

		// ── 左側: 固定タイムステップ ──
		const float leftX = sw * 0.25f;

		text->drawTextCentered(
			"Fixed Timestep", 22.0f,
			sgc::Vec2f{leftX, 70.0f},
			sgc::Colorf{0.3f, 0.8f, 1.0f, 1.0f});

		// 補間を適用した描画位置
		const float interpOffset = m_fixedVy * m_fixedInterp
			* static_cast<float>(m_stepper.stepSize());
		float drawFixedY = m_fixedY + interpOffset;
		if (drawFixedY + BALL_RADIUS > floorY)
		{
			drawFixedY = floorY - BALL_RADIUS;
		}

		renderer->drawCircle(
			sgc::Vec2f{leftX, drawFixedY}, BALL_RADIUS,
			sgc::Colorf{0.3f, 0.7f, 1.0f, 1.0f});
		renderer->drawCircleFrame(
			sgc::Vec2f{leftX, drawFixedY}, BALL_RADIUS, 2.0f,
			sgc::Colorf{0.5f, 0.85f, 1.0f, 0.6f});

		// 固定側の情報
		text->drawText(
			"Step: " + m_stepLabel, 16.0f,
			sgc::Vec2f{20.0f, 100.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		text->drawText(
			"Steps/frame: " + std::to_string(m_fixedStepCount), 16.0f,
			sgc::Vec2f{20.0f, 122.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		text->drawText(
			"Interp: " + std::to_string(static_cast<int>(m_fixedInterp * 100.0f)) + "%", 16.0f,
			sgc::Vec2f{20.0f, 144.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// ── 右側: 可変タイムステップ ──
		const float rightX = sw * 0.75f;

		text->drawTextCentered(
			"Variable Timestep", 22.0f,
			sgc::Vec2f{rightX, 70.0f},
			sgc::Colorf{1.0f, 0.7f, 0.3f, 1.0f});

		renderer->drawCircle(
			sgc::Vec2f{rightX, m_variableY}, BALL_RADIUS,
			sgc::Colorf{1.0f, 0.6f, 0.2f, 1.0f});
		renderer->drawCircleFrame(
			sgc::Vec2f{rightX, m_variableY}, BALL_RADIUS, 2.0f,
			sgc::Colorf{1.0f, 0.8f, 0.4f, 0.6f});

		// 可変側の情報
		text->drawText(
			"Direct dt integration", 16.0f,
			sgc::Vec2f{sw * 0.5f + 20.0f, 100.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// 経過時間
		const std::string elapsed = "Time: "
			+ std::to_string(static_cast<int>(m_elapsed)) + "s";
		text->drawTextCentered(elapsed, 18.0f,
			sgc::Vec2f{sw * 0.5f, sh - 50.0f},
			sgc::Colorf{0.8f, 0.8f, 0.8f, 1.0f});

		// 操作説明
		text->drawText(
			"[1] 60Hz  [2] 200Hz  [R] Reset  [Esc] Back", 14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr float GRAVITY = 600.0f;       ///< 重力加速度
	static constexpr float RESTITUTION = 0.7f;      ///< 反発係数
	static constexpr float BALL_RADIUS = 20.0f;     ///< ボール半径
	static constexpr float FLOOR_HEIGHT = 60.0f;    ///< 床の高さ
	static constexpr float START_Y = 120.0f;         ///< 初期Y座標

	sgc::physics::FixedTimestep m_stepper{1.0 / 200.0};  ///< 固定タイムステップ

	float m_fixedY{START_Y};       ///< 固定側Y座標
	float m_fixedVy{0.0f};         ///< 固定側Y速度
	float m_variableY{START_Y};    ///< 可変側Y座標
	float m_variableVy{0.0f};      ///< 可変側Y速度
	float m_fixedInterp{0.0f};     ///< 補間係数
	int m_fixedStepCount{0};       ///< フレーム内ステップ数
	float m_elapsed{0.0f};         ///< 経過時間
	std::string m_stepLabel{"200 Hz"};  ///< 現在のステップレート表示

	/// @brief シミュレーションをリセットする
	void resetSimulation()
	{
		m_fixedY = START_Y;
		m_fixedVy = 0.0f;
		m_variableY = START_Y;
		m_variableVy = 0.0f;
		m_fixedInterp = 0.0f;
		m_fixedStepCount = 0;
		m_elapsed = 0.0f;
		m_stepper.resetAccumulator();
	}
};
