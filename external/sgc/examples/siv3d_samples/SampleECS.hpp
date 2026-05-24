#pragma once

/// @file SampleECS.hpp
/// @brief ECSビジュアルサンプル
///
/// 300個のエンティティを生成し、Position/Velocity/Colorコンポーネントで
/// パーティクル風の動きを可視化する。
/// - Rキー: ランダム再配置
/// - Spaceキー: フリーズ/解除トグル

#include <chrono>
#include <cmath>
#include <random>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/ecs/World.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

namespace sample_ecs
{

/// @brief 位置コンポーネント
struct Position
{
	float x{};
	float y{};
};

/// @brief 速度コンポーネント
struct Velocity
{
	float vx{};
	float vy{};
};

/// @brief 色コンポーネント
struct ColorComp
{
	float r{};
	float g{};
	float b{};
};

} // namespace sample_ecs

/// @brief ECSビジュアルサンプルシーン
///
/// 300個のパーティクルエンティティが画面内を移動する。
/// 各エンティティはランダムな色と速度を持ち、端でラップアラウンドする。
class SampleECS : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_frozen = false;
		m_frameCount = 0;
		m_fpsTimer = 0.0f;
		m_displayFps = 0;
		resetEntities();
	}

	/// @brief 更新処理
	void update(float dt) override
	{
		// ESCでメニューに戻る
		if (getData().inputProvider->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rでリセット
		if (getData().inputProvider->isKeyJustPressed(KeyCode::R))
		{
			resetEntities();
		}

		// Spaceでフリーズトグル
		if (getData().inputProvider->isKeyJustPressed(KeyCode::SPACE))
		{
			m_frozen = !m_frozen;
		}

		// FPS計算
		++m_frameCount;
		m_fpsTimer += dt;
		if (m_fpsTimer >= 1.0f)
		{
			m_displayFps = m_frameCount;
			m_frameCount = 0;
			m_fpsTimer -= 1.0f;
		}

		if (m_frozen)
		{
			return;
		}

		// エンティティ移動: 位置を速度で更新し、画面端でラップ
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		m_world.forEach<sample_ecs::Position, sample_ecs::Velocity>(
			[sw, sh](sample_ecs::Position& pos, sample_ecs::Velocity& vel)
			{
				pos.x += vel.vx;
				pos.y += vel.vy;

				// ラップアラウンド
				if (pos.x < 0.0f)
				{
					pos.x += sw;
				}
				else if (pos.x > sw)
				{
					pos.x -= sw;
				}
				if (pos.y < 0.0f)
				{
					pos.y += sh;
				}
				else if (pos.y > sh)
				{
					pos.y -= sh;
				}
			}
		);

		// 時間経過による色のゆらぎ
		m_time += dt;
	}

	/// @brief 描画処理
	void draw() const override
	{
		const auto& data = getData();
		data.renderer->clearBackground(sgc::Colorf{0.02f, 0.02f, 0.06f, 1.0f});

		// 全エンティティを小さな円として描画
		// forEachはnon-constなので、mutableワールドに対してconst_castを使用
		auto& world = const_cast<sgc::ecs::World&>(m_world);
		world.forEach<sample_ecs::Position, sample_ecs::ColorComp>(
			[&data, this](sample_ecs::Position& pos, sample_ecs::ColorComp& col)
			{
				// 時間による微妙な明滅効果
				const float pulse = 0.8f + 0.2f * std::sin(m_time * 2.0f + pos.x * 0.01f);
				const sgc::Colorf color{col.r * pulse, col.g * pulse, col.b * pulse, 0.9f};
				data.renderer->drawCircle({pos.x, pos.y}, 3.0f, color);
			}
		);

		// 情報パネル背景
		data.renderer->drawRect(
			sgc::AABB2f{{0.0f, 0.0f}, {260.0f, 110.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.6f});

		// タイトル
		data.textRenderer->drawText(
			"ECS Particle System", 24.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.3f, 0.9f, 1.0f, 1.0f});

		// エンティティ数
		data.textRenderer->drawText(
			"Entities: " + std::to_string(ENTITY_COUNT), 18.0f,
			{10.0f, 40.0f}, sgc::Colorf::white());

		// FPS
		data.textRenderer->drawText(
			"FPS: " + std::to_string(m_displayFps), 18.0f,
			{10.0f, 62.0f}, sgc::Colorf{0.9f, 0.9f, 0.3f, 1.0f});

		// 操作ヒント
		const sgc::Colorf hintColor{0.6f, 0.6f, 0.6f, 1.0f};
		data.textRenderer->drawText(
			"[R] Reset  [Space] Freeze  [Esc] Back", 14.0f,
			{10.0f, 86.0f}, hintColor);

		// フリーズ中の表示
		if (m_frozen)
		{
			data.textRenderer->drawTextCentered(
				"FROZEN", 48.0f,
				{data.screenWidth * 0.5f, data.screenHeight * 0.5f},
				sgc::Colorf{1.0f, 0.3f, 0.3f, 0.7f});
		}
	}

private:
	static constexpr int ENTITY_COUNT = 300;  ///< エンティティ数

	sgc::ecs::World m_world;       ///< ECSワールド
	bool m_frozen{false};           ///< フリーズ中か
	int m_frameCount{0};            ///< フレームカウンタ
	float m_fpsTimer{0.0f};         ///< FPS計測用タイマー
	int m_displayFps{0};            ///< 表示用FPS
	mutable float m_time{0.0f};     ///< 経過時間（描画の明滅用）

	/// @brief エンティティを全て破棄して再生成する
	void resetEntities()
	{
		// ワールドを再構築（シンプルに新しいWorldに差し替え）
		m_world.~World();
		new (&m_world) sgc::ecs::World();

		std::mt19937 rng(static_cast<unsigned>(
			std::chrono::steady_clock::now().time_since_epoch().count()));

		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		std::uniform_real_distribution<float> distX(0.0f, sw);
		std::uniform_real_distribution<float> distY(0.0f, sh);
		std::uniform_real_distribution<float> distVel(-2.0f, 2.0f);
		std::uniform_real_distribution<float> distColor(0.3f, 1.0f);

		for (int i = 0; i < ENTITY_COUNT; ++i)
		{
			auto entity = m_world.createEntity();

			m_world.addComponent(entity, sample_ecs::Position{distX(rng), distY(rng)});
			m_world.addComponent(entity, sample_ecs::Velocity{distVel(rng), distVel(rng)});
			m_world.addComponent(entity, sample_ecs::ColorComp{
				distColor(rng), distColor(rng), distColor(rng)});
		}
	}
};
