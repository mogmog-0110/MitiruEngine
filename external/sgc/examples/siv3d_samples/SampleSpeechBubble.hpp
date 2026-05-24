#pragma once

/// @file SampleSpeechBubble.hpp
/// @brief 吹き出し（スピーチバブル）のサンプル

#include "SharedData.hpp"
#include <sgc/ui/SpeechBubble.hpp>
#include <cmath>

/// @brief 吹き出しのデモ（NPC会話イメージ）
class SampleSpeechBubble : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	void update(float dt) override
	{
		m_time += dt;

		const auto* input = getData().inputProvider;
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// クリックで次のメッセージ / 方向切り替え
		if (input->isMouseButtonPressed(0) || input->isKeyJustPressed(KeyCode::SPACE))
		{
			m_messageIndex = (m_messageIndex + 1) % 5;
		}
		if (input->isKeyJustPressed(KeyCode::LEFT))
		{
			m_direction = static_cast<int>((static_cast<int>(m_direction) + 3) % 4);
		}
		if (input->isKeyJustPressed(KeyCode::RIGHT))
		{
			m_direction = static_cast<int>((static_cast<int>(m_direction) + 1) % 4);
		}

		// NPC位置を軽く浮遊
		m_npcY = 350.0f + std::sin(m_time * 2.0f) * 5.0f;
	}

	void draw() const override
	{
		auto* r = getData().renderer;
		auto* t = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.12f});

		t->drawText("Speech Bubble Demo", 24.0f, {10.0f, 10.0f}, sgc::Colorf{0.9f, 0.9f, 0.95f});
		t->drawText("[Space/Click] Next message  [Left/Right] Change direction", 12.0f,
			{10.0f, 40.0f}, sgc::Colorf{0.5f, 0.5f, 0.65f});

		// NPC描画（簡易キャラクター）
		const float npcX = sw * 0.5f;
		const float npcRadius = 25.0f;
		r->drawCircle({npcX, m_npcY}, npcRadius, sgc::Colorf{0.2f, 0.8f, 0.5f});
		// 目
		r->drawCircle({npcX - 8.0f, m_npcY - 5.0f}, 4.0f, sgc::Colorf{0.0f, 0.0f, 0.0f});
		r->drawCircle({npcX + 8.0f, m_npcY - 5.0f}, 4.0f, sgc::Colorf{0.0f, 0.0f, 0.0f});
		r->drawCircle({npcX - 7.0f, m_npcY - 6.0f}, 1.5f, sgc::Colorf{1.0f, 1.0f, 1.0f});
		r->drawCircle({npcX + 9.0f, m_npcY - 6.0f}, 1.5f, sgc::Colorf{1.0f, 1.0f, 1.0f});

		// 吹き出し
		const sgc::Vec2f speakerPos{npcX, m_npcY - npcRadius};
		const sgc::Vec2f bubbleSize{280.0f, 70.0f};
		const auto dir = static_cast<sgc::ui::BubbleDirection>(m_direction);

		auto bubble = sgc::ui::evaluateSpeechBubble(
			speakerPos, bubbleSize, {sw, sh}, dir);

		// 吹き出し本体
		r->drawRect(sgc::AABB2f{
			{bubble.bounds.x(), bubble.bounds.y()},
			{bubble.bounds.right(), bubble.bounds.bottom()}},
			sgc::Colorf{0.95f, 0.95f, 0.98f, 0.95f});
		r->drawRectFrame(sgc::AABB2f{
			{bubble.bounds.x(), bubble.bounds.y()},
			{bubble.bounds.right(), bubble.bounds.bottom()}},
			1.5f, sgc::Colorf{0.3f, 0.3f, 0.4f});

		// テール三角形
		r->drawTriangle(bubble.tailTip, bubble.tailLeft, bubble.tailRight,
			sgc::Colorf{0.95f, 0.95f, 0.98f, 0.95f});

		// メッセージテキスト
		const char* messages[] = {
			"Hello! Welcome to the world!",
			"This is a speech bubble system.",
			"NPCs can talk to you like this.",
			"Try pressing Left/Right to change direction.",
			"Bubbles auto-flip when hitting screen edges!",
		};
		t->drawText(messages[m_messageIndex], 14.0f,
			{bubble.bounds.x() + 12.0f, bubble.bounds.y() + 12.0f},
			sgc::Colorf{0.1f, 0.1f, 0.15f});

		// 方向ラベル
		const char* dirNames[] = {"Above", "Below", "Left", "Right"};
		char dirBuf[64];
		std::snprintf(dirBuf, sizeof(dirBuf), "Direction: %s", dirNames[m_direction]);
		t->drawText(dirBuf, 14.0f, {10.0f, sh - 30.0f}, sgc::Colorf{0.5f, 0.5f, 0.65f});

		t->drawText("[ESC] Back", 12.0f, {sw - 100.0f, sh - 20.0f}, sgc::Colorf{0.4f, 0.4f, 0.5f});
	}

private:
	float m_time = 0.0f;
	float m_npcY = 350.0f;
	int m_messageIndex = 0;
	int m_direction = 0;  // BubbleDirection enum cast
};
