#pragma once

/// @file SampleObjectPool.hpp
/// @brief ObjectPool（オブジェクトプール）の弾幕デモ
///
/// ObjectPoolを使った弾の発射・回収サイクルを可視化する。
/// 画面下部中央からクリック/Spaceで弾を発射し、画面外で自動回収する。
/// - スペース/クリック: 弾を発射
/// - Rキー: プールをリセット
/// - ESCキー: メニューに戻る

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/patterns/ObjectPool.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief 弾データ
struct Bullet
{
	float x{};      ///< X座標
	float y{};      ///< Y座標
	float vx{};     ///< X速度(px/s)
	float vy{};     ///< Y速度(px/s)
	bool active{};  ///< アクティブフラグ
};

/// @brief ObjectPoolビジュアルサンプルシーン
///
/// 画面下部中央から弾を上方向に発射し、画面外に出た弾を
/// プールに返却するサイクルを可視化する。
/// プール統計（total, active, available）をリアルタイム表示する。
class SampleObjectPool : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_activeBullets.clear();
		m_fireTimer = 0.0f;
		m_totalFiredCount = 0;

		// プールを再構築
		m_pool = sgc::ObjectPool<Bullet>(POOL_SIZE, []
		{
			return Bullet{0.0f, 0.0f, 0.0f, 0.0f, false};
		});
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
			onEnter();
			return;
		}

		// 発射処理
		m_fireTimer -= dt;
		const bool wantFire = input->isKeyJustPressed(KeyCode::SPACE)
			|| input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		if (wantFire && m_fireTimer <= 0.0f)
		{
			fireBullet();
			m_fireTimer = FIRE_INTERVAL;
		}

		// 弾の更新: 移動 + 画面外判定で回収
		for (auto it = m_activeBullets.begin(); it != m_activeBullets.end();)
		{
			Bullet* b = *it;
			b->x += b->vx * dt;
			b->y += b->vy * dt;

			if (b->y < -10.0f || b->x < -10.0f
				|| b->x > getData().screenWidth + 10.0f)
			{
				b->active = false;
				m_pool.release(b);
				it = m_activeBullets.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 背景
		r->clearBackground(sgc::Colorf{0.02f, 0.02f, 0.08f, 1.0f});

		// グリッド線（宇宙感演出）
		for (int i = 0; i <= 20; ++i)
		{
			const float gx = static_cast<float>(i) * 40.0f;
			r->drawLine({gx, 0.0f}, {gx, sh}, 1.0f,
				sgc::Colorf{0.1f, 0.1f, 0.2f, 0.3f});
		}
		for (int i = 0; i <= 15; ++i)
		{
			const float gy = static_cast<float>(i) * 40.0f;
			r->drawLine({0.0f, gy}, {sw, gy}, 1.0f,
				sgc::Colorf{0.1f, 0.1f, 0.2f, 0.3f});
		}

		// 発射元マーカー（画面下部中央）
		const float launchX = sw * 0.5f;
		const float launchY = sh - 40.0f;
		r->drawTriangle(
			{launchX, launchY - 18.0f},
			{launchX - 14.0f, launchY + 8.0f},
			{launchX + 14.0f, launchY + 8.0f},
			sgc::Colorf{0.2f, 0.9f, 0.4f, 1.0f});
		// エンジン炎
		r->drawTriangle(
			{launchX - 5.0f, launchY + 8.0f},
			{launchX + 5.0f, launchY + 8.0f},
			{launchX, launchY + 20.0f},
			sgc::Colorf{1.0f, 0.5f, 0.1f, 0.8f});

		// 弾の描画
		for (const Bullet* b : m_activeBullets)
		{
			// 黄色い小さな矩形
			r->drawRect(
				sgc::AABB2f{{b->x - 2.0f, b->y - 6.0f}, {b->x + 2.0f, b->y + 6.0f}},
				sgc::Colorf{1.0f, 1.0f, 0.2f, 1.0f});
			// グロー効果
			r->drawRect(
				sgc::AABB2f{{b->x - 4.0f, b->y - 3.0f}, {b->x + 4.0f, b->y + 3.0f}},
				sgc::Colorf{1.0f, 1.0f, 0.5f, 0.3f});
		}

		// UI: プール統計情報パネル
		const float panelX = 10.0f;
		const float panelY = 10.0f;
		r->drawRect(
			sgc::AABB2f{{panelX, panelY}, {panelX + 260.0f, panelY + 140.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.7f});
		r->drawRectFrame(
			sgc::AABB2f{{panelX, panelY}, {panelX + 260.0f, panelY + 140.0f}},
			2.0f, sgc::Colorf{0.3f, 0.6f, 1.0f, 0.8f});

		tr->drawText("Object Pool Stats", 20.0f,
			{panelX + 10.0f, panelY + 8.0f},
			sgc::Colorf{0.3f, 0.8f, 1.0f, 1.0f});

		tr->drawText("Total:     " + std::to_string(m_pool.capacity()), 16.0f,
			{panelX + 10.0f, panelY + 38.0f}, sgc::Colorf::white());

		tr->drawText("Active:    " + std::to_string(m_pool.inUse()), 16.0f,
			{panelX + 10.0f, panelY + 60.0f},
			sgc::Colorf{1.0f, 0.4f, 0.4f, 1.0f});

		tr->drawText("Available: " + std::to_string(m_pool.available()), 16.0f,
			{panelX + 10.0f, panelY + 82.0f},
			sgc::Colorf{0.4f, 1.0f, 0.4f, 1.0f});

		tr->drawText("Fired:     " + std::to_string(m_totalFiredCount), 16.0f,
			{panelX + 10.0f, panelY + 104.0f}, sgc::Colorf::white());

		// プール使用率バー
		const float barX = 290.0f;
		const float barY = 20.0f;
		const float barW = 200.0f;
		const float barH = 20.0f;
		r->drawRectFrame(
			sgc::AABB2f{{barX, barY}, {barX + barW, barY + barH}},
			1.0f, sgc::Colorf::white());
		const float usage = (m_pool.capacity() > 0)
			? static_cast<float>(m_pool.inUse()) / static_cast<float>(m_pool.capacity())
			: 0.0f;
		const float filledW = barW * usage;
		// 使用率に応じた色（緑→黄→赤）
		const sgc::Colorf barColor = (usage < 0.5f)
			? sgc::Colorf{0.2f, 0.9f, 0.2f, 0.8f}
			: (usage < 0.8f)
				? sgc::Colorf{1.0f, 0.8f, 0.2f, 0.8f}
				: sgc::Colorf{1.0f, 0.2f, 0.2f, 0.8f};
		if (filledW > 0.0f)
		{
			r->drawRect(
				sgc::AABB2f{{barX, barY}, {barX + filledW, barY + barH}},
				barColor);
		}
		tr->drawText("Pool Usage", 14.0f,
			{barX, barY + barH + 4.0f}, sgc::Colorf::white());

		// 操作説明
		tr->drawText(
			"[Space/Click] Fire  [R] Reset  [Esc] Back", 14.0f,
			{10.0f, sh - 25.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});
	}

private:
	static constexpr std::size_t POOL_SIZE = 200;    ///< プール容量
	static constexpr float BULLET_SPEED = 600.0f;    ///< 弾速(px/s)
	static constexpr float FIRE_INTERVAL = 0.08f;    ///< 発射間隔(秒)
	static constexpr float SPREAD_ANGLE = 0.15f;     ///< 発射時の拡散角(rad)

	sgc::ObjectPool<Bullet> m_pool{POOL_SIZE, []{ return Bullet{}; }};
	std::vector<Bullet*> m_activeBullets;  ///< アクティブ弾リスト
	float m_fireTimer{0.0f};               ///< 発射クールダウン
	int m_totalFiredCount{0};              ///< 総発射数
	int m_fireIndex{0};                    ///< 発射パターン用インデックス

	/// @brief 画面下部中央から弾を上方向に発射する
	void fireBullet()
	{
		Bullet* b = m_pool.acquire();
		if (!b)
		{
			return;  // プール枯渇
		}

		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;
		const float launchX = sw * 0.5f;
		const float launchY = sh - 58.0f;

		// 少し拡散させて発射（3発ずつ交互）
		const float spreadOffset = static_cast<float>(m_fireIndex % 3 - 1) * SPREAD_ANGLE;

		b->x = launchX;
		b->y = launchY;
		b->vx = std::sin(spreadOffset) * BULLET_SPEED * 0.3f;
		b->vy = -BULLET_SPEED;
		b->active = true;
		m_activeBullets.push_back(b);
		++m_totalFiredCount;
		++m_fireIndex;
	}
};
