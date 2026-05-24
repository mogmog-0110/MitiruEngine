#pragma once

/// @file SampleTimer.hpp
/// @brief Timer（タイマーユーティリティ）サンプル
///
/// Stopwatch, CountdownTimer, IntervalTimerの3種類のタイマーを
/// 並べて可視化するデモ。
/// - Space: ストップウォッチの開始/停止
/// - 1キー: カウントダウンタイマーをリスタート
/// - 2キー: インターバルタイマーの速度変更
/// - Rキー: 全リセット
/// - ESCキー: メニューに戻る

#include <cmath>
#include <cstddef>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/core/Timer.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Timerサンプルシーン
///
/// Stopwatch（経過時間計測）、CountdownTimer（カウントダウン）、
/// IntervalTimer（周期的発火）の3種類を横並びで表示し、
/// それぞれの挙動を直感的に理解できるデモ。
class SampleTimer : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_stopwatch = sgc::Stopwatch{};
		m_countdown = sgc::CountdownTimer{COUNTDOWN_DURATION};
		m_interval = sgc::IntervalTimer{INTERVAL_SPEEDS[0]};
		m_intervalIndex = 0;
		m_intervalFlashCount = 0;
		m_intervalFlash = 0.0f;
	}

	/// @brief 毎フレームの更新処理
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
			onEnter();
			return;
		}

		// Space: ストップウォッチの開始/停止
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			if (m_stopwatch.isRunning())
			{
				m_stopwatch.stop();
			}
			else
			{
				m_stopwatch.start();
			}
		}

		// 1: カウントダウンリスタート
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_countdown.restart();
		}

		// 2: インターバル速度変更
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_intervalIndex = (m_intervalIndex + 1) % SPEED_COUNT;
			m_interval.setInterval(INTERVAL_SPEEDS[m_intervalIndex]);
		}

		// タイマー更新
		m_countdown.update(dt);
		m_interval.update(dt);

		// インターバルタイマー発火チェック
		while (m_interval.consume())
		{
			++m_intervalFlashCount;
			m_intervalFlash = 1.0f;
		}

		// フラッシュ減衰
		if (m_intervalFlash > 0.0f)
		{
			m_intervalFlash -= dt * 4.0f;
			if (m_intervalFlash < 0.0f) m_intervalFlash = 0.0f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.06f, 0.04f, 0.08f, 1.0f});

		// タイトル
		tr->drawText("Timer Utilities", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{1.0f, 0.8f, 0.6f, 1.0f});
		tr->drawText(
			"[Space] Stopwatch  [1] Restart Countdown  [2] Change Interval  [R] Reset  [Esc] Back",
			12.0f, {10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// 3つのタイマーを横並び
		const float colW = sw / 3.0f;
		drawStopwatch(r, tr, 0.0f, colW);
		drawCountdown(r, tr, colW, colW);
		drawInterval(r, tr, colW * 2.0f, colW);
	}

private:
	static constexpr float COUNTDOWN_DURATION = 10.0f;   ///< カウントダウン秒数
	static constexpr std::size_t SPEED_COUNT = 4;
	static constexpr float INTERVAL_SPEEDS[SPEED_COUNT] =
		{1.0f, 0.5f, 0.25f, 0.1f};

	sgc::Stopwatch m_stopwatch;             ///< ストップウォッチ
	sgc::CountdownTimer m_countdown{10.0f}; ///< カウントダウンタイマー
	sgc::IntervalTimer m_interval{1.0f};    ///< インターバルタイマー
	std::size_t m_intervalIndex{0};         ///< 現在の速度インデックス
	int m_intervalFlashCount{0};            ///< インターバル発火回数
	float m_intervalFlash{0.0f};            ///< フラッシュエフェクト

	/// @brief ストップウォッチ表示を描画する
	void drawStopwatch(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float x, float w) const
	{
		const float cx = x + w * 0.5f;
		const float py = 80.0f;

		// パネル背景
		r->drawRect(sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			sgc::Colorf{0.08f, 0.06f, 0.12f, 0.9f});
		r->drawRectFrame(
			sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			2.0f, sgc::Colorf{0.5f, 0.3f, 0.7f, 0.8f});

		tr->drawTextCentered("Stopwatch", 20.0f,
			{cx, py + 15.0f},
			sgc::Colorf{0.8f, 0.6f, 1.0f, 1.0f});

		// 経過時間表示
		const float elapsed = m_stopwatch.elapsedSecondsF();
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.3f s", elapsed);
		tr->drawTextCentered(buf, 36.0f,
			{cx, py + 80.0f},
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

		// 状態表示
		const bool running = m_stopwatch.isRunning();
		const sgc::Colorf stateCol = running
			? sgc::Colorf{0.2f, 1.0f, 0.4f, 1.0f}
			: sgc::Colorf{1.0f, 0.4f, 0.3f, 1.0f};
		tr->drawTextCentered(running ? "RUNNING" : "STOPPED", 16.0f,
			{cx, py + 130.0f}, stateCol);

		// パルスインジケータ
		if (running)
		{
			const float pulse = std::sin(elapsed * 4.0f) * 0.3f + 0.7f;
			r->drawCircle({cx, py + 180.0f}, 12.0f,
				sgc::Colorf{0.3f, 1.0f, 0.5f, pulse});
		}
		else
		{
			r->drawCircleFrame({cx, py + 180.0f}, 12.0f, 2.0f,
				sgc::Colorf{0.5f, 0.5f, 0.5f, 0.5f});
		}

		tr->drawTextCentered("[Space]", 14.0f,
			{cx, py + 250.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

	/// @brief カウントダウンタイマー表示を描画する
	void drawCountdown(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float x, float w) const
	{
		const float cx = x + w * 0.5f;
		const float py = 80.0f;

		r->drawRect(sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			sgc::Colorf{0.08f, 0.06f, 0.12f, 0.9f});
		r->drawRectFrame(
			sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			2.0f, sgc::Colorf{0.7f, 0.5f, 0.3f, 0.8f});

		tr->drawTextCentered("Countdown", 20.0f,
			{cx, py + 15.0f},
			sgc::Colorf{1.0f, 0.7f, 0.4f, 1.0f});

		// 残り時間
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.1f s", m_countdown.remaining());
		tr->drawTextCentered(buf, 36.0f,
			{cx, py + 80.0f},
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

		// プログレスバー
		const float barX = x + 30.0f;
		const float barW = w - 60.0f;
		const float barY = py + 130.0f;
		const float barH = 20.0f;
		r->drawRect(sgc::AABB2f{{barX, barY}, {barX + barW, barY + barH}},
			sgc::Colorf{0.15f, 0.15f, 0.2f, 1.0f});

		const float progress = m_countdown.progress();
		const float fillW = barW * (1.0f - progress);
		const sgc::Colorf barCol = m_countdown.isExpired()
			? sgc::Colorf{1.0f, 0.2f, 0.2f, 0.9f}
			: sgc::Colorf{0.3f, 0.8f, 1.0f, 0.9f};
		if (fillW > 0.0f)
		{
			r->drawRect(sgc::AABB2f{{barX, barY}, {barX + fillW, barY + barH}},
				barCol);
		}

		// 期限切れ表示
		if (m_countdown.isExpired())
		{
			tr->drawTextCentered("EXPIRED!", 18.0f,
				{cx, py + 180.0f},
				sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f});
		}

		tr->drawTextCentered("[1] Restart", 14.0f,
			{cx, py + 250.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

	/// @brief インターバルタイマー表示を描画する
	void drawInterval(
		sgc::IRenderer* r, sgc::ITextRenderer* tr,
		float x, float w) const
	{
		const float cx = x + w * 0.5f;
		const float py = 80.0f;

		// フラッシュエフェクト背景
		const float flashAlpha = m_intervalFlash * 0.15f;
		r->drawRect(sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			sgc::Colorf{0.08f + flashAlpha, 0.06f, 0.12f, 0.9f});
		r->drawRectFrame(
			sgc::AABB2f{{x + 10.0f, py}, {x + w - 10.0f, py + 280.0f}},
			2.0f, sgc::Colorf{0.3f, 0.7f, 0.5f, 0.8f});

		tr->drawTextCentered("Interval", 20.0f,
			{cx, py + 15.0f},
			sgc::Colorf{0.4f, 1.0f, 0.7f, 1.0f});

		// 現在のインターバル
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f s", m_interval.interval());
		tr->drawTextCentered(buf, 28.0f,
			{cx, py + 70.0f},
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

		// 発火回数
		tr->drawTextCentered(
			"Fires: " + std::to_string(m_intervalFlashCount), 18.0f,
			{cx, py + 120.0f},
			sgc::Colorf{0.4f, 1.0f, 0.6f, 1.0f});

		// フラッシュインジケータ
		const float flashR = 20.0f + m_intervalFlash * 15.0f;
		r->drawCircle({cx, py + 180.0f}, flashR,
			sgc::Colorf{0.3f, 1.0f, 0.5f, 0.3f + m_intervalFlash * 0.5f});
		r->drawCircleFrame({cx, py + 180.0f}, flashR, 2.0f,
			sgc::Colorf{0.4f, 1.0f, 0.6f, 0.8f});

		tr->drawTextCentered("[2] Speed", 14.0f,
			{cx, py + 250.0f},
			sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}
};
