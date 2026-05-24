#pragma once

/// @file SampleToggleRadio.hpp
/// @brief ToggleSwitch・RadioButton デモシーン
///
/// 左半分にトグルスイッチ4個（Music, SFX, Fullscreen, VSync）、
/// 右半分にラジオボタングループ「Difficulty」4択を配置する。
/// 画面下部に現在の設定サマリーを表示する。
/// - ESC: メニューに戻る
/// - R: デフォルトにリセット

#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/RadioButton.hpp"
#include "sgc/ui/ToggleSwitch.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief トグルスイッチ・ラジオボタンのデモシーン
///
/// トグルスイッチでON/OFF設定を切り替え、
/// ラジオボタンで難易度を選択する。
class SampleToggleRadio : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		resetDefaults();
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto& input = *getData().inputProvider;

		if (input.isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input.isKeyJustPressed(KeyCode::R))
		{
			resetDefaults();
			return;
		}

		const auto mousePos = input.mousePosition();
		const bool mouseDown = input.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		// ── トグルスイッチ評価 ──
		updateToggle(0, mousePos, mouseDown, mousePressed, m_music);
		updateToggle(1, mousePos, mouseDown, mousePressed, m_sfx);
		updateToggle(2, mousePos, mouseDown, mousePressed, m_fullscreen);
		updateToggle(3, mousePos, mouseDown, mousePressed, m_vsync);

		// ── ラジオボタン評価 ──
		for (int i = 0; i < RADIO_COUNT; ++i)
		{
			auto result = sgc::ui::evaluateRadio(
				radioRect(i), mousePos, mouseDown, mousePressed,
				i, m_difficulty);
			if (result.changed)
			{
				m_difficulty = result.selected;
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;

		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f});

		// ── タイトル ──
		text->drawTextCentered(
			"ToggleSwitch & RadioButton", 28.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		text->drawText(
			"ESC: Back to Menu  /  R: Reset", 14.0f,
			sgc::Vec2f{20.0f, 60.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── 左半分: トグルスイッチ ──
		drawToggles(renderer, text);

		// ── 右半分: ラジオボタン ──
		drawRadios(renderer, text);

		// ── 下部: サマリー ──
		drawSummary(text, sw);
	}

private:
	// ── レイアウト定数 ──
	static constexpr float TOGGLE_X = 60.0f;
	static constexpr float TOGGLE_W = 50.0f;
	static constexpr float TOGGLE_H = 24.0f;
	static constexpr float TOGGLE_START_Y = 130.0f;
	static constexpr float TOGGLE_GAP = 56.0f;
	static constexpr int TOGGLE_COUNT = 4;

	static constexpr float RADIO_X = 460.0f;
	static constexpr float RADIO_W = 24.0f;
	static constexpr float RADIO_H = 24.0f;
	static constexpr float RADIO_START_Y = 130.0f;
	static constexpr float RADIO_GAP = 48.0f;
	static constexpr int RADIO_COUNT = 4;

	static constexpr float SUMMARY_Y = 420.0f;

	// ── メンバ変数 ──
	bool m_music{true};
	bool m_sfx{true};
	bool m_fullscreen{false};
	bool m_vsync{true};
	int m_difficulty{1};

	// ── ヘルパー関数 ──

	/// @brief デフォルト設定にリセットする
	void resetDefaults()
	{
		m_music = true;
		m_sfx = true;
		m_fullscreen = false;
		m_vsync = true;
		m_difficulty = 1;
	}

	/// @brief トグルスイッチ矩形を計算する
	/// @param index トグルのインデックス
	/// @return トグルスイッチ矩形
	[[nodiscard]] static sgc::Rectf toggleRect(int index) noexcept
	{
		const float y = TOGGLE_START_Y + static_cast<float>(index) * TOGGLE_GAP;
		return {TOGGLE_X, y, TOGGLE_W, TOGGLE_H};
	}

	/// @brief ラジオボタン矩形を計算する
	/// @param index ラジオのインデックス
	/// @return ラジオボタン矩形
	[[nodiscard]] static sgc::Rectf radioRect(int index) noexcept
	{
		const float y = RADIO_START_Y + static_cast<float>(index) * RADIO_GAP;
		return {RADIO_X, y, RADIO_W, RADIO_H};
	}

	/// @brief トグルスイッチを評価して値を更新する
	/// @param index トグルのインデックス
	/// @param mousePos マウス座標
	/// @param mouseDown マウスボタン押下中か
	/// @param mousePressed マウスボタンがこのフレームで押されたか
	/// @param value 更新対象のbool値
	void updateToggle(
		int index, const sgc::Vec2f& mousePos,
		bool mouseDown, bool mousePressed, bool& value)
	{
		auto result = sgc::ui::evaluateToggle(
			toggleRect(index), mousePos, mouseDown, mousePressed, value);
		if (result.toggled)
		{
			value = result.value;
		}
	}

	/// @brief トグルスイッチ群を描画する
	/// @param renderer レンダラー
	/// @param text テキストレンダラー
	void drawToggles(sgc::IRenderer* renderer, sgc::ITextRenderer* text) const
	{
		text->drawText("Settings", 22.0f,
			sgc::Vec2f{TOGGLE_X, TOGGLE_START_Y - 36.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f});

		const char* labels[] = {"Music", "SFX", "Fullscreen", "VSync"};
		const bool values[] = {m_music, m_sfx, m_fullscreen, m_vsync};

		for (int i = 0; i < TOGGLE_COUNT; ++i)
		{
			const auto bounds = toggleRect(i);
			const bool isOn = values[i];

			// ラベル（トグルの右側）
			text->drawText(labels[i], 18.0f,
				sgc::Vec2f{bounds.x() + TOGGLE_W + 16.0f, bounds.y() + 2.0f},
				sgc::Colorf{0.85f, 0.85f, 0.9f});

			// トラック背景
			const sgc::Colorf trackColor = isOn
				? sgc::Colorf{0.2f, 0.7f, 0.3f}
				: sgc::Colorf{0.3f, 0.3f, 0.35f};
			const sgc::AABB2f trackBox = bounds.toAABB2();
			renderer->drawRect(trackBox, trackColor);
			renderer->drawRectFrame(trackBox, 1.0f,
				sgc::Colorf{0.4f, 0.4f, 0.45f});

			// ノブ（白い正方形）
			const auto knob = sgc::ui::toggleKnobRect(bounds, isOn);
			const sgc::AABB2f knobBox = knob.toAABB2();
			renderer->drawRect(knobBox, sgc::Colorf::white());

			// ON/OFF テキスト
			const char* stateText = isOn ? "ON" : "OFF";
			const sgc::Colorf stateColor = isOn
				? sgc::Colorf{0.3f, 0.9f, 0.4f}
				: sgc::Colorf{0.5f, 0.5f, 0.55f};
			text->drawText(stateText, 14.0f,
				sgc::Vec2f{bounds.x() + TOGGLE_W + 120.0f, bounds.y() + 4.0f},
				stateColor);
		}
	}

	/// @brief ラジオボタン群を描画する
	/// @param renderer レンダラー
	/// @param text テキストレンダラー
	void drawRadios(sgc::IRenderer* renderer, sgc::ITextRenderer* text) const
	{
		text->drawText("Difficulty", 22.0f,
			sgc::Vec2f{RADIO_X, RADIO_START_Y - 36.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f});

		const char* labels[] = {"Easy", "Normal", "Hard", "Expert"};
		const sgc::Colorf labelColors[] = {
			sgc::Colorf{0.4f, 0.9f, 0.5f},
			sgc::Colorf{0.5f, 0.7f, 1.0f},
			sgc::Colorf{1.0f, 0.7f, 0.3f},
			sgc::Colorf{1.0f, 0.3f, 0.3f}
		};

		for (int i = 0; i < RADIO_COUNT; ++i)
		{
			const auto bounds = radioRect(i);
			const auto center = sgc::ui::radioCircleCenter(bounds);
			const float radius = RADIO_H / 2.0f;
			const bool selected = (i == m_difficulty);

			// 外枠の円
			renderer->drawCircleFrame(center, radius, 2.0f,
				sgc::Colorf{0.5f, 0.5f, 0.7f});

			// 選択時: 内側の塗りつぶし円
			if (selected)
			{
				renderer->drawCircle(center, radius * 0.5f,
					sgc::Colorf{0.4f, 0.6f, 1.0f});
			}

			// ラベルテキスト
			const sgc::Colorf color = selected
				? labelColors[i]
				: sgc::Colorf{0.6f, 0.6f, 0.65f};
			text->drawText(labels[i], 18.0f,
				sgc::Vec2f{bounds.x() + RADIO_W + 14.0f, bounds.y() + 2.0f},
				color);
		}
	}

	/// @brief 設定サマリーを描画する
	/// @param text テキストレンダラー
	/// @param screenW 画面幅
	void drawSummary(sgc::ITextRenderer* text, float screenW) const
	{
		const char* diffLabels[] = {"Easy", "Normal", "Hard", "Expert"};
		const std::string summary =
			std::string("Music:") + (m_music ? "ON" : "OFF") +
			"  SFX:" + (m_sfx ? "ON" : "OFF") +
			"  Fullscreen:" + (m_fullscreen ? "ON" : "OFF") +
			"  VSync:" + (m_vsync ? "ON" : "OFF") +
			"  Difficulty:" + diffLabels[m_difficulty];

		text->drawTextCentered(summary, 16.0f,
			sgc::Vec2f{screenW * 0.5f, SUMMARY_Y},
			sgc::Colorf{0.7f, 0.8f, 0.9f});
	}
};
