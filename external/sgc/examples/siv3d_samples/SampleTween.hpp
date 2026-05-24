#pragma once

/// @file SampleTween.hpp
/// @brief トゥイーンアニメーションのサンプルシーン
///
/// 複数のイージング関数を同時に可視化する。
/// 各イージングは色付きの円として左から右へアニメーションする。
/// アニメーション完了後は自動でループ再生する。
/// - Rキー: 全リセット
/// - ESCキー: メニューに戻る

#include <array>
#include <functional>
#include <string>
#include <string_view>

#include "sgc/animation/Tween.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/math/Easing.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief トゥイーンアニメーションサンプル
///
/// 10種類のイージング関数を同時に表示し、
/// それぞれの動きの違いを視覚的に比較できる。
/// Rキーで全リセット、ESCでメニューに戻る。
class SampleTween : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

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

		// Rで全リセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			resetAllTweens();
		}

		// 初回更新時にトゥイーンを初期化
		if (!m_initialized)
		{
			initTweens();
			m_initialized = true;
		}

		// 各トゥイーンを更新
		bool allFinished = true;
		for (std::size_t i = 0; i < EASING_COUNT; ++i)
		{
			m_positions[i] = m_tweens[i].step(dt);
			if (!m_tweens[i].isFinished())
			{
				allFinished = false;
			}
		}

		// 全完了したら自動リスタート
		if (allFinished)
		{
			m_restartTimer += dt;
			if (m_restartTimer >= RESTART_DELAY)
			{
				resetAllTweens();
			}
		}
		else
		{
			m_restartTimer = 0.0f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;

		// 背景
		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f});

		// タイトル
		text->drawTextCentered(
			"Tween Animation - Easing Functions", 32.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// 操作説明
		text->drawText(
			"R: Reset | ESC: Back to Menu", 14.0f,
			sgc::Vec2f{20.0f, 65.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// 各イージングの描画
		const float labelX = 30.0f;
		const float trackLeft = 200.0f;
		const float trackRight = sw - 60.0f;

		for (std::size_t i = 0; i < EASING_COUNT; ++i)
		{
			const float y = START_Y + static_cast<float>(i) * ROW_HEIGHT;
			const auto color = rowColor(i);

			// イージング名ラベル
			text->drawText(
				std::string{EASING_NAMES[i]}, 16.0f,
				sgc::Vec2f{labelX, y - 8.0f}, color.withAlpha(0.9f));

			// トラックライン（背景）
			const sgc::AABB2f trackBg{
				{trackLeft, y - 1.0f},
				{trackRight, y + 1.0f}
			};
			renderer->drawRect(trackBg, sgc::Colorf{0.2f, 0.2f, 0.25f});

			// 円の位置を計算（0..1 を trackLeft..trackRight にマッピング）
			const float t = m_positions[i];
			const float cx = trackLeft + t * (trackRight - trackLeft);

			// 円を描画
			renderer->drawCircle(sgc::Vec2f{cx, y}, CIRCLE_RADIUS, color);

			// 円の外枠
			renderer->drawCircleFrame(
				sgc::Vec2f{cx, y}, CIRCLE_RADIUS, 1.5f,
				color.withAlpha(0.5f));
		}

		// 進捗バー（全体の視覚的インジケーター）
		if (m_initialized && !m_tweens[0].isFinished())
		{
			const float progress = m_tweens[0].progress();
			const float barW = (sw - 100.0f) * progress;
			const sgc::AABB2f bar{
				{50.0f, getData().screenHeight - 20.0f},
				{50.0f + barW, getData().screenHeight - 15.0f}
			};
			renderer->drawRect(bar, sgc::Colorf{0.3f, 0.6f, 1.0f, 0.5f});
		}
	}

private:
	/// @brief イージング関数の数
	static constexpr std::size_t EASING_COUNT = 10;

	/// @brief トゥイーン開始値
	static constexpr float LEFT_VALUE = 0.0f;

	/// @brief トゥイーン終了値
	static constexpr float RIGHT_VALUE = 1.0f;

	/// @brief アニメーション持続時間（秒）
	static constexpr float DURATION = 2.5f;

	/// @brief 自動リスタート遅延（秒）
	static constexpr float RESTART_DELAY = 0.8f;

	/// @brief 描画開始Y座標
	static constexpr float START_Y = 120.0f;

	/// @brief 行の高さ
	static constexpr float ROW_HEIGHT = 45.0f;

	/// @brief 円の半径
	static constexpr float CIRCLE_RADIUS = 10.0f;

	/// @brief イージング名
	static constexpr std::array<std::string_view, EASING_COUNT> EASING_NAMES =
	{
		"Linear",
		"InOutQuad",
		"OutCubic",
		"InOutCubic",
		"OutQuart",
		"OutElastic",
		"OutBounce",
		"InBack",
		"OutBack",
		"InOutExpo"
	};

	/// @brief 行ごとの色を色相で均等分配する
	/// @param index 行インデックス
	/// @return 対応するアクセントカラー
	static sgc::Colorf rowColor(std::size_t index)
	{
		const float hue = static_cast<float>(index)
			* (360.0f / static_cast<float>(EASING_COUNT));
		return sgc::Colorf::fromHSV(hue, 0.7f, 0.9f);
	}

	/// @brief トゥイーンを初期化する
	void initTweens()
	{
		using F = std::function<float(float)>;

		const std::array<F, EASING_COUNT> easingFuncs =
		{
			F{sgc::easing::linear<float>},
			F{sgc::easing::inOutQuad<float>},
			F{sgc::easing::outCubic<float>},
			F{sgc::easing::inOutCubic<float>},
			F{sgc::easing::outQuart<float>},
			F{sgc::easing::outElastic<float>},
			F{sgc::easing::outBounce<float>},
			F{sgc::easing::inBack<float>},
			F{sgc::easing::outBack<float>},
			F{sgc::easing::inOutExpo<float>}
		};

		for (std::size_t i = 0; i < EASING_COUNT; ++i)
		{
			m_tweens[i] = sgc::Tweenf{};
			m_tweens[i]
				.from(LEFT_VALUE)
				.to(RIGHT_VALUE)
				.during(DURATION)
				.withEasing(easingFuncs[i]);
			m_positions[i] = LEFT_VALUE;
		}
	}

	/// @brief 全トゥイーンをリセットする
	void resetAllTweens()
	{
		for (std::size_t i = 0; i < EASING_COUNT; ++i)
		{
			m_tweens[i].reset();
			m_positions[i] = LEFT_VALUE;
		}
		m_restartTimer = 0.0f;
	}

	/// @brief トゥイーン配列
	std::array<sgc::Tweenf, EASING_COUNT> m_tweens{};

	/// @brief 現在位置配列
	std::array<float, EASING_COUNT> m_positions{};

	/// @brief 完了後のリスタート遅延タイマー
	float m_restartTimer{0.0f};

	/// @brief 初期化済みフラグ
	bool m_initialized{false};
};
