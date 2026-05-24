#pragma once

/// @file SampleTweenTimeline.hpp
/// @brief TweenTimeline によるパラレルアニメーションデモ
///
/// 複数オブジェクトを TweenTimeline で同時にアニメーションする。
/// 各オブジェクトは異なるイージングで位置・サイズ・色が変化する。
/// - R: リスタート
/// - ESC: メニューに戻る

#include <array>
#include <cmath>
#include <functional>
#include <string>

#include "sgc/animation/Tween.hpp"
#include "sgc/animation/TweenTimeline.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/math/Easing.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief TweenTimeline パラレルアニメーションサンプル
///
/// 5つのオブジェクトが同時に異なるイージングでアニメーションする。
/// タイムラインが完了するとしばらく待って自動ループする。
class SampleTweenTimeline : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		buildTimeline();
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			buildTimeline();
			return;
		}

		// タイムライン更新
		m_timeline.step(dt);

		// 完了後、自動リスタート
		if (m_timeline.isFinished())
		{
			m_restartTimer += dt;
			if (m_restartTimer >= RESTART_DELAY)
			{
				buildTimeline();
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		renderer->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.08f});

		text->drawTextCentered(
			"TweenTimeline - Parallel Animation", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.85f, 1.0f});

		text->drawText(
			"R: Restart | ESC: Back to Menu", 14.0f,
			sgc::Vec2f{20.0f, 60.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── オブジェクト描画 ──
		const float trackLeft = 60.0f;
		const float trackRight = sw - 60.0f;
		const float trackWidth = trackRight - trackLeft;

		for (std::size_t i = 0; i < OBJ_COUNT; ++i)
		{
			const float baseY = START_Y + static_cast<float>(i) * ROW_HEIGHT;
			const auto color = objectColor(i);

			// ラベル
			text->drawText(
				std::string{EASING_LABELS[i]}, 14.0f,
				sgc::Vec2f{trackLeft, baseY - 25.0f}, color.withAlpha(0.8f));

			// トラック背景線
			renderer->drawLine(
				sgc::Vec2f{trackLeft, baseY},
				sgc::Vec2f{trackRight, baseY},
				1.0f, sgc::Colorf{0.2f, 0.2f, 0.25f});

			// 移動する円
			const float cx = trackLeft + m_posX[i] * trackWidth;
			const float radius = 8.0f + m_scale[i] * 12.0f;

			renderer->drawCircle(
				sgc::Vec2f{cx, baseY}, radius,
				color.withAlpha(m_alpha[i]));
			renderer->drawCircleFrame(
				sgc::Vec2f{cx, baseY}, radius, 1.5f,
				color.withAlpha(0.5f));

			// 軌跡（始点と終点のマーカー）
			renderer->drawCircle(
				sgc::Vec2f{trackLeft, baseY}, 3.0f,
				sgc::Colorf{0.3f, 0.3f, 0.35f});
			renderer->drawCircle(
				sgc::Vec2f{trackRight, baseY}, 3.0f,
				sgc::Colorf{0.3f, 0.3f, 0.35f});
		}

		// ── タイムライン進行状況 ──
		const float barY = sh - 40.0f;
		const float barW = sw - 120.0f;

		renderer->drawRect(
			sgc::AABB2f{{60.0f, barY}, {60.0f + barW, barY + 6.0f}},
			sgc::Colorf{0.15f, 0.15f, 0.2f});

		if (!m_timeline.isFinished())
		{
			// 大まかな進行率（最初のTweenの進行率を使用）
			const float progress = m_posX[0];
			renderer->drawRect(
				sgc::AABB2f{{60.0f, barY}, {60.0f + barW * progress, barY + 6.0f}},
				sgc::Colorf{0.4f, 0.6f, 1.0f, 0.7f});
		}

		// 状態テキスト
		const std::string status = m_timeline.isFinished()
			? "Completed - Restarting..."
			: "Playing...";
		text->drawTextCentered(
			status, 14.0f,
			sgc::Vec2f{sw * 0.5f, sh - 20.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f});
	}

private:
	/// @brief オブジェクト数
	static constexpr std::size_t OBJ_COUNT = 5;

	/// @brief アニメーション持続時間（秒）
	static constexpr float DURATION = 2.5f;

	/// @brief 自動リスタート遅延（秒）
	static constexpr float RESTART_DELAY = 1.0f;

	/// @brief 描画開始Y座標
	static constexpr float START_Y = 130.0f;

	/// @brief 行の高さ
	static constexpr float ROW_HEIGHT = 85.0f;

	/// @brief イージングラベル
	static constexpr std::array<std::string_view, OBJ_COUNT> EASING_LABELS =
	{
		"OutCubic (Position)",
		"InOutQuad (Position)",
		"OutElastic (Scale)",
		"OutBounce (Position)",
		"InOutExpo (Alpha)"
	};

	/// @brief タイムライン
	sgc::TweenTimelinef m_timeline;

	/// @brief リスタートタイマー
	float m_restartTimer{0.0f};

	/// @brief 各オブジェクトのX位置 [0, 1]
	std::array<float, OBJ_COUNT> m_posX{};

	/// @brief 各オブジェクトのスケール [0, 1]
	std::array<float, OBJ_COUNT> m_scale{};

	/// @brief 各オブジェクトのアルファ [0, 1]
	std::array<float, OBJ_COUNT> m_alpha{};

	/// @brief オブジェクトごとの色を返す
	[[nodiscard]] static sgc::Colorf objectColor(std::size_t index)
	{
		const float hue = static_cast<float>(index) * (360.0f / static_cast<float>(OBJ_COUNT));
		return sgc::Colorf::fromHSV(hue, 0.7f, 0.9f);
	}

	/// @brief タイムラインを構築し直す
	void buildTimeline()
	{
		m_restartTimer = 0.0f;
		m_posX.fill(0.0f);
		m_scale.fill(0.0f);
		m_alpha.fill(1.0f);

		m_timeline = sgc::TweenTimelinef{};

		// 0: X位置 OutCubic
		m_timeline.add(
			sgc::Tweenf{}.from(0.0f).to(1.0f).during(DURATION)
				.withEasing(sgc::easing::outCubic<float>)
				.onUpdate([this](float v) { m_posX[0] = v; }));

		// 1: X位置 InOutQuad
		m_timeline.add(
			sgc::Tweenf{}.from(0.0f).to(1.0f).during(DURATION)
				.withEasing(sgc::easing::inOutQuad<float>)
				.onUpdate([this](float v) { m_posX[1] = v; }));

		// 2: X位置 + スケール OutElastic
		m_timeline.add(
			sgc::Tweenf{}.from(0.0f).to(1.0f).during(DURATION)
				.withEasing(sgc::easing::outElastic<float>)
				.onUpdate([this](float v) { m_posX[2] = v; m_scale[2] = v; }));

		// 3: X位置 OutBounce
		m_timeline.add(
			sgc::Tweenf{}.from(0.0f).to(1.0f).during(DURATION)
				.withEasing(sgc::easing::outBounce<float>)
				.onUpdate([this](float v) { m_posX[3] = v; }));

		// 4: X位置 InOutExpo + アルファ変動
		m_timeline.add(
			sgc::Tweenf{}.from(0.0f).to(1.0f).during(DURATION)
				.withEasing(sgc::easing::inOutExpo<float>)
				.onUpdate([this](float v)
				{
					m_posX[4] = v;
					m_alpha[4] = 0.3f + 0.7f * v;
				}));

		// デフォルトのスケール値を設定
		m_scale[0] = 0.5f;
		m_scale[1] = 0.5f;
		m_scale[3] = 0.5f;
		m_scale[4] = 0.5f;
	}
};
