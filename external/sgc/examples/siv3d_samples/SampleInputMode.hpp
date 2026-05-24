#pragma once

/// @file SampleInputMode.hpp
/// @brief InputModeManager モーダル入力デモシーン
///
/// 入力モードスタックを視覚化し、モードに応じた入力制御を示す。
/// キャラクター(四角)はGameplayモード時のみWASDで移動可能。
/// - 1: Gameplay をプッシュ
/// - 2: Menu をプッシュ
/// - 3: Cutscene をプッシュ
/// - BACKSPACE: モードをポップ
/// - WASD: キャラクター移動（Gameplayモード時のみ）
/// - ESC: メニューに戻る

#include <algorithm>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/input/InputModeManager.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief InputModeManager サンプルシーン
///
/// モードスタックをリアルタイムに可視化し、
/// Gameplayモード時のみキャラクター操作を有効にする。
class SampleInputMode : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_modeManager.reset();
		m_playerX = 400.0f;
		m_playerY = 400.0f;
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

		// モードのプッシュ
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			m_modeManager.pushMode(sgc::InputMode::Gameplay);
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			m_modeManager.pushMode(sgc::InputMode::Menu);
		}
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			m_modeManager.pushMode(sgc::InputMode::Cutscene);
		}

		// モードのポップ
		if (input->isKeyJustPressed(KeyCode::BACKSPACE))
		{
			m_modeManager.popMode();
		}

		// Gameplayモード時のみキャラクター移動
		if (m_modeManager.isGameplay())
		{
			const float speed = PLAYER_SPEED * dt;
			if (input->isKeyDown(KeyCode::W)) { m_playerY -= speed; }
			if (input->isKeyDown(KeyCode::S)) { m_playerY += speed; }
			if (input->isKeyDown(KeyCode::A)) { m_playerX -= speed; }
			if (input->isKeyDown(KeyCode::D)) { m_playerX += speed; }

			// 画面内にクランプ
			const float halfSize = PLAYER_SIZE * 0.5f;
			m_playerX = std::clamp(m_playerX, halfSize, getData().screenWidth - halfSize);
			m_playerY = std::clamp(m_playerY, PLAY_AREA_TOP + halfSize,
				getData().screenHeight - halfSize);
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f});

		// ── タイトル ──
		text->drawTextCentered(
			"InputModeManager - Modal Input", 28.0f,
			sgc::Vec2f{sw * 0.5f, 25.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── モードスタック表示 ──
		text->drawText("Mode Stack:", 18.0f,
			sgc::Vec2f{30.0f, 60.0f}, sgc::Colorf{0.8f, 0.8f, 0.9f});

		const auto depth = m_modeManager.depth();
		const std::string depthStr = "Depth: " + std::to_string(depth);
		text->drawText(depthStr, 14.0f,
			sgc::Vec2f{30.0f, 85.0f}, sgc::Colorf{0.6f, 0.6f, 0.7f});

		// 現在のモード表示
		const auto currentMode = m_modeManager.currentMode();
		const auto modeColor = modeToColor(currentMode);
		const std::string modeName = "Current: " + modeToString(currentMode);
		text->drawText(modeName, 22.0f,
			sgc::Vec2f{30.0f, 110.0f}, modeColor);

		// スタックブロック描画（最大8個表示）
		const float stackX = 300.0f;
		const float stackY = 60.0f;
		const int maxDisplay = static_cast<int>(std::min(depth, static_cast<std::size_t>(8)));

		for (int i = 0; i < maxDisplay; ++i)
		{
			const float bx = stackX + static_cast<float>(i) * (BLOCK_W + BLOCK_GAP);
			const sgc::AABB2f block{
				{bx, stackY},
				{bx + BLOCK_W, stackY + BLOCK_H}};

			// 最上位（現在アクティブ）は明るく
			const float alpha = (i == maxDisplay - 1) ? 1.0f : 0.4f;
			renderer->drawRect(block, modeColor.withAlpha(alpha * 0.6f));
			renderer->drawRectFrame(block, 2.0f, modeColor.withAlpha(alpha));

			// スタック番号
			text->drawTextCentered(
				std::to_string(i + 1), 14.0f,
				sgc::Vec2f{bx + BLOCK_W * 0.5f, stackY + BLOCK_H * 0.5f},
				sgc::Colorf::white().withAlpha(alpha));
		}

		// ── 操作説明パネル ──
		const float instrY = 150.0f;
		const sgc::AABB2f instrPanel{
			{20.0f, instrY},
			{260.0f, instrY + 150.0f}};
		renderer->drawRect(instrPanel, sgc::Colorf{0.1f, 0.1f, 0.14f, 0.9f});
		renderer->drawRectFrame(instrPanel, 1.0f, sgc::Colorf{0.3f, 0.3f, 0.4f});

		text->drawText("1: Push Gameplay", 14.0f,
			sgc::Vec2f{30.0f, instrY + 10.0f}, sgc::Colorf{0.3f, 0.9f, 0.3f});
		text->drawText("2: Push Menu", 14.0f,
			sgc::Vec2f{30.0f, instrY + 32.0f}, sgc::Colorf{0.3f, 0.6f, 1.0f});
		text->drawText("3: Push Cutscene", 14.0f,
			sgc::Vec2f{30.0f, instrY + 54.0f}, sgc::Colorf{0.9f, 0.6f, 0.2f});
		text->drawText("Backspace: Pop", 14.0f,
			sgc::Vec2f{30.0f, instrY + 76.0f}, sgc::Colorf{0.8f, 0.3f, 0.3f});
		text->drawText("WASD: Move (Gameplay only)", 14.0f,
			sgc::Vec2f{30.0f, instrY + 98.0f}, sgc::Colorf{0.7f, 0.7f, 0.8f});
		text->drawText("ESC: Back to Menu", 14.0f,
			sgc::Vec2f{30.0f, instrY + 120.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── プレイエリア ──
		const sgc::AABB2f playArea{
			{20.0f, PLAY_AREA_TOP},
			{sw - 20.0f, sh - 20.0f}};
		renderer->drawRectFrame(playArea, 1.0f, sgc::Colorf{0.25f, 0.25f, 0.3f});

		// エリアラベル
		const std::string areaLabel = m_modeManager.isGameplay()
			? "GAMEPLAY - Move with WASD"
			: "INPUT LOCKED (" + modeToString(currentMode) + ")";
		const auto areaLabelColor = m_modeManager.isGameplay()
			? sgc::Colorf{0.3f, 0.9f, 0.3f, 0.7f}
			: sgc::Colorf{0.8f, 0.3f, 0.3f, 0.7f};
		text->drawText(areaLabel, 16.0f,
			sgc::Vec2f{30.0f, PLAY_AREA_TOP + 5.0f}, areaLabelColor);

		// ── キャラクター描画 ──
		const float halfSize = PLAYER_SIZE * 0.5f;
		const sgc::Colorf playerColor = m_modeManager.isGameplay()
			? sgc::Colorf{0.3f, 0.9f, 0.4f}
			: sgc::Colorf{0.5f, 0.5f, 0.5f};

		const sgc::AABB2f playerRect{
			{m_playerX - halfSize, m_playerY - halfSize},
			{m_playerX + halfSize, m_playerY + halfSize}};
		renderer->drawRect(playerRect, playerColor);
		renderer->drawRectFrame(playerRect, 2.0f, playerColor.withAlpha(0.5f));

		// キャラクター方向インジケーター（上向き三角）
		renderer->drawTriangle(
			sgc::Vec2f{m_playerX, m_playerY - halfSize - 8.0f},
			sgc::Vec2f{m_playerX - 6.0f, m_playerY - halfSize},
			sgc::Vec2f{m_playerX + 6.0f, m_playerY - halfSize},
			playerColor.withAlpha(0.7f));

		// 無効時のオーバーレイ
		if (!m_modeManager.isGameplay())
		{
			renderer->drawRect(playArea, sgc::Colorf{0.0f, 0.0f, 0.0f, 0.3f});
		}
	}

private:
	// ── 定数 ──
	static constexpr float PLAYER_SIZE = 30.0f;
	static constexpr float PLAYER_SPEED = 200.0f;
	static constexpr float PLAY_AREA_TOP = 320.0f;
	static constexpr float BLOCK_W = 50.0f;
	static constexpr float BLOCK_H = 35.0f;
	static constexpr float BLOCK_GAP = 6.0f;

	// ── メンバ変数 ──
	sgc::InputModeManager m_modeManager;
	float m_playerX{400.0f};
	float m_playerY{400.0f};

	/// @brief モード名を文字列に変換する
	[[nodiscard]] static std::string modeToString(sgc::InputMode mode)
	{
		switch (mode)
		{
		case sgc::InputMode::Gameplay:  return "Gameplay";
		case sgc::InputMode::TextInput: return "TextInput";
		case sgc::InputMode::Menu:      return "Menu";
		case sgc::InputMode::Cutscene:  return "Cutscene";
		case sgc::InputMode::Disabled:  return "Disabled";
		}
		return "Unknown";
	}

	/// @brief モードに対応する色を返す
	[[nodiscard]] static sgc::Colorf modeToColor(sgc::InputMode mode)
	{
		switch (mode)
		{
		case sgc::InputMode::Gameplay:  return sgc::Colorf{0.3f, 0.9f, 0.3f};
		case sgc::InputMode::TextInput: return sgc::Colorf{0.9f, 0.9f, 0.3f};
		case sgc::InputMode::Menu:      return sgc::Colorf{0.3f, 0.6f, 1.0f};
		case sgc::InputMode::Cutscene:  return sgc::Colorf{0.9f, 0.6f, 0.2f};
		case sgc::InputMode::Disabled:  return sgc::Colorf{0.5f, 0.5f, 0.5f};
		}
		return sgc::Colorf::white();
	}
};
