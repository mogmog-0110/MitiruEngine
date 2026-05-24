#pragma once

/// @file SampleFade.hpp
/// @brief シーン遷移フェードデモ
///
/// 3つのサブシーン（赤・緑・青）をTweenによるフェードで切り替える。
/// - 1/2/3キー: サブシーン切替
/// - ESC: メニューに戻る

#include <cmath>
#include <string>

#include "sgc/animation/Tween.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/math/Easing.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief フェード遷移デモシーン
class SampleFade : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_currentScene = 0;
		m_targetScene = 0;
		m_transitioning = false;
		m_fadeAlpha = 0.0f;
		m_fadePhase = FadePhase::None;
		m_time = 0.0f;
	}

	/// @brief 更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		m_time += dt;

		// サブシーン切替入力
		if (!m_transitioning)
		{
			int target = -1;
			if (input->isKeyJustPressed(KeyCode::NUM1)) target = 0;
			if (input->isKeyJustPressed(KeyCode::NUM2)) target = 1;
			if (input->isKeyJustPressed(KeyCode::NUM3)) target = 2;

			if (target >= 0 && target != m_currentScene)
			{
				m_targetScene = target;
				startTransition();
			}
		}

		// フェードアニメーション更新
		if (m_transitioning)
		{
			m_fadeAlpha = m_fadeTween.step(dt);

			if (m_fadePhase == FadePhase::FadeOut && m_fadeTween.isFinished())
			{
				// フェードアウト完了 → シーン切替 → フェードイン開始
				m_currentScene = m_targetScene;
				m_fadePhase = FadePhase::FadeIn;
				m_fadeTween = sgc::Tweenf{};
				m_fadeTween.from(1.0f).to(0.0f).during(FADE_DURATION)
					.withEasing(sgc::easing::outCubic<float>);
			}
			else if (m_fadePhase == FadePhase::FadeIn && m_fadeTween.isFinished())
			{
				m_transitioning = false;
				m_fadePhase = FadePhase::None;
				m_fadeAlpha = 0.0f;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;

		// サブシーン背景色
		const sgc::Colorf sceneColors[] =
		{
			{0.6f, 0.1f, 0.1f, 1.0f},  // 赤
			{0.1f, 0.5f, 0.15f, 1.0f},  // 緑
			{0.1f, 0.15f, 0.6f, 1.0f},  // 青
		};
		const char* sceneNames[] = {"Red Scene", "Green Scene", "Blue Scene"};
		const sgc::Colorf sceneAccents[] =
		{
			{1.0f, 0.4f, 0.4f, 1.0f},
			{0.4f, 1.0f, 0.5f, 1.0f},
			{0.4f, 0.5f, 1.0f, 1.0f},
		};

		// 現在のサブシーン背景
		r->clearBackground(sceneColors[m_currentScene]);

		// 装飾: 浮遊する円
		for (int i = 0; i < 8; ++i)
		{
			const float phase = m_time * 0.5f + static_cast<float>(i) * 0.8f;
			const float cx = 400.0f + std::cos(phase * 0.7f) * 250.0f;
			const float cy = 300.0f + std::sin(phase) * 150.0f;
			const float radius = 20.0f + std::sin(phase * 1.3f) * 10.0f;
			r->drawCircle({cx, cy}, radius,
				sgc::Colorf{sceneAccents[m_currentScene].r,
					sceneAccents[m_currentScene].g,
					sceneAccents[m_currentScene].b, 0.15f});
		}

		// 中央の大きなラベル
		tr->drawTextCentered(sceneNames[m_currentScene], 48.0f,
			{400.0f, 200.0f}, sceneAccents[m_currentScene]);

		// シーン番号表示
		tr->drawTextCentered("Scene " + std::to_string(m_currentScene + 1) + " / 3", 20.0f,
			{400.0f, 260.0f}, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.7f});

		// 遷移状態表示
		if (m_transitioning)
		{
			const char* phaseStr = (m_fadePhase == FadePhase::FadeOut) ? "Fading Out..." : "Fading In...";
			tr->drawTextCentered(phaseStr, 20.0f,
				{400.0f, 310.0f}, sgc::Colorf{1.0f, 1.0f, 0.5f, 1.0f});
		}
		else
		{
			tr->drawTextCentered("Press 1/2/3 to switch scenes", 20.0f,
				{400.0f, 310.0f}, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.6f});
		}

		// カラースウォッチ（下部）
		const float swatchY = 420.0f;
		const float swatchSize = 60.0f;
		const float swatchGap = 20.0f;
		const float totalWidth = 3.0f * swatchSize + 2.0f * swatchGap;
		const float startX = (800.0f - totalWidth) / 2.0f;

		for (int i = 0; i < 3; ++i)
		{
			const float sx = startX + static_cast<float>(i) * (swatchSize + swatchGap);

			// スウォッチ背景
			r->drawRect(
				sgc::AABB2f{{sx, swatchY}, {sx + swatchSize, swatchY + swatchSize}},
				sceneColors[i]);

			// 選択中のハイライト枠
			const float borderThickness = (i == m_currentScene) ? 3.0f : 1.0f;
			const sgc::Colorf borderColor = (i == m_currentScene)
				? sgc::Colorf::white()
				: sgc::Colorf{0.5f, 0.5f, 0.5f, 0.5f};
			r->drawRectFrame(
				sgc::AABB2f{{sx, swatchY}, {sx + swatchSize, swatchY + swatchSize}},
				borderThickness, borderColor);

			// キーラベル
			tr->drawTextCentered(std::to_string(i + 1), 20.0f,
				{sx + swatchSize / 2.0f, swatchY + swatchSize + 16.0f},
				sgc::Colorf::white());
		}

		// フェードアルファプログレスバー
		const float barX = 200.0f;
		const float barY = 530.0f;
		const float barW = 400.0f;
		const float barH = 12.0f;
		r->drawRectFrame(
			sgc::AABB2f{{barX, barY}, {barX + barW, barY + barH}},
			1.0f, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.4f});
		if (m_fadeAlpha > 0.001f)
		{
			r->drawRect(
				sgc::AABB2f{{barX, barY}, {barX + barW * m_fadeAlpha, barY + barH}},
				sgc::Colorf{1.0f, 1.0f, 0.3f, 0.8f});
		}
		tr->drawTextCentered("Fade Alpha", 14.0f,
			{400.0f, barY + barH + 10.0f}, sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f});

		// フェードオーバーレイ
		if (m_fadeAlpha > 0.001f)
		{
			r->drawFadeOverlay(m_fadeAlpha, sgc::Colorf::black());
		}

		tr->drawText("ESC: Back to Menu", 14.0f,
			{660.0f, 575.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

private:
	static constexpr float FADE_DURATION = 0.5f;  ///< フェード所要時間（秒）

	enum class FadePhase
	{
		None,
		FadeOut,
		FadeIn
	};

	int m_currentScene{0};       ///< 現在のサブシーンインデックス
	int m_targetScene{0};        ///< 遷移先サブシーンインデックス
	bool m_transitioning{false}; ///< 遷移中フラグ
	float m_fadeAlpha{0.0f};     ///< フェードアルファ値
	FadePhase m_fadePhase{FadePhase::None}; ///< フェードフェーズ
	sgc::Tweenf m_fadeTween;     ///< フェードTween
	float m_time{0.0f};          ///< 経過時間（装飾アニメーション用）

	/// @brief フェード遷移を開始する
	void startTransition()
	{
		m_transitioning = true;
		m_fadePhase = FadePhase::FadeOut;
		m_fadeTween = sgc::Tweenf{};
		m_fadeTween.from(0.0f).to(1.0f).during(FADE_DURATION)
			.withEasing(sgc::easing::inCubic<float>);
	}
};
