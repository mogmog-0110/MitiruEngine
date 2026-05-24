#pragma once

/// @file SampleDebugOverlay.hpp
/// @brief DebugOverlay / FpsCounter / Profiler ビジュアルサンプル
///
/// デバッグ情報の表示機能をデモする。
/// FPSカウンター、カスタムデバッグエントリ、プロファイラー計測結果を
/// オーバーレイとして表示する。
/// - Spaceキー: オーバーレイ表示切替
/// - 1キー: 負荷テスト（重い計算を実行）
/// - Rキー: プロファイラーリセット
/// - ESCキー: メニューに戻る

#include <array>
#include <cmath>
#include <cstddef>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/core/Profiler.hpp"
#include "sgc/debug/DebugOverlay.hpp"
#include "sgc/debug/FpsCounter.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief DebugOverlay/Profilerサンプルシーン
///
/// DebugOverlay、FpsCounter、Profilerの使い方をデモする。
/// 画面上にデバッグ情報をオーバーレイ表示し、
/// プロファイラーの計測結果をバーグラフで可視化する。
class SampleDebugOverlay : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_overlay.setVisible(true);
		m_overlay.setEntry("Scene", "DebugOverlay Demo");
		m_frameCount = 0;
		m_heavyLoad = false;
		sgc::Profiler::instance().reset();
	}

	/// @brief 毎フレームの更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// オーバーレイ表示切替
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			m_overlay.toggleVisible();
		}

		// 負荷テスト切替
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_heavyLoad = !m_heavyLoad;
		}

		// プロファイラーリセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			sgc::Profiler::instance().reset();
		}

		// プロファイル計測: Update
		{
			SGC_PROFILE_SCOPE("Update");
			m_overlay.update(dt);
			++m_frameCount;

			m_overlay.setEntry("Frame", std::to_string(m_frameCount));
			m_overlay.setEntry("Mouse",
				std::to_string(static_cast<int>(input->mousePosition().x)) + ", "
				+ std::to_string(static_cast<int>(input->mousePosition().y)));
			m_overlay.setEntry("Load", m_heavyLoad ? "ON" : "OFF");
		}

		// 負荷テスト（意図的に重い処理）
		if (m_heavyLoad)
		{
			SGC_PROFILE_SCOPE("HeavyWork");
			volatile float sum = 0.0f;
			for (int i = 0; i < 50000; ++i)
			{
				sum += std::sin(static_cast<float>(i) * 0.001f);
			}
		}

		// アニメーション用タイマー
		m_time += dt;

		// 動くボール（パフォーマンスの視覚的フィードバック）
		{
			SGC_PROFILE_SCOPE("BallPhysics");
			for (std::size_t i = 0; i < BALL_COUNT; ++i)
			{
				m_ballX[i] = 200.0f + static_cast<float>(i) * 100.0f
					+ std::sin(m_time * (1.0f + static_cast<float>(i) * 0.3f)) * 80.0f;
				m_ballY[i] = 350.0f
					+ std::cos(m_time * (0.8f + static_cast<float>(i) * 0.2f)) * 60.0f;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// タイトル
		tr->drawText(
			"Debug Overlay & Profiler", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.5f, 1.0f, 0.8f, 1.0f});

		tr->drawText(
			"Space: Toggle | 1: Load Test | R: Reset | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// 動くボール
		{
			SGC_PROFILE_SCOPE("DrawBalls");
			for (std::size_t i = 0; i < BALL_COUNT; ++i)
			{
				const float hue = static_cast<float>(i) * 72.0f;
				const auto color = sgc::Colorf::fromHSV(hue, 0.7f, 0.9f);
				r->drawCircle({m_ballX[i], m_ballY[i]}, 20.0f, color.withAlpha(0.6f));
				r->drawCircleFrame({m_ballX[i], m_ballY[i]}, 20.0f, 1.5f, color);
			}
		}

		// プロファイラー結果をバーグラフで表示
		drawProfilerBars(r, tr);

		// DebugOverlay描画（右上）
		if (m_overlay.isVisible())
		{
			// 背景パネル
			r->drawRect(
				sgc::AABB2f{{sw - 250.0f, 60.0f}, {sw, 220.0f}},
				sgc::Colorf{0.0f, 0.0f, 0.0f, 0.7f});

			m_overlay.draw(*tr,
				{sw - 240.0f, 70.0f}, 14.0f, 20.0f);
		}

		// 負荷状態インジケーター
		if (m_heavyLoad)
		{
			r->drawRect(
				sgc::AABB2f{{sw - 120.0f, 10.0f}, {sw - 10.0f, 30.0f}},
				sgc::Colorf{1.0f, 0.3f, 0.2f, 0.8f});
			tr->drawTextCentered("LOAD ON", 12.0f,
				{sw - 65.0f, 20.0f}, sgc::Colorf::white());
		}
	}

private:
	static constexpr std::size_t BALL_COUNT = 5;

	sgc::debug::DebugOverlay m_overlay;
	std::size_t m_frameCount{0};
	bool m_heavyLoad{false};
	float m_time{0.0f};

	std::array<float, BALL_COUNT> m_ballX{};
	std::array<float, BALL_COUNT> m_ballY{};

	/// @brief プロファイラー結果をバーグラフで描画する
	void drawProfilerBars(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const auto stats = sgc::Profiler::instance().allStats();
		if (stats.empty())
		{
			return;
		}

		const float barX = 30.0f;
		const float barY = 470.0f;
		const float barMaxW = 400.0f;
		const float barH = 16.0f;
		const float barGap = 22.0f;

		tr->drawText("Profiler (avg ms):", 14.0f,
			{barX, barY - 22.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// 最大値を求める（バーのスケーリング用）
		double maxMs = 0.01;
		for (const auto& s : stats)
		{
			if (s.averageMs > maxMs)
			{
				maxMs = s.averageMs;
			}
		}

		const std::size_t displayCount = (stats.size() < 5) ? stats.size() : 5;

		for (std::size_t i = 0; i < displayCount; ++i)
		{
			const auto& s = stats[i];
			const float y = barY + static_cast<float>(i) * barGap;
			const float ratio = static_cast<float>(s.averageMs / maxMs);
			const float w = ratio * barMaxW;

			// バー背景
			r->drawRect(
				sgc::AABB2f{{barX, y}, {barX + barMaxW, y + barH}},
				sgc::Colorf{0.15f, 0.15f, 0.2f, 0.5f});

			// バー本体（色は値に応じて変化）
			const float hue = 120.0f * (1.0f - ratio);
			const auto barColor = sgc::Colorf::fromHSV(hue, 0.7f, 0.8f);
			r->drawRect(
				sgc::AABB2f{{barX, y}, {barX + w, y + barH}},
				barColor.withAlpha(0.7f));

			// ラベル
			const std::string label = s.name + ": "
				+ std::to_string(s.averageMs).substr(0, 5) + "ms";
			tr->drawText(label, 10.0f,
				{barX + 4.0f, y + 2.0f},
				sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f});
		}
	}
};
