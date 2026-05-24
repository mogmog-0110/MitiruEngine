#pragma once

/// @file SampleCoroutine.hpp
/// @brief C++20コルーチンビジュアルサンプル
///
/// CoroutineSchedulerで複数のアニメーションシーケンスを並列実行する。
/// 各コルーチンが独立した円を移動・色変化させる。
/// - Rキー: 全コルーチン再起動
/// - Spaceキー: 新しいコルーチンを追加
/// - ESCキー: メニューに戻る

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>

#include "sgc/core/Coroutine.hpp"
#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

namespace sample_coro
{

/// @brief コルーチンが制御するアニメーション対象の状態
struct AnimTarget
{
	float x{400.0f};                   ///< X座標
	float y{300.0f};                   ///< Y座標
	float radius{20.0f};               ///< 半径
	sgc::Colorf color{sgc::Colorf::white()};  ///< 色
	bool alive{true};                  ///< 生存フラグ
};

/// @brief 円を矩形パスで巡回させるコルーチン
inline sgc::Task patrolCoroutine(std::shared_ptr<AnimTarget> t,
	float cx, float cy, float size, float hue)
{
	const sgc::Colorf baseColor = sgc::Colorf::fromHSV(hue, 0.7f, 0.9f);
	const sgc::Colorf altColor = sgc::Colorf::fromHSV(hue + 120.0f, 0.7f, 0.9f);
	t->color = baseColor;
	t->x = cx - size;
	t->y = cy - size;

	while (t->alive)
	{
		// 右へ移動
		t->color = baseColor;
		for (float progress = 0.0f; progress < 1.0f; progress += 0.02f)
		{
			t->x = (cx - size) + progress * size * 2.0f;
			t->y = cy - size;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.2f};

		// 下へ移動
		t->color = altColor;
		for (float progress = 0.0f; progress < 1.0f; progress += 0.02f)
		{
			t->x = cx + size;
			t->y = (cy - size) + progress * size * 2.0f;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.2f};

		// 左へ移動
		t->color = baseColor;
		for (float progress = 0.0f; progress < 1.0f; progress += 0.02f)
		{
			t->x = (cx + size) - progress * size * 2.0f;
			t->y = cy + size;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.2f};

		// 上へ移動
		t->color = altColor;
		for (float progress = 0.0f; progress < 1.0f; progress += 0.02f)
		{
			t->x = cx - size;
			t->y = (cy + size) - progress * size * 2.0f;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.2f};
	}
}

/// @brief 脈動するコルーチン
inline sgc::Task pulseCoroutine(std::shared_ptr<AnimTarget> t, float hue)
{
	t->color = sgc::Colorf::fromHSV(hue, 0.8f, 0.9f);

	while (t->alive)
	{
		// 膨張
		for (float s = 15.0f; s < 35.0f; s += 0.5f)
		{
			t->radius = s;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.3f};

		// 収縮
		for (float s = 35.0f; s > 15.0f; s -= 0.5f)
		{
			t->radius = s;
			co_await sgc::WaitForNextFrame{};
		}
		co_await sgc::WaitForSeconds{0.3f};
	}
}

} // namespace sample_coro

/// @brief コルーチンサンプルシーン
///
/// CoroutineSchedulerで複数のコルーチンを同時実行し、
/// 各コルーチンが独立した円を制御するアニメーションを表示する。
class SampleCoroutine : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		startCoroutines();
	}

	/// @brief 毎フレームの更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			// コルーチンを停止
			for (auto& t : m_targets)
			{
				if (t) t->alive = false;
			}
			m_scheduler.cancelAll();
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			for (auto& t : m_targets)
			{
				if (t) t->alive = false;
			}
			m_scheduler.cancelAll();
			m_targets = {};
			startCoroutines();
		}

		// コルーチンスケジューラーを更新
		m_scheduler.tick(dt);
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// 情報パネル
		tr->drawText(
			"C++20 Coroutines", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.6f, 0.9f, 1.0f, 1.0f});

		tr->drawText(
			"R: Restart | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		const std::string info = "Active coroutines: "
			+ std::to_string(m_scheduler.activeCount());
		tr->drawText(info, 14.0f,
			{sw - 220.0f, 38.0f}, sgc::Colorf{0.5f, 0.8f, 1.0f, 1.0f});

		// 巡回パスのガイドライン
		for (std::size_t i = 0; i < PATROL_COUNT; ++i)
		{
			const float cx = PATROL_START_X + static_cast<float>(i) * PATROL_SPACING;
			const float cy = 250.0f;
			const float size = PATROL_SIZE;
			const sgc::Colorf guide{0.2f, 0.2f, 0.3f, 0.3f};
			r->drawRectFrame(
				sgc::AABB2f{{cx - size, cy - size}, {cx + size, cy + size}},
				1.0f, guide);
		}

		// アニメーション対象を描画
		for (const auto& t : m_targets)
		{
			if (t && t->alive)
			{
				r->drawCircle({t->x, t->y}, t->radius, t->color);
				r->drawCircleFrame({t->x, t->y}, t->radius, 1.5f,
					t->color.withAlpha(0.4f));
			}
		}

		// 説明
		tr->drawTextCentered(
			"Each shape is controlled by an independent coroutine", 14.0f,
			{sw * 0.5f, getData().screenHeight - 60.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f, 0.8f});

		tr->drawTextCentered(
			"co_await WaitForSeconds / WaitForNextFrame", 12.0f,
			{sw * 0.5f, getData().screenHeight - 38.0f},
			sgc::Colorf{0.4f, 0.4f, 0.5f, 0.6f});
	}

private:
	static constexpr std::size_t MAX_TARGETS = 8;
	static constexpr std::size_t PATROL_COUNT = 3;
	static constexpr float PATROL_START_X = 180.0f;
	static constexpr float PATROL_SPACING = 200.0f;
	static constexpr float PATROL_SIZE = 60.0f;

	sgc::CoroutineScheduler m_scheduler;
	std::array<std::shared_ptr<sample_coro::AnimTarget>, MAX_TARGETS> m_targets{};

	/// @brief 初期コルーチンを起動する
	void startCoroutines()
	{
		// 巡回コルーチン×3（異なる位置・色）
		for (std::size_t i = 0; i < PATROL_COUNT; ++i)
		{
			auto target = std::make_shared<sample_coro::AnimTarget>();
			target->radius = 12.0f;
			m_targets[i] = target;

			const float cx = PATROL_START_X + static_cast<float>(i) * PATROL_SPACING;
			const float hue = static_cast<float>(i) * 120.0f;
			m_scheduler.start(
				sample_coro::patrolCoroutine(target, cx, 250.0f, PATROL_SIZE, hue));
		}

		// 脈動コルーチン×3（下段）
		for (std::size_t i = 0; i < 3; ++i)
		{
			auto target = std::make_shared<sample_coro::AnimTarget>();
			target->x = 180.0f + static_cast<float>(i) * 200.0f;
			target->y = 450.0f;
			target->radius = 20.0f;
			m_targets[PATROL_COUNT + i] = target;

			const float hue = 60.0f + static_cast<float>(i) * 120.0f;
			m_scheduler.start(
				sample_coro::pulseCoroutine(target, hue));
		}
	}
};
