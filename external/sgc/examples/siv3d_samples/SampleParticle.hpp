#pragma once

/// @file SampleParticle.hpp
/// @brief パーティクルシステムのサンプルシーン
///
/// クリックでバースト放出、スペースキー長押しで連続放出を体験できる。
/// 複数カラーのパーティクルと重力アフェクターを使用する。
/// - クリック: バースト放出
/// - Spaceキー長押し: 連続放出
/// - Rキー: 全パーティクルクリア
/// - ESCキー: メニューに戻る

#include <array>
#include <cstddef>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/effects/ParticleSystem.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief パーティクルシステムサンプル
///
/// マウスクリックでバースト放出、スペース長押しで連続放出。
/// 4色のパーティクルエフェクトを表示する。
/// Rキーで全クリア、ESCでメニューに戻る。
class SampleParticle : public sgc::AppScene<SharedData>
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

		// Rで全パーティクルクリア
		if (input->isKeyJustPressed(KeyCode::R))
		{
			clearAllParticles();
		}

		// 初回にアフェクターを登録
		if (!m_initialized)
		{
			initParticleSystems();
			m_initialized = true;
		}

		const auto mousePos = input->mousePosition();

		// クリックでバースト放出
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			emitBurst(mousePos.x, mousePos.y);
		}

		// スペース長押しで連続放出
		if (input->isKeyDown(KeyCode::SPACE))
		{
			emitContinuous(mousePos.x, mousePos.y, dt);
		}

		// 各パーティクルシステムを更新（自動放出を抑制）
		for (auto& ps : m_systems)
		{
			auto cfg = ps.config();
			cfg.rate = 0.0f;
			ps.setConfig(cfg);
			ps.update(dt);
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;

		// 背景
		renderer->clearBackground(sgc::Colorf{0.04f, 0.04f, 0.08f});

		// タイトル
		text->drawTextCentered(
			"Particle System", 32.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{1.0f, 0.9f, 0.8f});

		// 操作説明
		text->drawText(
			"Click: Burst | Space: Continuous | R: Clear | ESC: Back", 14.0f,
			sgc::Vec2f{20.0f, 65.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// パーティクル描画
		for (std::size_t si = 0; si < SYSTEM_COUNT; ++si)
		{
			const auto& particles = m_systems[si].activeParticles();
			for (const auto& p : particles)
			{
				const sgc::Colorf color{p.r, p.g, p.b, p.a};
				renderer->drawCircle(sgc::Vec2f{p.x, p.y}, p.size, color);
			}
		}

		// アクティブパーティクル数の表示
		std::size_t totalActive = 0;
		for (const auto& ps : m_systems)
		{
			totalActive += ps.activeCount();
		}

		const std::string countText = "Particles: "
			+ std::to_string(totalActive)
			+ " / "
			+ std::to_string(SYSTEM_COUNT * MAX_PER_SYSTEM);

		text->drawText(
			countText, 16.0f,
			sgc::Vec2f{20.0f, getData().screenHeight - 40.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f});

		// パーティクルが無いときのヒント表示
		if (totalActive == 0)
		{
			text->drawTextCentered(
				"Click anywhere!", 28.0f,
				sgc::Vec2f{sw * 0.5f, getData().screenHeight * 0.5f},
				sgc::Colorf{0.4f, 0.4f, 0.45f, 0.6f});
		}
	}

private:
	/// @brief パーティクルシステム数（色バリエーション）
	static constexpr std::size_t SYSTEM_COUNT = 4;

	/// @brief 各システムの最大パーティクル数
	static constexpr std::size_t MAX_PER_SYSTEM = 500;

	/// @brief バースト時の放出数（システムあたり）
	static constexpr std::size_t BURST_COUNT = 15;

	/// @brief 連続放出の毎秒放出数
	static constexpr float CONTINUOUS_RATE = 80.0f;

	/// @brief 重力加速度
	static constexpr float GRAVITY = 150.0f;

	/// @brief カラープリセット
	struct ColorPreset
	{
		float r;  ///< 赤
		float g;  ///< 緑
		float b;  ///< 青
	};

	/// @brief 4色のカラープリセット
	static constexpr std::array<ColorPreset, SYSTEM_COUNT> COLOR_PRESETS =
	{{
		{1.0f, 0.3f, 0.1f},   ///< オレンジ-赤
		{0.2f, 0.6f, 1.0f},   ///< 水色
		{0.1f, 1.0f, 0.4f},   ///< 緑
		{1.0f, 0.8f, 0.1f}    ///< 黄色
	}};

	/// @brief パーティクルシステムを初期化する
	void initParticleSystems()
	{
		for (std::size_t i = 0; i < SYSTEM_COUNT; ++i)
		{
			m_systems[i] = sgc::ParticleSystem(MAX_PER_SYSTEM);

			sgc::EmitterConfig cfg;
			cfg.rate = 0.0f;
			cfg.lifetime = 2.0f;
			cfg.speed = 120.0f + static_cast<float>(i) * 30.0f;
			cfg.spread = 6.28318f;
			cfg.startSize = 5.0f;
			cfg.endSize = 1.0f;
			cfg.startR = COLOR_PRESETS[i].r;
			cfg.startG = COLOR_PRESETS[i].g;
			cfg.startB = COLOR_PRESETS[i].b;
			cfg.startA = 1.0f;
			cfg.endA = 0.0f;

			m_systems[i].setConfig(cfg);

			// 重力アフェクター
			m_systems[i].addAffector([](sgc::Particle& p, float affDt)
			{
				p.vy += GRAVITY * affDt;
			});
		}
	}

	/// @brief 指定位置にバースト放出する
	/// @param x 放出位置X
	/// @param y 放出位置Y
	void emitBurst(float x, float y)
	{
		for (std::size_t i = 0; i < SYSTEM_COUNT; ++i)
		{
			auto cfg = m_systems[i].config();
			cfg.positionX = x;
			cfg.positionY = y;
			m_systems[i].setConfig(cfg);
			m_systems[i].emit(BURST_COUNT);
		}
	}

	/// @brief 指定位置に連続放出する
	/// @param x 放出位置X
	/// @param y 放出位置Y
	/// @param dt デルタタイム
	void emitContinuous(float x, float y, float dt)
	{
		m_continuousAccum += CONTINUOUS_RATE * dt;

		while (m_continuousAccum >= 1.0f)
		{
			// システムをラウンドロビンで選択
			const std::size_t si = m_emitIndex % SYSTEM_COUNT;
			auto cfg = m_systems[si].config();
			cfg.positionX = x;
			cfg.positionY = y;
			m_systems[si].setConfig(cfg);
			m_systems[si].emit(1);

			++m_emitIndex;
			m_continuousAccum -= 1.0f;
		}
	}

	/// @brief 全パーティクルをクリアする
	void clearAllParticles()
	{
		for (auto& ps : m_systems)
		{
			ps.clear();
		}
		m_continuousAccum = 0.0f;
	}

	/// @brief パーティクルシステム配列
	std::array<sgc::ParticleSystem, SYSTEM_COUNT> m_systems{
		sgc::ParticleSystem{MAX_PER_SYSTEM},
		sgc::ParticleSystem{MAX_PER_SYSTEM},
		sgc::ParticleSystem{MAX_PER_SYSTEM},
		sgc::ParticleSystem{MAX_PER_SYSTEM}
	};

	/// @brief 連続放出アキュムレータ
	float m_continuousAccum{0.0f};

	/// @brief 放出インデックス（ラウンドロビン用）
	std::size_t m_emitIndex{0};

	/// @brief 初期化済みフラグ
	bool m_initialized{false};
};
